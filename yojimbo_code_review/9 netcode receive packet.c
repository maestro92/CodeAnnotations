


1.  
    

                void netcode_server_update( struct netcode_server_t * server, double time )
                {
                    netcode_assert( server );
                    server->time = time;
                    netcode_server_receive_packets( server );
                    netcode_server_send_packets( server );
                    netcode_server_check_for_timeouts( server );
                }




2.  
                void netcode_server_receive_packets( struct netcode_server_t * server )
                {
                    netcode_assert( server );

                    uint8_t allowed_packets[NETCODE_CONNECTION_NUM_PACKETS];
                    memset( allowed_packets, 0, sizeof( allowed_packets ) );
                    allowed_packets[NETCODE_CONNECTION_REQUEST_PACKET] = 1;
                    allowed_packets[NETCODE_CONNECTION_RESPONSE_PACKET] = 1;
                    allowed_packets[NETCODE_CONNECTION_KEEP_ALIVE_PACKET] = 1;
                    allowed_packets[NETCODE_CONNECTION_PAYLOAD_PACKET] = 1;
                    allowed_packets[NETCODE_CONNECTION_DISCONNECT_PACKET] = 1;

                    uint64_t current_timestamp = (uint64_t) time( NULL );

                    if ( !server->config.network_simulator )
                    {
                        // process packets received from socket

                        while ( 1 )
                        {
                            struct netcode_address_t from;
                            
                            uint8_t packet_data[NETCODE_MAX_PACKET_BYTES];
                            
                            int packet_bytes = 0;
                            
                            if ( server->config.override_send_and_receive )
                            {
                                packet_bytes = server->config.receive_packet_override( server->config.callback_context, &from, packet_data, NETCODE_MAX_PACKET_BYTES );
                            }
                            else
                            {
                                if (server->socket_holder.ipv4.handle != 0)
                                    packet_bytes = netcode_socket_receive_packet( &server->socket_holder.ipv4, &from, packet_data, NETCODE_MAX_PACKET_BYTES );

                                if ( packet_bytes == 0 && server->socket_holder.ipv6.handle != 0)
                                    packet_bytes = netcode_socket_receive_packet( &server->socket_holder.ipv6, &from, packet_data, NETCODE_MAX_PACKET_BYTES );
                            }

                            if ( packet_bytes == 0 )
                                break;

                            netcode_server_read_and_process_packet( server, &from, packet_data, packet_bytes, current_timestamp, allowed_packets );
                        }
                    }
                    else
                    {
                        // process packets received from network simulator
                        ...................................................................
                        ............. Processing Packet from Network Simulator ............
                        ...................................................................
                    }
                }



3.  lets look at how netcode_socket_receive_packet works(); this is essentially a wrapper 
around the recvfrom(); function

we pretty much populate these two variables

                uint8_t packet_data[NETCODE_MAX_PACKET_BYTES];                
                int packet_bytes = 0;

from this call 

                int netcode_socket_receive_packet( struct netcode_socket_t * socket, struct netcode_address_t * from, void * packet_data, int max_packet_size )
                {
                    ...
                    ...

                    typedef int socklen_t;
                    
                    struct sockaddr_storage sockaddr_from;
                    socklen_t from_length = sizeof( sockaddr_from );

                    int result = recvfrom( socket->handle, (char*) packet_data, max_packet_size, 0, (struct sockaddr*) &sockaddr_from, &from_length );

                    if ( result == SOCKET_ERROR )
                    {
                        ....
                    }

                    ...
                    ...
                  
                    netcode_assert( result >= 0 );

                    int bytes_read = result;

                    return bytes_read;
                }






4.  if this is a new client, both "int client_index" and "encryption_index" will be -1; 
    so we will have to have initalize a new client

