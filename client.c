#include <sys/socket.h>
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
#include <sys/stat.h>
#define BUFF_SIZE 2048
#define MAX_CLIENT 100
#define BUFFER_SIZE 2048
#define SENT "/send"
#define MSG_DUP_FILE "Error: File is existet on server."
#define MSG_RECV_FILE "Successful transfering."
#define MSG_CLOSE "Cancel file transfer"
#define MSG_RECV "Received."

volatile sig_atomic_t flag = 0;
int sockfd = 0;
char name[32];

char* extract_file_name(char* file_path) {
	int i;
	int n = strlen(file_path);
	char* file_name;
	for(i = n-1; i >= 0; --i) {
		if(file_path[i] == '/')
			break;
	}

	if(i == 0) //current directory so that no '/'
		return file_path;

	file_name = (char*)malloc((n-i)*sizeof(char));
	memcpy(file_name, &file_path[i+1], n-i);

	return file_name;
}

int send_file(int client_sock, char* file_path) {
	struct stat st;
	char recv_data[BUFF_SIZE];
	char sendbuff[BUFF_SIZE];
	int bytes_sent, bytes_received;
	FILE* fp;
	int nLeft, idx;
	char* file_name = NULL;
	off_t file_size = 0;
	char file_size_str[65];
	size_t result = 0;

	if(file_path[0] == '\0') { // enter an empty string
		printf("Sending file ended. Exiting.\n");
		bytes_sent = send(client_sock, file_path, 1, 0); 
		if(bytes_sent <= 0) 
			printf("Connection closed!\n");
		return 1;
	}

	// check if file exists
	if(stat(file_path, &st) == -1) { // Not exists
		fprintf(stderr, "Error: File not found.\n");
		bytes_sent = send(client_sock, MSG_CLOSE, strlen(MSG_CLOSE), 0); //echo error message
		if(bytes_sent <= 0) 
			printf("Connection closed!\n");
		return -1;
	}

	file_name = extract_file_name(file_path);
	printf("Uploading file to server: %s\n",file_name);	
	bytes_sent = send(client_sock, file_name, strlen(file_name), 0);
	if(bytes_sent <= 0) {
		printf("Connection closed!\n");
		return -1;
	}
	
	// confirm that server received file name and check file status on server side
	bytes_received = recv(client_sock, recv_data, BUFF_SIZE-1, 0); 
	if(bytes_received <= 0) {
		printf("Connection closed!\n");
		return -1;
	}
	else
		recv_data[bytes_received] = '\0'; 

	printf("%s\n", recv_data);
	if(strcmp(recv_data, MSG_DUP_FILE) == 0)		//file was found on server, duplicate file	
		return -1;
	bzero(recv_data, sizeof(recv_data));

	file_size = st.st_size;
	sprintf(file_size_str,"%lu",file_size);
	bytes_sent = send(client_sock, file_size_str, strlen(file_size_str), 0);
	if(bytes_sent <= 0) {
		printf("Connection closed!\n");
		return -1;
	}

	//open file and send data
	if((fp=fopen(file_path, "rb")) == NULL) {
		fprintf(stderr, "Open file error.\n");
		exit(1);
	}
	int loop_size = file_size;
	nLeft = file_size%BUFF_SIZE;	// cuz file size is not divisible by BUFF_SIZE

	while(loop_size > 0) {
		idx = 0;

		result += fread(sendbuff, 1, nLeft, fp); // use fread instead of fgets because fgets stop reading if newline is read
		while (nLeft > 0)
		{
			// Assume s is a valid, connected stream socket
			bytes_sent = send(client_sock, &sendbuff[idx], nLeft, 0);
			if (bytes_sent <= 0)
			{
				// Error handler
				printf("Connection closed.Trying again.\n");
			}
			nLeft -= bytes_sent;
			idx += bytes_sent;
		}
		
		bzero(sendbuff, sizeof(sendbuff)); 
		loop_size -= BUFF_SIZE; // decrease unfinished bytes 
		nLeft = BUFF_SIZE;		// reset nLeft
	}

	if(result != file_size) {
		printf("Error reading file.\n");
		return -1;
	}

	bytes_received = recv(client_sock, recv_data, BUFF_SIZE-1, 0);
	if(bytes_received <= 0) {
		printf("Connection closed!\n");
		return -1;
	}
	else
		recv_data[bytes_received] = '\0'; 

	printf("%s\n", recv_data);
	if(strcmp(recv_data, MSG_RECV_FILE) != 0)		//if cannot receive last message, file transfer is interrupted	
		return -1;

	// clean
	fclose(fp);
	free(file_name);
	return 0;
}

