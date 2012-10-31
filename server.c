/*
 * =====================================================================================
 *
 *       Filename:  server.c
 *
 *    Description:  
 *
 *        Version:  1.0
 *        Created:  24/10/2012 11:23:26
 *       Revision:  none
 *       Compiler:  gcc
 *
 *         Author:  Connor Moreside (conman720), cmoresid@ualberta.ca
 *   Organization:  
 *
 * =====================================================================================
 */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <time.h>
#include <syslog.h>
#include <dirent.h>
#include <unistd.h>
#include <signal.h>
#include <pthread.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>

#include "server.h"
#include "common.h"

/* Ensures mutual exclusion for clients linked list */
pthread_mutex_t clients_lock = PTHREAD_MUTEX_INITIALIZER;
/* Shared mask for all threads */
sigset_t mask;
/* The name/path of the directory, as passed in the commandline argument */
char init_dir[PATH_MAX];
/* Represents all the currently connected clients */
struct clientlist* clients;
/* The period to monitor the directory */
int gperiod;

struct direntrylist* init_direntrylist() {
    struct direntrylist* list; 
    list = (struct direntrylist*) malloc(sizeof(struct direntrylist));
    
    if (list == NULL)
        return NULL;

    list->count = 0;
    list->head = NULL;
    list->tail = NULL;

    return list;
}

int add_direntry(struct direntrylist* list, struct direntry* entry) {
    if (list == NULL)
        return -1;

    if (list->head == NULL) {               // List is emptry
        list->head = entry; 
        list->tail = list->head;
    } else if (list->head->next == NULL) {  // 1 item in list
        list->head->next = entry;
        list->tail = list->head->next;
    } else {       							// More than 1 item                   
        list->tail->next = entry;
        list->tail = entry;
    }
    
    list->count++;

    return 1;
}

// Debugging purposes
void print_direntrylist(struct direntrylist* list) {
    struct direntry* p = list->head;

    while (p != NULL) {
        syslog(LOG_DEBUG,"%s : ino %i\n", p->filename, p->attrs->st_ino);
        p = p->next;
    }
}

struct direntry* find_direntry(struct direntrylist* list, struct direntry* entry) {
    if ( (list == NULL) || (list->head == NULL) )
        return NULL;

    struct direntry* p;
    p = list->head;

    while (p != NULL) {
        if (p->attrs->st_ino == entry->attrs->st_ino) {
            return p;
        }

        p = p->next;
    }
    
    return NULL;
}

struct direntrylist* exploredir(const char* path) {
    struct stat* fattr;
    struct direntrylist* list;
    struct direntry* list_entry;
    struct dirent* entry;
    DIR* dir;
    char abspath[PATH_MAX];
    
    if ( (dir = opendir(path)) == NULL ) {
        // Exit nicely, but for no EXIT NOW!!
        syslog(LOG_ERR, "Cannot open directory: %s", path);
        exit(1);
    }

    if ( (list = init_direntrylist()) == NULL ) {
        syslog(LOG_ERR, "Cannot malloc new direntrylist");
        exit(1);
    }

    readdir(dir); // eat current dir .
    readdir(dir); // and parent dir  ..
    
    while ( (entry = readdir(dir)) ) {
        fattr = (struct stat*) malloc(sizeof(struct stat));
        list_entry = (struct direntry*) malloc(sizeof(struct direntry));  
        list_entry->next = NULL;
        memset(list_entry, 0, sizeof(list_entry));

        if (fattr == NULL || list_entry == NULL) {
            syslog(LOG_ERR, "Cannot malloc direntry");
            exit(1);
        }

        if ((strlen(path) + strlen(entry->d_name) + 1) >= PATH_MAX) {
            syslog(LOG_ERR, "Path is too long.");
            exit(1);
        } else {
            strcpy(abspath, path);
            strcat(abspath, "/");
            strcat(abspath, entry->d_name);
        }

        if (stat(abspath, fattr) < 0) {
            syslog(LOG_ERR, "Cannot get stats on file: %s", entry->d_name);
            exit(1);
        }

        list_entry->filename = (char*) malloc((strlen(entry->d_name)+1) * sizeof(char));
        strcpy(list_entry->filename, entry->d_name);
        list_entry->attrs = fattr;

        add_direntry(list, list_entry);
    }

