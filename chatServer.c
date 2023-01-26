# include "chatServer.h"
# include <stdio.h>
# include <stdlib.h>
# include <netinet/in.h>
# include <sys/socket.h>
# include <unistd.h>
# include <string.h>
# include <signal.h>
# include <sys/ioctl.h>

static int end_server = 0;

void intHandler(int SIG_INT) {
    end_server = 1;
}

int requests(char **argv) {
    char buffer[BUFFER_SIZE];
    int port = atoi(argv[1]);
    // Registering the handler function for the interrupt signal (CTRL-C)
    signal(SIGINT, intHandler);

    conn_pool_t* pool = malloc(sizeof(conn_pool_t));
    init_pool(pool);

    struct sockaddr_in addr;
    // Creating the welcome socket using TCP protocol
    int welcome_socket = socket(AF_INET, SOCK_STREAM, 0);

    if(welcome_socket < 0){
        perror("ERROR creating welcome socket");
        return -1;
    }
    int on = 1;
    // Setting the welcome socket to be non-blocking
    ioctl(welcome_socket, (int)FIONBIO, (char*)&on);

    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr=INADDR_ANY;
    addr.sin_port = htons(port);

    if(bind(welcome_socket, (struct sockaddr*)&addr, sizeof(addr)) < 0){
        perror("ERROR on binding");
        return -1;
    }
    listen(welcome_socket, 5);
    FD_ZERO(&(pool->read_set));
    FD_ZERO(&(pool->write_set));
    FD_ZERO(&(pool->ready_read_set));
    FD_ZERO(&(pool->ready_write_set));
    FD_SET(welcome_socket, &(pool->read_set));

    do{
        pool->ready_read_set = pool->read_set;
        pool->ready_write_set = pool->write_set;
        printf("Waiting on select()...\nMaxFd %d\n", pool->maxfd);
        /* Wait for a connection to be ready to be read or written to */
        pool->nready = select(getdtablesize(),&(pool->ready_read_set),&(pool->ready_write_set),NULL,NULL);

        int sd = welcome_socket;
        do{
            /* Check if the current descriptor is in the read set */
            if (FD_ISSET(sd,&(pool->ready_read_set))){

                /* Check if the current descriptor is the welcome socket */
                if(sd == welcome_socket ){
                    /* Accept a new connection */
                    int new_sd = accept(welcome_socket, NULL, NULL);
                    /* Check if the accept call was successful */
                    if(new_sd == -1){
                        end_server = 1;
                        break;
                    }
                    printf("New incoming connection on sd %d\n",new_sd);
                    /* Add the new connection to the pool */
                    add_conn(new_sd,pool);
                    break;

                }else{
                    printf("Descriptor %d is readable\n",sd);

                    /* Read data from the descriptor */
                    int len = read(sd, buffer, BUFFER_SIZE - 1);
                    /* Print a message indicating how many bytes were received */
                    printf("%d bytes received from sd %d\n",len,sd);
                    /* Check if the read call returned 0 bytes */
                    if(len == 0){

                        printf("Connection closed for sd %d\nremoving connection with sd %d\n",sd,sd);

                        /* Remove the connection from the pool */
                        remove_conn(sd,pool);
                        break;

                    }else{
                        /* Add the received message to the pool */
                        add_msg(sd, buffer, len, pool);
                    }
                }
            }
            if (FD_ISSET(sd,&(pool->ready_write_set))) {
                write_to_client(sd,pool);
            }
            sd++;
        } while(sd <= pool->maxfd);

    } while (end_server == 0);

    while(pool->nr_conns>0){
        remove_conn(pool->conn_head->fd,pool);
    }
    free(pool);
    return 0;
}
int main (int argc, char *argv[])
{
    if(argc != 2){
        printf("Usage: ./chatServer <port>");
        exit(EXIT_FAILURE);
    }
    int req = requests(argv);
    if(req != 0){
        return -1;
    }
    return 0;
}

int init_pool(conn_pool_t* pool) {
    pool->conn_head = NULL;
    pool->nr_conns =0;
    pool->nready =0;
    return 0;
}

