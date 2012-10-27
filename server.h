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

struct client_thread {
    struct client_thread* next;
    struct client_thread* prev;
    pthread_t tid;
    int socket;
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
 *         Name:  handle_client
 *  Description:  Handles a client on a separate thread.
 * =====================================================================================
 */
void* handle_client(void* arg);

/* 
 * ===  FUNCTION  ======================================================================
 *         Name:  add_client
 *  Description:  Adds new client thread to list of clients.
 * =====================================================================================
 */
void add_client(pthread_t tid, int socketfd);


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
