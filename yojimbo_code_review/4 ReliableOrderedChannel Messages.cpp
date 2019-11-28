##############################################################################
##################### ReliableOrderedChannel #################################
##############################################################################

let us look at how the ReliableOrderedChannel works 

                /**
                    Messages sent across this channel are guaranteed to arrive in the order they were sent.
                    This channel type is best used for control messages and RPCs.
                    Messages sent over this channel are included in connection packets until one of those packets is acked. 
                    Messages are acked individually and remain in the send queue until acked.
                    
                    Blocks attached to messages sent over this channel are split up into fragments. 
                    Each fragment of the block is included in a connection packet until one of those packets are acked. 
                    Eventually, all fragments are received on the other side, and block is reassembled and attached to the message.
                    Only one message block may be in flight over the network at any time, so blocks stall out message delivery slightly. 
                    Therefore, only use blocks for large data that won't fit inside a single connection packet where you actually 
                    need the channel to split it up into fragments. If your block fits inside a packet, 
                    just serialize it inside your message serialize via serialize_bytes instead.
                 */

based on the comments, this is essentially what the "Sending Large Blocks of Data" is referring to 
https://gafferongames.com/post/sending_large_blocks_of_data/



Let us look at the class definitions 

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


int this channel, you will either be sending regular messages or block messages. Some of these are only used for block messages

                
                SendBlockData * m_sendBlock;                                                    ///< Data about the block being currently sent.
                ReceiveBlockData * m_receiveBlock;                                              ///< Data about the block being currently received.



-   sendMessageId is the sequence number for your messages.

                uint16_t m_sendMessageId; 
                uint16_t m_oldestUnackedMessageId;   

    notice that this is reliability on the message level


-   m_messageSendQueue is the message queu for the ReliableOrderedChannel channel 

                SequenceBuffer<MessageSendQueueEntry> * m_messageSendQueue;         

    notice that here we are using a SequenceBuffer, where as in the unreliable channel we just straight up use a queue









##############################################################################
############################# SequenceBuffer #################################
##############################################################################

2.  let us look at how the SequenceBuffer differs from a simple queue 

-   here we just straight up store an array of the T 

                T * m_entries; 


-   full code below:                

                yojimbo.h


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
                        
                };



3.  adding messages, lets look at the insert function

    if the new sequence number we want to add is larger than the old most recent sequence number,

    then it is a valid sequence.


-   note that m_entries, is a circular array. The index for this new sequence is gonna be sequence % m_size

-   we also store the actual sequence number in a separate array: m_entry_sequence

    assume your size is 255

    m_entry_sequence looks something like:


index       0       1       2       3

sequence    256     257     258     259...


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





4.  as seen above, if it is a valid entry, we overwite the existing entry. Most of the time we would oly overwrite one entry 
but we may have to remove multiple entries when entries are added with holes. 

note what is written in the comments 

        /** 
            Helper function to remove entries.
            This is used to remove old entries as we advance the sequence buffer forward. 
            Otherwise, if when entries are added with holes (eg. receive buffer for packets or messages, where not all sequence numbers are added to the buffer because we have high packet loss), 
            and we are extremely unlucky, we can have old sequence buffer entries from the previous sequence # wrap around still in the buffer, which corrupts our internal connection state.
            This actually happened in the soak test at high packet loss levels (>90%). It took me days to track it down :)
         */


-   full code below:

                void RemoveEntries( int start_sequence, int finish_sequence )
                {
                    if ( finish_sequence < start_sequence ) 
                        finish_sequence += 65535;
                    yojimbo_assert( finish_sequence >= start_sequence );
                    if ( finish_sequence - start_sequence < m_size )
                    {
                        for ( int sequence = start_sequence; sequence <= finish_sequence; ++sequence )
                            m_entry_sequence[sequence % m_size] = 0xFFFFFFFF;
                    }
                    else
                    {
                        for ( int i = 0; i < m_size; ++i )
                            m_entry_sequence[i] = 0xFFFFFFFF;
                    }
                }