-   read_packet_key is also null when "encryption_index" is -1

                void netcode_server_read_and_process_packet( struct netcode_server_t * server, 
                                                             struct netcode_address_t * from, 
                                                             uint8_t * packet_data, 
                                                             int packet_bytes, 
                                                             uint64_t current_timestamp, 
                                                             uint8_t * allowed_packets )
                {
                    if ( !server->running )
                        return;

                    if ( packet_bytes <= 1 )
                        return;

                    uint64_t sequence;

                    int encryption_index = -1;
                    int client_index = netcode_server_find_client_index_by_address( server, from );
                    if ( client_index != -1 )
                    {
                        netcode_assert( client_index >= 0 );
                        netcode_assert( client_index < server->max_clients );
                        encryption_index = server->client_encryption_index[client_index];
                    }
                    else
                    {
                        encryption_index = netcode_encryption_manager_find_encryption_mapping( &server->encryption_manager, from, server->time );
                    }
                    
                    uint8_t * read_packet_key = netcode_encryption_manager_get_receive_key( &server->encryption_manager, encryption_index );

                    if ( !read_packet_key && packet_data[0] != 0 )
                    {
                        char address_string[NETCODE_MAX_ADDRESS_STRING_LENGTH];
                        netcode_printf( NETCODE_LOG_LEVEL_DEBUG, "server could not process packet because no encryption mapping exists for %s\n", netcode_address_to_string( from, address_string ) );
                        return;
                    }

                    void * packet = netcode_read_packet( packet_data, 
                                                         packet_bytes, 
                                                         &sequence, 
                                                         read_packet_key, 
                                                         server->config.protocol_id, 
                                                         current_timestamp, 
                                                         server->config.private_key, 
                                                         allowed_packets, 
                                                         ( client_index != -1 ) ? &server->client_replay_protection[client_index] : NULL, 
                                                         server->config.allocator_context, 
                                                         server->config.allocate_function );

                    if ( !packet )
                        return;

                    netcode_server_process_packet_internal( server, from, packet, sequence, encryption_index, client_index );
                }



5.  first thing is that we find our client by address netcode_server_find_client_index_by_address();

                netcode.c

                int netcode_server_find_client_index_by_address( struct netcode_server_t * server, struct netcode_address_t * address )
                {
                    netcode_assert( server );
                    netcode_assert( address );

                    if ( address->type == 0 )
                        return -1;

                    int i;
                    for ( i = 0; i < server->max_clients; ++i )
                    {   
                        if ( server->client_connected[i] && netcode_address_equal( &server->client_address[i], address ) )
                            return i;
                    }

                    return -1;
                }



6.  

                int netcode_encryption_manager_find_encryption_mapping( struct netcode_encryption_manager_t * encryption_manager, struct netcode_address_t * address, double time )
                {
                    int i;
                    for ( i = 0; i < encryption_manager->num_encryption_mappings; ++i )
                    {
                        if ( netcode_address_equal( &encryption_manager->address[i], address ) && !netcode_encryption_manager_entry_expired( encryption_manager, i, time ) )
                        {
                            encryption_manager->last_access_time[i] = time;
                            return i;
                        }
                    }
                    return -1;
                }








