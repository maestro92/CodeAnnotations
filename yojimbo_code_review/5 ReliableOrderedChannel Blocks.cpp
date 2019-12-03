


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








                yojimbo.h 

let us look at the MessageSendQueueEntry. One thing to note is the block integer. 

note the comment:
                1 if this is a block message. Block messages are treated differently to regular messages when sent over a reliable-ordered channel.




                struct MessageSendQueueEntry
                {
                    Message * message;                  ///< Pointer to the message. When inserted in the send queue the message has one reference. It is released when the message is acked and removed from the send queue.
                    double timeLastSent;                ///< The time the message was last sent. Used to implement ChannelConfig::messageResendTime.
                    uint32_t measuredBits : 31;         ///< The number of bits the message takes up in a bit stream.
                    uint32_t block : 1;                 ///< 1 if this is a block message. Block messages are treated differently to regular messages when sent over a reliable-ordered channel.
                };





















##################################################################################
####################### Testing ##################################################
##################################################################################

if you look at test.cpp, there is actually multiple functions testing the reliable_ordered_messages
let us look at how the tests is set up

1.  first we initialize the sender and receiver with the default ConnectionConfig. 


                void Server::SendPackets()
                {
                    if ( m_server )
                    {
                        const int maxClients = GetMaxClients();
                        for ( int i = 0; i < maxClients; ++i )
                        {
                            if ( IsClientConnected( i ) )
                            {
                                uint8_t * packetData = GetPacketBuffer();
                                int packetBytes;
                                uint16_t packetSequence = reliable_endpoint_next_packet_sequence( GetClientEndpoint(i) );
        ------------------->    if ( GetClientConnection(i).GeneratePacket( GetContext(), packetSequence, packetData, m_config.maxPacketSize, packetBytes ) )
                                {
                                    reliable_endpoint_send_packet( GetClientEndpoint(i), packetData, packetBytes );
                                }
                            }
                        }
                    }
                }



2.  let us see what is Connection::GeneratePacket(); doing. Here we are actaully calling into the connection class and it is doing the following few things 

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
    so let us look at what is going on in there. 

                yojimbo.cpp 

                int ReliableOrderedChannel::GetPacketData( void *context, ChannelPacketData & packetData, uint16_t packetSequence, int availableBits )
                {
                    ...

                    if ( SendingBlockMessage() )
                    {
                        if (m_config.blockFragmentSize * 8 > availableBits)
                            return 0;

                        uint16_t messageId;
                        uint16_t fragmentId;
                        int fragmentBytes;
                        int numFragments;
                        int messageType;

                        uint8_t * fragmentData = GetFragmentToSend( messageId, fragmentId, fragmentBytes, numFragments, messageType );

                        if ( fragmentData )
                        {
                            const int fragmentBits = GetFragmentPacketData( packetData, messageId, fragmentId, fragmentData, fragmentBytes, numFragments, messageType );
                            AddFragmentPacketEntry( messageId, fragmentId, packetSequence );
                            return fragmentBits;
                        }
                    }
                    else
                    {
                        ..........................................
                        ....... Sending regular messages .........
                        ..........................................
                    }

                    return 0;
                }



8.  first we check if we have started sending this block. If we havent started sending this block, we initalize all the right variables to the m_sendBlock.
    m_sendBlock is essentially initalized with all the related values regarding this block message that we are sending. 

                yojimbo.cpp

                uint8_t * ReliableOrderedChannel::GetFragmentToSend( uint16_t & messageId, uint16_t & fragmentId, int & fragmentBytes, int & numFragments, int & messageType )
                {
                    MessageSendQueueEntry * entry = m_messageSendQueue->Find( m_oldestUnackedMessageId );

                    ...                

                    BlockMessage * blockMessage = (BlockMessage*) entry->message;

                    ...

                    messageId = blockMessage->GetId();

                    const int blockSize = blockMessage->GetBlockSize();

                    if ( !m_sendBlock->active )
                    {
                        // start sending this block

                        m_sendBlock->active = true;
                        m_sendBlock->blockSize = blockSize;
                        m_sendBlock->blockMessageId = messageId;
                        m_sendBlock->numFragments = (int) ceil( blockSize / float( m_config.blockFragmentSize ) );
                        m_sendBlock->numAckedFragments = 0;

                        const int MaxFragmentsPerBlock = m_config.GetMaxFragmentsPerBlock();

                        ...

                        m_sendBlock->ackedFragment->Clear();

                        for ( int i = 0; i < MaxFragmentsPerBlock; ++i )
                            m_sendBlock->fragmentSendTime[i] = -1.0;
                    }

                    ...
                    ...

                    return fragmentData;
                }





