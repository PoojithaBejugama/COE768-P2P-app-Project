/* time_client.c - main */

#include <sys/types.h>

#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <sys/socket.h>                                                                            
#include <netinet/in.h>
#include <arpa/inet.h>
                                                                                
#include <netdb.h>

#define BUFLEN		100	/* buffer length */
#define FILEDATABUFFLEN		1024	/* file data length */
#define	MSG		"Any Message \n"
#define MAXFILES 5

struct pdu {
    char type;
    char data[BUFLEN-1];
};

struct filePdu {
	char type;
	char data[FILEDATABUFFLEN-1];
};

void handle_register_content();
void handle_deregister_content(void);
void handle_list_and_download(void);
void clode_application(void);
void handle_std_input(void);
void handle_list_content(int udp_socket, struct sockaddr_in index_server);
void handle_search_content(int file_indx);
void handle_download_content(struct sockaddr_in sockarr, char filename[11]);
int handle_upload_content(int tcp_socket, struct sockaddr_in client, char filename[11]);
int listen_for_incomming_download_req(int tcp_socket, struct sockaddr_in sock_descriptor, char filename[11]);

void send_udp_request();
void send_tcp_request(int socket);

void receive_udp_response(int socket, char* response, size_t response_size);
void receive_tcp_response(int socket, char* response, size_t response_size);

void handle_error_response(char response_type);

char peer_name[11], std_buf[100], req_buffer[100], file_req_buffer[1640], file_res_buffer[1640], res_buffer[100], std_input[100], ip_add[10], filenames[MAXFILES][11];
int mode=0, indx_sock, did_list=0, file_indx;
fd_set afds, rfds;
struct pdu req_pdu, res_pdu;

void serialize() {
	req_buffer[0] = req_pdu.type;
	int i;
	for(i = 0 ; i < BUFLEN-1; i++) req_buffer[i+1] = req_pdu.data[i];
}

void deserialize() {
	res_pdu.type = res_buffer[0];
	int i;
	memcpy(res_pdu.data, res_buffer+1, sizeof(res_pdu.data));
	//for(i = 0; i < BUFLEN-1; i++) res_pdu.data[i] = res_buffer[i+1];
	memset(res_buffer, 0, sizeof(res_buffer));
}

void display_menu() {

	switch(mode) {
	case 0:
		printf("===== Menu =====\n");
		printf("1. Register Content\n");
		printf("2. Deregister Content\n");
		printf("3. List and Download Available Content\n");
		printf("0. Quit\n");
		printf("================\n");
		printf("Enter your choice: \n");
	break;
	case 1:
		printf("===== Register Content =====\n");
		printf("Please enter a file name: \n");
	break;
	case 2:
		printf("===== De-Register Content =====\n");
		printf("Please enter the filename: \n");
	break;
	case 3:
		if(did_list == 0) {
		printf("====Listing available content====\n");
		handle_list_and_download();
		did_list = 0;
		} else {
			printf("Select the corresponding file number to download or 0 to exit: \n");
		}
	break;
	}
    
}

void handle_std_input() {
	int new_mode;
	switch(mode) {
		case 0: 
			scanf("%d", &new_mode);
			if(new_mode >=0 && new_mode <= 3) {
				mode = new_mode;
			} else {
				printf("Invalid input. Please select an appropriate option\n");
			}
		break;
		case 1:
			scanf("%s", std_input);
			printf("registering file: %s\n", std_input);
			handle_register_content();
			printf("file registered\n");
		break;
		case 2:
			scanf("%s", std_input);
			handle_deregister_content();
		break;
		case 3:
			scanf("%d", &file_indx);
			if(file_indx == 0) {
				mode=0;
			} else {
				handle_search_content(file_indx-1);
			}
	}
}

void handle_socket_input(int socket) {
	int j;
	int i;
	while (j != 0) { /*repeatedly reading until termination*/
	j = read(socket, res_buffer, BUFLEN);
	if(j <0 ){
		printf("Error\n");
		close(socket);
	}
	}
	deserialize();
	printf("received request of type: %c\n", res_pdu.type);
	switch(mode) {
		case 0: {
			if(res_pdu.type == 'D') {
				printf("responding to download request...\n");
			} else {
				printf("Unsupported request\n");
			}
		}
		break;
		case 1: {
			if(res_pdu.type == 'A') {
				printf("Acknowledgement received\n");
				mode=0;
			} else if(res_pdu.type == 'E') {
				printf("Error registering content: %s\n", res_pdu.data);
				mode = 0;
			} else {
				printf("Unsupported request\n");
			}
		}
		break;
		case 2:
			printf("deregistering content\n");
		break;
	}
}

