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

void handle_registration();
void handle_deregistration(void);
void handle_search_and_download(void);

void handle_list_content(int udp_socket, struct sockaddr_in index_server);
void handle_search_content(int file_indx);
void handle_download_content(struct sockaddr_in sockarr, char filename[11]);
int handle_upload_content(int tcp_socket, struct sockaddr_in client, char filename[11]);
int listen_for_incomming_requests(int tcp_socket, struct sockaddr_in sock_descriptor, char filename[11]);

void send_udp_request();
void send_tcp_request(int socket);

void receive_udp_response(int socket, char* response, size_t response_size);
void receive_tcp_response(int socket, char* response, size_t response_size);

void handle_error_response(char response_type);

char peer_name[11], std_buf[100], req_buffer[100], file_req_buffer[1640], file_res_buffer[1640], res_buffer[100], std_input[100], ip_add[10], filenames[MAXFILES][11];
int mode=0, indx_sock, did_list=0, file_indx;
fd_set afds, rfds;
struct pdu req_pdu, res_pdu;

void receive_and_display_content_list() {
    if (read(indx_sock, res_buffer, BUFLEN) < 0) {
        printf("Error reading response from the index server.\n");
        return;
    }

    deserialize();  // Decode the response into the PDU structure

    if (res_pdu.type == 'O') {  // Check if the response type is a content list
        printf("\nIndex Server List of All Content\n");
        char *token = strtok(res_pdu.data, ":");
        int index = 0;

        // Clear the local filenames array
        memset(filenames, 0, sizeof(filenames));

        // Parse and update the local list
        while (token != NULL && index < MAXFILES) {
            strncpy(filenames[index], token, sizeof(filenames[index]) - 1);
            printf("%d. %s\n", index + 1, filenames[index]);
            token = strtok(NULL, ":");
            index++;
        }
        printf("--------------------------------------\n");
    } else if (res_pdu.type == 'E') {  // Handle errors
        printf("Error: %s\n", res_pdu.data);
    } else {
        printf("Unexpected response from the index server.\n");
    }
}


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
		printf("MENU\n");
		printf("1. REGISTER CONTENT\n");
		printf("2. DEREGISTER CONTENT\n");
		printf("3. LIST & DOWNLOAD AVAILABLE CONTENT\n");
		printf("4. LIST ALL AVAILABLE CONTENT\n");
		printf("5. QUIT\n");
		printf("--------------------------\n");
		printf("Enter Choice: \n");
	break;
	case 1:
		printf("Register Content\n");
		printf("Enter File Name: \n");
	break;
	case 2:
		printf("Deregister Content\n");
		printf("Enter File Name: \n");
	break;
	case 3:
		if(did_list == 0) {
		printf("List of Available Content\n");
		handle_search_and_download();
		did_list = 0;
		} else {
			printf("Please select the file number listed to download or 0 to exit: \n");
		}
	break;
	case 4:  // New case for listing content
            req_pdu.type = 'O';  // 'O' indicates a list content request
            send_udp_request();  // Send the request to the index server
            receive_and_display_content_list(); // New function to handle response
        break;
	case 5:
	printf("Quitting...\n");
	printf("Deregistering content");
	break;
	}
    
}

