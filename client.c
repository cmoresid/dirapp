/*
 * =====================================================================================
 *
 *       Filename:  client.c
 *
 *    Description:  
 *
 *        Version:  1.0
 *        Created:  24/10/2012 11:30:13
 *       Revision:  none
 *       Compiler:  gcc
 *
 *         Author:  Connor Moreside (conman720), cmoresid@ualberta.ca
 *   Organization:  
 *
 * =====================================================================================
 */
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <sys/socket.h>
#include <fcntl.h>
#include <arpa/inet.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <stdio.h>
#include <signal.h>
#include <netinet/in.h>
#include <unistd.h>
#include <pthread.h>

#include "client.h"
#include "common.h"

/* Shared mask for all threads */
sigset_t mask;
/* Ensures mutual exclusion for server linked list */
pthread_mutex_t servers_lock = PTHREAD_MUTEX_INITIALIZER;
/* Allow only 1 thread to write to stdin at once */
pthread_mutex_t io_lock = PTHREAD_MUTEX_INITIALIZER;
/* Used to keep track of all server connections. */
struct serverlist* servers;

int start_client() {	
	pthread_t tid;					/* Pass to pthread_create */
	struct thread_arg* targ;		/* Allows passing of multiple arguments to thread */
	struct server* recv_server;		/* Reference to server currently sending data */
	
	fd_set master;					/* Master list of FDs */
	fd_set read_fds;				/* List of FDs with incoming data */
	int fdmax;						/* Highest numbered FD */
	int i;							/* Index of FD */
	byte command;					/* Current command code of input */
	int server_socket;				/* Socket FD of server to add */
	int io_pipes[2];				/* Communication between I/O thread and main thread */
	int init_server_pipes[2];		/* Between an init_server thread and main thread */
	int remove_server_pipes[2];		/* Between a remove_server thread and main thread */
	struct sigaction sa;			/* Used to ignore SIGPIPE */
	int nbytes;						/* Number of bytes read in */
	char io_buff[128];				/* Filled from I/O thread */
	char* cmd_buff;					/* Filled from I/O thread to contain command args */
	byte server_buff[128];			/* Filled from incoming server data */
	
	servers = (struct serverlist*) malloc(sizeof(struct serverlist));
	servers->head = NULL;
	servers->tail = NULL;
	servers->count = 0;
	
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    sa.sa_handler = SIG_IGN;
	
	if (sigaction(SIGPIPE, &sa, NULL) < 0) {
        printf("SIGPIPE error\n"); 
    }
        
    sigemptyset(&mask);
    sigaddset(&mask, SIGHUP);
    sigaddset(&mask, SIGTERM);

    if (pthread_sigmask(SIG_BLOCK, &mask, NULL) != 0)
        printf("pthread_sigmask failed\n");

    FD_ZERO(&master);
    FD_ZERO(&read_fds);

	// Create pipe to communicate with I/O thread
	if (pipe(io_pipes) < 0) {
		fprintf(stderr, "Cannot create I/O pipe\n");
		exit(1);
	}
	// Create pipe to communicate with init_client thread
	if (pipe(init_server_pipes) < 0) {
		fprintf(stderr, "Cannot create init_client pipe\n");
		exit(1);
	}
	// Create pipe to communicate with remove_client thread
	if (pipe(remove_server_pipes) < 0) {
		fprintf(stderr, "Cannot create remove_client pipe\n");
		exit(1);
	}
	
	// Print out instructions
	printf("\ndirapp client:\n");
	printf("\tadd hostname port\n");
	printf("\tremove hostname port\n");
	printf("\tlist\n\n");
	
	// Spawn I/O thread
	pthread_create(&tid, NULL, handle_input, (void*)io_pipes[1]);
	// Spawn signal thread
	pthread_create(&tid, NULL, signal_thread, NULL);
	
	// Main loop	
	FD_SET(io_pipes[0], &master);
	FD_SET(init_server_pipes[0], &master);
	FD_SET(remove_server_pipes[0], &master);
	fdmax = remove_server_pipes[0];
	
	targ = NULL;
	cmd_buff = NULL;
	
	while (1) {
        read_fds = master;

        if (select(fdmax+1, &read_fds, NULL, NULL, NULL) == -1) {
            fprintf(stderr, "select error\n");
            exit(1);
        }

		for (i = 0; i <= fdmax; i++) {
            if (FD_ISSET(i, &read_fds)) {
				if (i == io_pipes[0]) {
					// Handle I/O
					if (read(io_pipes[0], io_buff, 1) <= 0) {
						fprintf(stderr, "\n\t  Cannot read from pipe.\n");
						exit(1);
					}
					
					nbytes = (int)io_buff[0];
					if (read(io_pipes[0], io_buff, nbytes) <= 0) {
						fprintf(stderr, "\n\t  Cannot read from pipe.\n");
						exit(1);
					}
					
					// Make sure io_buff is null terminated buffer
					io_buff[nbytes] = '\0';
					cmd_buff = (char*) malloc(nbytes*sizeof(char));
					
					command = io_buff[0];
					if (command == ADD_SERVER_C) {
						strcpy(cmd_buff, io_buff+1);
						targ = (struct thread_arg*) malloc(sizeof(struct thread_arg));
						targ->buff = cmd_buff;
						targ->socket_pipe = init_server_pipes[1];
						pthread_create(&tid, NULL, init_server, (void*)targ);
					} else if (command == REMOVE_SERVER_C) {
						//strcpy(cmd_buff, io_buff);
						//targ = (struct thread_arg*) malloc(sizeof(struct thread_arg));
						//targ->buff = cmd_buff;
						//pthread_create(&tid, NULL, remove_server, (void*)targ);
					} else if (command == LIST_SERVERS_C){
						// pthread_create(&tid, NULL, list_servers, NULL);
						if (cmd_buff != NULL)
							free(cmd_buff);
						if (targ != NULL)
							free(targ);
					} else {
						// Quit
						if (cmd_buff != NULL)
							free(cmd_buff);
						if (targ != NULL)
							free(targ);

						// New function
						// kill_server_connections()
						recv_server = servers->head;
						while (recv_server != NULL) {
							disconnect_from_server(recv_server->socket);
							recv_server = recv_server->next;
						}

						printf("\n\t  Good bye!\n\n");
						exit(1);
					}

					// Make sure to de-allocate memory in thread
					// Potential memory leak here.
					cmd_buff = NULL;
					targ = NULL;
				} else if (i == init_server_pipes[0]) {
					// Get socket fd from init_server thread
					if (read(init_server_pipes[0], io_buff, 1) <= 0) {
						fprintf(stderr, "\n\t  Cannot read from pipe.\n");
						exit(1);
					}
					// Save server socket
					server_socket = (int) io_buff[0];
					// Add socket to address to listen too
					FD_SET(server_socket, &master);
	                if (server_socket > fdmax) {
	                    fdmax = server_socket;
	                }
				} else {
					// Receiving data from a server...
					// Move to another thread
					byte b;
					if ( (b = read_string(i, server_buff, 128)) <= 0) {
						printf("\n\t  Cannot read string.\n");
					}
					recv_server = find_server_ref(i);
					server_buff[b] = '\0';
					printf("\n\t  %s:%d - %s\n", recv_server->host, recv_server->port, server_buff);
				}
			}
		}
	}

    return 0;
}