############################################################################################
################################## Sending Message #########################################
############################################################################################


5.  let us look at what happens in the SendMessage(); function in the ReliableOrderedChannel;
    similarly, we inserted a message to our m_messageSendQueue. Only that in this case m_messageSendQueue is a SequenceBuffer<MessageSendQueueEntry>();
    


                yojimbo.cpp

                void ReliableOrderedChannel::SendMessage( Message * message, void *context )
                {
                    ...
                    ...

    ----------->    message->SetId( m_sendMessageId );

                    MessageSendQueueEntry * entry = m_messageSendQueue->Insert( m_sendMessageId );

                    ...

                    entry->block = message->IsBlockMessage();
                    entry->message = message;
                    entry->measuredBits = 0;
                    entry->timeLastSent = -1.0;

                    ... 
                    ...

                    m_counters[CHANNEL_COUNTER_MESSAGES_SENT]++;
                    m_sendMessageId++;
                }



############################################################################################
###################################### SendPackets #########################################
############################################################################################

6.  now lets look at how SendPackets(); work 


                int ServerMain()
                {                    
                    double time = 100.0;

                    ...
                    ...

                    while ( !quit )
                    {
    --------------->    server.SendPackets();

                        server.ReceivePackets();

                        time += deltaTime;

                        server.AdvanceTime( time );

                        if ( !server.IsRunning() )
                            break;

                        yojimbo_sleep( deltaTime );
                    }

                    server.Stop();

                    return 0;
                }




6.  let us see what is Connection::GeneratePacket(); doing. Here we are actaully calling into the connection class and it is doing the following few things 

    a note about ConnectionPacket. A ConnectionPacket is actually a packet of this client/server connection, not a Request Connection Packet. So its just a regular packet 

    we can look at this definition fo the connection class 
                

                struct ConnectionPacket
                {
                    int numChannelEntries;
                    ChannelPacketData * channelEntry;
                    MessageFactory * messageFactory;
                }

    as you can see, the a ConnectionPacket stores data for each channel. So we actually sending data in both the reliable and unreliable channel 


-   we first loop through all of our channels, if the channel has data, we call GetPacketData on that channel 

                yojimbo.cpp 

                bool Connection::GeneratePacket( void * context, uint16_t packetSequence, uint8_t * packetData, int maxPacketBytes, int & packetBytes )
                {
                    ConnectionPacket packet;

                    if ( m_connectionConfig.numChannels > 0 )
                    {
                        int numChannelsWithData = 0;
                        bool channelHasData[MaxChannels];
                        memset( channelHasData, 0, sizeof( channelHasData ) );
                        ChannelPacketData channelData[MaxChannels];
                        
                        int availableBits = maxPacketBytes * 8 - ConservativePacketHeaderBits;
                        
                        for ( int channelIndex = 0; channelIndex < m_connectionConfig.numChannels; ++channelIndex )
                        {
                            int packetDataBits = m_channel[channelIndex]->GetPacketData( context, channelData[channelIndex], packetSequence, availableBits );
                            if ( packetDataBits > 0 )
                            {
                                availableBits -= ConservativeChannelHeaderBits;
                                availableBits -= packetDataBits;
                                channelHasData[channelIndex] = true;
                                numChannelsWithData++;
                            }
                        }

                        if ( numChannelsWithData > 0 )
                        {
                            if ( !packet.AllocateChannelData( *m_messageFactory, numChannelsWithData ) )
                            {
                                yojimbo_printf( YOJIMBO_LOG_LEVEL_ERROR, "error: failed to allocate channel data\n" );
                                return false;
                            }

                            int index = 0;

                            for ( int channelIndex = 0; channelIndex < m_connectionConfig.numChannels; ++channelIndex )
                            {
                                if ( channelHasData[channelIndex] )
                                {
                                    memcpy( &packet.channelEntry[index], &channelData[channelIndex], sizeof( ChannelPacketData ) );
                                    index++;
                                }
                            }
                        }
                    }

                    packetBytes = WritePacket( context, *m_messageFactory, m_connectionConfig, packet, packetData, maxPacketBytes );

                    return true;
                }









