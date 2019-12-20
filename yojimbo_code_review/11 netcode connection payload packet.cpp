

#####################################################################################
#################### NETCODE_CONNECTION_PAYLOAD_PACKET ##############################
#####################################################################################


1.  again, we start from this function

                void netcode_server_receive_packets( struct netcode_server_t * server )
                {
                    ...
                    ...


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
                                ...
                            }
                            else
                            {
                                if (server->socket_holder.ipv4.handle != 0)
                                    packet_bytes = netcode_socket_receive_packet( &server->socket_holder.ipv4, &from, packet_data, NETCODE_MAX_PACKET_BYTES );

                                ...
                                ...
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




2.  we first call recvfrom and put all the data onto packet_data

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




3.  

                void netcode_server_read_and_process_packet( struct netcode_server_t * server, 
                                                             struct netcode_address_t * from, 
                                                             uint8_t * packet_data, 
                                                             int packet_bytes, 
                                                             uint64_t current_timestamp, 
                                                             uint8_t * allowed_packets )
                {
                    ...

                    uint64_t sequence;

                    ...
                    ...

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



2.  then we pass the packet_data buffer into the netcode_read_packet(); function

-   one thing to note is that when we call 

                uint8_t prefix_byte = netcode_read_uint8( &buffer );


    we are actually modifying the pointer value buffer

                uint8_t netcode_read_uint8( uint8_t ** p )
                {
                    uint8_t value = **p;
                    ++(*p);
                    return value;
                }

so everytime we call netcode_read_uint8, we are advancing forward the buffer pointer value



-   note we defined: #define NETCODE_MAC_BYTES 16



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

                    uint8_t * start = buffer;

                    uint8_t prefix_byte = netcode_read_uint8( &buffer );




                    int encrypted_bytes = (int) ( buffer_length - ( buffer - start ) );

                    if ( encrypted_bytes < NETCODE_MAC_BYTES )
                    {
                        netcode_printf( NETCODE_LOG_LEVEL_DEBUG, "ignored encrypted packet. encrypted payload is too small\n" );
                        return NULL;
                    }


                    ...
                    ...

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

so we are literally putting everything we got from recv, read all the bytes for the connection payload 
then putting the actual pyaload data, putting it in packet->payload_data.





4.  


                void netcode_server_process_packet_internal( struct netcode_server_t * server, 
                                                             struct netcode_address_t * from, 
                                                             void * packet, 
                                                             uint64_t sequence, 
                                                             int encryption_index, 
                                                             int client_index )
                {
                    ...

                    (void) from;
                    (void) sequence;

                    uint8_t packet_type = ( (uint8_t*) packet ) [0];

                    switch ( packet_type )
                    {
                        ...
                        ...

                        case NETCODE_CONNECTION_PAYLOAD_PACKET:
                        {
                            if ( client_index != -1 )
                            {
                                ...

                                netcode_packet_queue_push( &server->client_packet_queue[client_index], packet, sequence );
                                return;
                            }
                        }
                        break;

                        ...

                        default:
                            break;
                    }

                    ...
                }



5.  

                int netcode_packet_queue_push( struct netcode_packet_queue_t * queue, void * packet_data, uint64_t packet_sequence )
                {
                    ...

                    int index = ( queue->start_index + queue->num_packets ) % NETCODE_PACKET_QUEUE_SIZE;
                    queue->packet_data[index] = packet_data;
                    queue->packet_sequence[index] = packet_sequence;
                    queue->num_packets++;
                    return 1;
                }

