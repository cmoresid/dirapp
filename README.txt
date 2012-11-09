=============================================================
                       README: DIRAPP
=============================================================

dirapp is divided into 2 parts: the client and server
functionality found in client.c and server.c, respectively.
dirapp.c contains the main method, which verifies the parameters
and starts the respective functionality. In the case of server
mode, it also verifies that the directory to be monitored exists
to begin with.

*************************************************************
common.h / common.c
*************************************************************
There are macro constants that are common between both
functionalities, such as the byte sequences to send. The functions
found in common.c are used to send / receive bytes and strings
as dictated by the assignment protocol.

*************************************************************
Server Functionality
*************************************************************
There are 2 threads in the server portion that are always
running: the signal thread and the main thread.

The signal thread deals with any signals that are raised. It
works through a global signal mask that is inherited by all
threads. The signal thread unblocks those signals and waits for
any incoming signals and deals with them appropriately. This is
nice because you don't have to deal with those pesky sigjmps and
the like.

The main thread is basically the coordinator. It accepts any
requests and then spawns a thread to deal with the request. When
a client tries to establish a connection, the socket is created on
the main thread, then it is passed on to a separate thread 
(init_client) to deal with the rest of establishing of a
connection. This allows the main thread not to be blocked while
dealing with the incoming connection. When data is received by
the server, it means a client wants to disconnect. A separate
thread is spawned in order to deal with this.

In an init_client thread, the socket representing a client
is added into a 'client' structure node, which contains the socket
and a mutex associated with that client. The node is added to
a 'clients' linked list, which contains all the connected clients.
The negotiation with the client is also handled in the thread.

In a remove_client thread, a client is removed from the linked
list of connected clients. When remove_client_ref is executed,
it tries to acquire the lock associated with that client. If it
cannot, it means the server is in the process of sending updates
to the client, so the remove_client thread blocks until it can
acquire the mutex.

The algorithm used to determine the changes made to a given
directory is as followed:

1) Create initial archive of directory contents in a direntrylist. A
   direntrylist contains direntry structures, which store a file's
   name, attributes, and a mask that represents changes made to those
   attributes or to the file itself.
2) When it is time to for the server to check for updates, it makes
   a list of the current entries in the directory right now, and
   compares it to the past entries. A bit mask is used to note the
   differences.

Note: Since direntry structures are constantly being add/removed and
are allocated with dynamic memory, a memory fragmentation problem
appeared. The executable's memory space kept on expanding, and
speed was hindered, so instead of constantly malloc/free entries, I
opted to used a memory pool to handle this. It seemed to fix the
problem. 

When the maximum number of client connections is reached, and a
new client tries to connect, the server sends an error message
to the client and drops the connection.

*************************************************************
Client Functionality
*************************************************************
There are 3 threads running at all times: the input thread, 
the signal thread, and the main thread.

The input thread handles all incoming input from the keyboard. It
parses the input and does some rudimentary verification on the
input. Once the input is parsed, the parsed text is sent back
to the main thread via a pipe so the main thread can take
appropriate action.

The signal thread acts the same as in the server portion.

The main thread has a similar role as in the server portion. It
acts as a dispatcher. It can spawn a new thread to initiate a
connection with a server, remove a server connection.

There is a lot more IPC going on in the client portion. When a
client wants to drop a connection to a server, it must go through
a series of steps. It must send the disconnection sequence to
a client and remove the socket from the master file descriptor
list. The socket to remove is sent through a pipe back to the
main thread and promptly removed from the master fd list. When
it is removed, the removal thread is signalled that it can
continue. See client.c for better clarification.

Also, to write anything out the console, a thread must first
acquire the io_lock mutex. This makes sure at most on thread
can write out to the console.

*************************************************************
User Interface
*************************************************************
The UI for the client is a simple command line interface. It
supports the basic commands as prescribed: remove, add, list
and quit. The instructions are initially printed out to the
console. '>' specifies that the client is ready to receive
input from the user. When the client has received an update, the
update will only be printed out AFTER the user has entered another
command or has just pressed entered. This ensures that when the
user is typing something, an update doesn't interrupt their
command.


*************************************************************
Tests Run
*************************************************************
Client --
1) Connect to multiple servers and alter directory using
   the 'touch' command
2) Send SIGHUP to client to disconnect all servers

Server --
1) Multiple clients connected to server
2) Disconnect while sending updates

I have not tested the program with other students. I did however
run it with the provided example and it seems to work just
fine.

*************************************************************
Problems
*************************************************************
1) When server is "daemonized" (i.e. the macro DAEMONIZE) is defined,
   the SIGHUP functionality stops working and in turn breaks any
   connected clients. I wish I had time to fix this. So by default,
   the server will NOT BE RUN IN DAEMON MODE!
2) The nastyTests.sh script appears to cause an issue only when I
   run it on a Linux machine. It runs just fine on Mac OS X. It is
   hard to debug the issue since on the school machines you need to
   be root to 'gdb attach' to a running process. When I run it on an
   Ubuntu virtual machine, it again works fine. The issue is during the
   last update, part of the text gets cut off and the prompt arrow
   doesn't reappear.