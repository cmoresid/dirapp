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

// Do we want to daemonize?
//#define DAEMONIZE

/* Ensures mutual exclusion for clients linked list */
pthread_mutex_t clients_lock = PTHREAD_MUTEX_INITIALIZER;
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
    struct direntrylist* list;		/* New directory list */ 

	// Allocate memory for a new directory list
    list = (struct direntrylist*) malloc(sizeof(struct direntrylist));    
    if (list == NULL)
        return NULL;

	// Initialize to default values
    list->count = 0;
    list->head = NULL;
    list->tail = NULL;

    return list;
}

void reuse_direntrylist(struct direntrylist* list) {
	struct direntry* p;		/* Used to traverse the linked list */

	p = list->head;	
	while (p != NULL) {
		list->head = list->head->next;
		p->next = NULL;
		// Return memory back to pool
		mempool_free(direntry_pool, p);
		p = list->head;
		list->count--;
	}
	
	// NULL for both head and tail
	list->tail = list->head;
}

int add_direntry(struct direntrylist* list, struct direntry* entry) {
    // Make sure list is not empty
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
    struct direntry* p;

	// Make sure list is not empty
	if (list == NULL)
        return NULL;

    p = list->head;
    while (p != NULL) {
		// Check for equality based on the inode, which should
		// be unique under the assumption that all the files
		// in the monitored directory are on the same filesystem
        if (p->attrs.st_ino == entry->attrs.st_ino) {
            return p;
        }

        p = p->next;
    }
    
    return NULL;
}

int exploredir(struct direntrylist* list, const char* path) {
    struct stat fattr;				/* Used to store attributes of a file entry */
    struct direntry* list_entry;	/* Used to capture information about file entry */
    struct dirent** entries;		/* Stores each file entry's name and stuff */
	int n;							/* How many file entries are in the directory */
	int i;							/* Used to traverse file entries */
    
	// Alphabetize the entries in the directory since Linux is stupid
	// and doesn't do this by default, which Mac OS X does...
    if ( (n = scandir(path, &entries, 0, alphasort)) < 0 ) {
        // Send error message to all clients and then exit
		kill_clients(remove_client_pipes[1], "Cannot open directory! ; Exiting now!");
        syslog(LOG_ERR, "Cannot open directory: %s", path);
        exit(1);
    }

	// Delete reference to current directory (.)
	free(entries[0]);
	// Delete reference to parent directory (..)
	free(entries[1]);   
 
	// Start after . and ..
	i = 2;
    while (i < n) {
		list_entry = (struct direntry*) mempool_alloc(direntry_pool, sizeof(struct direntry)); 
        list_entry->next = NULL;
        memset(list_entry, 0, sizeof(list_entry));

        if (list_entry == NULL) {
			// Mempool has no free nodes and malloc failed
			kill_clients(remove_client_pipes[1], "Unrecoverable server error! ; Exiting now!");
            syslog(LOG_ERR, "Cannot malloc direntry");
            exit(1);
        }

		// Make sure absolute path is not too long
        if ((strlen(path) + strlen(entries[i]->d_name) + 1) >= PATH_MAX) {
			kill_clients(remove_client_pipes[1], "Unrecoverable server error! ; Exiting now!");
            syslog(LOG_ERR, "Path is too long.");
            exit(1);
        } else {
            strcpy(abspath, path);
            strcat(abspath, "/");
            strcat(abspath, entries[i]->d_name);
        }

		// Get the attributes of the file entry
        if (stat(abspath, &fattr) < 0) {
			kill_clients(remove_client_pipes[1], "Unrecoverable server error! ; Exiting now!");
            syslog(LOG_ERR, "Cannot get stats on file: %s", entries[i]->d_name);
            exit(1);
        }

		// Make sure just the file name is not too long
		if (strlen(entries[i]->d_name) > MAX_FILENAME) {
			syslog(LOG_ERR, "Filename is too long to be saved.");
			exit(1);
		}

		// Copy the file entry name into direntry representation
        strcpy(list_entry->filename, entries[i]->d_name);
		list_entry->attrs = fattr;	

		// Add the list entry now
        add_direntry(list, list_entry);

		free(entries[i]);
		i++;
    }

	free(entries);	

    return 0;
}

