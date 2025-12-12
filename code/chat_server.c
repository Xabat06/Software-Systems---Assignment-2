
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <unistd.h>

#include "udp.h"

#define NAME_LEN 64
#define ADMIN_PORT 6666


// Linked list structures
typedef struct MuteNode {
    char name[NAME_LEN];
    struct MuteNode *next;
} MuteNode;

typedef struct Client {
    struct sockaddr_in addr;
    char name[NAME_LEN];
    MuteNode *mute_head;
    struct Client *next;
} Client;

Client *clients_head = NULL;
pthread_rwlock_t clients_lock = PTHREAD_RWLOCK_INITIALIZER;

int sd;


// Basic helper functions 
int same_addr(struct sockaddr_in *a, struct sockaddr_in *b) {
    return a->sin_addr.s_addr == b->sin_addr.s_addr &&
           a->sin_port == b->sin_port;
}

Client *find_client_by_addr(struct sockaddr_in *addr) {
    Client *cur = clients_head;
    while (cur) {
        if (same_addr(&cur->addr, addr))
            return cur;
        cur = cur->next;
    }
    return NULL;
}

Client *find_client_by_name(char *name) {
    Client *cur = clients_head;
    while (cur) {
        if (strcmp(cur->name, name) == 0)
            return cur;
        cur = cur->next;
    }
    return NULL;
}

void free_mute_list(MuteNode *m) {
    while (m) {
        MuteNode *tmp = m;
        m = m->next;
        free(tmp);
    }
}


// Mute basic functions
void mute_add(Client *c, char *name) {  // add into list
    MuteNode *cur = c->mute_head;
    while (cur) {
        if (strcmp(cur->name, name) == 0)
            return;
        cur = cur->next;
    }

    MuteNode *n = malloc(sizeof(MuteNode));
    strncpy(n->name, name, NAME_LEN - 1);
    n->name[NAME_LEN - 1] = '\0';

    n->next = c->mute_head;
    c->mute_head = n;
}

void mute_remove(Client *c, char *name) {  // remove from list
    MuteNode **ptr = &c->mute_head;
    while (*ptr) {
        if (strcmp((*ptr)->name, name) == 0) {
            MuteNode *tmp = *ptr;
            *ptr = tmp->next;
            free(tmp);
            return;
        }
        ptr = &((*ptr)->next);
    }
}

int has_muted(Client *receiver, char *sender_name) {
    MuteNode *cur = receiver->mute_head;
    while (cur) {
        if (strcmp(cur->name, sender_name) == 0)
            return 1;
        cur = cur->next;
    }
    return 0;
}


// Message sending functions
void send_to(struct sockaddr_in *addr, char *msg) {  //private send
    udp_socket_write(sd, addr, msg, strlen(msg) + 1);
}

void broadcast(char *msg, char *sender_name) {  //public send
    Client *cur = clients_head;
    while (cur) {
        if (!has_muted(cur, sender_name))
            send_to(&cur->addr, msg);
        cur = cur->next;
    }
}


// Request type handlers
void handle_conn(struct sockaddr_in *addr, char *content) {  //connect new client
    char name[NAME_LEN];
    if (!content || content[0] == '\0')
        strcpy(name, "Unknown");
    else
        strncpy(name, content, NAME_LEN - 1);

    name[NAME_LEN - 1] = '\0';

    pthread_rwlock_wrlock(&clients_lock);

    Client *existing = find_client_by_addr(addr);
    if (existing) {
        strncpy(existing->name, name, NAME_LEN - 1);
        existing->name[NAME_LEN - 1] = '\0';
    } else {
        Client *c = malloc(sizeof(Client));
        c->addr = *addr;
        strcpy(c->name, name);
        c->mute_head = NULL;
        c->next = clients_head;
        clients_head = c;
    }

    pthread_rwlock_unlock(&clients_lock);

    char resp[BUFFER_SIZE];
    snprintf(resp, sizeof(resp), "Hi %s, you have successfully connected to the chat", name);
    send_to(addr, resp);

    pthread_rwlock_rdlock(&clients_lock);
    char msg[BUFFER_SIZE];
    snprintf(msg, sizeof(msg), "%s has joined the chat", name);
    broadcast(msg, name);
    pthread_rwlock_unlock(&clients_lock);
}