void* init_server(void* arg) {
	struct thread_arg* server_args;
	char* host;
	char* tmp;
	int port;
	int socketfd;
	int period;
	int pipe;
	struct sockaddr_in server_info;
	byte buff[256];
	byte* path;
	size_t len;
	int results[1];
	
	server_args = (struct thread_arg*) arg;
	tmp = strtok(server_args->buff, " ");
	len = strlen(tmp);
	host = (char*) malloc(len*sizeof(char));
	strcpy(host, tmp);
	port = atoi(strtok(NULL, " "));
	pipe = server_args->socket_pipe;
	// Setup connection info
	memset(&server_info, 0, sizeof(struct sockaddr_in));
	server_info.sin_family = AF_INET;
	server_info.sin_addr.s_addr = inet_addr(host);
	server_info.sin_port = htons(port);
	// Free temporary structures
	free(server_args->buff);
	free(server_args);
	server_args = NULL;
	tmp = NULL;
	// Ensure valid port
	if (port < 1024 || port > 65535) {
		fprintf(stderr, "\n\t  Invalid port number.\n");
		pthread_exit((void*)1);
	}
	// Check if hostname is valid.
	if (server_info.sin_addr.s_addr == 0) {
		fprintf(stderr, "\n\t  Invalid host name.\n");
		pthread_exit((void*)1);
	}
	// Create socket
	if ( (socketfd = socket(AF_INET, SOCK_STREAM, 0)) < 0 ) {
        fprintf(stderr, "Could not create socket.\n");
		pthread_exit((void*) 1);
    }
	// Try to connect to server
	if (connect(socketfd, (struct sockaddr*)&server_info, sizeof(struct sockaddr_in)) < 0) {
		fprintf(stderr, "Cannot connect to server.\n");
		pthread_exit((void*) 1);
    }
	// Read acknowledgement from server 1
	if (read_byte(socketfd) != INIT_CLIENT1 ) {
		fprintf(stderr, "Unexpected response. Abort!\n");
		pthread_exit((void*) 1);
	}
	// Read acknowledgement from server 1
	if (read_byte(socketfd) != INIT_CLIENT2) { 
		fprintf(stderr, "Unexpected response. Abort!\n");
		pthread_exit((void*) 1);
	}
	// Read in the path name
	len = read_string(socketfd, buff, 256);
	path = (byte*) malloc(len*sizeof(byte));
	
	if (len <= 0) {
		fprintf(stderr, "Cannot read in string.\n");
		pthread_exit((void*) 1);
	} else {
		strcpy(path, buff);
	}
	// Read in period
	if ( (period = read_byte(socketfd)) <= 0 ) {
		printf("Cannot read period.\n");
		exit(1);
	}
	// Done
	add_server_ref(host, path, port, period, socketfd);
	// Send socket fd to main thread so client knows
	// about it
	results[0] = socketfd;
	write(pipe, results, 1);

	return ((void*)0);
}

