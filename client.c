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
/* Allow only 1 thread to read from network socket at once */
pthread_mutex_t network_lock = PTHREAD_MUTEX_INITIALIZER;
/* Only allow 1 thread to alter the io_buffer */
pthread_mutex_t iobuff_lock = PTHREAD_MUTEX_INITIALIZER;
/* Used to keep track of all server connections. */
struct serverlist* servers;

int start_client() {	
	pthread_t tid;					/* Pass to pthread_create */
	struct thread_arg* targ;		/* Allows passing of multiple arguments to thread */
	
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
     
  	// Signals to block on all threads, except
	// the signal thread
    sigemptyset(&mask);
    sigaddset(&mask, SIGHUP);
    sigaddset(&mask, SIGTERM);
	sigaddset(&mask, SIGINT);

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
	pthread_create(&tid, NULL, signal_thread, (void*)remove_server_pipes[1]);
	
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
			perror("select");
            exit(1);
        }

		for (i = 0; i <= fdmax; i++) {
            if (FD_ISSET(i, &read_fds)) {
				if (i == io_pipes[0]) {
					// LOCK io_buff
					pthread_mutex_lock(&iobuff_lock);
					// Get number of bytes of the command + arguments
					if (read(io_pipes[0], io_buff, 1) <= 0) {
						fprintf(stderr, "\n\t  Cannot read from pipe.\n");
						exit(1);
					}	
					// How many bytes to read out of io_buff,
					// which has the command and the arguments
					// for that command
					nbytes = (int)io_buff[0];
					if (read(io_pipes[0], io_buff, nbytes) <= 0) {
						fprintf(stderr, "\n\t  Cannot read from pipe.\n");
						exit(1);
					}
					
					// Get command code from io_buffer
					command = io_buff[0];
					// Add server or remove server
					if (command == ADD_SERVER_C || command == REMOVE_SERVER_C) {
						// cmd_buff is passed to either init_server or remove_server
						// in order to have access to the arguments
						cmd_buff = (char*) malloc(nbytes*sizeof(char));
						// NULL terminate the arguments
						io_buff[nbytes] = '\0';
						strcpy(cmd_buff, io_buff+1);
						targ = (struct thread_arg*) malloc(sizeof(struct thread_arg));
						targ->buff = cmd_buff;
						
						if (command == ADD_SERVER_C) {
							// Pipe is used to retrieve the newly created socket
							// from server
							targ->pipe = init_server_pipes[1];
							pthread_create(&tid, NULL, init_server, (void*)targ);
						} else {
							// Pipe is used to retrieve the socket of the server
							// to destroy
							targ->pipe = remove_server_pipes[1];
							pthread_create(&tid, NULL, remove_server, (void*)targ);
						}
					} else if (command == LIST_SERVERS_C){
						// Print out connected servers
						list_servers(servers);
					} else {
						// Nicely KILL ALL SERVERS!!
						kill_servers(servers, remove_server_pipes[1]);
						printf("\n\t  Goodbye!\n\n");
						exit(1);
					}
					// UNLOCK io_buff
					pthread_mutex_unlock(&iobuff_lock);
					// Make sure to de-allocate memory in thread.
					cmd_buff = NULL;
					targ = NULL;
				} else if (i == init_server_pipes[0]) {
					// LOCK io_buff
					pthread_mutex_lock(&iobuff_lock);
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
					// UNLOCK io_buff
					pthread_mutex_unlock(&iobuff_lock);
				} else if (i == remove_server_pipes[0]) { 
					// LOCK io_buff
					pthread_mutex_lock(&iobuff_lock);
					// Get socket fd from remove_server thread
					if (read(remove_server_pipes[0], io_buff, 1) <= 0) {
						fprintf(stderr, "\n\t  Cannot read from pipe.\n");
						exit(1);
					}
					// UNLOCK io_buff
					pthread_mutex_unlock(&iobuff_lock);
					// Save server socket
					server_socket = (int) io_buff[0];
					// Remove socket from listening set
					FD_CLR(server_socket, &master);
				} else {					
					byte b;
					// Receiving data from a server...
					b = read_byte(i);
					if (b > 0 && b < 255) {
						// Retrieve all updates from a server
						get_updates(i, (int)b);
					} else if (b == END_COM) {
						// Error message has been sent from server
						byte server_buff[128];
						struct server* err_serv;
						// LOCK
						pthread_mutex_lock(&io_lock);
						if (read_string(i, server_buff, 128) <= 0) {
							fprintf(stderr, "\n\t  Could not read in error message");
						} else {
							err_serv = find_server_ref(i);
							fprintf(stderr, "\n\t  ** Error from %s:%d --\n",
								err_serv->host,
								err_serv->port);
							fprintf(stderr, "\n\t\t%s\n", server_buff);
						}
						// UNLOCK
						pthread_mutex_unlock(&io_lock);	
						// LOCK
						pthread_mutex_lock(&servers_lock);
						// Remove server ref
						remove_server_ref(i);
						// UNLOCK
						pthread_mutex_unlock(&servers_lock);
						// Close connection and remove it from
						// master set
						close(i);
						FD_CLR(i, &master);
					}
				}
			}
		}
	}

    return 0;
}

