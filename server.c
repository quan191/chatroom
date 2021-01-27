#include <sys/types.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <pthread.h>
#include <sys/types.h>
#include <signal.h>
#include <ctype.h>

#define MAX_CLIENTS 100
#define BUFFER_SZ 2048
#define BACKLOG 2 /* Number of allowed connections */
#define BUFF_SIZE 1024

#define MSG_DUP_FILE "Error: File is existet on server."
#define MSG_RECV_FILE "Successful transfering."
#define MSG_CLOSE "Cancel file transfer"
#define MSG_RECV "Received."
static _Atomic unsigned int cli_count = 0;
static int uid = 10;



void clean_and_restore(FILE **fp)
{
    if (*fp != NULL)
        fclose(*fp);
    chdir("../..");
}

int recv_file(int conn_sock)
{
    struct stat st;
    int size_file;
    char recv_data[BUFF_SIZE];
    int bytes_sent, bytes_received;
    char dir_name[] = "destination";
    char *tok;
    FILE *fp = NULL;
    int nLeft, idx;

    // check if directory exists, if not mkdir
    if (stat(dir_name, &st) == -1)
    {
        tok = strtok(dir_name, "/");
        while (tok != NULL)
        {
            mkdir(tok, 0700); //config permissions by changing 2nd argument
            chdir(tok);       // chdir ~ cd
            tok = strtok(NULL, "/");
        }
    }
    else
        chdir(dir_name);

    //receives file name
    bytes_received = recv(conn_sock, recv_data, BUFF_SIZE - 1, 0);
    if (bytes_received <= 0)
    {
        printf("Connection closed\n");
        clean_and_restore(&fp);
        return -1; //meet error, aborted
    }
    else
        recv_data[bytes_received] = '\0'; // check with client send format

    if (recv_data[0] == '\0')
    {
        printf("Receiving data from client end. Exiting.\n");
        clean_and_restore(&fp);
        return 1;
    }

    if (strcmp(recv_data, MSG_CLOSE) == 0)
    { //file not found on client
        printf("No file found on client. Transmission aborted.\n");
        clean_and_restore(&fp);
        return -1;
    }

    // check if file exists, if not create new one, else return error
    // already at destination folder
    // echo to client
    if (stat(recv_data, &st) == -1)
    { // file does not exist
        fp = fopen(recv_data, "wb");
        if (fp == NULL)
        {
            printf("File path error\n");
            clean_and_restore(&fp);
            return -1;
        }
        bytes_sent = send(conn_sock, MSG_RECV, strlen(MSG_RECV), 0); //echo that received file name and no duplicate file on server
        if (bytes_sent <= 0)
        {
            printf("Connection closed\n");
            clean_and_restore(&fp);
            return 1; //meet error, aborted
        }
    }
    else
    {
        printf("Duplicate file.\n");
        bytes_sent = send(conn_sock, MSG_DUP_FILE, strlen(MSG_DUP_FILE), 0);
        if (bytes_sent <= 0)
        {
            printf("Connection closed\n");
        }
        clean_and_restore(&fp);
        return 1;
    }

    printf("File name: %s\n", recv_data);
    bzero(recv_data, bytes_received); //empty buffer

    //receives file size
    bytes_received = recv(conn_sock, recv_data, BUFF_SIZE - 1, 0);
    if (bytes_received <= 0)
    {
        printf("Connection closed\n");
        clean_and_restore(&fp);
        return 1; //meet error, aborted
    }
    else
        recv_data[bytes_received] = '\0'; // check with client send format

    size_file = atoi(recv_data);

    printf("File size: %s\n", recv_data);
    bzero(recv_data, bytes_received); //empty buffer

    nLeft = size_file % BUFF_SIZE; // cuz file size is not divisible by BUFF_SIZE
    int loop_size = size_file;

    while (loop_size > 0)
    {
        idx = 0; // reset idx

        while (nLeft > 0)
        {
            bytes_received = recv(conn_sock, &recv_data[idx], nLeft, 0); // read at missing data index
            if (bytes_received <= 0)
            {
                // Error handler
                printf("Connection closed. Trying again.\n");
            }
            idx += bytes_received; // if larger then socket size
            nLeft -= bytes_received;
        }

        fwrite(recv_data, 1, idx, fp); //idx is the real length of recv_data
        bzero(recv_data, sizeof(recv_data));
        loop_size -= BUFF_SIZE; // decrease unfinished bytes   		-
        nLeft = BUFF_SIZE;      // reset nLeft
    }

    // echo to client that transmission sucessfully completed
    bytes_sent = send(conn_sock, MSG_RECV_FILE, strlen(MSG_RECV_FILE), 0);
    if (bytes_sent <= 0)
    {
        printf("Connection closed\n");
        clean_and_restore(&fp);
        return 1; //meet error, aborted
    }

    // sucessful block
    fclose(fp);
    chdir("../.."); //return original folder

    return 0;
}