7.  we first check the message type of NETCODE_CONNECTION_REQUEST_PACKET


                void * netcode_read_packet( uint8_t * buffer, 
                                            int buffer_length, 
                                            uint64_t * sequence, 
                                            uint8_t * read_packet_key, 
                                            uint64_t protocol_id, 
                                            uint64_t current_timestamp, 
                                            uint8_t * private_key, 
                                            uint8_t * allowed_packets, 
                                            struct netcode_replay_protection_t * replay_protection, 
                                            void * allocator_context, 
                                            void* (*allocate_function)(void*,uint64_t) )
                {
                    netcode_assert( sequence );
                    netcode_assert( allowed_packets );

                    *sequence = 0;

                    ...
                    ...

                    uint8_t * start = buffer;

                    uint8_t prefix_byte = netcode_read_uint8( &buffer );

                    if ( prefix_byte == NETCODE_CONNECTION_REQUEST_PACKET )
                    {
                        .............................................
                        ............ Validation .....................
                        .............................................

                        uint64_t packet_connect_token_expire_timestamp = netcode_read_uint64( &buffer );
                        if ( packet_connect_token_expire_timestamp <= current_timestamp )
                        {
                            netcode_printf( NETCODE_LOG_LEVEL_DEBUG, "ignored connection request packet. connect token expired\n" );
                            return NULL;
                        }

                        uint8_t packet_connect_token_nonce[NETCODE_CONNECT_TOKEN_NONCE_BYTES];
                        netcode_read_bytes(&buffer, packet_connect_token_nonce, sizeof(packet_connect_token_nonce));

                        netcode_assert( buffer - start == 1 + NETCODE_VERSION_INFO_BYTES + 8 + 8 + NETCODE_CONNECT_TOKEN_NONCE_BYTES );

                        if ( netcode_decrypt_connect_token_private( buffer, 
                                                                    NETCODE_CONNECT_TOKEN_PRIVATE_BYTES, 
                                                                    version_info, 
                                                                    protocol_id, 
                                                                    packet_connect_token_expire_timestamp, 
                                                                    packet_connect_token_nonce, 
                                                                    private_key ) != NETCODE_OK )
                        {
                            netcode_printf( NETCODE_LOG_LEVEL_DEBUG, "ignored connection request packet. connect token failed to decrypt\n" );
                            return NULL;
                        }

                        struct netcode_connection_request_packet_t * packet = (struct netcode_connection_request_packet_t*) 
                            allocate_function( allocator_context, sizeof( struct netcode_connection_request_packet_t ) );

                        ...

                        packet->packet_type = NETCODE_CONNECTION_REQUEST_PACKET;
                        memcpy( packet->version_info, version_info, NETCODE_VERSION_INFO_BYTES );
                        packet->protocol_id = packet_protocol_id;
                        packet->connect_token_expire_timestamp = packet_connect_token_expire_timestamp;
                        memcpy( packet->connect_token_nonce, packet_connect_token_nonce, NETCODE_CONNECT_TOKEN_NONCE_BYTES );
                        netcode_read_bytes( &buffer, packet->connect_token_data, NETCODE_CONNECT_TOKEN_PRIVATE_BYTES );

                        netcode_assert( buffer - start == 1 + NETCODE_VERSION_INFO_BYTES + 8 + 8 + NETCODE_CONNECT_TOKEN_NONCE_BYTES + NETCODE_CONNECT_TOKEN_PRIVATE_BYTES );

                        return packet;
                    }
                    else
                    {
                        // *** encrypted packets ***

                        ...........................................................................
                        .................. Reading the Sequence Number ............................
                        ...........................................................................

                        // ignore the packet if it has already been received

    --------------->    if ( replay_protection && packet_type >= NETCODE_CONNECTION_KEEP_ALIVE_PACKET )
                        {
                            if ( netcode_replay_protection_already_received( replay_protection, *sequence ) )
                            {
                                netcode_printf( NETCODE_LOG_LEVEL_DEBUG, "ignored packet. sequence %.16" PRIx64 " already received (replay protection)\n", *sequence );
                                return NULL;
                            }
                        }

                        // decrypt the per-packet type data

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
                            netcode_write_uint64( &p, *sequence );
                        }

                        int encrypted_bytes = (int) ( buffer_length - ( buffer - start ) );

                        if ( encrypted_bytes < NETCODE_MAC_BYTES )
                        {
                            netcode_printf( NETCODE_LOG_LEVEL_DEBUG, "ignored encrypted packet. encrypted payload is too small\n" );
                            return NULL;
                        }

                        if ( netcode_decrypt_aead( buffer, encrypted_bytes, additional_data, sizeof( additional_data ), nonce, read_packet_key ) != NETCODE_OK )
                        {
                            netcode_printf( NETCODE_LOG_LEVEL_DEBUG, "ignored encrypted packet. failed to decrypt\n" );
                            return NULL;
                        }

                        int decrypted_bytes = encrypted_bytes - NETCODE_MAC_BYTES;

                        // update the latest replay protection sequence #

                        if ( replay_protection && packet_type >= NETCODE_CONNECTION_KEEP_ALIVE_PACKET )
                        {
                            netcode_replay_protection_advance_sequence( replay_protection, *sequence );
                        }

                        // process the per-packet type data that was just decrypted
                        
                        switch ( packet_type )
                        {
                            case NETCODE_CONNECTION_DENIED_PACKET:
                            {
                                if ( decrypted_bytes != 0 )
                                {
                                    netcode_printf( NETCODE_LOG_LEVEL_DEBUG, "ignored connection denied packet. decrypted packet data is wrong size\n" );
                                    return NULL;
                                }

                                struct netcode_connection_denied_packet_t * packet = (struct netcode_connection_denied_packet_t*) 
                                    allocate_function( allocator_context, sizeof( struct netcode_connection_denied_packet_t ) );

                                if ( !packet )
                                {
                                    netcode_printf( NETCODE_LOG_LEVEL_DEBUG, "ignored connection denied packet. could not allocate packet struct\n" );
                                    return NULL;
                                }
                                
                                packet->packet_type = NETCODE_CONNECTION_DENIED_PACKET;
                                
                                return packet;
                            }
                            break;

                            case NETCODE_CONNECTION_CHALLENGE_PACKET:
                            {
                                if ( decrypted_bytes != 8 + NETCODE_CHALLENGE_TOKEN_BYTES )
                                {
                                    netcode_printf( NETCODE_LOG_LEVEL_DEBUG, "ignored connection challenge packet. decrypted packet data is wrong size\n" );
                                    return NULL;
                                }

                                struct netcode_connection_challenge_packet_t * packet = (struct netcode_connection_challenge_packet_t*) 
                                    allocate_function( allocator_context, sizeof( struct netcode_connection_challenge_packet_t ) );

                                if ( !packet )
                                {
                                    netcode_printf( NETCODE_LOG_LEVEL_DEBUG, "ignored connection challenge packet. could not allocate packet struct\n" );
                                    return NULL;
                                }
                                
                                packet->packet_type = NETCODE_CONNECTION_CHALLENGE_PACKET;
                                packet->challenge_token_sequence = netcode_read_uint64( &buffer );
                                netcode_read_bytes( &buffer, packet->challenge_token_data, NETCODE_CHALLENGE_TOKEN_BYTES );
                                
                                return packet;
                            }
                            break;

                            case NETCODE_CONNECTION_RESPONSE_PACKET:
                            {
                                if ( decrypted_bytes != 8 + NETCODE_CHALLENGE_TOKEN_BYTES )
                                {
                                    netcode_printf( NETCODE_LOG_LEVEL_DEBUG, "ignored connection response packet. decrypted packet data is wrong size\n" );
                                    return NULL;
                                }

                                struct netcode_connection_response_packet_t * packet = (struct netcode_connection_response_packet_t*) 
                                    allocate_function( allocator_context, sizeof( struct netcode_connection_response_packet_t ) );

                                if ( !packet )
                                {
                                    netcode_printf( NETCODE_LOG_LEVEL_DEBUG, "ignored connection response packet. could not allocate packet struct\n" );
                                    return NULL;
                                }
                                
                                packet->packet_type = NETCODE_CONNECTION_RESPONSE_PACKET;
                                packet->challenge_token_sequence = netcode_read_uint64( &buffer );
                                netcode_read_bytes( &buffer, packet->challenge_token_data, NETCODE_CHALLENGE_TOKEN_BYTES );
                                
                                return packet;
                            }
                            break;

                            case NETCODE_CONNECTION_KEEP_ALIVE_PACKET:
                            {
                                if ( decrypted_bytes != 8 )
                                {
                                    netcode_printf( NETCODE_LOG_LEVEL_DEBUG, "ignored connection keep alive packet. decrypted packet data is wrong size\n" );
                                    return NULL;
                                }

                                struct netcode_connection_keep_alive_packet_t * packet = (struct netcode_connection_keep_alive_packet_t*) 
                                    allocate_function( allocator_context, sizeof( struct netcode_connection_keep_alive_packet_t ) );

                                if ( !packet )
                                {
                                    netcode_printf( NETCODE_LOG_LEVEL_DEBUG, "ignored connection keep alive packet. could not allocate packet struct\n" );
                                    return NULL;
                                }
                                
                                packet->packet_type = NETCODE_CONNECTION_KEEP_ALIVE_PACKET;
                                packet->client_index = netcode_read_uint32( &buffer );
                                packet->max_clients = netcode_read_uint32( &buffer );
                                
                                return packet;
                            }
                            break;
                            
                            case NETCODE_CONNECTION_PAYLOAD_PACKET:
                            {
                                if ( decrypted_bytes < 1 )
                                {
                                    netcode_printf( NETCODE_LOG_LEVEL_DEBUG, "ignored connection payload packet. payload is too small\n" );
                                    return NULL;
                                }

                                if ( decrypted_bytes > NETCODE_MAX_PAYLOAD_BYTES )
                                {
                                    netcode_printf( NETCODE_LOG_LEVEL_DEBUG, "ignored connection payload packet. payload is too large\n" );
                                    return NULL;
                                }

                                struct netcode_connection_payload_packet_t * packet = netcode_create_payload_packet( decrypted_bytes, allocator_context, allocate_function );

                                if ( !packet )
                                {
                                    netcode_printf( NETCODE_LOG_LEVEL_DEBUG, "ignored connection payload packet. could not allocate packet struct\n" );
                                    return NULL;
                                }
                                
                                memcpy( packet->payload_data, buffer, decrypted_bytes );
                                
                                return packet;
                            }
                            break;

                            case NETCODE_CONNECTION_DISCONNECT_PACKET:
                            {
                                if ( decrypted_bytes != 0 )
                                {
                                    netcode_printf( NETCODE_LOG_LEVEL_DEBUG, "ignored connection disconnect packet. decrypted packet data is wrong size\n" );
                                    return NULL;
                                }

                                struct netcode_connection_disconnect_packet_t * packet = (struct netcode_connection_disconnect_packet_t*) 
                                    allocate_function( allocator_context, sizeof( struct netcode_connection_disconnect_packet_t ) );

                                if ( !packet )
                                {
                                    netcode_printf( NETCODE_LOG_LEVEL_DEBUG, "ignored connection disconnect packet. could not allocate packet struct\n" );
                                    return NULL;
                                }
                                
                                packet->packet_type = NETCODE_CONNECTION_DISCONNECT_PACKET;
                                
                                return packet;
                            }
                            break;

                            default:
                                return NULL;
                        }
                    }
                }








