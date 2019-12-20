now let us look at how the network_simulator works

1.  the network_simulator is at yojimbo.h 

as the comments has said: 


    /**
        Simulates packet loss, latency, jitter and duplicate packets.
        This is useful during development, so your game is tested and played under real world conditions, instead of ideal LAN conditions.
        This simulator works on packet send. This means that if you want 125ms of latency (round trip), you must to add 125/2 = 62.5ms of latency to each side.
     */

-   the 4 main parameters are 

                float m_latency;                                ///< Latency in milliseconds
                float m_jitter;                                 ///< Jitter in milliseconds +/-
                float m_packetLoss;                             ///< Packet loss percentage.
                float m_duplicates;                             ///< Duplicate packet percentage

    these are the four things we will use to simulate network conditions 


-   notice we also keep an array of PacketEntrye

                int m_currentIndex;                             ///< Current index in the packet entry array. New packets are inserted here.
                int m_numPacketEntries;                         ///< Number of elements in the packet entry array.
                PacketEntry * m_packetEntries;                  ///< Pointer to dynamically allocated packet entries. This is where buffered packets are stored.                    



-   full code below: 

                yojimbo.h

                class NetworkSimulator
                {
                    private:

                        Allocator * m_allocator;                        ///< The allocator passed in to the constructor. It's used to allocate and free packet data.
                        float m_latency;                                ///< Latency in milliseconds
                        float m_jitter;                                 ///< Jitter in milliseconds +/-
                        float m_packetLoss;                             ///< Packet loss percentage.
                        float m_duplicates;                             ///< Duplicate packet percentage
                        bool m_active;                                  ///< True if network simulator is active, eg. if any of the network settings above are enabled.

                        /// A packet buffered in the network simulator.

                        struct PacketEntry
                        {
                            PacketEntry()
                            {
                                to = 0;
                                deliveryTime = 0.0;
                                packetData = NULL;
                                packetBytes = 0;
                            }

                            int to;                                     ///< To index this packet should be sent to (for server -> client packets).
                            double deliveryTime;                        ///< Delivery time for this packet (seconds).
                            uint8_t * packetData;                       ///< Packet data (owns this pointer).
                            int packetBytes;                            ///< Size of packet in bytes.
                        };

                        double m_time;                                  ///< Current time from last call to advance time.
                        int m_currentIndex;                             ///< Current index in the packet entry array. New packets are inserted here.
                        int m_numPacketEntries;                         ///< Number of elements in the packet entry array.
                        PacketEntry * m_packetEntries;                  ///< Pointer to dynamically allocated packet entries. This is where buffered packets are stored.                    
                };








######################################################################################
######################################### Server #####################################
######################################################################################

lets look at how the server uses this NetworkSimulator;

2.  
                yojimbo.h;

                class BaseServer : public ServerInterface
                {
                    ...
                    ...
                    private:
                        ...
                        NetworkSimulator * m_networkSimulator;                      ///< The network simulator used to simulate packet loss, latency, jitter etc. Optional. 
                        ...
                };


3.  
                yojimbo.cpp

                void Server::TransmitPacketFunction( int clientIndex, uint16_t packetSequence, uint8_t * packetData, int packetBytes )
                {
                    (void) packetSequence;
                    NetworkSimulator * networkSimulator = GetNetworkSimulator();
                    if ( networkSimulator && networkSimulator->IsActive() )
                    {
                        networkSimulator->SendPacket( clientIndex, packetData, packetBytes );
                    }
                    else
                    {
                        netcode_server_send_packet( m_server, clientIndex, packetData, packetBytes );
                    }
                }





#############################################################################
#################### test_client_server_messages ############################
#############################################################################

lets look at one of the test cases. in the test_client_server_messages(); function, we first
set all the parameters for the both client and server network simulator

as mentioned in the comments, with out setup, setting 250 ms latency each way, that results in a total of 500 ms 
latency. 

                test.cpp

                void test_client_server_messages()
                {
                    ...
                    
                    Client client( GetDefaultAllocator(), clientAddress, config, adapter, time );

                    ...

                    Server server( GetDefaultAllocator(), privateKey, serverAddress, config, adapter, time );

                    server.Start( MaxClients );

                    client.SetLatency( 250 );
                    client.SetJitter( 100 );
                    client.SetPacketLoss( 25 );
                    client.SetDuplicates( 25 );

                    server.SetLatency( 250 );
                    server.SetJitter( 100 );
                    server.SetPacketLoss( 25 );
                    server.SetDuplicates( 25 );

                }