    if (closedir(dir) < 0) {
        syslog(LOG_ERR, "Cannot close directory.");
        exit(1);
    }

    return list;
}

void create_daemon(const char* name) {
	pid_t pid;
	struct rlimit rl;
	struct sigaction sa;

	umask(0);

    if (getrlimit(RLIMIT_NOFILE, &rl) < 0)
        err_quit("Can't get file limit");

    if ((pid = fork()) < 0) {
        err_quit("Can't fork.");
    } else if (pid > 0) {
        exit(0);
    }

    setsid();

    sa.sa_handler = SIG_IGN;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;

    if (sigaction(SIGHUP, &sa, NULL) < 0) {
        err_quit("Can't ignore SIGHUP");
    }

    if (chdir("/") < 0)
        err_quit("Can't switch to root directory.");

    close(STDIN_FILENO);
    close(STDOUT_FILENO);
    close(STDERR_FILENO);

    openlog(name, LOG_CONS, LOG_DAEMON);
}

static void* signal_thread(void* arg) {
    int err, signo;
	pthread_t tid;
    int period = (int) arg;

    for (;;) {
        // Check directory for updates
        alarm(period);

        err = sigwait(&mask, &signo);
        if (err != 0) {
            syslog(LOG_ERR, "sigwait failed");
            exit(1);
        }

        switch (signo) {
            case SIGHUP:
                // Finish transfers, remove all clients
                syslog(LOG_INFO, "Received SIGHUP");
                break;
            case SIGALRM:
                // See if directory has updated
                syslog(LOG_INFO, "Received SIGALRM");
				pthread_create(&tid, NULL, send_updates, NULL);
                break;
            case SIGTERM:
                // Same as SIGHUP, then quit
                syslog(LOG_INFO, "Received SIGTERM"); 
                break;
            default:
                // Finish transfers, quit
                syslog(LOG_ERR, "Unexpected signal: %d", signo);
                exit(1);
                break;
        }
    }
}

void* send_updates(void* arg) {
	struct client* p;
	p = clients->head;
	
	// Get dir contents
	// Check differences
	
	// LOCK
	pthread_mutex_lock(&clients_lock);
	
	if (1) { // There are differences
		while (p != NULL) {
			// LOCK
			pthread_mutex_lock(p->c_lock);
			
			if (send_string(p->socket, "Update") != 6) {
				syslog(LOG_ERR, "Could not send update");
				exit(1);
			}
			// UNLOCK
			pthread_mutex_unlock(p->c_lock);
			
			p = p->next;
		}
	}
	// UNLOCK
	pthread_mutex_unlock(&clients_lock);
	
	return ((void*) 0);
}

void* init_client(void* arg) {
    pthread_t tid;
    int socketfd;
    size_t len;

    tid = pthread_self();
    socketfd = (int) arg;
    len = strlen(init_dir);

	if (clients->count == MAX_CLIENTS) {
		syslog(LOG_INFO, "No more clients can be accepted.");
		//send_error("No more clients can be accepted");
		//close(socketfd);
		//pthread_exit((void*)1);
	}

    add_client_ref(socketfd);

	if (send_byte(socketfd, INIT_CLIENT1) != 1) {
		syslog(LOG_WARNING, "Cannot send init client 1");
		exit(1);
	}
	
	if (send_byte(socketfd, INIT_CLIENT2) != 1) {
		syslog(LOG_WARNING, "Cannot send init client 2");
		exit(1);
	}
	
	if (send_string(socketfd, init_dir) != len) {
		syslog(LOG_WARNING, "Cannot send path directory");
		exit(1);
	}
	
	if (send_byte(socketfd, gperiod) != 1) {
		syslog(LOG_WARNING, "Cannot send period");
		exit(1);
	}

    syslog(LOG_INFO, "Spawned new thread to handle client!");
	return ((void*) 0);
}