8.  we do replay protection

                struct netcode_replay_protection_t
                {
                    uint64_t most_recent_sequence;
                    uint64_t received_packet[NETCODE_REPLAY_PROTECTION_BUFFER_SIZE];
                };

    -   full code below:

                #define NETCODE_REPLAY_PROTECTION_BUFFER_SIZE 256


                int netcode_replay_protection_already_received( struct netcode_replay_protection_t * replay_protection, uint64_t sequence )
                {
                    ...

                    if ( sequence + NETCODE_REPLAY_PROTECTION_BUFFER_SIZE <= replay_protection->most_recent_sequence )
                        return 1;
                    
                    int index = (int) ( sequence % NETCODE_REPLAY_PROTECTION_BUFFER_SIZE );

                    if ( replay_protection->received_packet[index] == 0xFFFFFFFFFFFFFFFFLL )
                        return 0;

                    if ( replay_protection->received_packet[index] >= sequence )
                        return 1;

                    return 0;
                }




5.  as you can see, the netcode layer only deals with these connections
                
                NETCODE_CONNECTION_REQUEST_PACKET
                NETCODE_CONNECTION_RESPONSE_PACKET
                NETCODE_CONNECTION_KEEP_ALIVE_PACKET
                NETCODE_CONNECTION_PAYLOAD_PACKET
                NETCODE_CONNECTION_DISCONNECT_PACKET



                void netcode_server_process_packet_internal( struct netcode_server_t * server, 
                                                             struct netcode_address_t * from, 
                                                             void * packet, 
                                                             uint64_t sequence, 
                                                             int encryption_index, 
                                                             int client_index )
                {
                    netcode_assert( server );
                    netcode_assert( packet );

                    (void) from;
                    (void) sequence;

                    uint8_t packet_type = ( (uint8_t*) packet ) [0];

                    switch ( packet_type )
                    {
                        case NETCODE_CONNECTION_REQUEST_PACKET:
                        {    
                            if ( ( server->flags & NETCODE_SERVER_FLAG_IGNORE_CONNECTION_REQUEST_PACKETS ) == 0 )
                            {
                                char from_address_string[NETCODE_MAX_ADDRESS_STRING_LENGTH];
                                netcode_printf( NETCODE_LOG_LEVEL_DEBUG, "server received connection request from %s\n", netcode_address_to_string( from, from_address_string ) );
                                netcode_server_process_connection_request_packet( server, from, (struct netcode_connection_request_packet_t*) packet );
                            }
                        }
                        break;

                        case NETCODE_CONNECTION_RESPONSE_PACKET:
                        {    
                            if ( ( server->flags & NETCODE_SERVER_FLAG_IGNORE_CONNECTION_RESPONSE_PACKETS ) == 0 )
                            {
                                char from_address_string[NETCODE_MAX_ADDRESS_STRING_LENGTH];
                                netcode_printf( NETCODE_LOG_LEVEL_DEBUG, "server received connection response from %s\n", netcode_address_to_string( from, from_address_string ) );
                                netcode_server_process_connection_response_packet( server, from, (struct netcode_connection_response_packet_t*) packet, encryption_index );
                            }
                        }
                        break;

                        case NETCODE_CONNECTION_KEEP_ALIVE_PACKET:
                        {
                            if ( client_index != -1 )
                            {
                                netcode_printf( NETCODE_LOG_LEVEL_DEBUG, "server received connection keep alive packet from client %d\n", client_index );
                                server->client_last_packet_receive_time[client_index] = server->time;
                                if ( !server->client_confirmed[client_index] )
                                {
                                    netcode_printf( NETCODE_LOG_LEVEL_DEBUG, "server confirmed connection with client %d\n", client_index );
                                    server->client_confirmed[client_index] = 1;
                                }
                            }
                        }
                        break;

                        case NETCODE_CONNECTION_PAYLOAD_PACKET:
                        {
                            if ( client_index != -1 )
                            {
                                netcode_printf( NETCODE_LOG_LEVEL_DEBUG, "server received connection payload packet from client %d\n", client_index );
                                server->client_last_packet_receive_time[client_index] = server->time;
                                if ( !server->client_confirmed[client_index] )
                                {
                                    netcode_printf( NETCODE_LOG_LEVEL_DEBUG, "server confirmed connection with client %d\n", client_index );
                                    server->client_confirmed[client_index] = 1;
                                }
                                netcode_packet_queue_push( &server->client_packet_queue[client_index], packet, sequence );
                                return;
                            }
                        }
                        break;

                        case NETCODE_CONNECTION_DISCONNECT_PACKET:
                        {
                            if ( client_index != -1 )
                            {
                                netcode_printf( NETCODE_LOG_LEVEL_DEBUG, "server received disconnect packet from client %d\n", client_index );
                                netcode_server_disconnect_client_internal( server, client_index, 0 );
                           }
                        }
                        break;

                        default:
                            break;
                    }

                    server->config.free_function( server->config.allocator_context, packet );
                }