void get_updates(int socketfd, int numdiffs) {
	byte server_buff[128];
	struct server* recv_server;
	int j;
	
	recv_server = find_server_ref(socketfd);
	
	// LOCK
	pthread_mutex_lock(&io_lock);
	pthread_mutex_lock(recv_server->s_lock);
	printf("\n\t * Updates from %s:%d  --\n", 
		recv_server->host,
		recv_server->port);
	for (j = 0; j < numdiffs; j++) {
		if (read_string(socketfd, server_buff, 128) <= 0) {
			fprintf(stderr, "\n\t  Cannot read in entry change.\n");
			break;
		} else {
			if (server_buff[0] == '!') {
				printf("\t\tModified : %s\n", server_buff+1);
			} else if (server_buff[0] == '-') {
				printf("\t\tRemoved  : %s\n", server_buff+1);
			} else {
				printf("\t\tAdded    : %s\n", server_buff+1);
			}
		}
	}
	// UNLOCK
	pthread_mutex_unlock(recv_server->s_lock);
	pthread_mutex_unlock(&io_lock);
}

void kill_servers(struct serverlist* servers, int pipe) {
	struct server* p;
	int count;
	int socket;
	
	p = servers->head;
	count = servers->count;
	
	// LOCK
	pthread_mutex_lock(&servers_lock);
	if (count != 0) {
		while (p != NULL) {
			// Only send termination to request to server
			// if client is currently not receiving updates
			// from server
			socket = p->socket;
			remove_server_ref(p->socket);
			disconnect_from_server(socket, pipe);
			p = servers->head;
		}
	}
	// UNLOCK
	pthread_mutex_unlock(&servers_lock);
}

void list_servers(struct serverlist* servers) {
	struct server* tmp;
	tmp = servers->head;
	
	pthread_mutex_lock(&io_lock);
	
	if (servers->count > 0) {
		printf("\n\t  Connected servers:\n");
	} else {
		printf("\n\t  No connected servers\n");
	}
	
	while (tmp != NULL) {
		printf("\t    %s:%d - Directory: %s, Period: %d\n",
			tmp->host, tmp->port, tmp->path, tmp->period);
		
		tmp = tmp->next;
	}
	pthread_mutex_unlock(&io_lock);
}

