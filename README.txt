=============================================================
                          DIRAPP
=============================================================

Disclaimer: Any use of this code by unauthorized individuals 
will be viewed as plagiarism as per the U of A Code of Student 
Behaviour (Section 30.3.2[1]) and will be reported to the 
appropriate University representatives accordingly.

Description: A simple directory monitor geared towards 
POSIX-based systems. It can act as either a server or client. 
In server mode, one specifies a directory monitor to and a 
refresh period. It sends updates to any connected client.

*************************************************************
User Interface
*************************************************************
The UI for the client is a simple command line interface. It
supports the basic commands: remove, add, list
and quit. The instructions are initially printed out to the
console. '>' specifies that the client is ready to receive
input from the user. When the client has received an update, the
update will only be printed out AFTER the user has entered another
command or has just pressed entered. This ensures that when the
user is typing something, an update doesn't interrupt their
command.