6.  lets see how we do the "netcode_server_process_connection_request_packet" function
                
                netcode.c                

                void netcode_server_process_connection_request_packet( struct netcode_server_t * server, 
                                                                       struct netcode_address_t * from, 
                                                                       struct netcode_connection_request_packet_t * packet )
                {
                    .........................................................................
                    ............... A bunch of Valiations on Connect Token ..................
                    .........................................................................
                    

                    double expire_time = ( connect_token_private.timeout_seconds >= 0 ) ? server->time + connect_token_private.timeout_seconds : -1.0;

                    if ( !netcode_encryption_manager_add_encryption_mapping( &server->encryption_manager, 
                                                                             from, 
                                                                             connect_token_private.server_to_client_key, 
                                                                             connect_token_private.client_to_server_key, 
                                                                             server->time, 
                                                                             expire_time,
                                                                             connect_token_private.timeout_seconds ) )
                    {
                        netcode_printf( NETCODE_LOG_LEVEL_DEBUG, "server ignored connection request. failed to add encryption mapping\n" );
                        return;
                    }

                    ...............................................................
                    .......... Send Connect Challenge to Client ...................
                    ...............................................................
                }






7.  this is where we add the the client as a "pending connection", in the "struct netcode_encryption_manager_t * encryption_manager"