7.  in our test sample, obviously we only have data on the ReliableOrderedChannel, so let us look at what is going on there. 

    you can see that we have the logic of either sending regular messages or block messages. Block messages will have special treatment.
    we will look at what is going in the regular message case. 

-   we first call GetMessagesToSend(); function. what happens there is that we want to see if we have messages to send. If so, we populate numMessageIds variable
    
-   if we actually go numMessageIds to send, then we call GetMessagePacketData(); and AddMessagePacketEntry(); 
    callint GetMessagePacketData(); will populate "ChannelPacketData & packetData".

    we will study each function in detail later. 

                yojimbo.cpp 

                int ReliableOrderedChannel::GetPacketData( void *context, ChannelPacketData & packetData, uint16_t packetSequence, int availableBits )
                {
                    ...

                    if ( SendingBlockMessage() )
                    {
                        ..........................................
                        ....... Sending block messages .........
                        ..........................................
                    }
                    else
                    {
                        int numMessageIds = 0;
                        uint16_t * messageIds = (uint16_t*) alloca( m_config.maxMessagesPerPacket * sizeof( uint16_t ) );
                        const int messageBits = GetMessagesToSend( messageIds, numMessageIds, availableBits, context );

                        if ( numMessageIds > 0 )
                        {
                            GetMessagePacketData( packetData, messageIds, numMessageIds );
                            AddMessagePacketEntry( messageIds, numMessageIds, packetSequence );
                            return messageBits;
                        }
                    }

                    return 0;
                }



8.  we are gonna first look at what happens in GetMessagesToSend(). The idea is that we can actually jam multiple messages in one packet. so we 
want to get an idea of how many messages we can send and calculate the exact value for "numMessageIds"

-   first thing is that we check if we have messages to send. 

                bool ReliableOrderedChannel::HasMessagesToSend() const
                {
                    return m_oldestUnackedMessageId != m_sendMessageId;
                }


    if we have any messages that we havent gotten acknowledged, then we will have to resend it 

    m_sendMessageId is usually always ahead of m_oldestUnackedMessageId. So as long as m_oldestUnackedMessageId hasnt caught up to m_sendMessageId,
    we have messages to send.


-   then notice that if we are currently sending a block message, we just break

                if ( entry->block )
                    break;

-   full code below:

                yojimbo.cpp

                int ReliableOrderedChannel::GetMessagesToSend( uint16_t * messageIds, int & numMessageIds, int availableBits, void *context )
                {
                    yojimbo_assert( HasMessagesToSend() );

                    numMessageIds = 0;

                    if ( m_config.packetBudget > 0 )
                        availableBits = yojimbo_min( m_config.packetBudget * 8, availableBits );

                    const int giveUpBits = 4 * 8;
                    const int messageTypeBits = bits_required( 0, m_messageFactory->GetNumTypes() - 1 );
                    const int messageLimit = yojimbo_min( m_config.messageSendQueueSize, m_config.messageReceiveQueueSize );
                    uint16_t previousMessageId = 0;
                    int usedBits = ConservativeMessageHeaderBits;
                    int giveUpCounter = 0;

                    for ( int i = 0; i < messageLimit; ++i )
                    {
                        if ( availableBits - usedBits < giveUpBits )
                            break;

                        if ( giveUpCounter > m_config.messageSendQueueSize )
                            break;

                        uint16_t messageId = m_oldestUnackedMessageId + i;
                        MessageSendQueueEntry * entry = m_messageSendQueue->Find( messageId );
                        if ( !entry )
                            continue;

                        if ( entry->block )
                            break;
                        
                        if ( entry->timeLastSent + m_config.messageResendTime <= m_time && availableBits >= (int) entry->measuredBits )
                        {                
                            int messageBits = entry->measuredBits + messageTypeBits;
                            
                            if ( numMessageIds == 0 )
                            {
                                messageBits += 16;
                            }
                            else
                            {
                                MeasureStream stream( GetDefaultAllocator() );
                                stream.SetContext( context );
                                serialize_sequence_relative_internal( stream, previousMessageId, messageId );
                                messageBits += stream.GetBitsProcessed();
                            }

                            if ( usedBits + messageBits > availableBits )
                            {
                                giveUpCounter++;
                                continue;
                            }

                            usedBits += messageBits;
                            messageIds[numMessageIds++] = messageId;
                            previousMessageId = messageId;
                            entry->timeLastSent = m_time;
                        }

                        if ( numMessageIds == m_config.maxMessagesPerPacket )
                            break;
                    }

                    return usedBits;
                }