3.  in the base Server Start function, we just initalize the m_networkSimulator

                yojimbo.cpp

                void BaseServer::Start( int maxClients )
                {
                    ...
                    ...
                    if ( m_config.networkSimulator )
                    {
                        m_networkSimulator = YOJIMBO_NEW( *m_globalAllocator, NetworkSimulator, *m_globalAllocator, m_config.maxSimulatorPackets, m_time );
                    }

                    ...
                    ...
                }




#############################################################################
############################## sending packets ##############################
#############################################################################

essentially what we are doing can be explained by the following graph

usually when you send a packet from a socket to another, it looks like 


     _______________                                                        ____________________    
    |               |  sendTo                                   receive    |                    |              
    |   Socket      | ---------------------------------------------------> |    Socket          |      
    |               |                                                      |                    |            
    |_______________|                                                      |                    |         
                                                                           |____________________|      



now with the network simulator, it just looks like. For the sender, we actually give it to the network simulator
then after applying delays, jitter, duplicates and packet loss, we eventually send the packet.

     _______________                   ____________________                 ____________________    
    |               | passes it to    |                    |   send To     |                    |              
    |   Socket      | --------------> |  Network Simulator |-------------->|   Home Server      |      
    |               |                 |                    |   receive     |                    |            
    |_______________|                 |                    |               |                    |         
                                      |____________________|               |____________________|      





4.  
                reliable.c

                void reliable_endpoint_send_packet( struct reliable_endpoint_t * endpoint, uint8_t * packet_data, int packet_bytes )
                {
                    ...
                    ...

                    if ( packet_bytes <= endpoint->config.fragment_above )
                    {
                        ...
                        ...

                        endpoint->config.transmit_packet_function( endpoint->config.context, endpoint->config.index, sequence, transmit_packet_data, packet_header_bytes + packet_bytes );

                        ...
                    }
                    else
                    {
                        ...
                    }
                }



5.  recall that the transmit_packet_function(); is a function pointer we set up 

                yojimbo.cpp

                void BaseServer::Start( int maxClients )
                {
                    ...
                    
                    reliable_config.transmit_packet_function = BaseServer::StaticTransmitPacketFunction;

                    ...
                }



6.  in the BaseServer::StaticTransmitPacketFunction(); function, we call server->TransmitPacketFunction();

                void BaseServer::StaticTransmitPacketFunction( void * context, int index, uint16_t packetSequence, uint8_t * packetData, int packetBytes )
                {
                    BaseServer * server = (BaseServer*) context;
                    server->TransmitPacketFunction( index, packetSequence, packetData, packetBytes );
                }



7.  and you can see the SendPacket(); path

                yojimbo.cpp

                void Server::TransmitPacketFunction( int clientIndex, uint16_t packetSequence, uint8_t * packetData, int packetBytes )
                {
                    (void) packetSequence;
                    NetworkSimulator * networkSimulator = GetNetworkSimulator();
                    if ( networkSimulator && networkSimulator->IsActive() )
                    {
    ----------->        networkSimulator->SendPacket( clientIndex, packetData, packetBytes );
                    }
                    else
                    {
                        netcode_server_send_packet( m_server, clientIndex, packetData, packetBytes );
                    }
                }



8.  lets take a look at the NetworkSimulator::SendPacket(); function
    
    here you can see how we achieve each of the 4 things 

-   first we look at packet loss, which is literally just a percentage thing 

-   then we look at latency and jitter. Essentially we add jitter on top of delay.

-   lastly, its duplicates. which is also a percentage thing. 


                yojimbo.cpp

                void NetworkSimulator::SendPacket( int to, uint8_t * packetData, int packetBytes )
                {
                    ...
                    ...

                    if ( random_float( 0.0f, 100.0f ) <= m_packetLoss )
                    {
                        return;
                    }

                    PacketEntry & packetEntry = m_packetEntries[m_currentIndex];

                    if ( packetEntry.packetData )
                    {
                        YOJIMBO_FREE( *m_allocator, packetEntry.packetData );
                        packetEntry = PacketEntry();
                    }

                    double delay = m_latency / 1000.0;

                    if ( m_jitter > 0 )
                        delay += random_float( -m_jitter, +m_jitter ) / 1000.0;

                    packetEntry.to = to;
                    packetEntry.packetData = (uint8_t*) YOJIMBO_ALLOCATE( *m_allocator, packetBytes );
                    memcpy( packetEntry.packetData, packetData, packetBytes );
                    packetEntry.packetBytes = packetBytes;
                    packetEntry.deliveryTime = m_time + delay;
                    m_currentIndex = ( m_currentIndex + 1 ) % m_numPacketEntries;

                    if ( random_float( 0.0f, 100.0f ) <= m_duplicates )
                    {
                        PacketEntry & nextPacketEntry = m_packetEntries[m_currentIndex];
                        nextPacketEntry.to = to;
                        nextPacketEntry.packetData = (uint8_t*) YOJIMBO_ALLOCATE( *m_allocator, packetBytes );
                        memcpy( nextPacketEntry.packetData, packetData, packetBytes );
                        nextPacketEntry.packetBytes = packetBytes;
                        nextPacketEntry.deliveryTime = m_time + delay + random_float( 0, +1.0 );
                        m_currentIndex = ( m_currentIndex + 1 ) % m_numPacketEntries;
                    }
                }