void append_diff(byte* buff, const char* mode, const char* filename, const char* desc) {
	size_t len;		/* Used to find length of filename */
	
	// Zero out buffer
	memset(buff, 0, sizeof(buff));
	
	// (! OR - OR +) filename human_description
	len = strlen(filename);
	strcat(buff, mode);
	strcat(buff+1, " ");
	strcat(buff+2, filename);
	strcat(buff+2+len, desc);
}

int difference_direntrylist() {
	int ndiffs;						/* Number of differences found */
	struct direntry* entry_prev;	/* Pointer to iterate through previous direntrylist */
	struct direntry* entry_cur;		/* Pointer to iterate through current direntry list */
	
	ndiffs = 0;

	// Populate the curdir list with entries in directory right now
	exploredir(curdir, (const char*) full_path); /* Global variable: full_path */
	
	// No differences if there is no entries in the directory
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
			
			// Show that the entries have been checked for differences
			// and not to check them again
			SET_CHECKED(entry_prev->mask);
			SET_CHECKED(entry_cur->mask);
		} else {
			// If a previous entry cannot be found in the current directory,
			// it was removed
			SET_REMOVED(entry_prev->mask);
			ndiffs++;
		}
		
		entry_prev = entry_prev->next;
	}
	
	// Now check for any entries that have been added to the monitored
	// directory
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
	pthread_attr_t tattr;		/* Used to set thread to detached mode */
	int err;					/* Indicates an error from sigwait */
	int signo;					/* The signal number that has been caught */
	pthread_t tid;				/* ID of the thread that has been spawnned */
	
	// Make sure any threads that are spawned are detached, so the OS
	// can reclaim the resources in a timely fashion
	pthread_attr_init(&tattr);
	pthread_attr_setdetachstate(&tattr, PTHREAD_CREATE_DETACHED);

    for (;;) {
        // Check directory for updates
        alarm(gperiod);

		// Block until signal has been caught
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
				// Mainly used when not running in daemon mode
                syslog(LOG_INFO, "Received SIGINT");
				kill_clients(remove_client_pipes[1], "Server received SIGINT; Disconnect all clients.");
				exit(0);
            case SIGTERM:
                syslog(LOG_INFO, "Received SIGTERM");
				kill_clients(remove_client_pipes[1], "Server received SIGTERM; Disconnect all clients.");
				exit(0);
            default:
                syslog(LOG_ERR, "Unexpected signal: %d", signo);
                break;
        }
    }
}