void* remove_client(void* arg) {
	int socketfd = (int) arg;
	byte b;
	
	// Will block until mutex is released
	remove_client_ref(socketfd);
	
	if ( (b = read_byte(socketfd)) != REQ_REMOVE1) {
		syslog(LOG_ERR, "Anticipated 0xDE: Received: 0x%x", b);
		close(socketfd);
	}
			
	if ( (b = read_byte(socketfd)) != REQ_REMOVE2) {
		syslog(LOG_ERR, "Anticipated 0xAD: Received: 0x%x", b);
		close(socketfd);
	}
			
	if (send_byte(socketfd, END_COM) != 1) {
		syslog(LOG_ERR, "Could not send byte!");
		close(socketfd);
	}
			
	if (send_string(socketfd, GOOD_BYE) != 7) {
		syslog(LOG_ERR, "Could not send string");
		close(socketfd);
	}

	close(socketfd);
	return ((void*) 0);
}

void add_client_ref(int socketfd) {
    struct client *ct;

	if (clients == NULL) {
		clients = (struct clientlist*) malloc(sizeof(struct clientlist));
		clients->head = NULL;
		clients->tail = NULL;
		clients->count = 0;
	}

    ct = (struct client*) malloc(sizeof(struct client));
    if (ct == NULL) {
        syslog(LOG_ERR, "Cannot malloc new client thread");
		return;
    }
	ct->c_lock = (pthread_mutex_t*) malloc(sizeof(pthread_mutex_t));
	pthread_mutex_init(ct->c_lock, NULL);
    ct->socket = socketfd;

    // LOCK
    pthread_mutex_lock(&clients_lock);
	ct->next = NULL;
	ct->prev = NULL;

	if (clients->head == NULL) {
		clients->head = ct;
		clients->tail = clients->head;
	} else if (clients->head == clients->tail) {
		clients->head->next = ct;
		ct->prev = clients->head;
		clients->tail = clients->head->next;
	} else {
		clients->tail->next = ct;
		ct->prev = clients->tail;
		clients->tail = ct;
	}
	
	clients->count++;

    // UNLOCK
    pthread_mutex_unlock(&clients_lock);
}

void remove_client_ref(int socketfd) {
	struct client* ct;
	struct client* tmp;
	
	// LOCK
	pthread_mutex_lock(&clients_lock);
	
	if ( (ct = find_client_ref(socketfd)) == NULL ) {
		syslog(LOG_ERR, "Could not find client.");
		exit(1);
	}
	
	// If lock is currently locked, maybe client is trying to send
	// out data. Wait until lock is aquired.
	pthread_mutex_lock(ct->c_lock);
	
	if (clients->count == 1) {
		clients->head = clients->head->next;
		clients->tail = clients->head;
	} else if (ct == clients->head) {
		clients->head = clients->head->next;
	} else if (ct == clients->tail) {
		clients->tail = clients->tail->prev;
		clients->tail->next = NULL;
	} else {
		tmp = ct->next;
		ct->prev->next = tmp;
		tmp->prev = ct->prev;
	}

	// UNLOCK
	pthread_mutex_unlock(ct->c_lock);
	pthread_mutex_unlock(&clients_lock);	
	
	// Deallocate client now
	ct->prev = NULL;
	ct->next = NULL;
	pthread_mutex_destroy(ct->c_lock);
	free(ct->c_lock);
	free(ct);
	
	clients->count--;
}

struct client* find_client_ref(int socketfd) {
	struct client* p;
	p = clients->head;
	
	while (p != NULL) {
		if (p->socket == socketfd) {
			return p;
		}
		
		p = p->next;
	}
	