void handle_user_input() {
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
			printf("Registering the file: %s\n", std_input);
			handle_registration();
			printf("File has been registered\n");
		break;
		case 2:
			scanf("%s", std_input);
			handle_deregistration();
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
	printf("Received request of type: %c\n", res_pdu.type);
	switch(mode) {
		case 0: {
			if(res_pdu.type == 'D') {
				printf("Responding to download request.\n");
			} else {
				printf("Unsupported request\n");
			}
		}
		break;
		case 1: {
			if(res_pdu.type == 'A') {
				printf("Acknowledgement Received\n");
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
			printf("Deregistering content\n");
		break;
	}
}

void handle_search_and_download() {
	int i=0, h=0, loop=1, j=0, files_processed=0, str_size;
	int offset = 0;
	req_pdu.type = 'O';
	send_udp_request();
	if(read(indx_sock, res_buffer, BUFLEN) < 0){
		printf("error\n");
		mode=0;
		return;
	}
	deserialize();
	printf("Response recived of type: %c\n", res_pdu.type);
	if(res_pdu.type == 'O') {

		while(loop) {
			if(res_pdu.data[h] == '\0') {
				printf("%d: %s\n",i+1, filenames[i]);
				loop = 0;
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


void handle_registration() {
    // Initialize socket address structure
    struct sockaddr_in reg_addr;
    int sock_id, alen;

    // Create a TCP socket for file transfer
    sock_id = socket(AF_INET, SOCK_STREAM, 0);
    reg_addr.sin_family = AF_INET;
    reg_addr.sin_port = htons(0); // Let the OS assign a dynamic port
    reg_addr.sin_addr.s_addr = htonl(INADDR_ANY); // Bind to any available IP
    bind(sock_id, (struct sockaddr *)&reg_addr, sizeof(reg_addr));

    // Retrieve the dynamically assigned port number
    alen = sizeof(struct sockaddr_in);
    getsockname(sock_id, (struct sockaddr *)&reg_addr, (socklen_t *)&alen);

    // Prepare the registration PDU (type 'R')
    req_pdu.type = 'R'; // 'R' indicates a registration request
    memcpy(req_pdu.data, peer_name, strlen(peer_name)); // Add peer name
    memcpy(req_pdu.data + 11, std_input, strlen(std_input)); // Add file name
    memcpy(req_pdu.data + 22, ip_add, sizeof(ip_add)); // Add peer's IP address
    memcpy(req_pdu.data + 32, &reg_addr.sin_port, sizeof(reg_addr.sin_port)); // Add TCP port number

    // Send registration PDU to the index server
    send_udp_request();
    printf("Data sent to server. Awaiting acknowledgment...\n");

    // Wait for acknowledgment from the index server
    if (read(indx_sock, res_buffer, BUFLEN) < 0) {
        printf("Error\n");
        mode = 0;
        return;
    }

    if (res_buffer[0] == 'A') { // If acknowledgment is received
        printf("Acknowledgment received. File registered successfully.\n");
        mode = 0;

        // Fork a child process to listen for incoming file requests
        switch (fork()) {
            case 0:
                printf("Child process: Listening for incoming requests on socket...\n");
                exit(listen_for_incomming_requests(sock_id, reg_addr, std_input));
            default:
                printf("Parent process: Continuing operations.\n");
                break;
        }
    } else if (res_buffer[0] == 'E') { // If error is received
        printf("Error: File already registered.\n");
        mode = 0;
    }
}


int listen_for_incomming_requests(int sock_id, struct sockaddr_in sock_descriptor, char filename[11]) {
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
			printf("Can't Accept Client \n");
			exit(1);
	  } else{
			printf("New Client Accepted\n");
			switch(fork()){
			case 0: {
				printf("Child process handling upload content\n");
				if( (n=read(new_sd, req_buf, BUFLEN) ) > 0 ) {
					if(req_buf[0]=='D')
						exit(handle_upload_content(new_sd, client, tmpfilename));
				} else {
					printf("Unsupported request\n");
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

void handle_download_content(struct sockaddr_in sockarr, char filename[11]) {
    // Create a TCP socket for connecting to the content server
    int sock, loopend = 1, j, total_bytes_received = 0, file_size;
    FILE *clientfileptr;

    sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        printf("Can't create socket\n");
        return;
    }

    // Create a file to save the downloaded content
    clientfileptr = fopen(filename, "wb");
    printf("Downloading Content...\n");

    // Connect to the content server using the provided address and port
    if (connect(sock, (struct sockaddr *)&sockarr, sizeof(sockarr)) < 0) {
        printf("Can't connect to file download host\n");
        return;
    }

    // Send a download request (type 'D') to the content server
    write(sock, "D", sizeof("D"));

    // Read the file size from the content server
    read(sock, &file_size, sizeof(int));

    // Receive file data in chunks
    while (loopend == 1) {
        j = read(sock, file_res_buffer, FILEDATABUFFLEN);
        if (file_res_buffer[0] == 'C') { // Check for 'C' (content data) PDUs
            fwrite(file_res_buffer + 1, 1, j - 1, clientfileptr); // Write data to file
            total_bytes_received += j - 1;

            // Check if all data has been received
            if (total_bytes_received >= file_size) {
                printf("File download complete.\n");
                loopend = 0;
            }
        } else {
            printf("Error during file download.\n");
            loopend = 0;
        }
    }

    fclose(clientfileptr); // Close the file
    printf("File received: %s\n", filename);
    printf("Received %d/%d bytes.\n", total_bytes_received, file_size);

    // Automatically register the downloaded file as a content server
    strncpy(std_input, filename, sizeof(std_input));
    handle_registration();

    close(sock); // Close the TCP socket
}


void handle_deregistration() {
    // Print the filename to deregister
    printf("\nDeregistering File...\n");
    printf("File name: %s\n", std_input);

    // Prepare the deregistration PDU (type 'T')
    req_pdu.type = 'T'; // 'T' indicates a deregistration request
    memcpy(req_pdu.data, std_input, strlen(std_input)); // Add the file name
    memcpy(req_pdu.data + 11, peer_name, strlen(peer_name)); // Add the peer name

    // Send the deregistration PDU to the index server
    send_udp_request();
    printf("Deregister request sent. Awaiting acknowledgment...\n");

    // Wait for acknowledgment from the index server
    if (read(indx_sock, res_buffer, BUFLEN) < 0) {
        printf("Error reading response from index server.\n");
        mode = 0;
        return;
    }

    // Deserialize the response PDU
    deserialize();

    if (res_pdu.type == 'A') { // If acknowledgment is received
        printf("Acknowledgment received. File deregistered successfully.\n");

        // Request the updated list of content from the index server
        req_pdu.type = 'O'; // 'O' indicates a list request
        send_udp_request();
        receive_and_display_content_list(); // Display the updated content list
    } else if (res_pdu.type == 'E') { // If error is received
        printf("Error: %s\n", res_buffer + 1);
    }

    mode = 0; // Return to the main menu
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
	printf("Downloading Content...\n");																
/* Connect the socket */
	if (connect(sock, (struct sockaddr *)&sockarr, sizeof(sockarr)) < 0)
		printf("Cannot connect to file download host \n");
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
				printf("File completed...\n");
				loopend=0;
			}
		} else {
			printf("Error in downloading file\n");
			loopend=0;
		}
	}
	fclose(clientfileptr);
	printf("File Received: %s\n", filename);
	printf("Received %d/%d bytes....\n", total_bytes_received, file_size);
	//need to set input buffer for register operation
	strncpy(std_input, filename, sizeof(std_input));
	handle_registration();
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
	printf("Received Search Response of: %c\n", res_pdu.type);
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

	printf("Please enter a peer name: ");
	scanf("%s", peer_name);
	printf("\nPlease enter your IP: ");
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
		handle_user_input();
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