void* send_updates(void* arg) {
	struct client* p;			/* Pointer to traverse through client list */
	struct direntry* entry;		/* Pointer to traverse through a direntry list */
	struct direntrylist* tmp;	/* Used as tmp storage to swap prevdir and curdir */
	int sent;					/* How many updates have been sent so far */
	int diffs;					/* The number of differences in monitored directory */
	int more_diffs;				/* If diffs > 254, save how many more need to be sent */

	// Get number of differences found in monitored directory
	diffs = difference_direntrylist();
	more_diffs = 0;
	
	// If there are more than 254 changed entries, send the first 254 updates
	// then send in separate update the rest of the updates
	if (diffs > 254) {
		more_diffs = diffs - 254;
		diffs = 254;
	}
	
	// LOCK : Make sure clients is not altered while sending updates
	pthread_mutex_lock(&clients_lock);
	
	p = clients->head;
	while (p != NULL) {
		// LOCK : Make sure client is not removed while update is being
		//        sent out
		pthread_mutex_lock(p->c_lock);
		
		// Reset
		sent = 0;
		
		// Send number of entries that have changed
		if (send_byte(p->socket, ((byte) diffs)) <= 0) {
			syslog(LOG_ERR, "Could not send number of changed entries");
		}
		
		// Now examine bitmask of each entry in prevdir and see if attributes
		// have changed or if entry has been removed
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
		
		// Now examine the current state of the monitored directory and see
		// if any entries have been added
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
		
		// UNLOCK
		pthread_mutex_unlock(p->c_lock);
		p = p->next;
	}
	// UNLOCK
	pthread_mutex_unlock(&clients_lock);
	
	// Now reverse the roles of prevdir and curdir
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
	struct client* p;		/* Used to hold client reference */
	
	// Try to find client in clients linked list
	if ((p = find_client_ref(socket)) == NULL) {
		syslog(LOG_ERR, "Could not find client to disconnect from.");
		return -1;
	} else {
		// LOCK : Make all updates have been sent to client first
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
    int socketfd;			/* Socket fd of new client */
    size_t len;				/* String length */
	int socket_buff[1];		/* Buffer used to send socket back to main thread */

    socketfd = (int) arg;
    len = strlen(init_dir);

	// No more clients are being accepted, remove socket from master list,
	// and wait until it has been removed properly
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

	// Add client to clients linked list
    pthread_mutex_lock(&clients_lock);
    add_client_ref(socketfd);
    pthread_mutex_unlock(&clients_lock);

	// Send 0xFE
	if (send_byte(socketfd, INIT_CLIENT1) != 1) {
		syslog(LOG_WARNING, "Cannot send init client 1");
		exit(1);
	}
	
	// Send 0xED
	if (send_byte(socketfd, INIT_CLIENT2) != 1) {
		syslog(LOG_WARNING, "Cannot send init client 2");
		exit(1);
	}
	
	// Send monitored directory name/path
	if (send_string(socketfd, init_dir) != len) {
		syslog(LOG_WARNING, "Cannot send path directory");
		exit(1);
	}
	
	// Send the refresh period
	if (send_byte(socketfd, gperiod) != 1) {
		syslog(LOG_WARNING, "Cannot send period");
		exit(1);
	}

	return ((void*) 0);
}

void* remove_client(void* arg) {
	struct thread_arg* targ;	/* Thread arguments */
	
	// Get thread args
	targ = (struct thread_arg*) arg;

	// LOCK : Make sure clients is not altered
	//        while trying to remove client ref
	pthread_mutex_lock(&clients_lock);
	remove_client_ref(targ->socket);
	pthread_mutex_unlock(&clients_lock);
	
	// Now disconnect from client nicely
	disconnect_from_client(targ->socket, 0);
	
	// Free thread arg
	free(targ);
	
	return ((void*) 0);
}

void kill_clients(int pipe, const char* msg) {
	struct client* p;			/* Used to traverse clients linked list */
	int socket_buff[1];			/* Send socket fd to main thread to remove from
								   master fd list */
	
	// LOCK : Make sure clients is not altered while removing all client connections
	pthread_mutex_lock(&clients_lock);
	if (clients->count > 0) {
		p = clients->head;
		while (p != NULL) {
			// Send the socket fd to the main thread to remove
			// it from the master fd list
			socket_buff[0] = p->socket;
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
			
			send_error(p->socket, msg);
			close(p->socket);
			remove_client_ref(p->socket);
			
			p = clients->head;
		}
	}
	// UNLOCK
	pthread_mutex_unlock(&clients_lock);
}

int disconnect_from_client(int socketfd, int pipe) {
	byte b;
	
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
    struct client *ct;		/* New client reference */

	if (clients == NULL) {
		clients = (struct clientlist*) malloc(sizeof(struct clientlist));
		clients->head = NULL;
		clients->tail = NULL;
		clients->count = 0;
	}

	// Try to allocate space for a new client
    ct = (struct client*) malloc(sizeof(struct client));
    if (ct == NULL) {
        syslog(LOG_ERR, "Cannot malloc new client thread");
		return;
    }

	// Initialize client ref
	ct->c_lock = (pthread_mutex_t*) malloc(sizeof(pthread_mutex_t));
	pthread_mutex_init(ct->c_lock, NULL);
    ct->socket = socketfd;
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
}

void remove_client_ref(int socketfd) {
	struct client* ct;		/* Client ref to remove */
	struct client* tmp;		/* Tmp storage */
	
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
	struct client* p;	/* Client ref with socketfd as its socket */
	
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
    pthread_t tid;					/* Passed to pthread_create */
	struct thread_arg* targ;		/* Used to pass arguments to threads */
	pthread_attr_t tattr;			/* Specifies that thread should be detached */
    fd_set master;					/* Keep track of all connections / pipes to multiplex */
    fd_set read_fds;				/* Copy of master for select to populate */
	int fdmax;						/* Highest numbered file descriptor */
	int i;							/* Used to index the master fd list */
    int listener;					/* Listening socket of the server */
    int newfd;						/* New connection socket fd */
    struct sockaddr_in local_addr;	/* Local connection info */
    struct sockaddr_in remote_addr;	/* Remote connection info */
    socklen_t addr_len;				/* Address length */
	int pipe_buff[1];				/* Get sockets into here */
	pipe_buff[0] = 0;
	
	// Init signal mask
    struct sigaction sa;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    sa.sa_handler = SIG_IGN;

	// Start each thread in detached mode, since we don't care about
	// their return values
	pthread_attr_init(&tattr);
	pthread_attr_setdetachstate(&tattr, PTHREAD_CREATE_DETACHED);

	// Initialize the memory pool to store the direntries
	direntry_pool = init_mempool(sizeof(struct direntry), 512);

	// Set done to 0
	done = 0;

	// Initialize the clients linked list
	clients = (struct clientlist*) malloc(sizeof(struct clientlist));
	clients->head = NULL;
	clients->tail = NULL;
	clients->count = 0;

	// Copy into global init_dir
    strcpy(init_dir, dir_name);
    gperiod = period;

	// Initialize pipe
	if (pipe(remove_client_pipes) < 0) {
		syslog(LOG_ERR, "Cannot create IPC in server.");
		exit(1);
	}

	// Get full path of the directory
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

	// Make sure SIGPIPE is blocked
    if (sigaction(SIGPIPE, &sa, NULL) < 0) {
        syslog(LOG_WARNING, "SIGPIPE error"); 
    }
        
	// Signals for the signal thread to handle
    sigemptyset(&mask);
    sigaddset(&mask, SIGHUP);
    sigaddset(&mask, SIGTERM);
	sigaddset(&mask, SIGINT);
    sigaddset(&mask, SIGALRM);

	// Set the mask
    if (pthread_sigmask(SIG_BLOCK, &mask, NULL) != 0)
        syslog(LOG_WARNING, "pthread_sigmask failed");

	// Initialize file descriptor lists
    FD_ZERO(&master);
    FD_ZERO(&read_fds);

	// Setup local connection info
    memset(&local_addr, 0, sizeof(local_addr));
    local_addr.sin_family = AF_INET;
    local_addr.sin_addr.s_addr = inet_addr("127.0.0.1");
    local_addr.sin_port = htons(port_number);
	
	// Create listener socket
    listener = socket(AF_INET, SOCK_STREAM, 0);

	// Try to bind
    if (bind(listener, (struct sockaddr*)&local_addr, sizeof(local_addr))) {
        syslog(LOG_ERR, "Cannot bind socket to address");    
        exit(1);
    }

	// Now listen!
    if (listen(listener, MAX_CLIENTS) < 0) {
        syslog(LOG_ERR, "Cannot listen on socket");
        exit(1);
    }

    syslog(LOG_INFO, "Starting server!");

	// Have select check for incoming data
	FD_SET(remove_client_pipes[0], &master);
    FD_SET(listener, &master);
    fdmax = listener;

	// Initialize the direntry lists
	prevdir = init_direntrylist();
	curdir  = init_direntrylist();

	// Initially populate list of file entries in monitored directory
    exploredir(prevdir, (const char*) full_path);

	// Start signal thread
    pthread_create(&tid, NULL, signal_thread, NULL);

	int errno = 0;

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
						// Remove socket from master list
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
