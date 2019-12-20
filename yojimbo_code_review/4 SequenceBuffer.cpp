So before we go into reliable ordered message channel, we want to explain a core data structure used in reliable ordered message channel:
the SequenceBuffer

                yojimbo.h

                class ReliableOrderedChannel : public Channel
                {
                    ...
                    ...

                    private:

                        uint16_t m_sendMessageId;                                                       ///< Id of the next message to be added to the send queue.
                        uint16_t m_receiveMessageId;                                                    ///< Id of the next message to be added to the receive queue.
                        uint16_t m_oldestUnackedMessageId;                                              ///< Id of the oldest unacked message in the send queue.
                        SequenceBuffer<SentPacketEntry> * m_sentPackets;                                ///< Stores information per sent connection packet about messages and block data included in each packet. Used to walk from connection packet level acks to message and data block fragment level acks.
                        SequenceBuffer<MessageSendQueueEntry> * m_messageSendQueue;                     ///< Message send queue.
                        SequenceBuffer<MessageReceiveQueueEntry> * m_messageReceiveQueue;               ///< Message receive queue.
                        uint16_t * m_sentPacketMessageIds;                                              ///< Array of n message ids per sent connection packet. Allows the maximum number of messages per-packet to be allocated dynamically.
                        SendBlockData * m_sendBlock;                                                    ///< Data about the block being currently sent.
                        ReceiveBlockData * m_receiveBlock;                                              ///< Data about the block being currently received.

                }



in the ReliableOrderedChannel, you can see we have three instances of it. Lets just focus only one 

                    
                        SequenceBuffer<MessageSendQueueEntry> * m_messageSendQueue; 



so when we create the ReliableOrderedChannel, we would allocate memory for this SequenceBuffer, which occurs in: 



                ReliableOrderedChannel::ReliableOrderedChannel( Allocator & allocator, MessageFactory & messageFactory, const ChannelConfig & config, int channelIndex, double time ) 
                        : Channel( allocator, messageFactory, config, channelIndex, time )
                {
                    ...
                    ...

                    m_sentPackets = YOJIMBO_NEW( *m_allocator, SequenceBuffer<SentPacketEntry>, *m_allocator, m_config.sentPacketBufferSize );
   ------------>    m_messageSendQueue = YOJIMBO_NEW( *m_allocator, SequenceBuffer<MessageSendQueueEntry>, *m_allocator, m_config.messageSendQueueSize );
                    m_messageReceiveQueue = YOJIMBO_NEW( *m_allocator, SequenceBuffer<MessageReceiveQueueEntry>, *m_allocator, m_config.messageReceiveQueueSize );
                    m_sentPacketMessageIds = (uint16_t*) YOJIMBO_ALLOCATE( *m_allocator, sizeof( uint16_t ) * m_config.maxMessagesPerPacket * m_config.sentPacketBufferSize );

                    ...
                    ...

                }



as you can see, the default messageSendQueueSize is 1024. I suppose that just a number that Glenn found to be good

                yojimbo.h

                struct ChannelConfig
                {
                    ChannelType type;                                           ///< Channel type: reliable-ordered or unreliable-unordered.
                    bool disableBlocks;                                         ///< Disables blocks being sent across this channel.
                    int sentPacketBufferSize;                                   ///< Number of packet entries in the sent packet sequence buffer. Please consider your packet send rate and make sure you have at least a few seconds worth of entries in this buffer.
   ------------>    int messageSendQueueSize;                                   ///< Number of messages in the send queue for this channel.
                    int messageReceiveQueueSize;                                ///< Number of messages in the receive queue for this channel.
                    int maxMessagesPerPacket;                                   ///< Maximum number of messages to include in each packet. Will write up to this many messages, provided the messages fit into the channel packet budget and the number of bytes remaining in the packet.
                    int packetBudget;                                           ///< Maximum amount of message data to write to the packet for this channel (bytes). Specifying -1 means the channel can use up to the rest of the bytes remaining in the packet.
                    int maxBlockSize;                                           ///< The size of the largest block that can be sent across this channel (bytes).
                    int blockFragmentSize;                                      ///< Blocks are split up into fragments of this size (bytes). Reliable-ordered channel only.
                    float messageResendTime;                                    ///< Minimum delay between message resends (seconds). Avoids sending the same message too frequently. Reliable-ordered channel only.
                    float blockFragmentResendTime;                              ///< Minimum delay between block fragment resends (seconds). Avoids sending the same fragment too frequently. Reliable-ordered channel only.

                    ChannelConfig() : type ( CHANNEL_TYPE_RELIABLE_ORDERED )
                    {
                        disableBlocks = false;
                        sentPacketBufferSize = 1024;
    --------------->    messageSendQueueSize = 1024;
                        messageReceiveQueueSize = 1024;

                        ...
                        ...
                    }

                };



so lets just work with 1024 as our message queue size.

One thing to note that is that the sequence number is uint16_t, which is 65536











