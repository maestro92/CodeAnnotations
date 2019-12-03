

So as mentioned in this post below:
https://gafferongames.com/post/reliable_ordered_messages/


        "a common approach to reliability in games is to have two packet types: reliable-ordered and unreliable. You'll see this approach 
        in many network libraries
        
        The basic idea is that the library resends reliable packets until they are received by the other side. This is the option that usually 
        ends up looking a bit like TCP-lite for the reliable-packets. 
        "


let us look at how yojimbo implements it;

the idea is that a connection between server and client will have multiple message channels. The channels lets you specify 
different reliability and ordering guarantees for messages sent across a connection. 


in yojimbo, we have two types of ChannelType

                yojimbo.h 

                enum ChannelType
                {
                    CHANNEL_TYPE_RELIABLE_ORDERED,                              ///< Messages are received reliably and in the same order they were sent. 
                    CHANNEL_TYPE_UNRELIABLE_UNORDERED                           ///< Messages are sent unreliably. Messages may arrive out of order, or not at all.
                };


the comments says it all

                yojimbo.h

                /** 
                    Configuration properties for a message channel.
                 
                    Channels let you specify different reliability and ordering guarantees for messages sent across a connection.
                 
                    They may be configured as one of two types: reliable-ordered or unreliable-unordered.
                 
                    Reliable ordered channels guarantee that messages (see Message) are received reliably and in the same order they were sent. 
                    This channel type is designed for control messages and RPCs sent between the client and server.
                
                    Unreliable unordered channels are like UDP. There is no guarantee that messages will arrive, and messages may arrive out of order.
                    This channel type is designed for data that is time critical and should not be resent if dropped, like snapshots of world state sent rapidly 
                    from server to client, or cosmetic events such as effects and sounds.
                    
                    Both channel types support blocks of data attached to messages (see BlockMessage), but their treatment of blocks is quite different.
                    
                    Reliable ordered channels are designed for blocks that must be received reliably and in-order with the rest of the messages sent over the channel. 
                    Examples of these sort of blocks include the initial state of a level, or server configuration data sent down to a client on connect. These blocks 
                    are sent by splitting them into fragments and resending each fragment until the other side has received the entire block. This allows for sending
                    blocks of data larger that maximum packet size quickly and reliably even under packet loss.
                    
                    Unreliable-unordered channels send blocks as-is without splitting them up into fragments. The idea is that transport level packet fragmentation
                    should be used on top of the generated packet to split it up into into smaller packets that can be sent across typical Internet MTU (<1500 bytes). 
                    Because of this, you need to make sure that the maximum block size for an unreliable-unordered channel fits within the maximum packet size.
                    
                    Channels are typically configured as part of a ConnectionConfig, which is included inside the ClientServerConfig that is passed into the Client and Server constructors.
                 */











we will look how the UnreliableUnorderedChannel works

A note about block messages


        Both channel types support blocks of data attached to messages (see BlockMessage), but their treatment of blocks is quite different.
        
        Reliable ordered channels are designed for blocks that must be received reliably and in-order with the rest of the messages sent over the channel. 
        Examples of these sort of blocks include the initial state of a level, or server configuration data sent down to a client on connect. These blocks 
        are sent by splitting them into fragments and resending each fragment until the other side has received the entire block. This allows for sending
        blocks of data larger that maximum packet size quickly and reliably even under packet loss.
        
        Unreliable-unordered channels send blocks as-is without splitting them up into fragments. The idea is that transport level packet fragmentation
        should be used on top of the generated packet to split it up into into smaller packets that can be sent across typical Internet MTU (<1500 bytes). 
        Because of this, you need to make sure that the maximum block size for an unreliable-unordered channel fits within the maximum packet size.
        

so the block message on the UnreliableUnorderedChannel, we dont actually split up block message




1.  as mentioned, all messages go through this SendMessage(); function 
    
    notice that it doesnt have a sequence number 

    and we push message to m_messageSendQueue();


-   also notice that even in the UnreliableUnorderedChannel, we can send block message

                yojimbo.cpp 

                void UnreliableUnorderedChannel::SendMessage( Message * message, void *context )
                {
                    yojimbo_assert( message );
                    yojimbo_assert( CanSendMessage() );
                    (void)context;

                    ...
                    ...

                    yojimbo_assert( !( message->IsBlockMessage() && m_config.disableBlocks ) );

                    if ( message->IsBlockMessage() && m_config.disableBlocks )
                    {
                        SetErrorLevel( CHANNEL_ERROR_BLOCKS_DISABLED );
                        m_messageFactory->ReleaseMessage( message );
                        return;
                    }

                    if ( message->IsBlockMessage() )
                    {
                        yojimbo_assert( ((BlockMessage*)message)->GetBlockSize() > 0 );
                        yojimbo_assert( ((BlockMessage*)message)->GetBlockSize() <= m_config.maxBlockSize );
                    }

    ----------->    m_messageSendQueue->Push( message );

                    m_counters[CHANNEL_COUNTER_MESSAGES_SENT]++;
                }