9.  now we want to extract the data out of the messages 

-   we first initalize the numMessages 

                packetData.message.numMessages = numMessageIds;             

-   then we go through numMessageIds and we fill in the data in the messages

                yojimbo.cpp

                void ReliableOrderedChannel::GetMessagePacketData( ChannelPacketData & packetData, const uint16_t * messageIds, int numMessageIds )
                {
                    ...

                    packetData.Initialize();
                    packetData.channelIndex = GetChannelIndex();
                    packetData.message.numMessages = numMessageIds;
                    
                    if ( numMessageIds == 0 )
                        return;

                    packetData.message.messages = (Message**) YOJIMBO_ALLOCATE( m_messageFactory->GetAllocator(), sizeof( Message* ) * numMessageIds );

                    for ( int i = 0; i < numMessageIds; ++i )
                    {
                        MessageSendQueueEntry * entry = m_messageSendQueue->Find( messageIds[i] );
                        ...
                        packetData.message.messages[i] = entry->message;
                        m_messageFactory->AcquireMessage( packetData.message.messages[i] );
                    }
                }



10.  we also look at AddMessagePacketEntry(); What is happening here is that we are adding the message records into SentPacketEntry
    note we are also adding records of it based on the sequence id

                yojimbo.cpp

                void ReliableOrderedChannel::AddMessagePacketEntry( const uint16_t * messageIds, int numMessageIds, uint16_t sequence )
                {
                    SentPacketEntry * sentPacket = m_sentPackets->Insert( sequence );
                    yojimbo_assert( sentPacket );
                    if ( sentPacket )
                    {
                        sentPacket->acked = 0;
                        sentPacket->block = 0;
                        sentPacket->timeSent = m_time;
                        sentPacket->messageIds = &m_sentPacketMessageIds[ ( sequence % m_config.sentPacketBufferSize ) * m_config.maxMessagesPerPacket ];
                        sentPacket->numMessageIds = numMessageIds;            
                        for ( int i = 0; i < numMessageIds; ++i )
                        {
                            sentPacket->messageIds[i] = messageIds[i];
                        }
                    }
                }





13. going back to the GeneratePacket(); function, we first go through each channel and get data into "ChannelPacketData channelData[MaxChannels];"

-   then we copied the data to "ConnectionPacket packet;".
    we first call AllocateChannelData(); on the ConnectionPacket, then we call memcpy(); to officially copy thte data into it 

-   lastly, we copy the that data from ConnectionPacket to "uint8_t * packetData", the byteArray that we passed into the function.
    soo all this work, jto just get data onto packetData.

-   TL,DR: we went from bytes ----> ConnectionPacket ----> packetData. you can imagein the receiver case, we will go from packetData ----> ConnectionPacket

