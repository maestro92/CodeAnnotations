


1.  
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



2.  

                void netcode_server_send_packet( struct netcode_server_t * server, int client_index, NETCODE_CONST uint8_t * packet_data, int packet_bytes )
                {
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




3.  
    -   when we send a packet, server->client_sequence[client_index]

    -   full code below:

                void netcode_server_send_client_packet( struct netcode_server_t * server, void * packet, int client_index )
                {
                    netcode_assert( server );
                    netcode_assert( packet );
                    netcode_assert( client_index >= 0 );
                    netcode_assert( client_index < server->max_clients );
                    netcode_assert( server->client_connected[client_index] );
                    netcode_assert( !server->client_loopback[client_index] );

                    uint8_t packet_data[NETCODE_MAX_PACKET_BYTES];

                    if ( !netcode_encryption_manager_touch( &server->encryption_manager, 
                                                            server->client_encryption_index[client_index], 
                                                            &server->client_address[client_index], 
                                                            server->time ) )
                    {
                        netcode_printf( NETCODE_LOG_LEVEL_ERROR, "error: encryption mapping is out of date for client %d\n", client_index );
                        return;
                    }

                    uint8_t * packet_key = netcode_encryption_manager_get_send_key( &server->encryption_manager, server->client_encryption_index[client_index] );

                    int packet_bytes = netcode_write_packet( packet, packet_data, NETCODE_MAX_PACKET_BYTES, server->client_sequence[client_index], packet_key, server->config.protocol_id );

                    netcode_assert( packet_bytes <= NETCODE_MAX_PACKET_BYTES );

                    if ( server->config.network_simulator )
                    {
                        netcode_network_simulator_send_packet( server->config.network_simulator, &server->address, &server->client_address[client_index], packet_data, packet_bytes );
                    }
                    else
                    {
                        if ( server->config.override_send_and_receive )
                        {
                            server->config.send_packet_override( server->config.callback_context, &server->client_address[client_index], packet_data, packet_bytes );
                        }
                        else
                        {
                            if ( server->client_address[client_index].type == NETCODE_ADDRESS_IPV4 )
                            {
                                netcode_socket_send_packet( &server->socket_holder.ipv4, &server->client_address[client_index], packet_data, packet_bytes );
                            }
                            else if ( server->client_address[client_index].type == NETCODE_ADDRESS_IPV6 )
                            {
                                netcode_socket_send_packet( &server->socket_holder.ipv6, &server->client_address[client_index], packet_data, packet_bytes );
                            }
                        }
                    }

                    server->client_sequence[client_index]++;

                    server->client_last_packet_send_time[client_index] = server->time;
                }







4.  the client that is passed in is the "server->client_sequence[client_index]"


                int packet_bytes = netcode_write_packet( packet, packet_data, NETCODE_MAX_PACKET_BYTES, server->client_sequence[client_index], packet_key, server->config.protocol_id );


    -   full code below:

                int netcode_write_packet( void * packet, uint8_t * buffer, int buffer_length, uint64_t sequence, uint8_t * write_packet_key, uint64_t protocol_id )
                {
                    netcode_assert( packet );
                    netcode_assert( buffer );
                    netcode_assert( write_packet_key );

                    (void) buffer_length;

                    uint8_t packet_type = ((uint8_t*)packet)[0];

                    if ( packet_type == NETCODE_CONNECTION_REQUEST_PACKET )
                    {
                        // connection request packet: first byte is zero

                        netcode_assert( buffer_length >= 1 + 13 + 8 + 8 + NETCODE_CONNECT_TOKEN_NONCE_BYTES + NETCODE_CONNECT_TOKEN_PRIVATE_BYTES );

                        struct netcode_connection_request_packet_t * p = (struct netcode_connection_request_packet_t*) packet;

                        uint8_t * start = buffer;

                        netcode_write_uint8( &buffer, NETCODE_CONNECTION_REQUEST_PACKET );
                        netcode_write_bytes( &buffer, p->version_info, NETCODE_VERSION_INFO_BYTES );
                        netcode_write_uint64( &buffer, p->protocol_id );
                        netcode_write_uint64( &buffer, p->connect_token_expire_timestamp );
                        netcode_write_bytes( &buffer, p->connect_token_nonce, NETCODE_CONNECT_TOKEN_NONCE_BYTES );
                        netcode_write_bytes( &buffer, p->connect_token_data, NETCODE_CONNECT_TOKEN_PRIVATE_BYTES );

                        netcode_assert( buffer - start == 1 + 13 + 8 + 8 + NETCODE_CONNECT_TOKEN_NONCE_BYTES + NETCODE_CONNECT_TOKEN_PRIVATE_BYTES );

                        return (int) ( buffer - start );
                    }
                    else
                    {
                        // *** encrypted packets ***

                        // write the prefix byte (this is a combination of the packet type and number of sequence bytes)

                        uint8_t * start = buffer;

                        uint8_t sequence_bytes = (uint8_t) netcode_sequence_number_bytes_required( sequence );

                        netcode_assert( sequence_bytes >= 1 );
                        netcode_assert( sequence_bytes <= 8 );

                        netcode_assert( packet_type <= 0xF );

                        uint8_t prefix_byte = packet_type | ( sequence_bytes << 4 );

                        netcode_write_uint8( &buffer, prefix_byte );

                        // write the variable length sequence number [1,8] bytes.

                        uint64_t sequence_temp = sequence;

                        int i;
                        for ( i = 0; i < sequence_bytes; ++i )
                        {
                            netcode_write_uint8( &buffer, (uint8_t) ( sequence_temp & 0xFF ) );
                            sequence_temp >>= 8;
                        }

                        // write packet data according to type. this data will be encrypted.

                        uint8_t * encrypted_start = buffer;

                        switch ( packet_type )
                        {
                            case NETCODE_CONNECTION_DENIED_PACKET:
                            {
                                // ...
                            }
                            break;

                            case NETCODE_CONNECTION_CHALLENGE_PACKET:
                            {
                                struct netcode_connection_challenge_packet_t * p = (struct netcode_connection_challenge_packet_t*) packet;
                                netcode_write_uint64( &buffer, p->challenge_token_sequence );
                                netcode_write_bytes( &buffer, p->challenge_token_data, NETCODE_CHALLENGE_TOKEN_BYTES );
                            }
                            break;

                            case NETCODE_CONNECTION_RESPONSE_PACKET:
                            {
                                struct netcode_connection_response_packet_t * p = (struct netcode_connection_response_packet_t*) packet;
                                netcode_write_uint64( &buffer, p->challenge_token_sequence );
                                netcode_write_bytes( &buffer, p->challenge_token_data, NETCODE_CHALLENGE_TOKEN_BYTES );
                            }
                            break;

                            case NETCODE_CONNECTION_KEEP_ALIVE_PACKET:
                            {
                                struct netcode_connection_keep_alive_packet_t * p = (struct netcode_connection_keep_alive_packet_t*) packet;
                                netcode_write_uint32( &buffer, p->client_index );
                                netcode_write_uint32( &buffer, p->max_clients );
                            }
                            break;

                            case NETCODE_CONNECTION_PAYLOAD_PACKET:
                            {
                                struct netcode_connection_payload_packet_t * p = (struct netcode_connection_payload_packet_t*) packet;

                                netcode_assert( p->payload_bytes <= NETCODE_MAX_PAYLOAD_BYTES );

                                netcode_write_bytes( &buffer, p->payload_data, p->payload_bytes );
                            }
                            break;

                            case NETCODE_CONNECTION_DISCONNECT_PACKET:
                            {
                                // ...
                            }
                            break;

                            default:
                                netcode_assert( 0 );
                        }

                        netcode_assert( buffer - start <= buffer_length - NETCODE_MAC_BYTES );

                        uint8_t * encrypted_finish = buffer;

                        // encrypt the per-packet packet written with the prefix byte, protocol id and version as the associated data. this must match to decrypt.

                        uint8_t additional_data[NETCODE_VERSION_INFO_BYTES+8+1];
                        {
                            uint8_t * p = additional_data;
                            netcode_write_bytes( &p, NETCODE_VERSION_INFO, NETCODE_VERSION_INFO_BYTES );
                            netcode_write_uint64( &p, protocol_id );
                            netcode_write_uint8( &p, prefix_byte );
                        }

                        uint8_t nonce[12];
                        {
                            uint8_t * p = nonce;
                            netcode_write_uint32( &p, 0 );
                            netcode_write_uint64( &p, sequence );
                        }

                        if ( netcode_encrypt_aead( encrypted_start, 
                                                   encrypted_finish - encrypted_start, 
                                                   additional_data, sizeof( additional_data ), 
                                                   nonce, write_packet_key ) != NETCODE_OK )
                        {
                            return NETCODE_ERROR;
                        }

                        buffer += NETCODE_MAC_BYTES;

                        netcode_assert( buffer - start <= buffer_length );

                        return (int) ( buffer - start );
                    }
                }











5.


                void netcode_socket_send_packet( struct netcode_socket_t * socket, struct netcode_address_t * to, void * packet_data, int packet_bytes )
                {
                    netcode_assert( socket );
                    netcode_assert( socket->handle != 0 );
                    netcode_assert( to );
                    netcode_assert( to->type == NETCODE_ADDRESS_IPV6 || to->type == NETCODE_ADDRESS_IPV4 );
                    netcode_assert( packet_data );
                    netcode_assert( packet_bytes > 0 );

                    if ( to->type == NETCODE_ADDRESS_IPV6 )
                    {
                        struct sockaddr_in6 socket_address;
                        memset( &socket_address, 0, sizeof( socket_address ) );
                        socket_address.sin6_family = AF_INET6;
                        int i;
                        for ( i = 0; i < 8; ++i )
                        {
                            ( (uint16_t*) &socket_address.sin6_addr ) [i] = htons( to->data.ipv6[i] );
                        }
                        socket_address.sin6_port = htons( to->port );
                        int result = sendto( socket->handle, (char*) packet_data, packet_bytes, 0, (struct sockaddr*) &socket_address, sizeof( struct sockaddr_in6 ) );
                        (void) result;
                    }
                    else if ( to->type == NETCODE_ADDRESS_IPV4 )
                    {
                        struct sockaddr_in socket_address;
                        memset( &socket_address, 0, sizeof( socket_address ) );
                        socket_address.sin_family = AF_INET;
                        socket_address.sin_addr.s_addr = ( ( (uint32_t) to->data.ipv4[0] ) )        | 
                                                         ( ( (uint32_t) to->data.ipv4[1] ) << 8 )   | 
                                                         ( ( (uint32_t) to->data.ipv4[2] ) << 16 )  | 
                                                         ( ( (uint32_t) to->data.ipv4[3] ) << 24 );
                        socket_address.sin_port = htons( to->port );
                        int result = sendto( socket->handle, (NETCODE_CONST char*) packet_data, packet_bytes, 0, (struct sockaddr*) &socket_address, sizeof( struct sockaddr_in ) );
                        (void) result;
                    }
                }



6.                 


                uint64_t netcode_server_next_packet_sequence( struct netcode_server_t * server, int client_index )
                {
                    netcode_assert( client_index >= 0 );
                    netcode_assert( client_index < server->max_clients );
                    if ( !server->client_connected[client_index] )
                        return 0;
                    return server->client_sequence[client_index];    
                }