Let us first look at the definition of the SequenceBuffer data structure 

                /**
                    Data structure that stores data indexed by sequence number.
                    Entries may or may not exist. If they don't exist the sequence value for the entry at that index is set to 0xFFFFFFFF. 
                    This provides a constant time lookup for an entry by sequence number. If the entry at sequence modulo buffer size doesn't have the same sequence number, that sequence number is not stored.
                    This is incredibly useful and is used as the foundation of the packet level ack system and the reliable message send and receive queues.
                    @see Connection
                 */

                template <typename T> class SequenceBuffer
                {
                    ...
                    ...

                    private:

                        Allocator * m_allocator;                   ///< The allocator passed in to the constructor.
                        int m_size;                                ///< The size of the sequence buffer.
                        uint16_t m_sequence;                       ///< The most recent sequence number added to the buffer.
                        uint32_t * m_entry_sequence;               ///< Array of sequence numbers corresponding to each sequence buffer entry for fast lookup. Set to 0xFFFFFFFF if no entry exists at that index.
                        T * m_entries;                             ///< The sequence buffer entries. This is where the data is stored per-entry. Separate from the sequence numbers for fast lookup (hot/cold split) when the data per-sequence number is relatively large.
                        
                        SequenceBuffer( const SequenceBuffer<T> & other );

                        SequenceBuffer<T> & operator = ( const SequenceBuffer<T> & other );
                };


you can see that these are the main variables inside a SequenceBuffer

-   Allocator * m_allocator; 
the allocator is just an allocator, we can ignore that. 


-   int m_size;
the is just the size of the sequence buffer, if we are using the default size from the ChannelConfig, this will be 1024.


-   uint16_t m_sequence;
this is just the most recent sequence number added to the buffer. Later you will see that this number gets updated
whenever we call Insert();


-   uint32_t * m_entry_sequence;
    T * m_entries;

so these two arrays works together. The first array stores the sequence number of the entries stored in the the "T * m_entries" array.


lets see the following for example: 
initially our m_entry_sequence starts out all with 0xFFFFFFFF, since of our entries are empty 

(I actually also suspect that the reason why m_entry_sequence is a uint32_t is becuz we want to be able to have the value of 0xFFFFFFFF
since we are using 0xFFFFFFFF to indicate an empty slot)



                index                   0           1           2           3           ---------------           1022           1023          

                m_entry_sequence    0xFFFFFFFF  0xFFFFFFFF  0xFFFFFFFF  0xFFFFFFFF                            0xFFFFFFFF      0xFFFFFFFF     



now lets say we added a new entry of 2. since 2 % 1024 = 2, therefore we fill in the 2nd entry 


                index                   0           1           2           3           ---------------           1022           1023           

                m_entry_sequence    0xFFFFFFFF  0xFFFFFFFF      2       0xFFFFFFFF                            0xFFFFFFFF      0xFFFFFFFF     


recall that our array is only 1024 in length. if you have sequence number of 1024, it will fill in index 0


                index                   0           1           2           3           ---------------           1022           1023           

                m_entry_sequence      1024      0xFFFFFFFF      2       0xFFFFFFFF                            0xFFFFFFFF      0xFFFFFFFF     




and if you something a lot higher, such as 50000, (still less then 65526), 50000 % 1024 is 904. and of course that will fill up index 904


                index                   0           1           2           3     ---------   904   -------       1022           1023           

                m_entry_sequence      1024      0xFFFFFFFF      2       0xFFFFFFFF           50000            0xFFFFFFFF      0xFFFFFFFF     






lets look at the insert(); function. 

you can see that if the incoming sequence 



                yojimbo.h

                T * Insert( uint16_t sequence )
                {
                    if ( sequence_greater_than( sequence + 1, m_sequence ) )
                    {
                        RemoveEntries( m_sequence, sequence );
                        m_sequence = sequence + 1;
                    }
                    else if ( sequence_less_than( sequence, m_sequence - m_size ) )
                    {
                        return NULL;
                    }
                    const int index = sequence % m_size;
                    m_entry_sequence[index] = sequence;
                    return &m_entries[index];
                }




-   notice the first if check

                if ( sequence_greater_than( sequence + 1, m_sequence ) )
                {
                    RemoveEntries( m_sequence, sequence );
                    m_sequence = sequence + 1;
                }


we are doing all the comparisons against sequence + 1 rather than sequence.

this is becuz we want to use m_sequence = 0 to indicate we havent received any message.
so when the user calls SequenceBuffer.Insert(0);

m_sequence will actually be 1 to indicate that we have received sequence number 0.



-   regarding the else if check, Glenn has answer for it 


                It's to catch old packets that DON'T fit in the sequence buffer, eg. 
                if the most recent packet sequence number is 1024 and the sequence buffer size is 512, 
                then packets with sequence numbers 511 and older would be rejected by the else if.

https://github.com/networkprotocol/yojimbo/issues/29

