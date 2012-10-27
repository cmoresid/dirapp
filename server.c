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

// Global variables
pthread_mutex_t clients_lock = PTHREAD_MUTEX_INITIALIZER;
sigset_t mask;
struct client_thread* clients;

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

    if (list->head == NULL) {
        list->head = entry;
        list->tail = list->head;
    } else if (list->head->next == NULL) {
        list->head->next = entry;
        list->tail = list->head->next;
    } else {
        list->tail->next = entry;
        list->tail = entry;
    }
    
    list->count++;

    return 1;
}

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

void* signal_thread(void* arg) {
    int err, signo;

    for (;;) {
        err = sigwait(&mask, &signo);
    }
}

void add_client(pthread_t tid, int socketfd) {
    struct client_thread *ct;

    ct = (struct client_thread*)malloc(sizeof(struct client_thread));
    if (ct == NULL) {
        syslog(LOG_ERR, "Cannot malloc new client thread");
        pthread_exit((void*)1);
    }

    ct->tid = tid;
    ct->socket = socketfd;
    // LOCK
    pthread_mutex_lock(&clients_lock);
    ct->prev = NULL;
    ct->next = clients;

    if (clients == NULL) {
        clients = ct;
    } else {
        clients->prev = ct;
    }    
    // UNLOCK
    pthread_mutex_lock(&clients_lock);
}

void* handle_client(void* arg) {
    pthread_t tid;
    int socketfd;

    tid = pthread_self();
    socketfd = (int) arg;

    syslog(LOG_INFO, "Spawned new thread to handle client!");
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
    pthread_create(&tid, NULL, signal_thread, NULL);

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
                        // SPAWN NEW THREAD HERE
                        pthread_create(&tid, NULL, handle_client, (void*)newfd);
                    }

                }
            }
        }
    }

	return 0;
}