-   encryption_manager->address
    
    is essentially a list of pending connections_s address 


-   as you can see, if you are a brand new client we go through the "for ( i = 0; i < NETCODE_MAX_ENCRYPTION_MAPPINGS; ++i )" for loop
    if we have a empty slot by checking "encryption_manager->address[i].type == NETCODE_ADDRESS_NONE", then we add our pending connection 

-   if this was an existing connection from a previous attempt, then we go through the "for ( i = 0; i < encryption_manager->num_encryption_mappings; ++i )" path 



                netcode.c

                int netcode_encryption_manager_add_encryption_mapping( struct netcode_encryption_manager_t * encryption_manager, 
                                                                       struct netcode_address_t * address, 
                                                                       uint8_t * send_key, 
                                                                       uint8_t * receive_key, 
                                                                       double time, 
                                                                       double expire_time,
                                                                       int timeout )
                {
                    int i;
                    for ( i = 0; i < encryption_manager->num_encryption_mappings; ++i )
                    {
                        if ( netcode_address_equal( &encryption_manager->address[i], address ) && !netcode_encryption_manager_entry_expired( encryption_manager, i, time ) )
                        {
                            encryption_manager->timeout[i] = timeout;
                            encryption_manager->expire_time[i] = expire_time;
                            encryption_manager->last_access_time[i] = time;
                            memcpy( encryption_manager->send_key + i * NETCODE_KEY_BYTES, send_key, NETCODE_KEY_BYTES );
                            memcpy( encryption_manager->receive_key + i * NETCODE_KEY_BYTES, receive_key, NETCODE_KEY_BYTES );
                            return 1;
                        }
                    }

                    for ( i = 0; i < NETCODE_MAX_ENCRYPTION_MAPPINGS; ++i )
                    {
                        if ( encryption_manager->address[i].type == NETCODE_ADDRESS_NONE || netcode_encryption_manager_entry_expired( encryption_manager, i, time ) )
                        {
                            encryption_manager->timeout[i] = timeout;
                            encryption_manager->address[i] = *address;
                            encryption_manager->expire_time[i] = expire_time;
                            encryption_manager->last_access_time[i] = time;
                            memcpy( encryption_manager->send_key + i * NETCODE_KEY_BYTES, send_key, NETCODE_KEY_BYTES );
                            memcpy( encryption_manager->receive_key + i * NETCODE_KEY_BYTES, receive_key, NETCODE_KEY_BYTES );
                            if ( i + 1 > encryption_manager->num_encryption_mappings )
                                encryption_manager->num_encryption_mappings = i + 1;
                            return 1;
                        }
                    }

                    return 0;
                }




7.  
                int netcode_server_find_free_client_index( struct netcode_server_t * server )
                {
                    netcode_assert( server );

                    int i;
                    for ( i = 0; i < server->max_clients; ++i )
                    {
                        if ( !server->client_connected[i] )
                            return i;
                    }

                    return -1;
                }







##############################################################################
################################ Connection Response  ########################
##############################################################################