void add_server_ref(const char* host, const char* path, int port, int period, int socketfd) {
	struct server *s;

    s = (struct server*) malloc(sizeof(struct server));
    if (s == NULL) {
		fprintf(stderr, "\n\t  Cannot malloc new server.\n");
		return;
    }

	s->s_lock = (pthread_mutex_t*) malloc(sizeof(pthread_mutex_t));
	pthread_mutex_init(s->s_lock, NULL);
    s->socket = socketfd;
	s->host = (char*) host;
	s->path = (char*) path;
	s->port = port;
	s->period = period;

    // LOCK
    pthread_mutex_lock(&servers_lock);
	s->next = NULL;
	s->prev = NULL;

	if (servers->head == NULL) {
		servers->head = s;
		servers->tail = servers->head;
	} else if (servers->head == servers->tail) {
		servers->head->next = s;
		s->prev = servers->head;
		servers->tail = servers->head->next;
	} else {
		servers->tail->next = s;
		s->prev = servers->tail;
		servers->tail = s;
	}
	
	servers->count++;

    // UNLOCK
    pthread_mutex_unlock(&servers_lock);
}

void remove_server_ref(int socketfd) {
	struct server* s;
	struct server* tmp;
	
	// LOCK
	pthread_mutex_lock(&servers_lock);
	
	if ( (s = find_server_ref(socketfd)) == NULL ) {
		fprintf(stderr, "\n\t  Cannot find server.\n");
		return;
	}
	
	// If lock is currently locked, maybe client is trying to send
	// out data. Wait until lock is aquired.
	pthread_mutex_lock(s->s_lock);
	
	if (servers->count == 1) {
		servers->head = servers->head->next;
		servers->tail = servers->head;
	} else if (s == servers->head) {
		servers->head = servers->head->next;
	} else if (s == servers->tail) {
		servers->tail = servers->tail->prev;
		servers->tail->next = NULL;
	} else {
		tmp = s->next;
		s->prev->next = tmp;
		tmp->prev = s->prev;
	}

	// UNLOCK
	pthread_mutex_unlock(s->s_lock);
	pthread_mutex_unlock(&servers_lock);	
	
	// Deallocate client now
	s->prev = NULL;
	s->next = NULL;
	pthread_mutex_destroy(s->s_lock);
	free(s->s_lock);
	free(s->host);
	free(s->path);
	free(s);
	
	servers->count--;
}

struct server* find_server_ref(int socketfd) {
	struct server* p;
	p = servers->head;
	
	while (p != NULL) {
		if (p->socket == socketfd) {
			return p;
		}
		
		p = p->next;
	}
	
	return NULL;
}