/* Client structure */
typedef struct
{
    struct sockaddr_in address;
    int sockfd;
    int uid;
    char name[32];
} client_t;

client_t *clients[MAX_CLIENTS];

pthread_mutex_t clients_mutex = PTHREAD_MUTEX_INITIALIZER;

void str_overwrite_stdout()
{
    printf("\r%s", "> ");
    fflush(stdout);
}

void str_trim_lf(char *arr, int length)
{
    int i;
    for (i = 0; i < length; i++)
    { // trim \n
        if (arr[i] == '\n')
        {
            arr[i] = '\0';
            break;
        }
    }
}

void print_client_addr(struct sockaddr_in addr)
{
    printf("%d.%d.%d.%d",
           addr.sin_addr.s_addr & 0xff,
           (addr.sin_addr.s_addr & 0xff00) >> 8,
           (addr.sin_addr.s_addr & 0xff0000) >> 16,
           (addr.sin_addr.s_addr & 0xff000000) >> 24);
}

/* Add clients to queue */
void queue_add(client_t *cl)
{
    pthread_mutex_lock(&clients_mutex);

    for (int i = 0; i < MAX_CLIENTS; ++i)
    {
        if (!clients[i])
        {
            clients[i] = cl;
            break;
        }
    }

    pthread_mutex_unlock(&clients_mutex);
}

/* Remove clients to queue */
void queue_remove(int uid)
{
    pthread_mutex_lock(&clients_mutex);

    for (int i = 0; i < MAX_CLIENTS; ++i)
    {
        if (clients[i])
        {
            if (clients[i]->uid == uid)
            {
                clients[i] = NULL;
                break;
            }
        }
    }

    pthread_mutex_unlock(&clients_mutex);
}

/* Send message to all clients except sender */
void send_message(char *s, int uid)
{
    pthread_mutex_lock(&clients_mutex);

    for (int i = 0; i < MAX_CLIENTS; ++i)
    {
        if (clients[i])
        {
            if (clients[i]->uid != uid)
            {
                if (write(clients[i]->sockfd, s, strlen(s)) < 0)
                {
                    perror("ERROR: write to descriptor failed");
                    break;
                }
            }
        }
    }

    pthread_mutex_unlock(&clients_mutex);
}

void send_err_mess(char *s, int uid)
{
    pthread_mutex_lock(&clients_mutex);

    for (int i = 0; i < MAX_CLIENTS; ++i)
    {
        if (clients[i])
        {
            if (clients[i]->uid == uid)
            {
                if (write(clients[i]->sockfd, s, strlen(s)) < 0)
                {
                    perror("ERROR: write to descriptor failed");
                    break;
                }
                break;
            }
        }
    }

    pthread_mutex_unlock(&clients_mutex);
}
void send_message2(char *s, client_t* cli)
{   char *send_user;
    char *comand;
    char *message;
    int m=0;
    int n=0;
    int k=0;
    int temp;
    int check=0;
    send_user=(char *)malloc(sizeof(char));
    comand=(char *)malloc(sizeof(char));
    message=(char *)malloc(sizeof(char));

    pthread_mutex_lock(&clients_mutex);
    for (int i =0; i < strlen(s) ; i++){
        if(s[i]=='/'){
            temp=i;
            break;
        }
        send_user[m]=s[i];
        m++;
    }
    printf("%s\n",send_user);
    for (int i =temp+1; i < strlen(s) ; i++){
        if(s[i]==' '){
            temp=i;
            break;
        }
        comand[n]=s[i];
        n++;
    }
    for (int i =temp+1; i < strlen(s) ; i++){
        message[k]=s[i];
        k++;
    }
    strcat(send_user,message);
    if (strcmp(comand,"all")==0){
    for (int i = 0; i < MAX_CLIENTS; ++i)
    {
        if (clients[i])
        {
            if (clients[i]->uid != cli->uid)
            {
                if (write(clients[i]->sockfd, send_user, strlen(send_user)) < 0)
                {
                    perror("ERROR: write to descriptor failed");
                    break;
                }
            }
        }
    }
    }
    else {
        for (int i = 0; i < MAX_CLIENTS; ++i)
    {
        if (clients[i])
        {
            if (clients[i]->uid != cli->uid && strcmp(clients[i]->name,comand)==0)
            {   check=10;
                if (write(clients[i]->sockfd, send_user, strlen(send_user)) < 0)
                {
                    perror("ERROR: write to descriptor failed");
                    break;
                }
            }
        }
    }
    // if (check==0) {
    //     send_err_mess("No user found\n",uid);
    // }
    }
    pthread_mutex_unlock(&clients_mutex);
}