void handle_disconn(struct sockaddr_in *addr) {  //disconnect client
    pthread_rwlock_wrlock(&clients_lock);

    Client *c = find_client_by_addr(addr);
    char name[NAME_LEN] = "";
    if (c)
        strcpy(name, c->name);

    Client **ptr = &clients_head;
    int removed = 0;

    while (*ptr) {
        if (same_addr(&(*ptr)->addr, addr)) {
            Client *tmp = *ptr;
            *ptr = tmp->next;
            free_mute_list(tmp->mute_head);
            free(tmp);
            removed = 1;
            break;
        }
        ptr = &((*ptr)->next);
    }

    pthread_rwlock_unlock(&clients_lock);

    if (!removed) {
        send_to(addr, "You were not registered. Goodbye!");
        return;
    }

    char resp[BUFFER_SIZE];
    sprintf(resp, "Disconnected. Bye %s!", name);
    send_to(addr, resp);

    pthread_rwlock_rdlock(&clients_lock);
    char msg[BUFFER_SIZE];
    sprintf(msg, "%s has left the chat", name);
    broadcast(msg, name);
    pthread_rwlock_unlock(&clients_lock);
}

void handle_say(struct sockaddr_in *addr, char *content) {  //send public message
    pthread_rwlock_rdlock(&clients_lock);
    Client *sender = find_client_by_addr(addr);

    char sender_name[NAME_LEN];
    strcpy(sender_name, sender ? sender->name : "Unknown");

    char msg[BUFFER_SIZE];
    snprintf(msg, sizeof(msg), "%s: %s", sender_name, content);
    broadcast(msg, sender_name);

    pthread_rwlock_unlock(&clients_lock);
}

void handle_sayto(struct sockaddr_in *addr, char *content) {  //send private message

    char recipient[NAME_LEN];
    int i = 0;

    while (*content == ' ')
        content++;

    while (*content && *content != ' ' && i < NAME_LEN - 1)
        recipient[i++] = *content++;

    recipient[i] = '\0';

    while (*content == ' ')
        content++;

    char *msg_body = content;

    pthread_rwlock_rdlock(&clients_lock);

    Client *sender = find_client_by_addr(addr);
    Client *destination = find_client_by_name(recipient);

    if (!destination) {
        char resp[BUFFER_SIZE];
        sprintf(resp, "User '%s' not found or not online.", recipient);
        send_to(addr, resp);
        pthread_rwlock_unlock(&clients_lock);
        return;
    }

    char sender_name[NAME_LEN];
    strcpy(sender_name, sender ? sender->name : "Unknown");

    if (!has_muted(destination, sender_name)) {
        char msg[BUFFER_SIZE];
        sprintf(msg, "%s: %s", sender_name, msg_body);
        send_to(&destination->addr, msg);
    }

    pthread_rwlock_unlock(&clients_lock);

    // confirmation for sender
    char conf[BUFFER_SIZE];
    snprintf(conf, sizeof(conf), "%s (To %s): %s", sender_name, recipient, msg_body);
    send_to(addr, conf);

}

void handle_rename(struct sockaddr_in *addr, char *new_name) {  //change name
    if (!new_name || new_name[0] == '\0') {
        send_to(addr, "rename$ requires a new name");
        return;
    }

    pthread_rwlock_wrlock(&clients_lock);

    Client *c = find_client_by_addr(addr);
    if (!c) {
        send_to(addr, "You are not registered. Use conn$ first.");
        pthread_rwlock_unlock(&clients_lock);
        return;
    }

    strncpy(c->name, new_name, NAME_LEN - 1);
    c->name[NAME_LEN - 1] = '\0';

    char resp[BUFFER_SIZE];
    sprintf(resp, "You are now known as %s", c->name);
    send_to(addr, resp);

    pthread_rwlock_unlock(&clients_lock);
}

