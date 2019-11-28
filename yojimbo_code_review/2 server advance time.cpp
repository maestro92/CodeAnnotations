


1.  lets look at the server.AdvanceTime(); function

                int ServerMain()
                {                    
                    double time = 100.0;

                    ...
                    ...

                    while ( !quit )
                    {
                        server.SendPackets();

                        server.ReceivePackets();

                        time += deltaTime;

    --------------->    server.AdvanceTime( time );

                        if ( !server.IsRunning() )
                            break;

                        yojimbo_sleep( deltaTime );
                    }

                    server.Stop();

                    return 0;
                }



2.  lets look ath 

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


3.  let us look at the BaseServer::AdvanceTime(); function 


                yojimbo.cpp


                void BaseServer::AdvanceTime( double time )
                {
                    m_time = time;
                    if ( IsRunning() )
                    {
                        for ( int i = 0; i < m_maxClients; ++i )
                        {
                            m_clientConnection[i]->AdvanceTime( time );
                            
                            ...
                            ...

                            reliable_endpoint_update( m_clientEndpoint[i], m_time );
                            int numAcks;
                            const uint16_t * acks = reliable_endpoint_get_acks( m_clientEndpoint[i], &numAcks );
                            m_clientConnection[i]->ProcessAcks( acks, numAcks );
                            reliable_endpoint_clear_acks( m_clientEndpoint[i] );
                        }
                        NetworkSimulator * networkSimulator = GetNetworkSimulator();
                        if ( networkSimulator )
                        {
                            networkSimulator->AdvanceTime( time );
                        }        
                    }
                }





4.  for each channel, we call AdvanceTime();

                yojimbo.cpp

                void Connection::AdvanceTime( double time )
                {
                    for ( int i = 0; i < m_connectionConfig.numChannels; ++i )
                    {
                        m_channel[i]->AdvanceTime( time );

                        ...
                        ...
                    }

                    ...
                    ...
                }




