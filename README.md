# Software-Systems---Assignment-2

**Assignment 2: Multithreaded Chat Application**

In this assignment, we were required to build a UDP-based chat application where multiple clients communicate through a multithreaded server. The goal was to manage client connections, messaging, and commands while ensuring proper synchronization. 

All requirements of the basic chat application were met, including those for the client, server, and user interface. The communication requests that were implemented include conn (for connecting a new client), say (to send a message publicly), sayto (to send a message privately), mute (to mute a specific user), unmute (to unmute a muted user), rename (to change user's name), disconn (to disconnect from the chat), and kick (for admin to remove a specific user). These were all fully implemented. 

For the user interface, we decided to go with the second option of using two terminal windows, one for user input and the other to display the chat content. This was designed in a way so that each client would get their own chat interface according to their individual port number (which was randomly assigned by the OS to have no overlaps). 

Synchronization was also properly implemented using pthread reader-writer locks, allowing safe concurrent acces to the shared client list by avoiding race conditions. The read-mode lock allows unlimited readers at the same time, while the write-mode lock blocks all other threads including both reads and writes.