-   full code below:


                yojimbo.cpp 

                bool Connection::GeneratePacket( void * context, uint16_t packetSequence, uint8_t * packetData, int maxPacketBytes, int & packetBytes )
                {
                    ConnectionPacket packet;

                    if ( m_connectionConfig.numChannels > 0 )
                    {
                        int numChannelsWithData = 0;
                        bool channelHasData[MaxChannels];
                        memset( channelHasData, 0, sizeof( channelHasData ) );
                        ChannelPacketData channelData[MaxChannels];
                        
                        int availableBits = maxPacketBytes * 8 - ConservativePacketHeaderBits;
                        

                        ..............................................................................
                        ............... retrieving data from each channel ............................
                        ..............................................................................


                        if ( numChannelsWithData > 0 )
                        {
                            if ( !packet.AllocateChannelData( *m_messageFactory, numChannelsWithData ) )
                            {
                                yojimbo_printf( YOJIMBO_LOG_LEVEL_ERROR, "error: failed to allocate channel data\n" );
                                return false;
                            }

                            int index = 0;

                            for ( int channelIndex = 0; channelIndex < m_connectionConfig.numChannels; ++channelIndex )
                            {
                                if ( channelHasData[channelIndex] )
                                {
                                    memcpy( &packet.channelEntry[index], &channelData[channelIndex], sizeof( ChannelPacketData ) );
                                    index++;
                                }
                            }
                        }
                    }

                    packetBytes = WritePacket( context, *m_messageFactory, m_connectionConfig, packet, packetData, maxPacketBytes );

                    return true;
                }



#####################################################################################################
############################ ProcessPacket ##########################################################
#####################################################################################################

14. now we want to look at server.ReceivePackets();

                int ServerMain()
                {                    
                    double time = 100.0;

                    ...
                    ...

                    while ( !quit )
                    {
                        server.SendPackets();

    --------------->    server.ReceivePackets();

                        time += deltaTime;

                        server.AdvanceTime( time );

                        if ( !server.IsRunning() )
                            break;

                        yojimbo_sleep( deltaTime );
                    }

                    server.Stop();

                    return 0;
                }







15. we call netcode_server_receive_packet(); till we have looked at all the packets in this tick

                void Server::ReceivePackets()
                {
                    if ( m_server )
                    {
                        const int maxClients = GetMaxClients();
                        for ( int clientIndex = 0; clientIndex < maxClients; ++clientIndex )
                        {
                            while ( true )
                            {
                                int packetBytes;
                                uint64_t packetSequence;
                                uint8_t * packetData = netcode_server_receive_packet( m_server, clientIndex, &packetBytes, &packetSequence );
                                if ( !packetData )
                                    break;
                                reliable_endpoint_receive_packet( GetClientEndpoint( clientIndex ), packetData, packetBytes );
                                netcode_server_free_packet( m_server, packetData );
                            }
                        }
                    }
                }


    for details please see article 1.server.cpp



15. let us look at how the receiver process packets 

                bool Connection::ProcessPacket( void * context, uint16_t packetSequence, const uint8_t * packetData, int packetBytes )
                {


                    ConnectionPacket packet;

                    if ( !ReadPacket( context, *m_messageFactory, m_connectionConfig, packet, packetData, packetBytes ) )
                    {
                        ...       
                    }

                    for ( int i = 0; i < packet.numChannelEntries; ++i )
                    {
                        const int channelIndex = packet.channelEntry[i].channelIndex;
                        ...
                        m_channel[channelIndex]->ProcessPacketData( packet.channelEntry[i], packetSequence );
                        ...
                    }

                    return true;
                }




16. let us look at the ReadPacket(); function. We are literally just putting converting from packetData --> ConnectionPacket. 

                yojimbo.cpp

                static bool ReadPacket( void * context, 
                        MessageFactory & messageFactory, 
                        const ConnectionConfig & connectionConfig, 
                        ConnectionPacket & packet, 
                        const uint8_t * buffer, 
                        int bufferSize )
                {
                    ...

                    ReadStream stream( messageFactory.GetAllocator(), buffer, bufferSize );
                    
                    stream.SetContext( context );
                    
                    if ( !packet.SerializeInternal( stream, messageFactory, connectionConfig ) )
                    {
                        yojimbo_printf( YOJIMBO_LOG_LEVEL_ERROR, "error: serialize connection packet failed (read packet)\n" );
                        return false;
                    }

                    return true;
                }