void* handle_input(void* arg) {
	int out_pipe, args, offset;
	size_t len, token_len;
	char command;
	char buff[128];
	char results[128];
	char* token;
	
	out_pipe = (int) arg;
	offset = 2; // Offset of 2
	
	while (1) {
		args = 0;
		offset = 2;
		printf("  > ");
		
		if (fgets(buff, 100, stdin) != NULL) {
			// Ignore blank line
			len = strlen(buff);
			if (len == 1) {
				continue;
			}
			// Eat name of command
			token = strtok(buff, " \n");
			// Supported commands
			if (CMD_CMP(token, ADD)) {
				command = ADD_SERVER_C;
			} else if (CMD_CMP(token, REMOVE)) {
				command = REMOVE_SERVER_C;
			} else if (CMD_CMP(token, LIST)) {
				command = LIST_SERVERS_C;
			} else if (CMD_CMP(token, QUIT)) {
				command = QUIT_C;
			} else {
				command = INVALID_C;
			}
			
			if (command == INVALID_C) {
				fprintf(stderr, "\t  Invalid commmand.\n");
				continue;
			} else {
				args++;
			}
			
			if (command == ADD_SERVER_C || command == REMOVE_SERVER_C) {
				token = strtok(NULL, " \n");
				
				if (token == NULL) {
					fprintf(stderr, "\t  Missing arguments.\n");
					continue;
				} else {
					do {
						token_len = strlen(token);
						strncpy(results+offset, token, token_len);
						offset += token_len + 1;
						results[offset-1] = ' ';
						token = strtok(NULL, " \n");
						args++;
					} while (token != NULL);
				}
				
				if (args < 3) {
					fprintf(stderr, "\t  Missing arguments.\n");
					continue;
				}
				
				results[0] = (char) (offset - args + 1);
				results[1] = ADD_SERVER_C;
				results[offset-args+2] = '\0';
			} else if (command == LIST_SERVERS_C) {
				token = strtok(NULL, " \n");
				
				if (token != NULL) {
					fprintf(stderr, "\t  Too many arguments.\n");
				}
				
				results[0] = 1;
				results[1] = LIST_SERVERS_C;
			} else {
				// Quit command
				results[0] = 1;
				results[1] = QUIT_C;
			}
			
			// Write to pipe
			write(out_pipe, results, (results[0]+1));
		}
	}
	
	return ((void*) 0);
}

static void* signal_thread(void* arg) {
    int err, signo;

    for (;;) {
        err = sigwait(&mask, &signo);
        if (err != 0) {
			fprintf(stderr, "sigwait failed\n");
            exit(1);
        }

        switch (signo) {
            case SIGHUP:
                // Finish transfers, remove all clients
                printf("Received SIGHUP\n");
                break;
            case SIGTERM:
                // Same as SIGHUP, then quit
				printf("Received SIGTERM\n");
                break;
            default:
                // Finish transfers, quit
                fprintf(stderr, "Unexpected signal: %d", signo);
                exit(1);
                break;
        }
    }

	return ((void*) 0);
}

int disconnect_from_server(int socketfd) {
	byte buff[8];
	
	// Only send termination to request to server
	// if client is currently not receiving updates
	// from server
	// LOCK
	remove_server_ref(socketfd);
	// UNLOCK
	
	if (send_byte(socketfd, REQ_REMOVE1) != 1) {
		fprintf(stderr, "Could not send byte.\n");
		shutdown(socketfd, SHUT_RDWR);
		return -1;
	}
	
	if (send_byte(socketfd, REQ_REMOVE2) != 1) {
		fprintf(stderr, "Could not send byte.\n");
		shutdown(socketfd, SHUT_RDWR);
		return -1;
	}
	
	if (read_byte(socketfd) != 0xFF) {
		fprintf(stderr, "Non-orderly shutdown.\n");
		shutdown(socketfd, SHUT_RDWR);
		return -1;
	}
	
	if (read_string(socketfd, buff, 8) != 7) {
		fprintf(stderr, "Non-orderly shutdown.\n");
		shutdown(socketfd, SHUT_RDWR);
		return -1;
	}
	
	buff[7] = '\0';
	if (strcmp(buff, "Goodbye") != 0) {
		fprintf(stderr, "Unrecognized string.\n");
		shutdown(socketfd, SHUT_RDWR);
		return -1;
	}
	
	close(socketfd);
	
	return 0;
}