void* remove_server(void* arg) {
	struct thread_arg* server_args;
	struct server* s;
	char* host;
	char* tmp;
	int port;
	int pipe;
	size_t len;
	// Get server arguments
	server_args = (struct thread_arg*) arg;
	// Store host token
	tmp = strtok(server_args->buff, " ");
	len = strlen(tmp);
	// Copy hostname from temp to host
	host = (char*) malloc(len*sizeof(char));
	strcpy(host, tmp);
	// Last token should be the port
	port = atoi(strtok(NULL, " "));
	// Get pipe
	pipe = server_args->pipe;
	// Free temporary structures
	free(server_args->buff);
	free(server_args);
	server_args = NULL;
	tmp = NULL;
	
	// Try to find server now to remove
	if ( (s = find_server_ref2(host, port)) == NULL) {
		fprintf(stderr, "\n\t  Cannot find connected server.\n");
		return ((void*)1);
	} else {
		// Only send termination to request to server
		// if client is currently not receiving updates
		// from server
		pthread_mutex_lock(&servers_lock);
		remove_server_ref(s->socket);
		pthread_mutex_unlock(&servers_lock);
		
		// UNLOCK
		printf("\n\t  Disconnecting from %s:%d\n", s->host, s->port);
		if (disconnect_from_server(s->socket, pipe) < 0) {
			printf("\n\t  Messy disconnect from server.\n");
		}
	}
	
	free(host);
	
	return ((void*)0);
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
	byte b;
	size_t len;
	int results[1];
	
	if (servers->count == MAX_SERVERS) {
		printf("\n\t Cannot connect to any more servers.\n");
		pthread_exit((void*)1);
	}
	
	server_args = (struct thread_arg*) arg;
	tmp = strtok(server_args->buff, " ");
	len = strlen(tmp);
	host = (char*) malloc(len*sizeof(char));
	strcpy(host, tmp);
	port = atoi(strtok(NULL, " "));
	pipe = server_args->pipe;
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
	b = read_byte(socketfd);
	
	// Error from server
	if (b == END_COM) {
		read_string(socketfd, buff, 256);
		printf("\n\t  ** %s\n", buff);
		close(socketfd);
		pthread_exit((void*) 1);
	}
	// Make sure b is INIT_CLIENT1
	if (b != INIT_CLIENT1)  {
		fprintf(stderr, "Unexpected response. Abort!\n");
		close(socketfd);
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
	
	printf("\n\t  Directory: %s, Period: %d\n", path, period);

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
		servers->head->prev->next = NULL;
		servers->head->prev = NULL;
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

struct server* find_server_ref2(const char* host, int port) {
	struct server* p;
	p = servers->head;
	
	while (p != NULL) {
		if ( (strcmp(p->host, host) == 0) && (p->port == port) ) {
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
				
				if (command == REMOVE_SERVER_C) {
					results[1] = REMOVE_SERVER_C;
				} else {
					results[1] = ADD_SERVER_C;
				}

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
	int err;
	int signo;
	int pipe;
	
	pipe = (int) arg;

    for (;;) {
        err = sigwait(&mask, &signo);
        if (err != 0) {
			fprintf(stderr, "sigwait failed\n");
            exit(1);
        }

        switch (signo) {
            case SIGHUP:
                // Finish transfers, remove all clients
                printf("\n\t ** Received SIGHUP ; Purging all server connections.\n");
				kill_servers(servers, pipe);
                break;
            case SIGTERM:
                // Same as SIGHUP, then quit
				printf("\n\t  ** Purging all server connections and quiting.\n\n");
				kill_servers(servers, pipe);
				exit(0);
                break;
			case SIGINT:
				printf("\n\t  ** Purging all server connections and quitting.\n\n");
				kill_servers(servers, pipe);
				exit(0);
				break;
            default:
                // Finish transfers, quit
                fprintf(stderr, "\n\t  Unexpected signal: %d", signo);
                //exit(1);
                break;
        }
    }

	return ((void*) 0);
}

int disconnect_from_server(int socketfd, int pipe) {
	int pipe_buff[1];
	byte buff[8];
	
	// Send request to remove the socket from the
	// master file descriptor list
	pipe_buff[0] = socketfd;
	write(pipe, pipe_buff, 1);
	
	if (send_byte(socketfd, REQ_REMOVE1) != 1) {
		shutdown(socketfd, SHUT_RDWR);
		return -1;
	}
	
	if (send_byte(socketfd, REQ_REMOVE2) != 1) {
		shutdown(socketfd, SHUT_RDWR);
		return -1;
	}
	
	if (read_byte(socketfd) != END_COM) {
		shutdown(socketfd, SHUT_RDWR);
		return -1;
	}
	
	if (read_string(socketfd, buff, 8) != 7) {
		shutdown(socketfd, SHUT_RDWR);
		return -1;
	}
	
	buff[7] = '\0';
	if (strcmp(buff, "Goodbye") != 0) {
		shutdown(socketfd, SHUT_RDWR);
		return -1;
	}
	// Close the socket now
	close(socketfd);
	
	return 0;
}