void handle_list_and_download() {
	int i=0, h=0, loopactive=1, j=0, files_processed=0, str_size;
	int offset = 0;
	req_pdu.type = 'O';
	send_udp_request();
	if(read(indx_sock, res_buffer, BUFLEN) < 0){
		printf("error\n");
		mode=0;
		return;
	}
	deserialize();
	printf("response recived of type: %c\n", res_pdu.type);
	if(res_pdu.type == 'O') {

		while(loopactive) {
			if(res_pdu.data[h] == '\0') {
				printf("%d: %s\n",i+1, filenames[i]);
				loopactive = 0;
				continue;
			} else if(res_pdu.data[h] == ':') {
				printf("%d: %s\n",i+1, filenames[i]);
				i++;
				j=0;
			} else {
				if(j < 10){
				filenames[i][j] = res_pdu.data[h];
				j++;
				}
			} 
			h++;
		}
		//set this to 1 to disable printing the mode header again and calling this function again.
		did_list = 1;
	} else if(res_pdu.type == 'E') {
		printf("Received error\n");
		printf("%s", res_pdu.data);
		mode=0;
	}

}

void handle_register_content() {
	//socket init stuff
	struct sockaddr_in reg_addr, client;
	int sock_id, alen, loopactive=1, j=0, client_len, clien, new_sd;
	sock_id = socket(AF_INET, SOCK_STREAM, 0);
	reg_addr.sin_family = AF_INET;
	reg_addr.sin_port = htons(0);
	reg_addr.sin_addr.s_addr = htonl(INADDR_ANY);
	bind(sock_id, (struct sockaddr *)&reg_addr, sizeof(reg_addr));
	alen = sizeof (struct sockaddr_in);
	getsockname(sock_id, (struct sockaddr *) &reg_addr, (socklen_t *) &alen);

	//setting pdu type
	req_pdu.type = 'R';
	//copying informaiton
	memcpy(req_pdu.data, peer_name , strlen(peer_name));
	memcpy(req_pdu.data+11, std_input , strlen(std_input));
	memcpy(req_pdu.data+22, ip_add , sizeof(ip_add));
	memcpy(req_pdu.data+32, &reg_addr.sin_port , sizeof(reg_addr.sin_port));
	printf("Port is %d\n", ntohs(reg_addr.sin_port));

	send_udp_request();
	printf("Data send to server. Awaiting acknowledgement....\n");

	//waiting for response from udp
	if(read(indx_sock, res_buffer, BUFLEN) < 0) {
		printf("Error\n");
		mode=0;
	}
	if(res_buffer[0] == 'A'){
		printf("Acknowledgment received\n");
		mode=0;
		switch(fork()) {
			case 0:
				printf("child process listening for incomming requests to socket\n");
				exit(listen_for_incomming_download_req(sock_id, reg_addr, std_input));
			default:
				printf("Parent process\n");
				break;
		}
	} else if(res_buffer[0] == 'E') {
		mode=0;
		printf("File already registered\n");
	} 
}

int listen_for_incomming_download_req(int sock_id, struct sockaddr_in sock_descriptor, char filename[11]) {
	struct sockaddr_in reg_addr, client;
	int client_len, new_sd, n;
	char req_buf[BUFLEN], tmpfilename[11];
	//filename buffer is subject to change so copy contents to temporary memory address
	strncpy(tmpfilename, filename, sizeof(tmpfilename));
	listen(sock_id, 5);
	while(1) {
		client_len = sizeof(client);
		new_sd = accept(sock_id, (struct sockaddr *)&client, (unsigned int *) &client_len);
		if(new_sd < 0){
			printf("Can't accept client \n");
			exit(1);
	  } else{
			printf("New client accepted\n");
			switch(fork()){
			case 0: {
				printf("child process handling upload content\n");
				if( (n=read(new_sd, req_buf, BUFLEN) ) > 0 ) {
					if(req_buf[0]=='D')
						exit(handle_upload_content(new_sd, client, tmpfilename));
				} else {
					printf("unsupported request\n");
					exit(1);
				}
			}
			default:
				break;
			}
	  }
	}	
	return 0;
}

