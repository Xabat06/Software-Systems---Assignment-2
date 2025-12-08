
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <unistd.h>
#include <signal.h>

#include "udp.h"

//#define CLIENT_PORT 10000
#define SERVER_IP "127.0.0.1"

int sd;  //UDP socket descriptor
int done = 0;
struct sockaddr_in server_addr, responder_addr;  //server and responder address

FILE *chat_file;  //store msg

// linked list for muted users
typedef struct MuteNode {
    char name[64];
    struct MuteNode *next;
} MuteNode;

MuteNode *mute_head = NULL;
pthread_mutex_t mute_lock = PTHREAD_MUTEX_INITIALIZER;

// add to mute list
void add_mute(char *name) {
    pthread_mutex_lock(&mute_lock);

    // check if name is in list
    MuteNode *cur = mute_head;
    while (cur) {
        if (strcmp(cur->name, name) == 0) {
            pthread_mutex_unlock(&mute_lock);
            return;
        }
        cur = cur->next;
    }

    MuteNode *n = malloc(sizeof(MuteNode));
    strcpy(n->name, name);
    n->next = mute_head;
    mute_head = n;

    pthread_mutex_unlock(&mute_lock);
}

// remove from mute list
void remove_mute(char *name) {
    
}

// client code
int main(int argc, char *argv[])
{
    // This function opens a UDP socket,
    // binding it to all IP interfaces of this machine,
    // and port number CLIENT_PORT.
    // (See details of the function in udp.h)
    int sd = udp_socket_open(CLIENT_PORT);

    // Variable to store the server's IP address and port
    // (i.e. the server we are trying to contact).
    // Generally, it is possible for the responder to be
    // different from the server requested.
    // Although, in our case the responder will
    // always be the same as the server.
    struct sockaddr_in server_addr, responder_addr;

    // Initializing the server's address.
    // We are currently running the server on localhost (127.0.0.1).
    // You can change this to a different IP address
    // when running the server on a different machine.
    // (See details of the function in udp.h)
    int rc = set_socket_addr(&server_addr, "127.0.0.1", SERVER_PORT);

    // Storage for request and response messages
    char client_request[BUFFER_SIZE], server_response[BUFFER_SIZE];

    // Demo code (remove later)
    strcpy(client_request, "Dummy Request");

    // This function writes to the server (sends request)
    // through the socket at sd.
    // (See details of the function in udp.h)
    rc = udp_socket_write(sd, &server_addr, client_request, BUFFER_SIZE);

    if (rc > 0)
    {
        // This function reads the response from the server
        // through the socket at sd.
        // In our case, responder_addr will simply be
        // the same as server_addr.
        // (See details of the function in udp.h)
        int rc = udp_socket_read(sd, &responder_addr, server_response, BUFFER_SIZE);

        // Demo code (remove later)
        printf("server_response: %s", server_response);
    }

    return 0;
}
