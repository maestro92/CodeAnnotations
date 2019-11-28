1.  lets look at how the server code works 

                server.cspp 

                int ServerMain()
                {
                    printf( "started server on port %d (insecure)\n", ServerPort );

                    double time = 100.0;

                    ClientServerConfig config;
                    ...
                    ...

                    Server server( GetDefaultAllocator(), privateKey, Address( "127.0.0.1", ServerPort ), config, adapter, time );

                    server.Start( MaxClients );

                    char addressString[256];
                    server.GetAddress().ToString( addressString, sizeof( addressString ) );
                    ...

                    const double deltaTime = 0.01f;

                    signal( SIGINT, interrupt_handler );    

                    while ( !quit )
                    {
                        server.SendPackets();

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

#############################################################################
########################### Server SendPackets(); ###########################
#############################################################################


2.  lets look at how server.SendPackets(); work 
    as you can see, we are just looping through clients and calling GeneratePacket(); 

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


3.  lets look at how reliable_endpoint_next_packet_sequence(); work 

                uint16_t reliable_endpoint_next_packet_sequence( struct reliable_endpoint_t * endpoint )
                {
                    reliable_assert( endpoint );
                    return endpoint->sequence;
                }




4.  let us look at reliable_endpoint_send_packet(); In this function 

    -   at first, we get our packet sequence number, and we increase the endpoint->sequence number 

                uint16_t sequence = endpoint->sequence++;


    -   then we generate the ack_bits. More on this later 

    -   then we insert this sequence to our "endpoint->sent_packets" sequence_buffer.


    -   full code below:

                reliable.c

                void reliable_endpoint_send_packet( struct reliable_endpoint_t * endpoint, uint8_t * packet_data, int packet_bytes )
                {
                    ...

                    if ( packet_bytes > endpoint->config.max_packet_size )
                    {
                        reliable_printf( RELIABLE_LOG_LEVEL_ERROR, "[%s] packet too large to send. packet is %d bytes, maximum is %d\n", 
                            endpoint->config.name, packet_bytes, endpoint->config.max_packet_size );
                        endpoint->counters[RELIABLE_ENDPOINT_COUNTER_NUM_PACKETS_TOO_LARGE_TO_SEND]++;
                        return;
                    }

                    uint16_t sequence = endpoint->sequence++;
                    uint16_t ack;
                    uint32_t ack_bits;

                    reliable_sequence_buffer_generate_ack_bits( endpoint->received_packets, &ack, &ack_bits );

                    ...

                    struct reliable_sent_packet_data_t * sent_packet_data = (struct reliable_sent_packet_data_t*) reliable_sequence_buffer_insert( endpoint->sent_packets, sequence );

                    ...

                    sent_packet_data->time = endpoint->time;
                    sent_packet_data->packet_bytes = endpoint->config.packet_header_size + packet_bytes;
                    sent_packet_data->acked = 0;

                    if ( packet_bytes <= endpoint->config.fragment_above )
                    {
                        // regular packet
                        ......................................................
                        ............ Sending Regular Packet ..................
                        ......................................................
                    }
                    else
                    {
                        // fragmented packet
                        .........................................................
                        ............ Sending Fragmented Packet ..................
                        .........................................................
                    }

                    endpoint->counters[RELIABLE_ENDPOINT_COUNTER_NUM_PACKETS_SENT]++;
                }




5.  lets look at how reliable_sequence_buffer_generate_ack_bits(); 
    recall in this article,
    https://gafferongames.com/post/reliability_ordering_and_congestion_avoidance_over_udp/

                [uint protocol id]
                [uint sequence]
                [uint ack]
                [uint ack bitfield]
                <em>(packet data...)</em>


    Glenn mentioned that for our protocol we will be including 33 acks. So here, we will be building 33 acks bit field here 


                reliable.c

                void reliable_sequence_buffer_generate_ack_bits( struct reliable_sequence_buffer_t * sequence_buffer, uint16_t * ack, uint32_t * ack_bits )
                {
                    ...
                    *ack = sequence_buffer->sequence - 1;
                    *ack_bits = 0;
                    uint32_t mask = 1;
                    int i;
                    for ( i = 0; i < 32; ++i )
                    {
                        uint16_t sequence = *ack - ((uint16_t)i);
                        if ( reliable_sequence_buffer_exists( sequence_buffer, sequence ) )
                            *ack_bits |= mask;
                        mask <<= 1;
                    }
                }




6.  lets look at what happens in the regular message case. 

    here we construct the full packet data which includes the packet_bytes and the packet_header_bytes 

    -   we first write the header 

                int packet_header_bytes = reliable_write_packet_header( transmit_packet_data, sequence, ack, ack_bits );

    -   then we write the packet body data to the packet 

                memcpy( transmit_packet_data + packet_header_bytes, packet_data, packet_bytes );

    -   then we transmit the packet calling 

                endpoint->config.transmit_packet_function( endpoint->config.context, endpoint->config.index, sequence, transmit_packet_data, packet_header_bytes + packet_bytes );

        this is actually a function pointer that you can set up.(more on this later);

    -   full code below:

                reliable.c

                void reliable_endpoint_send_packet( struct reliable_endpoint_t * endpoint, uint8_t * packet_data, int packet_bytes )
                {
                    ...

                    uint16_t sequence = endpoint->sequence++;
                    uint16_t ack;
                    uint32_t ack_bits;

                    reliable_sequence_buffer_generate_ack_bits( endpoint->received_packets, &ack, &ack_bits );

                    ...

                    struct reliable_sent_packet_data_t * sent_packet_data = (struct reliable_sent_packet_data_t*) reliable_sequence_buffer_insert( endpoint->sent_packets, sequence );

                    ...

                    sent_packet_data->time = endpoint->time;
                    sent_packet_data->packet_bytes = endpoint->config.packet_header_size + packet_bytes;
                    sent_packet_data->acked = 0;

                    if ( packet_bytes <= endpoint->config.fragment_above )
                    {
                        // regular packet
                        uint8_t * transmit_packet_data = (uint8_t*) endpoint->allocate_function( endpoint->allocator_context, packet_bytes + RELIABLE_MAX_PACKET_HEADER_BYTES );

                        int packet_header_bytes = reliable_write_packet_header( transmit_packet_data, sequence, ack, ack_bits );

                        memcpy( transmit_packet_data + packet_header_bytes, packet_data, packet_bytes );

                        endpoint->config.transmit_packet_function( endpoint->config.context, endpoint->config.index, sequence, transmit_packet_data, packet_header_bytes + packet_bytes );

                        endpoint->free_function( endpoint->allocator_context, transmit_packet_data );
                    }
                    else
                    {
                        // fragmented packet
                        .........................................................
                        ............ Sending Fragmented Packet ..................
                        .........................................................
                    }

                    endpoint->counters[RELIABLE_ENDPOINT_COUNTER_NUM_PACKETS_SENT]++;
                }



7.  if you look at yojimbo.cpp, we setup the function pointer for reliable_config.transmit_packet_function here;

                yojimbo.cpp

                void BaseServer::Start( int maxClients )
                {
                    ...
                    ...
                    reliable_config_t reliable_config;

                    reliable_config.transmit_packet_function = BaseServer::StaticTransmitPacketFunction;

                    ...
                    ...
                }


                void BaseServer::StaticTransmitPacketFunction( void * context, int index, uint16_t packetSequence, uint8_t * packetData, int packetBytes )
                {
                    BaseServer * server = (BaseServer*) context;
                    server->TransmitPacketFunction( index, packetSequence, packetData, packetBytes );
                }



if you look at the definition for reliable_config_t, transmit_packet_function is just a function pointer

                reliable.h

                struct reliable_config_t
                {
                    char name[256];
                    void * context;
                    int index;
                    int max_packet_size;
                    int fragment_above;
                    int max_fragments;
                    int fragment_size;
                    int ack_buffer_size;
                    int sent_packets_buffer_size;
                    int received_packets_buffer_size;
                    int fragment_reassembly_buffer_size;
                    float rtt_smoothing_factor;
                    float packet_loss_smoothing_factor;
                    float bandwidth_smoothing_factor;
                    int packet_header_size;
    ----------->    void (*transmit_packet_function)(void*,int,uint16_t,uint8_t*,int);
                    int (*process_packet_function)(void*,int,uint16_t,uint8_t*,int);
                    void * allocator_context;
                    void * (*allocate_function)(void*,uint64_t);
                    void (*free_function)(void*,void*);
                };



8.  if you look at server->TransmitPacketFunction(); function, you see the following:
                
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










#################################################################################
########################### Sending Fragment Packets ############################
#################################################################################


9.  sending fragment packets is very straight-forward. We just send our fragments


                reliable.c

                void reliable_endpoint_send_packet( struct reliable_endpoint_t * endpoint, uint8_t * packet_data, int packet_bytes )
                {
                    ...

                    uint16_t sequence = endpoint->sequence++;
                    uint16_t ack;
                    uint32_t ack_bits;

                    reliable_sequence_buffer_generate_ack_bits( endpoint->received_packets, &ack, &ack_bits );

                    ...

                    struct reliable_sent_packet_data_t * sent_packet_data = (struct reliable_sent_packet_data_t*) reliable_sequence_buffer_insert( endpoint->sent_packets, sequence );

                    ...

                    sent_packet_data->time = endpoint->time;
                    sent_packet_data->packet_bytes = endpoint->config.packet_header_size + packet_bytes;
                    sent_packet_data->acked = 0;

                    if ( packet_bytes <= endpoint->config.fragment_above )
                    {
                        // regular packet
                        ......................................................
                        ............ Sending Regular Packet ..................
                        ......................................................
                    }
                    else
                    {
                        // fragmented packet

                        uint8_t packet_header[RELIABLE_MAX_PACKET_HEADER_BYTES];

                        memset( packet_header, 0, RELIABLE_MAX_PACKET_HEADER_BYTES );

                        int packet_header_bytes = reliable_write_packet_header( packet_header, sequence, ack, ack_bits );        

                        int num_fragments = ( packet_bytes / endpoint->config.fragment_size ) + ( ( packet_bytes % endpoint->config.fragment_size ) != 0 ? 1 : 0 );

                        ...
                        ...

                        int fragment_buffer_size = RELIABLE_FRAGMENT_HEADER_BYTES + RELIABLE_MAX_PACKET_HEADER_BYTES + endpoint->config.fragment_size;

                        uint8_t * fragment_packet_data = (uint8_t*) endpoint->allocate_function( endpoint->allocator_context, fragment_buffer_size );

                        uint8_t * q = packet_data;

                        uint8_t * end = q + packet_bytes;

                        int fragment_id;
                        for ( fragment_id = 0; fragment_id < num_fragments; ++fragment_id )
                        {
                            uint8_t * p = fragment_packet_data;

                            reliable_write_uint8( &p, 1 );
                            reliable_write_uint16( &p, sequence );
                            reliable_write_uint8( &p, (uint8_t) fragment_id );
                            reliable_write_uint8( &p, (uint8_t) ( num_fragments - 1 ) );

                            if ( fragment_id == 0 )
                            {
                                memcpy( p, packet_header, packet_header_bytes );
                                p += packet_header_bytes;
                            }

                            int bytes_to_copy = endpoint->config.fragment_size;
                            if ( q + bytes_to_copy > end )
                            {
                                bytes_to_copy = (int) ( end - q );
                            }

                            memcpy( p, q, bytes_to_copy );

                            p += bytes_to_copy;
                            q += bytes_to_copy;

                            int fragment_packet_bytes = (int) ( p - fragment_packet_data );

                            endpoint->config.transmit_packet_function( endpoint->config.context, endpoint->config.index, sequence, fragment_packet_data, fragment_packet_bytes );

                            endpoint->counters[RELIABLE_ENDPOINT_COUNTER_NUM_FRAGMENTS_SENT]++;
                        }

                        endpoint->free_function( endpoint->allocator_context, fragment_packet_data );
                    }

                    endpoint->counters[RELIABLE_ENDPOINT_COUNTER_NUM_PACKETS_SENT]++;
                }





#################################################################################
########################### Server Receive Packets(); ###########################
#################################################################################

10. now lets look at how the ReceivePackets(); work 

                server.cspp 

                int ServerMain()
                {
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




11. The netcode_server_receive_packet just returns an array of bytes for you

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








12. let us look at how the reliable_endpoint_receive_packet(); function works 
as you can see, there is the difference between regular packets and fragment packets

                reliable.c

                void reliable_endpoint_receive_packet( struct reliable_endpoint_t * endpoint, uint8_t * packet_data, int packet_bytes )
                {
                    ...
                    ...

                    uint8_t prefix_byte = packet_data[0];

                    if ( ( prefix_byte & 1 ) == 0 )
                    {
                        // regular packet
                        ...................................................
                        .......... Reading Regular Packet ................
                        ...................................................
                    }
                    else
                    {
                        // fragment packet

                        ...................................................
                        .......... Reading Fragment Packet ................
                        ...................................................
                    }
                }


11. let us look at regular packet case 

-   first we do a reliable_sequence_buffer_test_insert check. we only want to insert a packet with a sequence number that is 
    larger than the one we have received. so anything less than what we have received (which are stale packets) we just ignore them

    this is where we differ from TCP. TCP ensures that all packets are delivered. but for us if there is a more recent packet with a more recent sequence number,
    we are gonna use that and ignore the older ones. 


-   full code below:

                reliable.c

                void reliable_endpoint_receive_packet( struct reliable_endpoint_t * endpoint, uint8_t * packet_data, int packet_bytes )
                {
                    ...
                    ...

                    uint8_t prefix_byte = packet_data[0];

                    if ( ( prefix_byte & 1 ) == 0 )
                    {
                        // regular packet
                        endpoint->counters[RELIABLE_ENDPOINT_COUNTER_NUM_PACKETS_RECEIVED]++;

                        uint16_t sequence;
                        uint16_t ack;
                        uint32_t ack_bits;

                        int packet_header_bytes = reliable_read_packet_header( endpoint->config.name, packet_data, packet_bytes, &sequence, &ack, &ack_bits );
                        ...
                        ...

    --------------->    if ( !reliable_sequence_buffer_test_insert( endpoint->received_packets, sequence ) )
                        {
                            reliable_printf( RELIABLE_LOG_LEVEL_DEBUG, "[%s] ignoring stale packet %d\n", endpoint->config.name, sequence );
                            endpoint->counters[RELIABLE_ENDPOINT_COUNTER_NUM_PACKETS_STALE]++;
                            return;
                        }

                        ...
                        ...
                       
                    }
                    else
                    {
                        // fragment packet

                        ...................................................
                        .......... Reading Fragment Packet ................
                        ...................................................
                    }
                }







12. the reliable_sequence_buffer_test_insert is literally just a sequence number compare. 

-   first we check if this is a valid sequence number 
                
                if ( !reliable_sequence_buffer_test_insert( endpoint->received_packets, sequence ) )
                {
                    ...
                    return;
                }


    which calls into:

                reliable.c

                int reliable_sequence_buffer_test_insert( struct reliable_sequence_buffer_t * sequence_buffer, uint16_t sequence )
                {
                    return reliable_sequence_less_than( sequence, sequence_buffer->sequence - ((uint16_t)sequence_buffer->num_entries) ) ? ((uint16_t)0) : ((uint16_t)1);
                }


                

    recall that the reliable_endpoint_t has a received_packets reliable_sequence_buffer_t

                reliable.c

                struct reliable_endpoint_t
                {
                    ...
                    ...

                    struct reliable_sequence_buffer_t * received_packets;

                    ...
                };


    and the reliable_sequence_buffer_t has 

                reliable.c

                struct reliable_sequence_buffer_t
                {
                    void * allocator_context;
                    void * (*allocate_function)(void*,uint64_t);
                    void (*free_function)(void*,void*);
    ----------->    uint16_t sequence;
    ----------->    int num_entries;
                    int entry_stride;
                    uint32_t * entry_sequence;
                    uint8_t * entry_data;
                };





13. back to the top


                reliable.c

                void reliable_endpoint_receive_packet( struct reliable_endpoint_t * endpoint, uint8_t * packet_data, int packet_bytes )
                {
                    ...
                    ...

                    uint8_t prefix_byte = packet_data[0];

                    if ( ( prefix_byte & 1 ) == 0 )
                    {
                        // regular packet
                        endpoint->counters[RELIABLE_ENDPOINT_COUNTER_NUM_PACKETS_RECEIVED]++;

                        .....................................................................
                        ............ Reading Packet Header ..................................
                        .....................................................................

                        if ( !reliable_sequence_buffer_test_insert( endpoint->received_packets, sequence ) )
                        {
                            reliable_printf( RELIABLE_LOG_LEVEL_DEBUG, "[%s] ignoring stale packet %d\n", endpoint->config.name, sequence );
                            endpoint->counters[RELIABLE_ENDPOINT_COUNTER_NUM_PACKETS_STALE]++;
                            return;
                        }

                        ...
                        ...

    --------------->    if ( endpoint->config.process_packet_function( endpoint->config.context, 
                                                                       endpoint->config.index, 
                                                                       sequence, 
                                                                       packet_data + packet_header_bytes, 
                                                                       packet_bytes - packet_header_bytes ) )
                        {
                            ...
                            ...
                        }
                        else
                        {
                            reliable_printf( RELIABLE_LOG_LEVEL_ERROR, "[%s] process packet failed\n", endpoint->config.name );
                        }
                    }
                    else
                    {
                        // fragment packet

                        ...................................................
                        .......... Reading Fragment Packet ................
                        ...................................................
                    }
                }





-   once we know that this function is valid, we call process_packet_function(); to process the packet

    the process_packet_function is another function pointer, just 
                




#############################################################################
########################### reliable_endpoint_t; ############################
#############################################################################



                struct reliable_endpoint_t
                {
                    void * allocator_context;
                    void * (*allocate_function)(void*,uint64_t);
                    void (*free_function)(void*,void*);
                    struct reliable_config_t config;
                    double time;
                    float rtt;
                    float packet_loss;
                    float sent_bandwidth_kbps;
                    float received_bandwidth_kbps;
                    float acked_bandwidth_kbps;
                    int num_acks;
                    uint16_t * acks;
    ----------->    uint16_t sequence;
                    struct reliable_sequence_buffer_t * sent_packets;
                    struct reliable_sequence_buffer_t * received_packets;
                    struct reliable_sequence_buffer_t * fragment_reassembly;
                    uint64_t counters[RELIABLE_ENDPOINT_NUM_COUNTERS];
                };


4.  lets 







                void reliable_endpoint_send_packet( struct reliable_endpoint_t * endpoint, uint8_t * packet_data, int packet_bytes )
                {
                    ...

                    if ( packet_bytes > endpoint->config.max_packet_size )
                    {
                        reliable_printf( RELIABLE_LOG_LEVEL_ERROR, "[%s] packet too large to send. packet is %d bytes, maximum is %d\n", 
                            endpoint->config.name, packet_bytes, endpoint->config.max_packet_size );
                        endpoint->counters[RELIABLE_ENDPOINT_COUNTER_NUM_PACKETS_TOO_LARGE_TO_SEND]++;
                        return;
                    }

                    uint16_t sequence = endpoint->sequence++;
                    uint16_t ack;
                    uint32_t ack_bits;

                    reliable_sequence_buffer_generate_ack_bits( endpoint->received_packets, &ack, &ack_bits );

                    ...

                    struct reliable_sent_packet_data_t * sent_packet_data = (struct reliable_sent_packet_data_t*) reliable_sequence_buffer_insert( endpoint->sent_packets, sequence );

                    ...

                    sent_packet_data->time = endpoint->time;
                    sent_packet_data->packet_bytes = endpoint->config.packet_header_size + packet_bytes;
                    sent_packet_data->acked = 0;

                    if ( packet_bytes <= endpoint->config.fragment_above )
                    {
                        // regular packet
                        uint8_t * transmit_packet_data = (uint8_t*) endpoint->allocate_function( endpoint->allocator_context, packet_bytes + RELIABLE_MAX_PACKET_HEADER_BYTES );

                        int packet_header_bytes = reliable_write_packet_header( transmit_packet_data, sequence, ack, ack_bits );

                        memcpy( transmit_packet_data + packet_header_bytes, packet_data, packet_bytes );

                        endpoint->config.transmit_packet_function( endpoint->config.context, endpoint->config.index, sequence, transmit_packet_data, packet_header_bytes + packet_bytes );

                        endpoint->free_function( endpoint->allocator_context, transmit_packet_data );
                    }
                    else
                    {
                        // fragmented packet

                        uint8_t packet_header[RELIABLE_MAX_PACKET_HEADER_BYTES];

                        memset( packet_header, 0, RELIABLE_MAX_PACKET_HEADER_BYTES );

                        int packet_header_bytes = reliable_write_packet_header( packet_header, sequence, ack, ack_bits );        

                        int num_fragments = ( packet_bytes / endpoint->config.fragment_size ) + ( ( packet_bytes % endpoint->config.fragment_size ) != 0 ? 1 : 0 );

                        ...
                        ...

                        int fragment_buffer_size = RELIABLE_FRAGMENT_HEADER_BYTES + RELIABLE_MAX_PACKET_HEADER_BYTES + endpoint->config.fragment_size;

                        uint8_t * fragment_packet_data = (uint8_t*) endpoint->allocate_function( endpoint->allocator_context, fragment_buffer_size );
                        uint8_t * q = packet_data;
                        uint8_t * end = q + packet_bytes;

                        int fragment_id;
                        for ( fragment_id = 0; fragment_id < num_fragments; ++fragment_id )
                        {
                            uint8_t * p = fragment_packet_data;

                            reliable_write_uint8( &p, 1 );
                            reliable_write_uint16( &p, sequence );
                            reliable_write_uint8( &p, (uint8_t) fragment_id );
                            reliable_write_uint8( &p, (uint8_t) ( num_fragments - 1 ) );

                            if ( fragment_id == 0 )
                            {
                                memcpy( p, packet_header, packet_header_bytes );
                                p += packet_header_bytes;
                            }

                            int bytes_to_copy = endpoint->config.fragment_size;
                            if ( q + bytes_to_copy > end )
                            {
                                bytes_to_copy = (int) ( end - q );
                            }

                            memcpy( p, q, bytes_to_copy );

                            p += bytes_to_copy;
                            q += bytes_to_copy;

                            int fragment_packet_bytes = (int) ( p - fragment_packet_data );

                            endpoint->config.transmit_packet_function( endpoint->config.context, endpoint->config.index, sequence, fragment_packet_data, fragment_packet_bytes );

                            endpoint->counters[RELIABLE_ENDPOINT_COUNTER_NUM_FRAGMENTS_SENT]++;
                        }

                        endpoint->free_function( endpoint->allocator_context, fragment_packet_data );
                    }

                    endpoint->counters[RELIABLE_ENDPOINT_COUNTER_NUM_PACKETS_SENT]++;
                }