/* Handle all communication with the client */
void *handle_client(void *arg)
{
    char buff_out[BUFFER_SZ];
    char name[32];
    int leave_flag = 0;

    cli_count++;
    client_t *cli = (client_t *)arg;

    // Name
    if (recv(cli->sockfd, name, 32, 0) <= 0 || strlen(name) < 2 || strlen(name) >= 32 - 1)
    {
        printf("Didn't enter the name.\n");
        leave_flag = 1;
    }
    else
    {
        strcpy(cli->name, name);
        sprintf(buff_out,"%s has joined\n", cli->name);
        printf("%s", buff_out);
        send_message(buff_out, cli->uid);
        
    }

    bzero(buff_out, BUFFER_SZ);

    while (1)
    {
        if (leave_flag)
        {
            break;
        }
        
        int receive = recv(cli->sockfd, buff_out, BUFFER_SZ, 0);
        if (receive > 0)
        {
            
            if (strlen(buff_out) > 0)
            {
                send_message2(buff_out, cli);

                str_trim_lf(buff_out, strlen(buff_out));
                printf("%s -> %s\n", buff_out, cli->name);
                
            }
        }
        else if (receive == 0 || strcmp(buff_out, "exit") == 0)
        {
            sprintf(buff_out, "%s has left\n", cli->name);
            printf("%s", buff_out);
            send_message(buff_out, cli->uid);
            leave_flag = 1;
        }
        else
        {
            printf("ERROR: -1\n");
            leave_flag = 1;
        }

        bzero(buff_out, BUFFER_SZ);
    }

    /* Delete client from queue and yield thread */
    close(cli->sockfd);
    queue_remove(cli->uid);
    free(cli);
    cli_count--;
    pthread_detach(pthread_self());

    return NULL;
}

int main(int argc, char **argv)
{
    if (argc != 2)
    {
        printf("Usage: %s <port>\n", argv[0]);
        return EXIT_FAILURE;
    }
    int port = atoi(argv[1]);
    int option = 1;
    int listenfd = 0, connfd = 0;
    struct sockaddr_in serv_addr;
    struct sockaddr_in cli_addr;
    pthread_t tid;

    /* Socket settings */
    listenfd = socket(AF_INET, SOCK_STREAM, 0);
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_addr.s_addr = INADDR_ANY;
    serv_addr.sin_port = htons(port);

    /* Ignore pipe signals */
    signal(SIGPIPE, SIG_IGN);

    if (setsockopt(listenfd, SOL_SOCKET, SO_REUSEADDR, (char *)&option, sizeof(option)) < 0)
    {
        perror("ERROR: setsockopt failed");
        return EXIT_FAILURE;
    }

    /* Bind */
    if (bind(listenfd, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0)
    {
        perror("ERROR: Socket binding failed");
        return EXIT_FAILURE;
    }

    /* Listen */
    if (listen(listenfd, 10) < 0)
    {
        perror("ERROR: Socket listening failed");
        return EXIT_FAILURE;
    }

        printf("=== WELCOME TO THE CHATROOM ===\n");

        while (1)
        {
               socklen_t clilen = sizeof(cli_addr);
            connfd = accept(listenfd, (struct sockaddr *)&cli_addr, &clilen);
            /* Check if max clients is reached */
            if ((cli_count + 1) == MAX_CLIENTS)
            {
                printf("Max clients reached. Rejected: ");
                print_client_addr(cli_addr);
                printf(":%d\n", cli_addr.sin_port);
                close(connfd);
                continue;
            }

            /* Client settings */
            client_t *cli = (client_t *)malloc(sizeof(client_t));
            cli->address = cli_addr;
            cli->sockfd = connfd;
            cli->uid = uid++;

            /* Add client to the queue and fork thread */
            queue_add(cli);
            pthread_create(&tid, NULL, &handle_client, (void *)cli);

            /* Reduce CPU usage */
            sleep(1);
        }
    close(listenfd);

    return EXIT_SUCCESS;
}