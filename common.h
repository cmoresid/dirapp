/*
 * =====================================================================================
 *
 *       Filename:  dirapp.h
 *
 *    Description:  
 *
 *        Version:  1.0
 *        Created:  22/10/2012 15:42:08
 *       Revision:  none
 *       Compiler:  gcc
 *
 *         Author:  Connor Moreside (conman720), cmoresid@ualberta.ca
 *   Organization:  CMPUT379
 *
 * =====================================================================================
 */

#ifndef DIRAPP_H
#define DIRAPP_H

#define	CLIENT_INIT     0xFE	 /* Initiates connection.    */
#define	SERVER_ACKN     0xED	 /* Acknowledge connection.  */

#define	REQUEST_REMOVE  0xDE     /* Client requests removal from server. */	
#define	ACKN_REMOVE		0xAD	 /* Server acknowledges client's removal request. */
#define	NO_UPDATES		0x00 	 /* No updates to send to clients. */

#define	MAX_CLIENTS     10		 /* Max number of clients. */
#define MAX_SERVERS     5        /* Max number of servers running concurrently. */

#define PATH_MAX        256      /* Max path size. */

void err_quit(const char* error);

// Get max length of file name
// Have separate thread to deal with signals
// Have separate thread to deal with incoming clients
// Have linked list containing all current files and their stats
// Count how many entries are in directory
// #include <dirent.h> .. DIR* dp = opendir(path); dirent* entry = readdir(dp
// AND entries in stat structure to see if the same
// first check if number of 

#endif
