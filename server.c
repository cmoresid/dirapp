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
#include "mempool.h"

//#define DAEMONIZE

/* Ensures mutual exclusion for clients linked list */
pthread_mutex_t clients_lock = PTHREAD_MUTEX_INITIALIZER;
/* Ensure update_buff is only accessed one at a time */
pthread_mutex_t updatebuff_lock = PTHREAD_MUTEX_INITIALIZER;
/* Shared mask for all threads */
sigset_t mask;
/* The name/path of the directory, as passed in the commandline argument */
char init_dir[PATH_MAX];
/* The full path to the directory */
char full_path[PATH_MAX];
/* Represents all the currently connected clients */
struct clientlist* clients;
/* The previous contents/attributes of directory being monitored */
struct direntrylist* prevdir;
/* The current contents/attributes of directory being monitored */
struct direntrylist* curdir;
/* The period to monitor the directory */
int gperiod;
/* The update buffer */
byte update_buff[MAX_FILENAME];
/* Pipe used to send sockets to remove from the master list in the main thread */
int remove_client_pipes[2];
/* Memory pool for directory entry nodes */
struct mempool* direntry_pool;
/* Absolute path for file names */
char abspath[PATH_MAX];
/* Used to ensure that a socket is removed from the master fd list before proceeding */
pthread_cond_t sready = PTHREAD_COND_INITIALIZER;
/* Mutex that protects the sready condition */
pthread_mutex_t slock = PTHREAD_MUTEX_INITIALIZER;
/* Used in tandum with slock and sready */
int done;

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