9.  now that we have started sending our block message, we continue on with our logic. essentially we are getting the data for our next fragment

essentially there are three parts to thsi function

-   first we find the next fragment we want to send. we look for the one that is not yet acknowledged and well pass the resendtime threshold 
    we have this resendtime threshold becuz we dont want to spam the receiver side with this fragment. 

                fragmentId = 0xFFFF;

                for ( int i = 0; i < m_sendBlock->numFragments; ++i )
                {
                    if ( !m_sendBlock->ackedFragment->GetBit( i ) && m_sendBlock->fragmentSendTime[i] + m_config.blockFragmentResendTime < m_time )
                    {
                        fragmentId = uint16_t( i );
                        break;
                    }
                }

                if ( fragmentId == 0xFFFF )
                    return NULL;



-   then we return a copy of th efragment data. 
    in c++, the memcpy(dst, source, bytes);
    so by calling 

                memcpy( fragmentData,  
                        blockMessage->GetBlockData() + fragmentId * m_config.blockFragmentSize,  
                        fragmentBytes );

    so here we are copying from 

                blockMessage->GetBlockData() + fragmentId * m_config.blockFragmentSize, 

    to 

                fragmentData



-   see full code below:                

                yojimbo.cpp

                uint8_t * ReliableOrderedChannel::GetFragmentToSend( uint16_t & messageId, uint16_t & fragmentId, int & fragmentBytes, int & numFragments, int & messageType )
                {
                    MessageSendQueueEntry * entry = m_messageSendQueue->Find( m_oldestUnackedMessageId );

                    ...                

                    BlockMessage * blockMessage = (BlockMessage*) entry->message;

                    ...

                    messageId = blockMessage->GetId();

                    const int blockSize = blockMessage->GetBlockSize();

                    if ( !m_sendBlock->active )
                    {
                        // start sending this block

                        m_sendBlock->active = true;
                        m_sendBlock->blockSize = blockSize;
                        m_sendBlock->blockMessageId = messageId;
                        m_sendBlock->numFragments = (int) ceil( blockSize / float( m_config.blockFragmentSize ) );
                        m_sendBlock->numAckedFragments = 0;

                        const int MaxFragmentsPerBlock = m_config.GetMaxFragmentsPerBlock();

                        ...

                        m_sendBlock->ackedFragment->Clear();

                        for ( int i = 0; i < MaxFragmentsPerBlock; ++i )
                            m_sendBlock->fragmentSendTime[i] = -1.0;
                    }

                    numFragments = m_sendBlock->numFragments;



                    // find the next fragment to send (there may not be one)
                    fragmentId = 0xFFFF;

                    for ( int i = 0; i < m_sendBlock->numFragments; ++i )
                    {
                        if ( !m_sendBlock->ackedFragment->GetBit( i ) && m_sendBlock->fragmentSendTime[i] + m_config.blockFragmentResendTime < m_time )
                        {
                            fragmentId = uint16_t( i );
                            break;
                        }
                    }

                    if ( fragmentId == 0xFFFF )
                        return NULL;




                    // allocate and return a copy of the fragment data
                    messageType = blockMessage->GetType();
                    fragmentBytes = m_config.blockFragmentSize;
                    
                    const int fragmentRemainder = blockSize % m_config.blockFragmentSize;

                    if ( fragmentRemainder && fragmentId == m_sendBlock->numFragments - 1 )
                        fragmentBytes = fragmentRemainder;

                    uint8_t * fragmentData = (uint8_t*) YOJIMBO_ALLOCATE( m_messageFactory->GetAllocator(), fragmentBytes );

                    if ( fragmentData )
                    {
                        memcpy( fragmentData, blockMessage->GetBlockData() + fragmentId * m_config.blockFragmentSize, fragmentBytes );

                        m_sendBlock->fragmentSendTime[fragmentId] = m_time;
                    }

                    return fragmentData;
                }







