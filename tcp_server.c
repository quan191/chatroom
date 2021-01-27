#include <stdio.h> /* These are the usual header files */
#include <stdlib.h>
#include <ctype.h>

#include <sys/types.h>
#include <sys/socket.h>
#include <sys/stat.h>

#include <netinet/in.h>
#include <arpa/inet.h>
#include <string.h>
#include <unistd.h>

#define BACKLOG 2 /* Number of allowed connections */
#define BUFF_SIZE 1024

#define MSG_DUP_FILE "Error: File is existet on server."
#define MSG_RECV_FILE "Successful transfering."
#define MSG_CLOSE "Cancel file transfer"
#define MSG_RECV "Received."

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
			chdir(tok);		  // chdir ~ cd
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
	printf("%s",recv_data);
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
		nLeft = BUFF_SIZE;		// reset nLeft
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

int main(int argc, char *argv[])
{
	int listen_sock, conn_sock; /* file descriptors */
	// char recv_data[BUFF_SIZE];
	// int bytes_sent, bytes_received;
	struct sockaddr_in server; /* server's address information */
	struct sockaddr_in client; /* client's address information */
	unsigned int sin_size;
	int sin_port;
	int status = 2;

	if (argc < 2)
	{
		printf("No specified port.\n");
		exit(0);
	}

	//Step 1: Construct a TCP socket to listen connection request
	if ((listen_sock = socket(AF_INET, SOCK_STREAM, 0)) == -1)
	{ /* calls socket() */
		perror("\nError: ");
		return 0;
	}

	sin_port = atoi(argv[1]);
	bzero(&server, sizeof(server));
	server.sin_family = AF_INET;
	server.sin_port = htons(sin_port);			/* Remember htons() from "Conversions" section? =) */
	server.sin_addr.s_addr = htonl(INADDR_ANY); /* INADDR_ANY puts your IP address automatically */
	if (bind(listen_sock, (struct sockaddr *)&server, sizeof(server)) == -1)
	{ /* calls bind() */
		perror("\nError: ");
		return 0;
	}
	if (listen(listen_sock, BACKLOG) == -1)
	{ /* calls listen() */
		perror("\nError: ");
		return EXIT_FAILURE;
	}
	while (1)
	{
		//accept request
		sin_size = sizeof(struct sockaddr_in);
		if ((conn_sock = accept(listen_sock, (struct sockaddr *)&client, &sin_size)) == -1)
			perror("\nError: ");

		printf("You got a connection from %s\n", inet_ntoa(client.sin_addr)); /* prints client's IP */
		while (1) {
			status = recv_file(conn_sock);
			if (status != 0){
				break;
			}
		}
		close(conn_sock);
	}

	close(listen_sock);
	return 0;
}