17. once we converted our bytes data to a ConnectionPacket, lets take a look at the ProcessPacketData(); function            

    as you can see, its either a ProcessPacketFragment(); function or a ProcessPacketMessages(); we are more interested in the ProcessPacketFragment(); logic path

                yojimbo.cpp

                void ReliableOrderedChannel::ProcessPacketData( const ChannelPacketData & packetData, uint16_t packetSequence )
                {
                    if ( m_errorLevel != CHANNEL_ERROR_NONE )
                        return;
                    
                    if ( packetData.messageFailedToSerialize )
                    {
                        // A message failed to serialize read for some reason, eg. mismatched read/write.
                        SetErrorLevel( CHANNEL_ERROR_FAILED_TO_SERIALIZE );
                        return;
                    }

                    (void)packetSequence;

                    if ( packetData.blockMessage )
                    {
                        ProcessPacketFragment( packetData.block.messageType, 
                                               packetData.block.messageId, 
                                               packetData.block.numFragments, 
                                               packetData.block.fragmentId, 
                                               packetData.block.fragmentData, 
                                               packetData.block.fragmentSize, 
                                               packetData.block.message );
                    }
                    else
                    {
                        ProcessPacketMessages( packetData.message.numMessages, packetData.message.messages );
                    }
                }



18. The ProcessPacketMessages just adds a fragment to the block. then we check if we are done assembling this block

    -   in the last part, we check if we are done assembling the message. if so, we push the message to the m_messageReceiveQueue

                if ( m_receiveBlock->numReceivedFragments == m_receiveBlock->numFragments )
                {
                    ...
                    MessageReceiveQueueEntry * entry = m_messageReceiveQueue->Insert( messageId );
                    ...
                }

    
    -   full code below:

                yojimbo.cpp

                void ReliableOrderedChannel::ProcessPacketFragment( int messageType, 
                                                                    uint16_t messageId, 
                                                                    int numFragments, 
                                                                    uint16_t fragmentId, 
                                                                    const uint8_t * fragmentData, 
                                                                    int fragmentBytes, 
                                                                    BlockMessage * blockMessage )
                {  
                    ...

                    if ( fragmentData )
                    {
                        const uint16_t expectedMessageId = m_messageReceiveQueue->GetSequence();
                        if ( messageId != expectedMessageId )
                            return;

                        // start receiving a new block
                        if ( !m_receiveBlock->active )
                        {
                            ...

                            m_receiveBlock->active = true;
                            m_receiveBlock->numFragments = numFragments;
                            m_receiveBlock->numReceivedFragments = 0;
                            m_receiveBlock->messageId = messageId;
                            m_receiveBlock->blockSize = 0;
                            m_receiveBlock->receivedFragment->Clear();
                        }

                        ...
                        ...


                        // receive the fragment
                        if ( !m_receiveBlock->receivedFragment->GetBit( fragmentId ) )
                        {
                            m_receiveBlock->receivedFragment->SetBit( fragmentId );

                            memcpy( m_receiveBlock->blockData + fragmentId * m_config.blockFragmentSize, fragmentData, fragmentBytes );

                            if ( fragmentId == 0 )
                            {
                                m_receiveBlock->messageType = messageType;
                            }

                            if ( fragmentId == m_receiveBlock->numFragments - 1 )
                            {
                                m_receiveBlock->blockSize = ( m_receiveBlock->numFragments - 1 ) * m_config.blockFragmentSize + fragmentBytes;

                                if ( m_receiveBlock->blockSize > (uint32_t) m_config.maxBlockSize )
                                {
                                    // The block size is outside range
                                    SetErrorLevel( CHANNEL_ERROR_DESYNC );
                                    return;
                                }
                            }

                            m_receiveBlock->numReceivedFragments++;

                            if ( fragmentId == 0 )
                            {
                                // save block message (sent with fragment 0)
                                m_receiveBlock->blockMessage = blockMessage;
                                m_messageFactory->AcquireMessage( m_receiveBlock->blockMessage );
                            }

                            if ( m_receiveBlock->numReceivedFragments == m_receiveBlock->numFragments )
                            {
                                ...
                                ...

                                blockMessage = m_receiveBlock->blockMessage;

                                ...

                                uint8_t * blockData = (uint8_t*) YOJIMBO_ALLOCATE( m_messageFactory->GetAllocator(), m_receiveBlock->blockSize );

                                ...

                                memcpy( blockData, m_receiveBlock->blockData, m_receiveBlock->blockSize );

                                blockMessage->AttachBlock( m_messageFactory->GetAllocator(), blockData, m_receiveBlock->blockSize );

                                blockMessage->SetId( messageId );

                                MessageReceiveQueueEntry * entry = m_messageReceiveQueue->Insert( messageId );
                                ...
                                entry->message = blockMessage;
                                m_receiveBlock->active = false;
                                m_receiveBlock->blockMessage = NULL;
                            }
                        }
                    }
                }