8.  
                void netcode_server_process_connection_response_packet( struct netcode_server_t * server, 
                                                                        struct netcode_address_t * from, 
                                                                        struct netcode_connection_response_packet_t * packet, 
                                                                        int encryption_index )
                {
                    .........................................................
                    ...................... Validations ......................
                    .........................................................

                    int client_index = netcode_server_find_free_client_index( server );

                    netcode_assert( client_index != -1 );

                    int timeout_seconds = netcode_encryption_manager_get_timeout( &server->encryption_manager, encryption_index );

                    netcode_server_connect_client( server, client_index, from, challenge_token.client_id, encryption_index, timeout_seconds, challenge_token.user_data );
                }






9.  we offically convert this client to become a real client

                void netcode_server_connect_client( struct netcode_server_t * server, 
                                                    int client_index, 
                                                    struct netcode_address_t * address, 
                                                    uint64_t client_id, 
                                                    int encryption_index,
                                                    int timeout_seconds, 
                                                    void * user_data )
                {
                    netcode_assert( server );
                    netcode_assert( server->running );
                    netcode_assert( client_index >= 0 );
                    netcode_assert( client_index < server->max_clients );
                    netcode_assert( address );
                    netcode_assert( encryption_index != -1 );
                    netcode_assert( user_data );

                    server->num_connected_clients++;

                    netcode_assert( server->num_connected_clients <= server->max_clients );

                    netcode_assert( server->client_connected[client_index] == 0 );

                    netcode_encryption_manager_set_expire_time( &server->encryption_manager, encryption_index, -1.0 );

                    server->client_connected[client_index] = 1;
                    server->client_timeout[client_index] = timeout_seconds;
                    server->client_encryption_index[client_index] = encryption_index;
                    server->client_id[client_index] = client_id;
                    server->client_sequence[client_index] = 0;
                    server->client_address[client_index] = *address;
                    server->client_last_packet_send_time[client_index] = server->time;
                    server->client_last_packet_receive_time[client_index] = server->time;
                    memcpy( server->client_user_data[client_index], user_data, NETCODE_USER_DATA_BYTES );

                    char address_string[NETCODE_MAX_ADDRESS_STRING_LENGTH];

                    netcode_printf( NETCODE_LOG_LEVEL_INFO, "server accepted client %s %.16" PRIx64 " in slot %d\n", 
                        netcode_address_to_string( address, address_string ), client_id, client_index );

                    struct netcode_connection_keep_alive_packet_t packet;
                    packet.packet_type = NETCODE_CONNECTION_KEEP_ALIVE_PACKET;
                    packet.client_index = client_index;
                    packet.max_clients = server->max_clients;

                    netcode_server_send_client_packet( server, &packet, client_index );

                    if ( server->config.connect_disconnect_callback )
                    {
                        server->config.connect_disconnect_callback( server->config.callback_context, client_index, 1 );
                    }
                }






##############################################################################
################### NETCODE_CONNECTION_PAYLOAD_PACKET ########################
##############################################################################



8.  whenever we have a connected client, and we have receive connection payload packets,
we push it into netcode_packet_queue_push. and then the application layer will read it.
                
                netcode.c

                void netcode_server_process_packet_internal( struct netcode_server_t * server, 
                                                             struct netcode_address_t * from, 
                                                             void * packet, 
                                                             uint64_t sequence, 
                                                             int encryption_index, 
                                                             int client_index )
                {

                    ...
                    ...

                    case NETCODE_CONNECTION_PAYLOAD_PACKET:
                    {
                        if ( client_index != -1 )
                        {
                            netcode_printf( NETCODE_LOG_LEVEL_DEBUG, "server received connection payload packet from client %d\n", client_index );
                            server->client_last_packet_receive_time[client_index] = server->time;
                            if ( !server->client_confirmed[client_index] )
                            {
                                netcode_printf( NETCODE_LOG_LEVEL_DEBUG, "server confirmed connection with client %d\n", client_index );
                                server->client_confirmed[client_index] = 1;
                            }
                            netcode_packet_queue_push( &server->client_packet_queue[client_index], packet, sequence );
                            return;
                        }
                    }
                    break;

                    ...
                    ...
                }








#################################################################################################
############################ Packet Queue #######################################################
#################################################################################################


9.  we push the packet_sequence and the packet

                netcode.c

                int netcode_packet_queue_push( struct netcode_packet_queue_t * queue, void * packet_data, uint64_t packet_sequence )
                {
                    netcode_assert( queue );
                    netcode_assert( packet_data );
                    if ( queue->num_packets == NETCODE_PACKET_QUEUE_SIZE )
                    {
                        queue->free_function( queue->allocator_context, packet_data );
                        return 0;
                    }
                    int index = ( queue->start_index + queue->num_packets ) % NETCODE_PACKET_QUEUE_SIZE;
                    queue->packet_data[index] = packet_data;
                    queue->packet_sequence[index] = packet_sequence;
                    queue->num_packets++;
                    return 1;
                }


