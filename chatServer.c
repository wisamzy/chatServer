# include "chatServer.h"
# include <stdio.h>
# include <stdlib.h>
# include <netinet/in.h>
# include <sys/socket.h>
# include <unistd.h>
# include <string.h>
# include <signal.h>
# include <sys/ioctl.h>

#define msg_head currCon->write_msg_head
#define msg_tail currCon->write_msg_tail
static int turn_off = 0;

void intHandler(int SIG_INT) {
    turn_off = 1;
}

int main(int argc, char *argv[]) {
    if (argc != 3) {
        printf("Usage: ./chatServer <number of clients> <port>");
        exit(EXIT_FAILURE);
    }
    char buffer[BUFFER_SIZE];
    int max_connection = atoi(argv[1]);
    int port = atoi(argv[2]);

    signal(SIGINT, intHandler);

    conn_pool_t *pool = malloc(sizeof(conn_pool_t));
    init_pool(pool);

    struct sockaddr_in addr;
    int main_socket = socket(AF_INET, SOCK_STREAM, 0);

    if (main_socket < 0) {
        perror("erorr making main socket");
        return -1;
    }
    int on = 1;
    ioctl(main_socket, (int) FIONBIO, (char *) &on);

    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(port);

    if (bind(main_socket, (struct sockaddr *) &addr, sizeof(addr)) < 0) {
        perror("ERORR on binding");
        return -1;
    }
    listen(main_socket, 5);
    FD_ZERO(&(pool->read_set));
    FD_ZERO(&(pool->write_set));
    FD_ZERO(&(pool->ready_read_set));
    FD_ZERO(&(pool->ready_write_set));
    FD_SET(main_socket, &(pool->read_set));

    do {

        pool->ready_read_set = pool->read_set;
        pool->ready_write_set = pool->write_set;

        printf("Waiting on select()...\nMaxFd %d\n", pool->maxfd);

        pool->nready = select(getdtablesize(), &(pool->ready_read_set), &(pool->ready_write_set), NULL, NULL);

        int sd = main_socket;
        do {

            if (FD_ISSET(sd, &(pool->ready_read_set))) {

                if (sd == main_socket) {
                    if (pool->nr_conns == max_connection) {
                        sd++;
                        continue;
                    }
                    int new_sd = accept(main_socket, NULL, NULL);
                    if (new_sd == -1) {
                        turn_off = 1;
                        break;
                    }
                    printf("New incoming connection on sd %d\n", new_sd);
                    add_conn(new_sd, pool);
                    break;

                } else {
                    printf("Descriptor %d is readable\n", sd);

                    int len = read(sd, buffer, BUFFER_SIZE - 1);
                    printf("%d bytes received from sd %d\n", len, sd);
                    if (len == 0) {

                        printf("Connection closed for sd %d\n", sd);
                        printf("removing connection with sd %d \n", sd);

                        remove_conn(sd, pool);

                        break;

                    } else {
                        add_msg(sd, buffer, len, pool);
                    }
                }
            }
            if (FD_ISSET(sd, &(pool->ready_write_set))) {
                write_to_client(sd, pool);
            }
            sd++;
        } while (sd <= pool->maxfd);

    } while (turn_off == 0);

    while (pool->nr_conns > 0) {

        remove_conn(pool->conn_head->fd, pool);
    }
    free(pool);

    return 0;
}

int init_pool(conn_pool_t *pool) {
    pool->conn_head = NULL;
    pool->nr_conns = 0;
    pool->nready = 0;
    return 0;
}