#####################################################################################################
############################### ProcessAck ##########################################################
#####################################################################################################


19. now lets look at how ProcessAck(); work. we assume that the receiver sent back an ack for the message and the sender is processing this ack.
    -   recall that if we are sending a block message, "sentPacketEntry->numMessageIds;" is set to 0, and "sentPacketEntry->messageIds is null"
        so we essentially skip the first for loop if we are processing a block message

    -   you can see that we first find the SentPacketEntry. We first check if it is a block message and we check if this SentPacketEntry matches our m_sendBlock

    -   and we only process this fragment block message if we havent acknowledge it 

    -   if we have offically completed sending this message with 

                if ( m_sendBlock->numAckedFragments == m_sendBlock->numFragments )

        then we reset m_sendBlock; and we call UpdateOldestUnackedMessageId();


    -   full code below:

                yojimbo.cpp

                void ReliableOrderedChannel::ProcessAck( uint16_t ack )
                {
                    SentPacketEntry * sentPacketEntry = m_sentPackets->Find( ack );
                    if ( !sentPacketEntry )
                        return;

                    ...

                    for ( int i = 0; i < (int) sentPacketEntry->numMessageIds; ++i )
                    {
                        const uint16_t messageId = sentPacketEntry->messageIds[i];
                        MessageSendQueueEntry * sendQueueEntry = m_messageSendQueue->Find( messageId );
                        if ( sendQueueEntry )
                        {
                            ...
                            m_messageFactory->ReleaseMessage( sendQueueEntry->message );
                            m_messageSendQueue->Remove( messageId );
                            UpdateOldestUnackedMessageId();
                        }
                    }

                    if ( !m_config.disableBlocks && sentPacketEntry->block && m_sendBlock->active && m_sendBlock->blockMessageId == sentPacketEntry->blockMessageId )
                    {        
                        const int messageId = sentPacketEntry->blockMessageId;
                        const int fragmentId = sentPacketEntry->blockFragmentId;

                        if ( !m_sendBlock->ackedFragment->GetBit( fragmentId ) )
                        {
                            m_sendBlock->ackedFragment->SetBit( fragmentId );
                            m_sendBlock->numAckedFragments++;
                            if ( m_sendBlock->numAckedFragments == m_sendBlock->numFragments )
                            {
                                m_sendBlock->active = false;
                                MessageSendQueueEntry * sendQueueEntry = m_messageSendQueue->Find( messageId );
                                yojimbo_assert( sendQueueEntry );
                                m_messageFactory->ReleaseMessage( sendQueueEntry->message );
                                m_messageSendQueue->Remove( messageId );
                                UpdateOldestUnackedMessageId();
                            }
                        }
                    }
                }