9.  

                yojimbo.cpp

                void Server::AdvanceTime( double time )
                {
                    if ( m_server )
                    {
                        netcode_server_update( m_server, time );
                    }
                    BaseServer::AdvanceTime( time );
                    NetworkSimulator * networkSimulator = GetNetworkSimulator();
                    if ( networkSimulator && networkSimulator->IsActive() )
                    {
                        uint8_t ** packetData = (uint8_t**) alloca( sizeof( uint8_t*) * m_config.maxSimulatorPackets );
                        int * packetBytes = (int*) alloca( sizeof(int) * m_config.maxSimulatorPackets );
                        int * to = (int*) alloca( sizeof(int) * m_config.maxSimulatorPackets );
                        int numPackets = networkSimulator->ReceivePackets( m_config.maxSimulatorPackets, packetData, packetBytes, to );
                        for ( int i = 0; i < numPackets; ++i )
                        {
                            netcode_server_send_packet( m_server, to[i], (uint8_t*) packetData[i], packetBytes[i] );
                            YOJIMBO_FREE( networkSimulator->GetAllocator(), packetData[i] );
                        }
                    }
                }





10. 
                int NetworkSimulator::ReceivePackets( int maxPackets, uint8_t * packetData[], int packetBytes[], int to[] )
                {
                    if ( !IsActive() )
                        return 0;

                    int numPackets = 0;

                    for ( int i = 0; i < yojimbo_min( m_numPacketEntries, maxPackets ); ++i )
                    {
                        if ( !m_packetEntries[i].packetData )
                            continue;

                        if ( m_packetEntries[i].deliveryTime < m_time )
                        {
                            packetData[numPackets] = m_packetEntries[i].packetData;
                            packetBytes[numPackets] = m_packetEntries[i].packetBytes;
                            if ( to )
                            {
                                to[numPackets] = m_packetEntries[i].to;
                            }
                            m_packetEntries[i].packetData = NULL;
                            numPackets++;
                        }
                    }

                    return numPackets;
                }



11. 
                void netcode_server_send_packet( struct netcode_server_t * server, int client_index, NETCODE_CONST uint8_t * packet_data, int packet_bytes )
                {
                    ...
                    ...

                    if ( !server->running )
                        return;

                    ...
                    ...

                    if ( !server->client_connected[client_index] )
                        return;

                    if ( !server->client_loopback[client_index] )
                    {
                        uint8_t buffer[NETCODE_MAX_PAYLOAD_BYTES*2];

                        struct netcode_connection_payload_packet_t * packet = (struct netcode_connection_payload_packet_t*) buffer;

                        packet->packet_type = NETCODE_CONNECTION_PAYLOAD_PACKET;
                        packet->payload_bytes = packet_bytes;
                        memcpy( packet->payload_data, packet_data, packet_bytes );

                        if ( !server->client_confirmed[client_index] )
                        {
                            struct netcode_connection_keep_alive_packet_t keep_alive_packet;
                            keep_alive_packet.packet_type = NETCODE_CONNECTION_KEEP_ALIVE_PACKET;
                            keep_alive_packet.client_index = client_index;
                            keep_alive_packet.max_clients = server->max_clients;
                            netcode_server_send_client_packet( server, &keep_alive_packet, client_index );
                        }

                        netcode_server_send_client_packet( server, packet, client_index );
                    }
                    else
                    {
                        netcode_assert( server->config.send_loopback_packet_callback );

                        server->config.send_loopback_packet_callback( server->config.callback_context,
                                                                      client_index, 
                                                                      packet_data, 
                                                                      packet_bytes, 
                                                                      server->client_sequence[client_index]++ );

                        server->client_last_packet_send_time[client_index] = server->time;
                    }
                }






##################################################################################
####################### Receiving Packets ########################################
##################################################################################


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



