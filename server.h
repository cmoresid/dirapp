/*
 * =====================================================================================
 *
 *       Filename:  server.h
 *
 *    Description:  
 *
 *        Version:  1.0
 *        Created:  24/10/2012 11:23:20
 *       Revision:  none
 *       Compiler:  gcc
 *
 *         Author:  Connor Moreside (conman720), cmoresid@ualberta.ca
 *   Organization:  
 *
 * =====================================================================================
 */

#ifndef SERVER_H
#define SERVER_H

#include <pthread.h>

struct client {
    struct client* next;
    struct client* prev;
    int socket;
	pthread_mutex_t* c_lock;
};

struct direntrylist {
    int count;
    struct direntry* head;
    struct direntry* tail;
};

struct direntry {
    char* filename;
    struct stat* attrs;
    struct direntry* next;
};

struct client* find_client(int socketfd);

void* send_updates(void* arg);

/* 
 * ===  FUNCTION  ======================================================================
 *         Name:  create_daemon
 *  Description:  Turns the current running process into a daemon
 * =====================================================================================
 */
void create_daemon(const char* name);

/* 
 * ===  FUNCTION  ======================================================================
 *         Name:  start_server
 *  Description:  Starts the server functionality of dirapp.
 * =====================================================================================
 */
int start_server(int port_number, const char* dir_name, int period);

/* 
 * ===  FUNCTION  ======================================================================
 *         Name:  signal_thread
 *  Description:  Seperate thread to handle signals
 * =====================================================================================
 */
void* signal_thread(void* arg);

/* 
 * ===  FUNCTION  ======================================================================
 *         Name:  init_client
 *  Description:  Handles a client on a separate thread.
 * =====================================================================================
 */
void* init_client(void* arg);

/* 
 * ===  FUNCTION  ======================================================================
 *         Name:  remove_client
 *  Description:  Handles a client on a separate thread.
 * =====================================================================================
 */
void* remove_client(void* arg);

/* 
 * ===  FUNCTION  ======================================================================
 *         Name:  add_client_ref
 *  Description:  Adds new client to a list of clients.
 * =====================================================================================
 */
void add_client_ref(int socketfd);

/* 
 * ===  FUNCTION  ======================================================================
 *         Name:  remove_client_ref
 *  Description:  Removes a client from a list of clients.
 * =====================================================================================
 */
void remove_client_ref(int socketfd);

/* 
 * ===  FUNCTION  ======================================================================
 *         Name:  init_direntrylist
 *  Description:  Initializes and returns a new direntrylist
 * =====================================================================================
 */
struct direntrylist* init_direntrylist();

/* 
 * ===  FUNCTION  ======================================================================
 *         Name:  add_direntry
 *  Description:  Adds a direntry to a given direntrylist. Returns 1 on success, 0 on
 *                fail.
 * =====================================================================================
 */
int add_direntry(struct direntrylist* list, struct direntry* entry);

/* 
 * ===  FUNCTION  ======================================================================
 *         Name:  remove_direntry
 *  Description:  Removes a direntry in list if found. Returns 1 on success, 0 on fail.
 * =====================================================================================
 */
int remove_direntry(struct direntrylist* list, struct direntry* entry);

/* 
 * ===  FUNCTION  ======================================================================
 *         Name:  replace_direntry
 *  Description:  Replaces a direntry in a direntrylist with another direntry
 * =====================================================================================
 */
int replace_direntry(struct direntrylist* list,
        struct direntry* inlist,
        struct direntry* replacewith);


/* 
 * ===  FUNCTION  ======================================================================
 *         Name:  exploredir
 *  Description:  Explores the given directory.
 * =====================================================================================
 */
struct direntrylist* exploredir(const char* path);

#endif