20.  going back to the PumpConnectionUpdate(); function the receiver obviously doesnt do anything in the GeneratePacket(); function 

                void PumpConnectionUpdate( ConnectionConfig & connectionConfig, double & time, Connection & sender, Connection & receiver, uint16_t & senderSequence, uint16_t & receiverSequence, float deltaTime = 0.1f, int packetLossPercent = 90 )
                {
                    uint8_t * packetData = (uint8_t*) alloca( connectionConfig.maxPacketSize );

                    int packetBytes;
                    if ( sender.GeneratePacket( NULL, senderSequence, packetData, connectionConfig.maxPacketSize, packetBytes ) )
                    {
                        if ( random_int( 0, 100 ) >= packetLossPercent )
                        {
                            receiver.ProcessPacket( NULL, senderSequence, packetData, packetBytes );
                            sender.ProcessAcks( &senderSequence, 1 );
                        }
                    }

                    if ( receiver.GeneratePacket( NULL, receiverSequence, packetData, connectionConfig.maxPacketSize, packetBytes ) )
                    {
                        if ( random_int( 0, 100 ) >= packetLossPercent )
                        {
                            sender.ProcessPacket( NULL, receiverSequence, packetData, packetBytes );
                            receiver.ProcessAcks( &receiverSequence, 1 );
                        }
                    }

                    time += deltaTime;

                    sender.AdvanceTime( time );
                    receiver.AdvanceTime( time );

                    senderSequence++;
                    receiverSequence++;
                }




21.  going back to the PumpConnectionUpdate(); function the receiver obviously doesnt do anything in the GeneratePacket(); function 


                void test_connection_reliable_ordered_blocks()
                {
                    TestMessageFactory messageFactory( GetDefaultAllocator() );

                    double time = 100.0;

                    ...............................................................
                    .......... initializing Sender and Receiver ...................
                    ...............................................................

                    const int NumMessagesSent = 32;


                    ........................................................................
                    .......... initializing Block Messages to the Sender ...................
                    ........................................................................

                    uint16_t senderSequence = 0;
                    uint16_t receiverSequence = 0;


                    for ( int i = 0; i < NumIterations; ++i )
                    {
                        PumpConnectionUpdate( connectionConfig, time, sender, receiver, senderSequence, receiverSequence );

                        while ( true )
                        {
                            Message * message = receiver.ReceiveMessage( 0 );
                            if ( !message )
                                break;

                            ...

                            TestBlockMessage * blockMessage = (TestBlockMessage*) message;

                            const int blockSize = blockMessage->GetBlockSize();
                            const uint8_t * blockData = blockMessage->GetBlockData();


                            for ( int j = 0; j < blockSize; ++j )
                            {
                                check( blockData[j] == uint8_t( numMessagesReceived + j ) );
                            }

                            ++numMessagesReceived;

                            messageFactory.ReleaseMessage( message );
                        }

                        if ( numMessagesReceived == NumMessagesSent )
                            break;
                    }
                }



22. so now we want to look at receive.ReceiveMessage();
    recall in receiver.ProcessPacket(); we push a message to our m_messageReceiveQueue if the block message is done assembling.
    here we actually process the message 

                Message * ReliableOrderedChannel::ReceiveMessage()
                {
                    ...

                    MessageReceiveQueueEntry * entry = m_messageReceiveQueue->Find( m_receiveMessageId );
                    if ( !entry )
                        return NULL;

                    Message * message = entry->message;
                    ...
                    m_messageReceiveQueue->Remove( m_receiveMessageId );
                    m_counters[CHANNEL_COUNTER_MESSAGES_RECEIVED]++;
                    m_receiveMessageId++;

                    return message;
                }

