int reuse_direntrylist(struct direntrylist* list) {
	struct direntry* p;

	p = list->head;	
	while (p != NULL) {
		list->head = list->head->next;
		p->next = NULL;
		mempool_free(direntry_pool, p);
		p = list->head;
		list->count--;
	}
	
	list->tail = list->head;
	
	return 0;
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

struct direntry* find_direntry(struct direntrylist* list, struct direntry* entry) {
    if ( (list == NULL) || (list->head == NULL) )
        return NULL;

    struct direntry* p;
    p = list->head;

    while (p != NULL) {
        if (p->attrs.st_ino == entry->attrs.st_ino) {
            return p;
        }

        p = p->next;
    }
    
    return NULL;
}

int exploredir(struct direntrylist* list, const char* path) {
    struct stat fattr;
    struct direntry* list_entry;
    struct dirent* entry;
    DIR* dir;
    
    if ( (dir = opendir(path)) == NULL ) {
        // Send error message to all clients and then exit
		kill_clients(remove_client_pipes[1], "Cannot open directory! ; Exiting now!");
        syslog(LOG_ERR, "Cannot open directory: %s", path);
        exit(1);
    }

    readdir(dir); // eat current dir .
    readdir(dir); // and parent dir  ..
    
    while ( (entry = readdir(dir)) ) {
		list_entry = (struct direntry*) mempool_alloc(direntry_pool, sizeof(struct direntry)); 
        list_entry->next = NULL;
		CLR_MASK(list_entry->mask);
		
        memset(list_entry, 0, sizeof(list_entry));

        if (list_entry == NULL) {
			kill_clients(remove_client_pipes[1], "Unrecoverable server error! ; Exiting now!");
            syslog(LOG_ERR, "Cannot malloc direntry");
            exit(1);
        }

        if ((strlen(path) + strlen(entry->d_name) + 1) >= PATH_MAX) {
			kill_clients(remove_client_pipes[1], "Unrecoverable server error! ; Exiting now!");
            syslog(LOG_ERR, "Path is too long.");
            exit(1);
        } else {
            strcpy(abspath, path);
            strcat(abspath, "/");
            strcat(abspath, entry->d_name);
        }

        if (stat(abspath, &fattr) < 0) {
			kill_clients(remove_client_pipes[1], "Unrecoverable server error! ; Exiting now!");
            syslog(LOG_ERR, "Cannot get stats on file: %s", entry->d_name);
            exit(1);
        }

		if (strlen(entry->d_name) > MAX_FILENAME) {
			syslog(LOG_ERR, "Filename is too long to be saved.");
			exit(1);
		}

        strcpy(list_entry->filename, entry->d_name);
		list_entry->attrs = fattr;	

        add_direntry(list, list_entry);
    }

    if (closedir(dir) < 0) {
		kill_clients(remove_client_pipes[1], "Unrecoverable server error! ; Exiting now!");
        syslog(LOG_ERR, "Cannot close directory.");
        exit(1);
    }

    return 0;
}

int append_diff(byte* buff, const char* mode, const char* filename, const char* desc) {
	size_t len;
	memset(buff, 0, sizeof(buff));
	
	len = strlen(filename);
	strcat(buff, mode);
	strcat(buff+1, " ");
	strcat(buff+2, filename);
	strcat(buff+2+len, desc);
	
	return 0;
}

int difference_direntrylist() {
	int ndiffs;
	struct direntry* entry_prev;
	struct direntry* entry_cur;
	
	exploredir(curdir, (const char*) full_path); /* Global variable: full_path */
	
	if (curdir->count == 0 && prevdir->count == 0) {
		return 0;
	}
	
	entry_prev = prevdir->head;
	while (entry_prev != NULL) {
		if ( (entry_cur = find_direntry(curdir, entry_prev)) != NULL 
				&& !IS_CHECKED(entry_prev->mask) ) {
			// Permissions
			if (entry_prev->attrs.st_mode != entry_cur->attrs.st_mode) {
				SET_MODIFIED(entry_prev->mask);
				SET_PERM(entry_prev->mask);
				ndiffs++;
			}
			// UID
			if (entry_prev->attrs.st_uid != entry_cur->attrs.st_uid) {
				SET_MODIFIED(entry_prev->mask);
				SET_UID(entry_prev->mask);
				ndiffs++;
			}
			// GID
			if (entry_prev->attrs.st_gid != entry_cur->attrs.st_gid) {
				SET_MODIFIED(entry_prev->mask);
				SET_GID(entry_prev->mask);
				ndiffs++;
			}
			// Size
			if (entry_prev->attrs.st_size != entry_cur->attrs.st_size) {
				SET_MODIFIED(entry_prev->mask);
				SET_SIZE(entry_prev->mask);
				ndiffs++;
			}
			// Access time
			if (entry_prev->attrs.st_atime != entry_cur->attrs.st_atime) {
				SET_MODIFIED(entry_prev->mask);
				SET_LAT(entry_prev->mask);
				ndiffs++;
			}
			// Modified time
			if (entry_prev->attrs.st_mtime != entry_cur->attrs.st_mtime) {
				SET_MODIFIED(entry_prev->mask);
				SET_LMT(entry_prev->mask);
				ndiffs++;
			}
			// File status time
			if (entry_prev->attrs.st_ctime != entry_cur->attrs.st_ctime) {
				SET_MODIFIED(entry_prev->mask);
				SET_LFST(entry_prev->mask);
				ndiffs++;
			}
			
			SET_CHECKED(entry_prev->mask);
			SET_CHECKED(entry_cur->mask);
		} else {
			SET_REMOVED(entry_prev->mask);
			ndiffs++;
		}
		
		entry_prev = entry_prev->next;
	}
	
	entry_cur = curdir->head;
	while (entry_cur != NULL) {
		if (!IS_CHECKED(entry_cur->mask)) {
			SET_ADDED(entry_cur->mask);
			ndiffs++;
		}
		
		entry_cur = entry_cur->next;
	}
	
	return ndiffs;
}

void create_daemon(const char* name) {
	pid_t pid;
	struct sigaction sa;

	umask(0);

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

	pid = getpid();
	printf("Process ID: %d\n", pid);

    close(STDIN_FILENO);
    close(STDOUT_FILENO);
    close(STDERR_FILENO);

    openlog(name, LOG_CONS, LOG_DAEMON);
}

static void* signal_thread(void* arg) {
	pthread_attr_t tattr;
    int err, signo;
	pthread_t tid;
	
	pthread_attr_init(&tattr);
	pthread_attr_setdetachstate(&tattr, PTHREAD_CREATE_DETACHED);

    for (;;) {
        // Check directory for updates
        alarm(gperiod);

        err = sigwait(&mask, &signo);
        if (err != 0) {
            syslog(LOG_ERR, "sigwait failed");
            exit(1);
        }

        switch (signo) {
            case SIGHUP:
                // Finish transfers, remove all clients
                syslog(LOG_INFO, "Received SIGHUP");
				kill_clients(remove_client_pipes[1], "Server received SIGHUP; Disconnect all clients.");
                break;
            case SIGALRM:
                // See if directory has updated
                syslog(LOG_INFO, "Received SIGALRM");
				pthread_create(&tid, &tattr, send_updates, NULL);
                break;
			case SIGINT:
                syslog(LOG_INFO, "Received SIGINT");
				kill_clients(remove_client_pipes[1], "Server received SIGINT; Disconnect all clients.");
				exit(0);
            case SIGTERM:
                syslog(LOG_INFO, "Received SIGTERM");
				kill_clients(remove_client_pipes[1], "Server received SIGTERM; Disconnect all clients.");
				exit(0);
            default:
                // Finish transfers, quit
                syslog(LOG_ERR, "Unexpected signal: %d", signo);
                break;
        }
    }
}

void* send_updates(void* arg) {
	struct client* p;
	struct direntry* entry;
	struct direntrylist* tmp;
	int sent;
	int diffs;
	int more_diffs;
	
	diffs = difference_direntrylist();
	more_diffs = 0;
	
	if (diffs > 254) {
		more_diffs = diffs - 254;
		diffs = 254;
	}
	
	// LOCK
	pthread_mutex_lock(&clients_lock);
	
	p = clients->head;
	while (p != NULL) {
		// LOCK
		pthread_mutex_lock(p->c_lock);
		
		sent = 0;
		
		// Send number of entries that have changed
		if (send_byte(p->socket, (byte) diffs) <= 0) {
			syslog(LOG_ERR, "Could not send number of changed entries");
		}
		
		entry = prevdir->head;
		while (entry != NULL) {
			if (IS_MODIFIED(entry->mask)) {	
				if (IS_PERM(entry->mask)) {
					append_diff(update_buff, "!", entry->filename, " -> permissions");
					
					if (send_string(p->socket, update_buff) <= 0)
						syslog(LOG_ERR, "Could not send string");
					
					sent++;
					if (sent == diffs && more_diffs > 0) send_byte(p->socket, (byte) more_diffs);
				}
				
				if (IS_UID(entry->mask)) {
					append_diff(update_buff, "!", entry->filename, " -> UID");
					
					if (send_string(p->socket, update_buff) <= 0)
						syslog(LOG_ERR, "Could not send string");
						
					sent++;
					if (sent == diffs && more_diffs > 0) send_byte(p->socket, (byte) more_diffs);
				}
				
				if (IS_GID(entry->mask)) {
					append_diff(update_buff, "!", entry->filename, " -> GID");
					
					if (send_string(p->socket, update_buff) <= 0)
						syslog(LOG_ERR, "Could not send string");
						
					sent++;
					if (sent == diffs && more_diffs > 0) send_byte(p->socket, (byte) more_diffs);
				} 
				
				if (IS_SIZE(entry->mask)) {
					append_diff(update_buff, "!", entry->filename, " -> size");
					
					if (send_string(p->socket, update_buff) <= 0)
						syslog(LOG_ERR, "Could not send string");
						
					sent++;
					if (sent == diffs && more_diffs > 0) send_byte(p->socket, (byte) more_diffs);
				}
				
				if (IS_LAT(entry->mask)) {
					append_diff(update_buff, "!", entry->filename, " -> last access time");
					
					if (send_string(p->socket, update_buff) <= 0)
						syslog(LOG_ERR, "Could not send string");
						
					sent++;
					if (sent == diffs && more_diffs > 0) send_byte(p->socket, (byte) more_diffs);
				}
				
				if (IS_LMT(entry->mask)) {
					append_diff(update_buff, "!", entry->filename, " -> last modfied time");
					
					if (send_string(p->socket, update_buff) <= 0)
						syslog(LOG_ERR, "Could not send string");
						
					sent++;
					if (sent == diffs && more_diffs > 0) send_byte(p->socket, (byte) more_diffs);
				}
				
				if (IS_LFST(entry->mask)) {
					append_diff(update_buff, "!", entry->filename, " -> last file status time");
					
					if (send_string(p->socket, update_buff) <= 0)
						syslog(LOG_ERR, "Could not send string");
						
					sent++;
					if (sent == diffs && more_diffs > 0) send_byte(p->socket, (byte) more_diffs);
				}
			} else if (IS_REMOVED(entry->mask)) {
				append_diff(update_buff, "-", entry->filename, " ");
				
				if (send_string(p->socket, update_buff) <= 0)
					syslog(LOG_ERR, "Could not send string");
				
				sent++;
				if (sent == diffs && more_diffs > 0) send_byte(p->socket, (byte) more_diffs);
			}
			
			entry = entry->next;
		}
		
		entry = curdir->head;
		while (entry != NULL) {
			if (IS_ADDED(entry->mask)) {
				append_diff(update_buff, "+", entry->filename, " ");
				
				if (send_string(p->socket, update_buff) <= 0)
					syslog(LOG_ERR, "Could not send string");
				
				sent++;
				if (sent == diffs && more_diffs > 0) send_byte(p->socket, (byte) more_diffs);
			}
			
			entry = entry->next;
		}
		
		pthread_mutex_unlock(p->c_lock);
		p = p->next;
	}
	// UNLOCK
	pthread_mutex_unlock(&clients_lock);
	
	// Now reverse the values of prevdir and curdir
	// i.e. the curdir becomes the old dir
	reuse_direntrylist(prevdir);
	tmp = prevdir;
	prevdir = curdir;
	curdir = tmp;
	
	// Clear bitmasks
	entry = prevdir->head;
	while (entry != NULL) {
		entry->mask = 0;
		entry = entry->next;
	}
	
	return ((void*) 0);
}

int send_error(int socket, const char* err_msg) {
	struct client* p;
	
	if ((p = find_client_ref(socket)) == NULL) {
		syslog(LOG_ERR, "Could not find client to disconnect from.");
		return -1;
	} else {
		// LOCK
		pthread_mutex_lock(p->c_lock);
		
		if (send_byte(p->socket, END_COM) <= 0) {
			syslog(LOG_ERR, "Could not send error byte");
			return -1;
		}
		
		if (send_string(p->socket, err_msg) <= 0) {
			syslog(LOG_ERR, "Could not send error string");
			return -1;
		}
		// UNLOCK
		pthread_mutex_unlock(p->c_lock);
	}
	
	return 0;
}

int send_error2(int socket, const char* err_msg) {
	if (send_byte(socket, END_COM) <= 0) {
		syslog(LOG_ERR, "Could not send error byte");
		return -1;
	}
	
	if (send_string(socket, err_msg) <= 0) {
		syslog(LOG_ERR, "Could not send error string");
		return -1;
	}
	
	return 0;
}

void* init_client(void* arg) {
    pthread_t tid;
    int socketfd;
    size_t len;
	int socket_buff[1];

    tid = pthread_self();
    socketfd = (int) arg;
    len = strlen(init_dir);

	if (clients->count == MAX_CLIENTS) {
		syslog(LOG_INFO, "No more clients can be accepted.");
		// Tell main thread to remove socket from master list
		socket_buff[0] = socketfd;
		write(remove_client_pipes[1], socket_buff, (int) 1);
		
		// Make sure that the socket is removed from
		// the master list before preceding any farther
		// (Removes a race condition which would cause 
		// select() to break)
		pthread_mutex_lock(&slock);
		while (done == 0)
			pthread_cond_wait(&sready, &slock);
		done = 0;
		pthread_mutex_unlock(&slock);
		
		// Send the error now
		send_error2(socketfd, "No more clients can be accepted.");

		close(socketfd);
		pthread_exit((void*)1);
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

	return ((void*) 0);
}

void* remove_client(void* arg) {
	struct thread_arg* targ;
	
	targ = (struct thread_arg*) arg;
	// LOCK
	pthread_mutex_lock(&clients_lock);
	// Will block until particular client mutex is released
	remove_client_ref(targ->socket);
	// UNLOCK
	pthread_mutex_unlock(&clients_lock);
	// Now disconnect from client nicely
	disconnect_from_client(targ->socket, 0);
	
	free(targ);
	
	return ((void*) 0);
}

void kill_clients(int pipe, const char* msg) {
	struct client* p;
	int count;
	int socket;
	int socket_buff[1];
	
	p = clients->head;
	count = clients->count;
	
	// LOCK
	pthread_mutex_lock(&clients_lock);
	if (count > 0) {
		while (p != NULL) {
			socket = p->socket;
			
			// Send the socket fd to the main thread to remove
			// it from the master fd list
			socket_buff[0] = socket;
			write(pipe, socket_buff, 1);
			
			// Make sure that the socket is removed from
			// the master list before preceding any farther
			// (Removes a race condition which would cause 
			// select() to break)
			pthread_mutex_lock(&slock);
			while (done == 0)
				pthread_cond_wait(&sready, &slock);
			done = 0;
			pthread_mutex_unlock(&slock);
			
			send_error2(socket, msg);
			remove_client_ref(socket);
			close(socket);
			
			p = clients->head;
		}
	}
	// UNLOCK
	pthread_mutex_unlock(&clients_lock);
}

int disconnect_from_client(int socketfd, int pipe) {
	int pipe_buff[1];
	byte b;
	
	// Now send request to remove the socket from the
	// master file descriptor list, only if not already cleared
	if (pipe > 0) {
		pipe_buff[0] = socketfd;
		write(pipe, pipe_buff, 1);
	}
	
	if ((b = read_byte(socketfd)) != REQ_REMOVE1) {
		syslog(LOG_ERR, "Anticipated 0xDE: Received: 0x%x", b);
		close(socketfd);
		return -1;
	}
			
	if ((b = read_byte(socketfd)) != REQ_REMOVE2) {
		syslog(LOG_ERR, "Anticipated 0xAD: Received: 0x%x", b);
		close(socketfd);
		return -1;
	}
			
	if (send_byte(socketfd, END_COM) != 1) {
		syslog(LOG_ERR, "Could not send byte!");
		close(socketfd);
		return -1;
	}
			
	if (send_string(socketfd, GOOD_BYE) != 7) {
		syslog(LOG_ERR, "Could not send string");
		close(socketfd);
		return -1;
	}
	// Close socket now
	close(socketfd);
	
	return 0;
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
	
	if ( (ct = find_client_ref(socketfd)) == NULL ) {
		syslog(LOG_ERR, "Could not find client.");
		exit(1);
	}
	
	// If lock is currently locked, maybe server is trying to send
	// out data. Wait until lock is aquired.
	pthread_mutex_lock(ct->c_lock);
	
	if (clients->count == 1) {
		clients->head = clients->head->next;
		clients->tail = clients->head;
	} else if (ct == clients->head) {
		clients->head = clients->head->next;
		clients->head->prev = NULL;
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
	struct thread_arg* targ;
	pthread_attr_t tattr;

    fd_set master;
    fd_set read_fds;
    int fdmax, i;

    int listener;
    int newfd;
    struct sockaddr_in local_addr;
    struct sockaddr_in remote_addr;
    socklen_t addr_len;
	int pipe_buff[1];
	pipe_buff[0] = 0;

    struct sigaction sa;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    sa.sa_handler = SIG_IGN;

	pthread_attr_init(&tattr);
	pthread_attr_setdetachstate(&tattr, PTHREAD_CREATE_DETACHED);

	direntry_pool = init_mempool(sizeof(struct direntry), 512);

	done = 0;

	clients = (struct clientlist*) malloc(sizeof(struct clientlist));
	clients->head = NULL;
	clients->tail = NULL;
	clients->count = 0;

    strcpy(init_dir, dir_name);
    gperiod = period;

	if (pipe(remove_client_pipes) < 0) {
		syslog(LOG_ERR, "Cannot create IPC in server.");
		exit(1);
	}

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
	sigaddset(&mask, SIGINT);
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

	FD_SET(remove_client_pipes[0], &master);
    FD_SET(listener, &master);
    fdmax = listener;

	// Initialize the direntry lists
	prevdir = init_direntrylist();
	curdir  = init_direntrylist();

    exploredir(prevdir, (const char*) full_path);

	// Start signal thread
    pthread_create(&tid, NULL, signal_thread, NULL);

	int errno;

    // Main server loop
    while (1) {
        read_fds = master;

        if (select(fdmax+1, &read_fds, NULL, NULL, NULL) == -1) {
            syslog(LOG_ERR, "select: %s", strerror(errno));
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
                        pthread_create(&tid, &tattr, init_client, (void*) newfd);
                    }
                } else if (i == remove_client_pipes[0]) {
					// Get the file descriptor of the socket that is to
					// be removed and store into the first position of
					// tmp_buff
					if (read(remove_client_pipes[0], pipe_buff, 1) <= 0) {
						syslog(LOG_ERR, "Cannot read in socket to close.");
					} else {
						pthread_mutex_lock(&slock);
						FD_CLR(pipe_buff[0], &master);
						done = 1;
						pthread_mutex_unlock(&slock);
						
						// Tell kill_clients or whoever they
						// can procede
						pthread_cond_signal(&sready);		
					}
				} else {
					// Handle disconnect
					targ = (struct thread_arg*) malloc(sizeof(struct thread_arg));
					targ->socket = i;
					targ->pipe = remove_client_pipes[1];
					
					pthread_create(&tid, &tattr, remove_client, (void*) targ);
					FD_CLR(i, &master);
				}
            }
        }
    }

	return 0;
}