2.  the UnreliableUnorderedChannel class has two queues 

                yojimbo.h

                class UnreliableUnorderedChannel : public Channel
                {

                    ...
                    ...

                    Queue<Message*> * m_messageSendQueue;                   ///< Message send queue.
                    Queue<Message*> * m_messageReceiveQueue;                ///< Message receive queue.

                    ...
                }




3.  now lets look at how the UnreliableUnorderedChannel actually sends out packets 


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




4.  in send packets, we will call GeneratePackets();

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
                                if ( GetClientConnection(i).GeneratePacket( GetContext(), packetSequence, packetData, m_config.maxPacketSize, packetBytes ) )
                                {
                                    reliable_endpoint_send_packet( GetClientEndpoint(i), packetData, packetBytes );
                                }
                            }
                        }
                    }
                }


5.  here we are returning packetSequence. 

                relaible.c

                uint16_t reliable_endpoint_next_packet_sequence( struct reliable_endpoint_t * endpoint )
                {
                    reliable_assert( endpoint );
                    return endpoint->sequence;
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


-   GeneratePacket(); is just gonna go through messages in the m_messageSendQueue and generate bytes from it. 
    
    that happens in m_channel[channelIndex]->GetPacketData(); where we retrieve the bytes. 


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




7.  first we check if m_messageSendQueue->IsEmpty();, we return
    
-   notice we are jamming multiple messages in a single packet, like what Eric said 

                Message ** messages = (Message**) alloca( sizeof( Message* ) * m_config.maxMessagesPerPacket );


-   everytime we send a message, we pop(); messages from the m_messageSendQueue

                Message * message = m_messageSendQueue->Pop();


-   if its a block message, we call SerializeMessageBlock();

                if ( message->IsBlockMessage() )
                {
                    BlockMessage * blockMessage = (BlockMessage*) message;
                    SerializeMessageBlock( measureStream, *m_messageFactory, blockMessage, m_config.maxBlockSize );
                }


-   full code below:

                yojimbo.cpp 

                int UnreliableUnorderedChannel::GetPacketData( void *context, ChannelPacketData & packetData, uint16_t packetSequence, int availableBits )
                {
                    (void) packetSequence;

                    if ( m_messageSendQueue->IsEmpty() )
                        return 0;

                    if ( m_config.packetBudget > 0 )
                        availableBits = yojimbo_min( m_config.packetBudget * 8, availableBits );

                    const int giveUpBits = 4 * 8;

                    const int messageTypeBits = bits_required( 0, m_messageFactory->GetNumTypes() - 1 );

                    int usedBits = ConservativeMessageHeaderBits;
                    int numMessages = 0;
                    Message ** messages = (Message**) alloca( sizeof( Message* ) * m_config.maxMessagesPerPacket );

                    while ( true )
                    {
                        if ( m_messageSendQueue->IsEmpty() )
                            break;

                        if ( availableBits - usedBits < giveUpBits )
                            break;

                        if ( numMessages == m_config.maxMessagesPerPacket )
                            break;

                        Message * message = m_messageSendQueue->Pop();

                        ...

                        MeasureStream measureStream( m_messageFactory->GetAllocator() );
                        measureStream.SetContext( context );
                        message->SerializeInternal( measureStream );
                        
                        if ( message->IsBlockMessage() )
                        {
                            BlockMessage * blockMessage = (BlockMessage*) message;
                            SerializeMessageBlock( measureStream, *m_messageFactory, blockMessage, m_config.maxBlockSize );
                        }

                        const int messageBits = messageTypeBits + measureStream.GetBitsProcessed();
                        
                        if ( usedBits + messageBits > availableBits )
                        {
                            m_messageFactory->ReleaseMessage( message );
                            continue;
                        }

                        usedBits += messageBits;        

                        yojimbo_assert( usedBits <= availableBits );

                        messages[numMessages++] = message;
                    }

                    if ( numMessages == 0 )
                        return 0;

                    Allocator & allocator = m_messageFactory->GetAllocator();

                    packetData.Initialize();
                    packetData.channelIndex = GetChannelIndex();
                    packetData.message.numMessages = numMessages;
                    packetData.message.messages = (Message**) YOJIMBO_ALLOCATE( allocator, sizeof( Message* ) * numMessages );
                    for ( int i = 0; i < numMessages; ++i )
                    {
                        packetData.message.messages[i] = messages[i];
                    }

                    return usedBits;
                }




8.  lets look at what SerializeMessageBlock(); do.
    

                yojimbo.cpp

                template <typename Stream> bool SerializeMessageBlock( Stream & stream, MessageFactory & messageFactory, BlockMessage * blockMessage, int maxBlockSize )
                {
                    int blockSize = Stream::IsWriting ? blockMessage->GetBlockSize() : 0;

                    serialize_int( stream, blockSize, 1, maxBlockSize );

                    uint8_t * blockData;

                    if ( Stream::IsReading )
                    {
                        Allocator & allocator = messageFactory.GetAllocator();
                        blockData = (uint8_t*) YOJIMBO_ALLOCATE( allocator, blockSize );
                        if ( !blockData )
                        {
                            yojimbo_printf( YOJIMBO_LOG_LEVEL_ERROR, "error: failed to allocate message block (SerializeMessageBlock)\n" );
                            return false;
                        }
                        blockMessage->AttachBlock( allocator, blockData, blockSize );
                    }                   
                    else
                    {
                        blockData = blockMessage->GetBlockData();
                    } 

                    serialize_bytes( stream, blockData, blockSize );

                    return true;
                }



#########################################################################################
############################# Receiving Packet ##########################################
#########################################################################################




1.  


                yojimbo.cpp

                bool Connection::ProcessPacket( void * context, uint16_t packetSequence, const uint8_t * packetData, int packetBytes )
                {
                    ...

                    ConnectionPacket packet;

                    if ( !ReadPacket( context, *m_messageFactory, m_connectionConfig, packet, packetData, packetBytes ) )
                    {
                        yojimbo_printf( YOJIMBO_LOG_LEVEL_ERROR, "error: failed to read packet\n" );
                        m_errorLevel = CONNECTION_ERROR_READ_PACKET_FAILED;
                        return false;            
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




2.  as you can see, we are just pushing our messages into "m_messageReceiveQueue"
    another thing is that, you can see the messages sets their id to the packet sequence id.


                void UnreliableUnorderedChannel::ProcessPacketData( const ChannelPacketData & packetData, uint16_t packetSequence )
                {
                    if ( m_errorLevel != CHANNEL_ERROR_NONE )
                        return;
                    
                    if ( packetData.messageFailedToSerialize )
                    {
                        SetErrorLevel( CHANNEL_ERROR_FAILED_TO_SERIALIZE );
                        return;
                    }

                    for ( int i = 0; i < (int) packetData.message.numMessages; ++i )
                    {
                        Message * message = packetData.message.messages[i];
                        yojimbo_assert( message );  
                        message->SetId( packetSequence );
                        if ( !m_messageReceiveQueue->IsFull() )
                        {
                            m_messageFactory->AcquireMessage( message );
                            m_messageReceiveQueue->Push( message );
                        }
                    }
                }











                while ( true )
                {
                    Message * message = server.ReceiveMessage( clientIndex, RELIABLE_ORDERED_CHANNEL );
                    if ( !message )
                        break;

                    yojimbo_assert( message->GetId() == (uint16_t) numMessagesReceivedFromClient );

                    switch ( message->GetType() )
                    {
                        case TEST_MESSAGE:
                        {
                            TestMessage * testMessage = (TestMessage*) message;
                            yojimbo_assert( testMessage->sequence == uint16_t( numMessagesReceivedFromClient ) );
                            printf( "server received message %d\n", testMessage->sequence );
                            server.ReleaseMessage( clientIndex, message );
                            numMessagesReceivedFromClient++;
                        }
                        break;

                        case TEST_BLOCK_MESSAGE:
                        {
                            TestBlockMessage * blockMessage = (TestBlockMessage*) message;
                            yojimbo_assert( blockMessage->sequence == uint16_t( numMessagesReceivedFromClient ) );
                            const int blockSize = blockMessage->GetBlockSize();
                            const int expectedBlockSize = 1 + ( int( numMessagesReceivedFromClient ) * 33 ) % MaxBlockSize;
                            if ( blockSize  != expectedBlockSize )
                            {
                                printf( "error: block size mismatch. expected %d, got %d\n", expectedBlockSize, blockSize );
                                return 1;
                            }
                            const uint8_t * blockData = blockMessage->GetBlockData();
                            yojimbo_assert( blockData );
                            for ( int i = 0; i < blockSize; ++i )
                            {
                                if ( blockData[i] != uint8_t( numMessagesReceivedFromClient + i ) )
                                {
                                    printf( "error: block data mismatch. expected %d, but blockData[%d] = %d\n", uint8_t( numMessagesReceivedFromClient + i ), i, blockData[i] );
                                    return 1;
                                }
                            }
                            printf( "server received message %d\n", uint16_t( numMessagesReceivedFromClient ) );
                            server.ReleaseMessage( clientIndex, message );
                            numMessagesReceivedFromClient++;
                        }
                        break;
                    }
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