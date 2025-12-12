
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <unistd.h>

#include "udp.h"

//#define CLIENT_PORT 10000
#define SERVER_IP "127.0.0.1"

int sd;  //UDP socket descriptor
int done = 0;
struct sockaddr_in server_addr, responder_addr;  //server and responder address

FILE *chat_file;  //UI store msg


void *listener_thread(void *arg) {
    char buffer[BUFFER_SIZE];

    // open UI output file
    char *filename = (char*)arg;
    chat_file = fopen(filename, "w");
    if (!chat_file) {
        perror("fopen");
        return NULL;
    }

    while (!done) {
        int rc = udp_socket_read(sd, &responder_addr, buffer, BUFFER_SIZE);  // blocking

        if (rc <= 0) {
            break;
        }

        // terminate with '\0'
        if (rc < BUFFER_SIZE) buffer[rc] = '\0';
        else buffer[BUFFER_SIZE - 1] = '\0';

        fprintf(chat_file, "%s\n", buffer);
        fflush(chat_file);
    }

    fclose(chat_file);
    return NULL;
}

void *sender_thread(void *arg) {
    char client_request[BUFFER_SIZE];

    while (!done) {

        printf("> ");

        // disconnect on error
        if (!fgets(client_request, BUFFER_SIZE, stdin)) {
            strcpy(client_request, "disconn$");
        }

        // Remove newline
        client_request[strcspn(client_request, "\n")] = '\0';


        // Send request to server
        int rc = udp_socket_write(sd, &server_addr, client_request, strlen(client_request) + 1);

        if (rc < 0)
            perror("udp_socket_write");

        // Disconnect
        if (strcmp(client_request, "disconn$") == 0) {
            done = 1;
            break;
        }
    }

    return NULL;
}


int main(int argc, char *argv[]) {

    // 0 assigns random port
    sd = udp_socket_open(0);

    // Get client's local port number to make UI output file
    struct sockaddr_in local_addr;
    socklen_t len = sizeof(local_addr);
    getsockname(sd, (struct sockaddr*)&local_addr, &len);
    int my_port = ntohs(local_addr.sin_port);

    char filename[64];
    snprintf(filename, sizeof(filename), "iChat_%d.txt", my_port);


    int rc = set_socket_addr(&server_addr, SERVER_IP, SERVER_PORT);

    pthread_t sender, listener;
    pthread_create(&listener, NULL, listener_thread, filename);
    pthread_create(&sender, NULL, sender_thread, NULL);

    pthread_join(sender, NULL);

    // Signal listener to exit, close socket so udp_socket_read unblocks
    done = 1;
    close(sd);

    pthread_join(listener, NULL);

    return 0;
}