10. then for packet_sequence, we pop the packet and the packet sequence

                void * netcode_packet_queue_pop( struct netcode_packet_queue_t * queue, uint64_t * packet_sequence )
                {
                    if ( queue->num_packets == 0 )
                        return NULL;
                    void * packet = queue->packet_data[queue->start_index];
                    if ( packet_sequence )
                        *packet_sequence = queue->packet_sequence[queue->start_index];
                    queue->start_index = ( queue->start_index + 1 ) % NETCODE_PACKET_QUEUE_SIZE;
                    queue->num_packets--;
                    return packet;
                }










11. 
                yojimbo.cpp 

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



                uint8_t * netcode_server_receive_packet( struct netcode_server_t * server, int client_index, int * packet_bytes, uint64_t * packet_sequence )
                {
                    netcode_assert( server );
                    netcode_assert( packet_bytes );

                    if ( !server->running )
                        return NULL;

                    if ( !server->client_connected[client_index] )
                        return NULL;

                    netcode_assert( client_index >= 0 );
                    netcode_assert( client_index < server->max_clients );

                    struct netcode_connection_payload_packet_t * packet = (struct netcode_connection_payload_packet_t*) 
                        netcode_packet_queue_pop( &server->client_packet_queue[client_index], packet_sequence );
                    
                    if ( packet )
                    {
                        netcode_assert( packet->packet_type == NETCODE_CONNECTION_PAYLOAD_PACKET );
                        *packet_bytes = packet->payload_bytes;
                        netcode_assert( *packet_bytes >= 0 );
                        netcode_assert( *packet_bytes <= NETCODE_MAX_PAYLOAD_BYTES );
                        return (uint8_t*) &packet->payload_data;
                    }
                    else
                    {
                        return NULL;
                    }
                }



#####################################################################################
#################### NETCODE_CONNECTION_PAYLOAD_PACKET ##############################
#####################################################################################




1.  we first call recvfrom and put all the data onto packet_data

                int netcode_socket_receive_packet( struct netcode_socket_t * socket, struct netcode_address_t * from, void * packet_data, int max_packet_size )
                {
                    ...
                    ...

                    typedef int socklen_t;
                    
                    struct sockaddr_storage sockaddr_from;
                    socklen_t from_length = sizeof( sockaddr_from );

                    int result = recvfrom( socket->handle, (char*) packet_data, max_packet_size, 0, (struct sockaddr*) &sockaddr_from, &from_length );

                    if ( result == SOCKET_ERROR )
                    {
                        ....
                    }

                    ...
                    ...
                  
                    netcode_assert( result >= 0 );

                    int bytes_read = result;

                    return bytes_read;
                }


2.  then we pass the packet_data buffer into the netcode_read_packet(); function

you can see that in the packet, we just literally call a memcpy to get all of the data from the 
buffer to the packet 


                void * netcode_read_packet( uint8_t * buffer, 
                                            int buffer_length, 
                                            uint64_t * sequence, 
                                            uint8_t * read_packet_key, 
                                            uint64_t protocol_id, 
                                            uint64_t current_timestamp, 
                                            uint8_t * private_key, 
                                            uint8_t * allowed_packets, 
                                            struct netcode_replay_protection_t * replay_protection, 
                                            void * allocator_context, 
                                            void* (*allocate_function)(void*,uint64_t) )
                {

                    case NETCODE_CONNECTION_PAYLOAD_PACKET:
                    {
                        if ( decrypted_bytes < 1 )
                        {
                            netcode_printf( NETCODE_LOG_LEVEL_DEBUG, "ignored connection payload packet. payload is too small\n" );
                            return NULL;
                        }

                        if ( decrypted_bytes > NETCODE_MAX_PAYLOAD_BYTES )
                        {
                            netcode_printf( NETCODE_LOG_LEVEL_DEBUG, "ignored connection payload packet. payload is too large\n" );
                            return NULL;
                        }

                        struct netcode_connection_payload_packet_t * packet = netcode_create_payload_packet( decrypted_bytes, allocator_context, allocate_function );

                        if ( !packet )
                        {
                            netcode_printf( NETCODE_LOG_LEVEL_DEBUG, "ignored connection payload packet. could not allocate packet struct\n" );
                            return NULL;
                        }
                        
                        memcpy( packet->payload_data, buffer, decrypted_bytes );
                        
                        return packet;
                    }
                    break;
                }


3.  you can see that packet is of type netcode_connection_payload_packet_t. 

                struct netcode_connection_payload_packet_t
                {
                    uint8_t packet_type;
                    uint32_t payload_bytes;
                    uint8_t payload_data[1];
                };

so we are literally putting everything we got from recv and putting it in packet->payload_data.



4.  