int add_conn(int sd, conn_pool_t* pool) {
    // Check if the connection pool is empty
    if(pool->conn_head==NULL){
        // Allocate memory for new connection node
        pool->conn_head = (conn_t*) malloc(sizeof(conn_t));
        if(pool->conn_head == NULL){
            printf("ERROR malloc");
            return -1;
        }
        // Increment number of connections in the pool
        pool->nr_conns++;
        // Set the file descriptor for the new connection
        pool->conn_head->fd = sd;
        // Set the "prev" and "next" pointers to NULL
        pool->conn_head->prev = NULL;
        pool->conn_head->next = NULL;
        // Set the head and tail pointers for the write message queue to NULL
        pool->conn_head->write_msg_head = NULL;
        pool->conn_head->write_msg_tail = NULL;
    }else{
        // Create pointers to traverse the connection list
        conn_t *currConn ,*prevConn;
        // Loop through existing connections to find the last one
        for(currConn = pool->conn_head ; currConn != NULL ; currConn = currConn->next){
            prevConn = currConn;
        }
        // Allocate memory for new connection node
        currConn = (conn_t*)malloc(sizeof(conn_t));
        if(currConn == NULL){
            return -1;
        }
        // Increment number of connections in the pool
        pool->nr_conns++;
        // Set the "prev" pointer to the last connection in the list
        currConn->prev = prevConn;
        // Set the "next" pointer to NULL
        currConn->next = NULL;
        // Update the "next" pointer of the last connection
        prevConn->next = currConn;
        // Set the file descriptor for the new connection
        currConn->fd = sd;
        // Set the head and tail pointers for the write message queue to NULL
        currConn->write_msg_head = NULL;
        currConn->write_msg_tail = NULL;
    }
// Update the max file descriptor value if necessary
    if(sd > pool->maxfd){
        pool->maxfd = sd;
    }
// Add the new connection's socket descriptor to the read and write sets of the connection pool
    FD_SET(sd,&(pool->read_set));
    return 0;
}

int remove_conn(int sd, conn_pool_t* pool) {
    conn_t *currConn, *prevConn;
    // Iterate through the connections in the pool
    currConn = pool->conn_head;
    do{
        // Check if the current connection's file descriptor matches the given socket descriptor
        if(sd == currConn->fd){

            // Remove the connection from the linked list
            prevConn = currConn->prev;
            if(currConn->next != NULL && currConn->prev != NULL){
                currConn->next->prev = prevConn;
                prevConn->next = currConn->next;
            }else if(currConn->next == NULL && currConn->prev != NULL){
                prevConn->next = NULL;
            }else if(currConn->prev == NULL && currConn->next != NULL){
                currConn->next->prev = NULL;
                pool->conn_head = currConn->next;
            }else{
                pool->conn_head = NULL;
            }
            // Free any associated write messages
            if(currConn->write_msg_head != NULL){
                for(msg_t *currMsg = currConn->write_msg_head ; currMsg != NULL ; currMsg = currMsg->next){
                    if(currMsg->prev != NULL){
                        free(currMsg->prev);
                    }
                    if(currMsg->message != NULL){
                        free(currMsg->message);
                    }
                    if(currMsg ->next == NULL){
                        free(currMsg);
                        break;
                    }
                }
            }
            FD_CLR(sd,&(pool->read_set));
            FD_CLR(sd,&(pool->write_set));
        }
            currConn = currConn->next;
    }while(currConn != NULL);
    pool->nr_conns--;
    if(sd == pool->maxfd){
        pool->maxfd = -1;
        if(pool->nr_conns>0){
            for(currConn = pool->conn_head ; currConn != NULL ; currConn = currConn->next){
                if(currConn->fd > pool->maxfd){
                    pool->maxfd = currConn->fd;
                }
            }
        }
    }
    close(sd);
    free(currConn);
    return 0;
}

int add_msg(int sd, char* buffer, int len, conn_pool_t* pool) {
    conn_t *client;
    client = pool->conn_head;
    do {
        if (client->fd != sd) {
            // Add the client's fd to the write set
            FD_SET(client->fd, &(pool->write_set));
            // Allocate memory for the message
            msg_t *pMsg = (msg_t*)malloc(sizeof(msg_t));
            pMsg->prev = client->write_msg_tail;
            pMsg->next = NULL;
            // Allocate memory for the message content
            pMsg->message = (char*)malloc((len + 1));
            // Copy the message content to the new memory
            strcpy(pMsg->message, buffer);
            pMsg->message[len] = '\0';
            pMsg->size = len;
            client->write_msg_tail = pMsg;
            if (client->write_msg_head == NULL) {
                client->write_msg_head = pMsg;
            }
        }
        client = client->next;
    } while (client != NULL);
    return 0;
}


int write_to_client(int sd,conn_pool_t* pool) {
    conn_t *client;
// Find the client connection associated with the given socket descriptor
    client = pool->conn_head;
    do {
        if (client->fd == sd) {
            break;
        }
        client = client->next;
    } while (client != NULL);
    // If there are no messages to write, return 0
    if (client->write_msg_head == NULL) {
        return 0;
    }
// Iterate through the message list and write each message to the client
    msg_t *currMsg;
    currMsg = client->write_msg_head;
    do {
        // free the prev message
        if (currMsg->prev != NULL) {
            free(currMsg->prev);
        }
        // write the current message to the client
        if (write(client->fd, currMsg->message, currMsg->size) < 0) {
            return -1;
        }
        // free the current message
        free(currMsg->message);
        currMsg = currMsg->next;
    } while (currMsg != NULL);

    FD_CLR(sd, &pool->write_set);
    FD_CLR(sd, &pool->ready_write_set);
    // Reset write message head and tail for this client
    client->write_msg_head = NULL;
    client->write_msg_tail = NULL;
    return 0;
}