int handle_upload_content(int tcp_socket, struct sockaddr_in client, char filename[11]) {
	FILE 	*file_ptr;
	file_ptr = fopen(filename, "rb");
	int numBytes=0, totalBytes=0, file_size;
	int32_t tmp_file_size;
	char tmpFileBuffer[FILEDATABUFFLEN-1];
	printf("\n===== Handling file download request =====\n");
	if(file_ptr == NULL){
		write(tcp_socket, "E", 1); 
		close(tcp_socket);
	} else {
			//retreive file size
			fseek(file_ptr, 0L, SEEK_END);
			file_size = ftell(file_ptr);
			tmp_file_size = htonl(file_size);
			fseek(file_ptr, 0L, SEEK_SET);
			write(tcp_socket, &file_size, sizeof(int));

			while((numBytes = fread(tmpFileBuffer, 1, sizeof(tmpFileBuffer), file_ptr)) > 0){
				file_req_buffer[0] = 'C';
				memcpy(file_req_buffer+1, tmpFileBuffer, sizeof(tmpFileBuffer));
				write(tcp_socket, file_req_buffer, numBytes+sizeof(file_req_buffer[0]));
				printf("%d bytes uploaded...\n", numBytes);
				totalBytes += numBytes;
			}
		fclose(file_ptr);	
	}
	printf("Total bytes of %d bytes sent.\n", totalBytes);
	close(tcp_socket);
	return 0;
}

void handle_deregister_content() {
	printf("\n=====Deregistering file=====\n");
	printf("Filename is: %s\n", std_input);
	req_pdu.type = 'T';
	//retreiving filename from command line input buffer
	memcpy(req_pdu.data, std_input , strlen(std_input));
	memcpy(req_pdu.data+11, peer_name , strlen(peer_name));
	send_udp_request();
	printf("Deregister request sent. Awaiting Acknlowledgement.....\n");

	//reading response from udp index server
	if(read(indx_sock, res_buffer, BUFLEN) < 0) {
		printf("Error reading search file\n");
		mode=0;
		return;
	}
	deserialize();
	if(res_pdu.type == 'A') {
		printf("Acknowledgment received. File deregistered!\n");
	} else if(res_pdu.type == 'E') {
		printf("Error received: %s\n", res_buffer+1);
	}
	mode=0;
}

void handle_download_content(struct sockaddr_in sockarr, char filename[11]) {
	//socket init stuff
	int sock, loopend=1, j, total_bytes_received=0, file_size;
	FILE *clientfileptr;
	sock = socket(AF_INET, SOCK_STREAM, 0);
	if (sock < 0)
	printf("Can't create socket \n");
	
	//creating file with filename
	clientfileptr = fopen(filename, "wb");
	printf("===== Downloading Content =====\n");																
/* Connect the socket */
	if (connect(sock, (struct sockaddr *)&sockarr, sizeof(sockarr)) < 0)
		printf("Can't connect to file download host \n");
	write(sock, "D", sizeof("D"));
	//read file size first and then read file
	read(sock, &file_size, sizeof(int));
	//loop until file finished transmitting.
	while (loopend == 1) { 
		j = read(sock, file_res_buffer, FILEDATABUFFLEN);
		printf("%d bytes received... of type %c\n", j, file_res_buffer[0]);
		if(file_res_buffer[0] == 'C') {
			//write data to file
			fwrite(file_res_buffer+1, 1, j-sizeof(file_res_buffer[0]), clientfileptr);
			//write(1, file_res_buffer, j);
			total_bytes_received += j-1;

			//if file buffer is not full then we assume file is done transmitting.
			if(total_bytes_received >= file_size) {
				printf("File should be done now....\n");
				loopend=0;
			}
		} else {
			printf("Error in downloading file\n");
			loopend=0;
		}
	}
	fclose(clientfileptr);
	printf("file Received: %s\n", filename);
	printf("Received %d/%d bytes....\n", total_bytes_received, file_size);
	//need to set input buffer for register operation
	strncpy(std_input, filename, sizeof(std_input));
	handle_register_content();
	mode=0;
	close(sock);
}