int add_conn(int sd, conn_pool_t *pool) {

    if (pool->conn_head == NULL) {
        pool->conn_head = (conn_t *) malloc(sizeof(conn_t));
        if (pool->conn_head == NULL) {
            printf("erroe malloc");
            return -1;
        }
        pool->nr_conns++;
        pool->conn_head->fd = sd;
        pool->conn_head->prev = NULL;
        pool->conn_head->next = NULL;
        pool->conn_head->write_msg_head = NULL;
        pool->conn_head->write_msg_tail = NULL;
    } else {
        conn_t *con, *prevConn;

        for (con = pool->conn_head; con != NULL; con = con->next) {
            prevConn = con;
        }

        con = (conn_t *) malloc(sizeof(conn_t));
        if (con == NULL) {
            return -1;
        }

        pool->nr_conns++;
        con->prev = prevConn;
        con->next = NULL;

        prevConn->next = con;

        con->fd = sd;

        con->write_msg_head = NULL;
        con->write_msg_tail = NULL;
    }
    if (sd > pool->maxfd) {
        pool->maxfd = sd;
    }
    FD_SET(sd, &(pool->read_set));
    FD_SET(sd, &(pool->write_set));
    return 0;
}

int remove_conn(int sd, conn_pool_t *pool) {
    conn_t *con, *prevConn;
    for (con = pool->conn_head; con != NULL; con = con->next) {
        if (sd == con->fd) {
            prevConn = con->prev;
            if (con->next != NULL && con->prev != NULL) {

                con->next->prev = prevConn;
                prevConn->next = con->next;
            } else if (con->next == NULL && con->prev != NULL) {
                prevConn->next = NULL;
            } else if (con->prev == NULL && con->next != NULL) {

                con->next->prev = NULL;
                pool->conn_head = con->next;
            } else {

                pool->conn_head = NULL;
            }
            if (con->write_msg_head != NULL) {
                for (msg_t *currMsg = con->write_msg_head; currMsg != NULL; currMsg = currMsg->next) {
                    if (currMsg->prev != NULL) {
                        free(currMsg->prev);
                    }
                    if (currMsg->message != NULL) {
                        free(currMsg->message);
                    }
                    if (currMsg->next == NULL) {
                        free(currMsg);
                        break;
                    }
                }
            }
            FD_CLR(sd, &(pool->read_set));
            FD_CLR(sd, &(pool->write_set));
        }
    }
    pool->nr_conns--;
    if (sd == pool->maxfd) {
        pool->maxfd = -1;

        if (pool->nr_conns > 0) {
            for (con = pool->conn_head; con != NULL; con = con->next) {
                if (con->fd > pool->maxfd) {
                    pool->maxfd = con->fd;
                }
            }
        }
    }

    close(sd);
    free(con);
    return 0;
}

int add_msg(int sd, char *buffer, int len, conn_pool_t *pool) {

    conn_t *con;
    for (con = pool->conn_head; con != NULL; con = con->next) {

        if (con->fd != sd) {

            msg_t *currMsg = (msg_t *) malloc(sizeof(msg_t));
            currMsg->prev = con->write_msg_tail;
            currMsg->next = NULL;
            currMsg->message = (char *) malloc((len + 1));
            strcpy(currMsg->message, buffer);
            currMsg->message[len] = '\0';
            currMsg->size = len;
            con->write_msg_tail = currMsg;
            if (con->write_msg_head == NULL) {
                con->write_msg_head = currMsg;
            }

        }
    }
    return 0;
}

int write_to_client(int sd, conn_pool_t *pool) {
    conn_t *currCon;
    for (currCon = pool->conn_head; currCon != NULL; currCon = currCon->next) {
        if (currCon->fd == sd) {
            break;
        }
    }
    if (msg_head == NULL) {
        return 0;
    }
    msg_t *currMsg;
    for (currMsg = currCon->write_msg_head; currMsg != NULL; currMsg = currMsg->next) {

        if (currMsg->prev != NULL) {
            free(currMsg->prev);
        }
        if (write(currCon->fd, currMsg->message, currMsg->size) < 0) {
            return -1;
        }
        free(currMsg->message);

        if (currMsg->next == NULL) {
            free(currMsg);
            break;
        }
    }
    msg_head = NULL;
    msg_tail = NULL;
    return 0;
}