5.  let us look at reliable_endpoint_update();

                reliable.c

                void reliable_endpoint_update( struct reliable_endpoint_t * endpoint, double time )
                {
                    reliable_assert( endpoint );

                    endpoint->time = time;
                    
                    // calculate packet loss
                    {
                        uint32_t base_sequence = ( endpoint->sent_packets->sequence - endpoint->config.sent_packets_buffer_size + 1 ) + 0xFFFF;
                        int i;
                        int num_dropped = 0;
                        int num_samples = endpoint->config.sent_packets_buffer_size / 2;
                        for ( i = 0; i < num_samples; ++i )
                        {
                            uint16_t sequence = (uint16_t) ( base_sequence + i );
                            struct reliable_sent_packet_data_t * sent_packet_data = (struct reliable_sent_packet_data_t*) 
                                reliable_sequence_buffer_find( endpoint->sent_packets, sequence );
                            if ( sent_packet_data && !sent_packet_data->acked )
                            {
                                num_dropped++;
                            }
                        }
                        float packet_loss = ( (float) num_dropped ) / ( (float) num_samples ) * 100.0f;
                        if ( fabs( endpoint->packet_loss - packet_loss ) > 0.00001 )
                        {
                            endpoint->packet_loss += ( packet_loss - endpoint->packet_loss ) * endpoint->config.packet_loss_smoothing_factor;
                        }
                        else
                        {
                            endpoint->packet_loss = packet_loss;
                        }
                    }

                    // calculate sent bandwidth
                    {
                        uint32_t base_sequence = ( endpoint->sent_packets->sequence - endpoint->config.sent_packets_buffer_size + 1 ) + 0xFFFF;
                        int i;
                        int bytes_sent = 0;
                        double start_time = FLT_MAX;
                        double finish_time = 0.0;
                        int num_samples = endpoint->config.sent_packets_buffer_size / 2;
                        for ( i = 0; i < num_samples; ++i )
                        {
                            uint16_t sequence = (uint16_t) ( base_sequence + i );
                            struct reliable_sent_packet_data_t * sent_packet_data = (struct reliable_sent_packet_data_t*) 
                                reliable_sequence_buffer_find( endpoint->sent_packets, sequence );
                            if ( !sent_packet_data )
                            {
                                continue;
                            }
                            bytes_sent += sent_packet_data->packet_bytes;
                            if ( sent_packet_data->time < start_time )
                            {
                                start_time = sent_packet_data->time;
                            }
                            if ( sent_packet_data->time > finish_time )
                            {
                                finish_time = sent_packet_data->time;
                            }
                        }
                        if ( start_time != FLT_MAX && finish_time != 0.0 )
                        {
                            float sent_bandwidth_kbps = (float) ( ( (double) bytes_sent ) / ( finish_time - start_time ) * 8.0f / 1000.0f );
                            if ( fabs( endpoint->sent_bandwidth_kbps - sent_bandwidth_kbps ) > 0.00001 )
                            {
                                endpoint->sent_bandwidth_kbps += ( sent_bandwidth_kbps - endpoint->sent_bandwidth_kbps ) * endpoint->config.bandwidth_smoothing_factor;
                            }
                            else
                            {
                                endpoint->sent_bandwidth_kbps = sent_bandwidth_kbps;
                            }
                        }
                    }

                    // calculate received bandwidth
                    {
                        uint32_t base_sequence = ( endpoint->received_packets->sequence - endpoint->config.received_packets_buffer_size + 1 ) + 0xFFFF;
                        int i;
                        int bytes_sent = 0;
                        double start_time = FLT_MAX;
                        double finish_time = 0.0;
                        int num_samples = endpoint->config.received_packets_buffer_size / 2;
                        for ( i = 0; i < num_samples; ++i )
                        {
                            uint16_t sequence = (uint16_t) ( base_sequence + i );
                            struct reliable_received_packet_data_t * received_packet_data = (struct reliable_received_packet_data_t*) 
                                reliable_sequence_buffer_find( endpoint->received_packets, sequence );
                            if ( !received_packet_data )
                            {
                                continue;
                            }
                            bytes_sent += received_packet_data->packet_bytes;
                            if ( received_packet_data->time < start_time )
                            {
                                start_time = received_packet_data->time;
                            }
                            if ( received_packet_data->time > finish_time )
                            {
                                finish_time = received_packet_data->time;
                            }
                        }
                        if ( start_time != FLT_MAX && finish_time != 0.0 )
                        {
                            float received_bandwidth_kbps = (float) ( ( (double) bytes_sent ) / ( finish_time - start_time ) * 8.0f / 1000.0f );
                            if ( fabs( endpoint->received_bandwidth_kbps - received_bandwidth_kbps ) > 0.00001 )
                            {
                                endpoint->received_bandwidth_kbps += ( received_bandwidth_kbps - endpoint->received_bandwidth_kbps ) * endpoint->config.bandwidth_smoothing_factor;
                            }
                            else
                            {
                                endpoint->received_bandwidth_kbps = received_bandwidth_kbps;
                            }
                        }
                    }

                    // calculate acked bandwidth
                    {
                        uint32_t base_sequence = ( endpoint->sent_packets->sequence - endpoint->config.sent_packets_buffer_size + 1 ) + 0xFFFF;
                        int i;
                        int bytes_sent = 0;
                        double start_time = FLT_MAX;
                        double finish_time = 0.0;
                        int num_samples = endpoint->config.sent_packets_buffer_size / 2;
                        for ( i = 0; i < num_samples; ++i )
                        {
                            uint16_t sequence = (uint16_t) ( base_sequence + i );
                            struct reliable_sent_packet_data_t * sent_packet_data = (struct reliable_sent_packet_data_t*) 
                                reliable_sequence_buffer_find( endpoint->sent_packets, sequence );
                            if ( !sent_packet_data || !sent_packet_data->acked )
                            {
                                continue;
                            }
                            bytes_sent += sent_packet_data->packet_bytes;
                            if ( sent_packet_data->time < start_time )
                            {
                                start_time = sent_packet_data->time;
                            }
                            if ( sent_packet_data->time > finish_time )
                            {
                                finish_time = sent_packet_data->time;
                            }
                        }
                        if ( start_time != FLT_MAX && finish_time != 0.0 )
                        {
                            float acked_bandwidth_kbps = (float) ( ( (double) bytes_sent ) / ( finish_time - start_time ) * 8.0f / 1000.0f );
                            if ( fabs( endpoint->acked_bandwidth_kbps - acked_bandwidth_kbps ) > 0.00001 )
                            {
                                endpoint->acked_bandwidth_kbps += ( acked_bandwidth_kbps - endpoint->acked_bandwidth_kbps ) * endpoint->config.bandwidth_smoothing_factor;
                            }
                            else
                            {
                                endpoint->acked_bandwidth_kbps = acked_bandwidth_kbps;
                            }
                        }
                    }
                }




6.  let us look at reliable_endpoint_update();

                uint16_t * reliable_endpoint_get_acks( struct reliable_endpoint_t * endpoint, int * num_acks )
                {
                    reliable_assert( endpoint );
                    reliable_assert( num_acks );
                    *num_acks = endpoint->num_acks;
                    return endpoint->acks;
                }