void handle_search_content(int file_indx) {
	char ip_addr[10];
	int loopend=1;

	//socket stuff
	in_port_t receiving_port;
	struct sockaddr_in file_client_sin;
	struct hostent	*phe;
	//init new socket address memory
	memset(&file_client_sin, 0, sizeof(file_client_sin));
	file_client_sin.sin_family = AF_INET;    

	printf("Searching for content: %s\n", filenames[file_indx]);
	req_pdu.type = 'S';
	strncpy(req_pdu.data, filenames[file_indx], sizeof(filenames[file_indx]));
	send_udp_request();
	printf("Awaiting response from server....\n");
	//read response from udp server
	if(read(indx_sock, res_buffer, BUFLEN) < 0) {
		printf("Error reading search file\n");
		mode=0;
		return;
	}
	deserialize();
	printf("Received search respsonse of: %c\n", res_pdu.type);
	//if request is S we should receive ip and port of client with file.
	if(res_pdu.type == 'S') {
		//init socket stuff
		strncpy(ip_addr, res_pdu.data, sizeof(ip_addr));
		memcpy(&receiving_port, res_pdu.data+10, sizeof(receiving_port));
		file_client_sin.sin_port = receiving_port;
		printf("client ip is: %s\n", ip_addr);
		printf("client port is: %d\n", ntohs(&file_client_sin.sin_port));
		if ( (phe = gethostbyname(ip_addr) )){
			memcpy(&file_client_sin.sin_addr, phe->h_addr, phe->h_length);
			//send client address to handle_download_content
			handle_download_content(file_client_sin, filenames[file_indx]);
		} else {
			printf("Error getting ip add\n");
		}

	} else if(res_pdu.type == 'E') {
		printf("Error received: %s\n", res_pdu.data);
	}

}

void send_udp_request() {
	serialize();
	write(indx_sock, req_buffer,sizeof(req_buffer));
	memset(req_buffer, 0, sizeof(req_buffer));
	memset(req_pdu.data, 0, sizeof(req_pdu.data));
	req_pdu.type = '\0';
}

void send_tcp_request(int socket) {
	if(req_pdu.type != 'C') {
		serialize();
		write(socket, req_buffer, BUFLEN);
	} else {
		write(socket, file_req_buffer, FILEDATABUFFLEN);
	}
}
/*------------------------------------------------------------------------
 * main - UDP client for TIME service that prints the resulting time
 *------------------------------------------------------------------------
 */
int
main(int argc, char **argv){

	struct hostent	*phe;	/* pointer to host information entry	*/
	struct sockaddr_in sin;	/* an Internet endpoint address		*/
	int	s, n, j, type, fds_indx, port = 3000;	/* socket descriptor and socket type	*/
	char *host = "localhost";

	//get index server port and ip from command line arguments
	switch (argc) {
	case 1:
		break;
	case 2:
		host = argv[1];
	case 3:
		host = argv[1];
		port = atoi(argv[2]);
		break;
	default:
		fprintf(stderr, "usage: UDPtime [host [port]]\n");
		exit(1);
	}

	struct pdu spdu;
	struct pdu file_pdu;
	memset(&sin, 0, sizeof(sin));
        sin.sin_family = AF_INET;                                                                
        sin.sin_port = htons(port);


	//creating socket for udp server connection.
	/* Map host name to IP address, allowing for dotted decimal */
	if ( (phe = gethostbyname(host) )){
			memcpy(&sin.sin_addr, phe->h_addr, phe->h_length);
	}
	else if ( (sin.sin_addr.s_addr = inet_addr(host)) == INADDR_NONE )
	fprintf(stderr, "Can't get host entry \n");
																			
/* Allocate a socket */
	indx_sock = socket(AF_INET, SOCK_DGRAM, 0);
	if (indx_sock < 0)
		printf("Can't create socket \n");

																			
/* Connect the socket */
	if (connect(indx_sock, (struct sockaddr *)&sin, sizeof(sin)) < 0)
		printf("Can't connect to %s %s \n", host, "Time");

	printf("Please enter a peer name (max 10 chars): ");
	scanf("%s", peer_name);
	printf("\nPlease enter your ip: ");
	scanf("%s", ip_add);

	//add stdin to fds
	FD_ZERO(&afds);
	FD_SET(0, &afds);
	memcpy(&rfds, &afds, sizeof(rfds));


	//run code
	while (1) {
	display_menu();
	if (select(FD_SETSIZE, &rfds, NULL, NULL, NULL) == -1) {
	perror("Select failed\n");
	break;  // Exit the loop on select failure
	}
	if(FD_ISSET(0, &rfds)) {
		handle_std_input();
	} else {
		for(fds_indx=1; fds_indx < FD_SETSIZE; fds_indx++) {
			if(FD_ISSET(fds_indx, &rfds)) {
				handle_socket_input(fds_indx);
			}
		}
	}

	}
	exit(0);
}