10.  back to the GetPacketData(); function. once we have retrieved the GetFragmentToSend(); function, we call AddFragmentPacketEntry();

                yojimbo.cpp

                int ReliableOrderedChannel::GetPacketData( void *context, ChannelPacketData & packetData, uint16_t packetSequence, int availableBits )
                {
                    ...

                    if ( SendingBlockMessage() )
                    {
                        if (m_config.blockFragmentSize * 8 > availableBits)
                            return 0;

                        uint16_t messageId;
                        uint16_t fragmentId;
                        int fragmentBytes;
                        int numFragments;
                        int messageType;

                        uint8_t * fragmentData = GetFragmentToSend( messageId, fragmentId, fragmentBytes, numFragments, messageType );

                        if ( fragmentData )
                        {
                            const int fragmentBits = GetFragmentPacketData( packetData, messageId, fragmentId, fragmentData, fragmentBytes, numFragments, messageType );
                            AddFragmentPacketEntry( messageId, fragmentId, packetSequence );
                            return fragmentBits;
                        }
                    }
                    else
                    {
                        ..........................................
                        ....... Sending regular messages .........
                        ..........................................
                    }

                    return 0;
                }



11. in this function, we are essentially filling in information inside packetData.

                int ReliableOrderedChannel::GetFragmentPacketData( ChannelPacketData & packetData, 
                                                                   uint16_t messageId, 
                                                                   uint16_t fragmentId, 
                                                                   uint8_t * fragmentData, 
                                                                   int fragmentSize, 
                                                                   int numFragments, 
                                                                   int messageType )
                {
                    packetData.Initialize();

                    packetData.channelIndex = GetChannelIndex();

                    packetData.blockMessage = 1;

                    packetData.block.fragmentData = fragmentData;
                    packetData.block.messageId = messageId;
                    packetData.block.fragmentId = fragmentId;
                    packetData.block.fragmentSize = fragmentSize;
                    packetData.block.numFragments = numFragments;
                    packetData.block.messageType = messageType;

                    const int messageTypeBits = bits_required( 0, m_messageFactory->GetNumTypes() - 1 );

                    int fragmentBits = ConservativeFragmentHeaderBits + fragmentSize * 8;

                    if ( fragmentId == 0 )
                    {
                        MessageSendQueueEntry * entry = m_messageSendQueue->Find( packetData.block.messageId );

                        ...
                        ...

                        packetData.block.message = (BlockMessage*) entry->message;

                        m_messageFactory->AcquireMessage( packetData.block.message );

                        fragmentBits += entry->measuredBits + messageTypeBits;
                    }
                    else
                    {
                        packetData.block.message = NULL;
                    }

                    return fragmentBits;
                }







12. let us look at the AddFragmentPacketEntry(); here essentially, we are adding a record of our fragment to our SentPacketEntry;
notice that inside SentPacketEntry, some of the variables are used for sending messages, others are used for sending blockmessages
 
-   as you can see, numMessageIds and messageIds are used when sending regular messages 

                sentPacket->numMessageIds = 0;
                sentPacket->messageIds = NULL;

    the blockMessageId and blockFragmentId are used when sending block message 

                sentPacket->block = 1;
                sentPacket->blockMessageId = messageId;
                sentPacket->blockFragmentId = fragmentId;