void handle_kick(struct sockaddr_in *addr, char *target) {  //kick user by admin
    if (ntohs(addr->sin_port) != ADMIN_PORT) {
        send_to(addr, "Only admin can kick users.");
        return;
    }

    pthread_rwlock_wrlock(&clients_lock);

    Client **ptr = &clients_head;
    Client *victim = NULL;

    while (*ptr) {
        if (strcmp((*ptr)->name, target) == 0) {
            victim = *ptr;
            *ptr = victim->next;
            break;
        }
        ptr = &((*ptr)->next);
    }

    pthread_rwlock_unlock(&clients_lock);

    if (!victim) {
        char msg[BUFFER_SIZE];
        sprintf(msg, "User '%s' not found.", target);
        send_to(addr, msg);
        return;
    }

    send_to(&victim->addr, "You have been removed from the chat");

    pthread_rwlock_rdlock(&clients_lock);
    char msg[BUFFER_SIZE];
    sprintf(msg, "%s has been removed from the chat", target);
    broadcast(msg, target);
    pthread_rwlock_unlock(&clients_lock);

    free_mute_list(victim->mute_head);
    free(victim);
}

void handle_mute(struct sockaddr_in *addr, char *target) {  //mute user
    pthread_rwlock_wrlock(&clients_lock);
    Client *c = find_client_by_addr(addr);
    if (c)
        mute_add(c, target);
    pthread_rwlock_unlock(&clients_lock);
}

void handle_unmute(struct sockaddr_in *addr, char *target) {  //unmute muted user
    pthread_rwlock_wrlock(&clients_lock);
    Client *c = find_client_by_addr(addr);
    if (c)
        mute_remove(c, target);
    pthread_rwlock_unlock(&clients_lock);
}


// Worker threads
typedef struct {
    struct sockaddr_in client_addr;
    char buf[BUFFER_SIZE];
} Request;

void *worker(void *arg) {
    Request *r = arg;
    r->buf[BUFFER_SIZE - 1] = '\0';

    char *d = strchr(r->buf, '$');
    if (!d) {
        char tmp[64];
        int i = 0;
        while (r->buf[i] && i < 63) {
            char ch = r->buf[i];
            if (ch == ' ' || ch == '\t' || ch == '\n' || ch == '\r' || ch == '\v' || ch == '\f')
                break;

            tmp[i] = r->buf[i];
            i++;
        }
        tmp[i] = '\0';

        char msg[BUFFER_SIZE];
        sprintf(msg, "Unknown request: '%s'", tmp);
        send_to(&r->client_addr, msg);
        free(r);
        return NULL;
    }

    int len = d - r->buf;
    char type[64];
    if (len >= 63) len = 63;
    strncpy(type, r->buf, len);
    type[len] = '\0';

    char *content = d + 1;
    while (*content == ' ') content++;

    if (strcmp(type, "conn") == 0) handle_conn(&r->client_addr, content);
    else if (strcmp(type, "disconn") == 0) handle_disconn(&r->client_addr);
    else if (strcmp(type, "say") == 0) handle_say(&r->client_addr, content);
    else if (strcmp(type, "sayto") == 0) handle_sayto(&r->client_addr, content);
    else if (strcmp(type, "rename") == 0) handle_rename(&r->client_addr, content);
    else if (strcmp(type, "kick") == 0) handle_kick(&r->client_addr, content);
    else if (strcmp(type, "mute") == 0) handle_mute(&r->client_addr, content);
    else if (strcmp(type, "unmute") == 0) handle_unmute(&r->client_addr, content);
    else {
        char msg[BUFFER_SIZE];
        sprintf(msg, "Unknown request: '%s'", type);
        send_to(&r->client_addr, msg);
    }

    free(r);
    return NULL;
}



void *listener(void *arg) {
    printf("Server listening on port %d\n", SERVER_PORT);

    while (1) {
        Request *r = malloc(sizeof(Request));
        int rc = udp_socket_read(sd, &r->client_addr, r->buf, BUFFER_SIZE);
        if (rc <= 0) {
            free(r);
            continue;
        }

        if (rc < BUFFER_SIZE)
            r->buf[rc] = '\0';
        else
            r->buf[BUFFER_SIZE - 1] = '\0';

        pthread_t tid;
        if (pthread_create(&tid, NULL, worker, r) == 0)
            pthread_detach(tid);
        else
            free(r);
    }
    return NULL;
}



int main() {
    sd = udp_socket_open(SERVER_PORT);

    if (sd < 0) {
        printf("Error opening server socket\n");
        return 1;
    }

    pthread_t t;
    pthread_create(&t, NULL, listener, NULL);
    pthread_join(t, NULL);

    return 0;
}