	return NULL;
}

int start_server(int port_number, const char* dir_name, int period) {
    pthread_t tid;
    struct direntrylist* list;
    
    fd_set master;
    fd_set read_fds;
    int fdmax, i;

    int listener;
    int newfd;
    struct sockaddr_in local_addr;
    struct sockaddr_in remote_addr;
    socklen_t addr_len;

    struct sigaction sa;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    sa.sa_handler = SIG_IGN;

	clients = (struct clientlist*) malloc(sizeof(struct clientlist));
	clients->head = NULL;
	clients->tail = NULL;
	clients->count = 0;

    strcpy(init_dir, dir_name);
    gperiod = period;

    char full_path[PATH_MAX];
    if (realpath(dir_name, full_path) == NULL) {
        syslog(LOG_ERR, "Cannot resolve full path.");
        exit(1);
    }

#ifdef DAEMONIZE
    create_daemon("dirapp"); 
#endif
#ifndef DAEMONIZE
    openlog("dirapp", LOG_CONS, LOG_DAEMON);
#endif

    if (sigaction(SIGPIPE, &sa, NULL) < 0) {
        syslog(LOG_WARNING, "SIGPIPE error"); 
    }
        
    sigemptyset(&mask);
    sigaddset(&mask, SIGHUP);
    sigaddset(&mask, SIGTERM);
    sigaddset(&mask, SIGALRM);

    if (pthread_sigmask(SIG_BLOCK, &mask, NULL) != 0)
        syslog(LOG_WARNING, "pthread_sigmask failed");

    FD_ZERO(&master);
    FD_ZERO(&read_fds);

    memset(&local_addr, 0, sizeof(local_addr));
    local_addr.sin_family = AF_INET;
    local_addr.sin_addr.s_addr = inet_addr("127.0.0.1");
    local_addr.sin_port = htons(port_number);

    listener = socket(AF_INET, SOCK_STREAM, 0);

    if (bind(listener, (struct sockaddr*)&local_addr, sizeof(local_addr))) {
        syslog(LOG_ERR, "Cannot bind socket to address");    
        exit(1);
    }

    if (listen(listener, MAX_CLIENTS) < 0) {
        syslog(LOG_ERR, "Cannot listen on socket");
        exit(1);
    }

    syslog(LOG_INFO, "Starting server!");

    FD_SET(listener, &master);
    fdmax = listener;

    list = exploredir((const char*) full_path);
    print_direntrylist(list);
    syslog(LOG_INFO, "Number of entries: %i", list->count);
    
    // Create thread to handle signals
    pthread_create(&tid, NULL, signal_thread, (void*)period);

    // Main server loop
    while (1) {
        read_fds = master;

        if (select(fdmax+1, &read_fds, NULL, NULL, NULL) == -1) {
            syslog(LOG_ERR, "select error");
            exit(1);
        }

        for (i = 0; i <= fdmax; i++) {
            if (FD_ISSET(i, &read_fds)) {
                if (i == listener) {
                    addr_len = sizeof(remote_addr);
                    newfd = accept(listener, (struct sockaddr*)&remote_addr, &addr_len);

                    if (newfd == -1) {
                        syslog(LOG_WARNING, "Cannot new client.");
                    } else {
                        FD_SET(newfd, &master);
                        if (newfd > fdmax) {
                            fdmax = newfd;
                        }

                        syslog(LOG_INFO, "New connection from: %s:%d",
                                inet_ntoa(remote_addr.sin_addr),
                                ntohs(remote_addr.sin_port));
                        // Add client to clients list in order to receive
						// updates
                        pthread_create(&tid, NULL, init_client, (void*) newfd);
                    }
                } else {
					// Handle disconnect
					pthread_create(&tid, NULL, remove_client, (void*) i);
					FD_CLR(i, &master);
				}
            }
        }
    }

	return 0;
}