void str_overwrite_stdout()
{
    printf("\r%s", "> ");
    fflush(stdout);
}

void str_trim_lf(char *arr, int length)
{
    for (int i = 0; i < length; i++)
    {
        if (arr[i] == '\n')
        {
            arr[i] = '\0';
            break;
        }
    }
}

void catch_ctrl_c_and_exit()
{
    flag = 1;
}

void recv_msg_handler()
{
    char message[BUFFER_SIZE] = {};
    while (1)
    {
        int receive = recv(sockfd, message, BUFFER_SIZE, 0);
        if (receive > 0)
        {   printf("%s",message);
        }
        else if (receive == 0)
        {
            break;
        }
        bzero(message, BUFFER_SIZE);
    }
}

void send_msg_handler()
{
    char buffer[BUFFER_SIZE];
    char message[BUFFER_SIZE + 32] = {};
    
    while (1)
    {
        str_overwrite_stdout();
        fgets(buffer, BUFFER_SIZE, stdin);

        str_trim_lf(buffer, BUFFER_SIZE);

        if (strcmp(buffer, "exit") == 0)
        {
            break;
        }
        else
        {   char *comand;
            char *file_mess;
            comand=(char *)malloc(sizeof(char));
            file_mess=(char *)malloc(BUFFER_SIZE*sizeof(char));
            int m=0;
            int n=0;
            int temp=0;
            for (int i=0;i<strlen(buffer);i++){
                if(buffer[i]==' '){
                    temp=i;
                    break;
                }
                comand[m]=buffer[i];
                m++;
            }
            for (int i=temp+1;i<strlen(buffer);i++){
                file_mess[n]=buffer[i];
                n++;
            }
            printf("%s", file_mess);
            if (strcmp(comand,"/send")==0){
                sprintf(message, "/send");
                send(sockfd, message, strlen(message), 0);

                send_file(sockfd,file_mess);
            }
            else {
            sprintf(message, "%s:%s\n", name, buffer);
            send(sockfd, message, strlen(message), 0);
            }
        }

        bzero(buffer, BUFFER_SIZE);
        bzero(message, BUFFER_SIZE + 32);
    }

    catch_ctrl_c_and_exit(2);
}

int main(int argc, char **argv)
{
    if (argc != 2)
    {
        printf("Usage:  %s <port>\n", argv[0]);
        return EXIT_FAILURE;
    }

      char *ip = "127.0.0.1";
    int port = atoi(argv[1]);
    int listenfd;
     struct sockaddr_in server_addr;

    /* Socket settings */
    sockfd = socket(AF_INET, SOCK_STREAM, 0);
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(port);
    inet_pton(AF_INET, ip, &server_addr.sin_addr);
    // Connect to Server
    int err = connect(sockfd, (struct sockaddr *)&server_addr, sizeof(server_addr));
    if (err == -1)
    {
        printf("ERROR: connect\n");
        return EXIT_FAILURE;
    }
    
    signal(SIGINT, catch_ctrl_c_and_exit);
    if (port == 2000){
    
    printf("Enter your name and password\n");
    fgets(name, 32, stdin);
    str_trim_lf(name, strlen(name));

    if (strlen(name) > 32 - 1 || strlen(name) < 2)
    {
        printf("Enter your name correctly\n");
        return EXIT_FAILURE;
    }

    // Send name
    send(sockfd, name, 32, 0);

    printf("=== WELCOME TO THE CHATROOM ===\n");

    pthread_t send_msg_thread;
    if (pthread_create(&send_msg_thread, NULL, (void *)send_msg_handler, NULL) != 0)
    {
        printf("ERROR: pthread\n");
        return EXIT_FAILURE;
    }

    pthread_t recv_msg_thread;
    if (pthread_create(&recv_msg_thread, NULL, (void *)recv_msg_handler, NULL) != 0)
    {
        printf("ERROR: pthread\n");
        return EXIT_FAILURE;
    }

    while (1)
    {
        if (flag)
        {
            printf("Bye\n");
            break;
        }
    }

    } else if (port == 2001){
	char buff[BUFF_SIZE];
	int status;
	
	while(1) {
		//Step 4: Communicate with server
		printf("Insert string to send:");
		memset(buff,'\0',(strlen(buff)+1));
		fgets(buff, BUFF_SIZE, stdin);	
		buff[strlen(buff)-1] = '\0'; //remove trailing newline	
		status = send_file(sockfd, buff);
		
		if(status == 1)
			break;
	}
    }

    close(sockfd);
    return EXIT_SUCCESS;
}