-   full code below:

                yojimbo.cpp 

                void ReliableOrderedChannel::AddFragmentPacketEntry( uint16_t messageId, uint16_t fragmentId, uint16_t sequence )
                {
                    SentPacketEntry * sentPacket = m_sentPackets->Insert( sequence );

                    if ( sentPacket )
                    {
                        sentPacket->numMessageIds = 0;
                        sentPacket->messageIds = NULL;
                        sentPacket->timeSent = m_time;
                        sentPacket->acked = 0;
                        sentPacket->block = 1;
                        sentPacket->blockMessageId = messageId;
                        sentPacket->blockFragmentId = fragmentId;
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

14  so the GeneratePacket(); function retrieving data from all channels and puts it inside packetData
    now the receiver wants to process the packet 

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




                Message * UnreliableUnorderedChannel::ReceiveMessage()
                {
                    if ( GetErrorLevel() != CHANNEL_ERROR_NONE )
                        return NULL;

                    if ( m_messageReceiveQueue->IsEmpty() )
                        return NULL;

                    m_counters[CHANNEL_COUNTER_MESSAGES_RECEIVED]++;

                    return m_messageReceiveQueue->Pop();
                }











11. in this function, we are essentially filling in information inside packetData.

                int ReliableOrderedChannel::GetFragmentPacketData( ChannelPacketData & packetData, 
                                                                   uint16_t messageId, 
                                                                   uint16_t fragmentId, 
                                                                   uint8_t * fragmentData, 
                                                                   int fragmentSize, 
                                                                   int numFragments, 
                                                                   int messageType )
                {
                    packetData.Initialize();

                    packetData.channelIndex = GetChannelIndex();

                    packetData.blockMessage = 1;

                    packetData.block.fragmentData = fragmentData;
                    packetData.block.messageId = messageId;
                    packetData.block.fragmentId = fragmentId;
                    packetData.block.fragmentSize = fragmentSize;
                    packetData.block.numFragments = numFragments;
                    packetData.block.messageType = messageType;

                    const int messageTypeBits = bits_required( 0, m_messageFactory->GetNumTypes() - 1 );

                    int fragmentBits = ConservativeFragmentHeaderBits + fragmentSize * 8;

                    if ( fragmentId == 0 )
                    {
                        MessageSendQueueEntry * entry = m_messageSendQueue->Find( packetData.block.messageId );

                        ...
                        ...

                        packetData.block.message = (BlockMessage*) entry->message;

                        m_messageFactory->AcquireMessage( packetData.block.message );

                        fragmentBits += entry->measuredBits + messageTypeBits;
                    }
                    else
                    {
                        packetData.block.message = NULL;
                    }

                    return fragmentBits;
                }







12. let us look at the AddFragmentPacketEntry(); here essentially, we are adding a record of our fragment to our SentPacketEntry;
notice that inside SentPacketEntry, some of the variables are used for sending messages, others are used for sending blockmessages
 
-   as you can see, numMessageIds and messageIds are used when sending regular messages 

                sentPacket->numMessageIds = 0;
                sentPacket->messageIds = NULL;

    the blockMessageId and blockFragmentId are used when sending block message 

                sentPacket->block = 1;
                sentPacket->blockMessageId = messageId;
                sentPacket->blockFragmentId = fragmentId;

-   full code below:

                yojimbo.cpp 

                void ReliableOrderedChannel::AddFragmentPacketEntry( uint16_t messageId, uint16_t fragmentId, uint16_t sequence )
                {
                    SentPacketEntry * sentPacket = m_sentPackets->Insert( sequence );

                    if ( sentPacket )
                    {
                        sentPacket->numMessageIds = 0;
                        sentPacket->messageIds = NULL;
                        sentPacket->timeSent = m_time;
                        sentPacket->acked = 0;
                        sentPacket->block = 1;
                        sentPacket->blockMessageId = messageId;
                        sentPacket->blockFragmentId = fragmentId;
                    }
                }



