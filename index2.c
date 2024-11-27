/* time_server.c - main */

#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <stdlib.h>
#include <string.h>
#include <netdb.h>
#include <stdio.h>
#include <time.h>

#define BUFLEN 100
#define MAXNUMFILES 9

char content_name_values[MAXNUMFILES][11];
char unique_content_name_values[MAXNUMFILES][11];
char peer_name_values[MAXNUMFILES][11];
char ip_values[MAXNUMFILES][10];
in_port_t client_port_values[MAXNUMFILES];
int num_times_read[MAXNUMFILES];
int numClients = 0;
int numuniqueVals=0;

struct pdu req_pdu, res_pdu;
char req_buffer[100], res_buffer[100], test_buf[50];
in_port_t receiving_port;

struct pdu {
    char type;
    char data[99];
};

void serialize(char type,char data[99] , char buffer[BUFLEN]) {
	buffer[0] = type;
	int i;
	for(i = 0 ; i < BUFLEN-1; i++) {
		buffer[i+1] = data[i];
	}
}

void deserialize(struct pdu pdu_, char buffer[BUFLEN]) {
	pdu_.type = buffer[0];
	fprintf(stderr,"%c",buffer[0]);
	int i;
	for(i = 0; i < BUFLEN-1; i++) {
		pdu_.data[i] = buffer[i+1];
	}
}

int findIndexOfRecord(char peerName[10], char fileName[10]) {
	int i;
	for(i=0; i < numClients; i++) {
		if( strcmp(peer_name_values[i], peerName) == 0 && strcmp(content_name_values[i], fileName) == 0) {
			return i;
		}
	}
	return -1;
}

int findIndexOfFilename(char fileName[10]) {
	int i;
	for(i=0; i < numClients; i++) {
		if( strcmp(content_name_values[i], fileName) == 0) {
			return i;
		}
	}
	return -1;
}

int findIndexOfPeerName(char peerName[10]) {
	int i;
	for(i=0; i < numClients; i++) {
		if( strcmp(peer_name_values[i], peerName) == 0) {
			return i;
		}
	}
	return -1;
}

int findIndexOfUniqueFileName(char fileName[10]) {
	int i;
	for(i=0; i < numuniqueVals; i++) {
		if( strcmp(unique_content_name_values[i], fileName) == 0) {
			return i;
		}
	}
	return -1;
}


struct pdu register_client_server(struct pdu req, struct sockaddr_in sockadd) {
    // Variables to hold extracted data
    int i;
    struct pdu resPdu; // Response PDU
    char peerName[11]; // Peer name
    char fileName[11]; // File name
    char ip_sent[10];  // IP address of the peer

    // Extract peer name, file name, and IP address from the request PDU
    strncpy(peerName, req_pdu.data, sizeof(peerName));
    strncpy(fileName, req_pdu.data + 11, sizeof(fileName));
    strncpy(ip_sent, req_pdu.data + 22, sizeof(ip_sent));
    memcpy(&receiving_port, req.data + 32, sizeof(receiving_port)); // Extract port

    fprintf(stderr, "\nRegistering file.\n");
    fprintf(stderr, "Filename: %s\nPeer: %s\nIP: %s\nPort: %d\n",
            fileName, peerName, ip_sent, ntohs(receiving_port));

    // Check if the record is unique and within server capacity
    if (findIndexOfRecord(peerName, fileName) == -1 && numClients < MAXNUMFILES) {
        // Add the new record to server's arrays
        strncpy(peer_name_values[numClients], peerName, sizeof(peer_name_values[numClients]));
        strncpy(content_name_values[numClients], fileName, sizeof(content_name_values[numClients]));
        strncpy(ip_values[numClients], ip_sent, sizeof(ip_values[numClients]));
        client_port_values[numClients] = receiving_port;
        numClients++; // Increment client count

        // Add to the unique file list if it's a new file
        if (findIndexOfUniqueFileName(fileName) < 0) {
            strncpy(unique_content_name_values[numuniqueVals], fileName, sizeof(unique_content_name_values[numuniqueVals]));
            numuniqueVals++;
        }
        fprintf(stderr, "File registered successfully. Sending acknowledgment.\n");

        // Set response type to acknowledgment
        resPdu.type = 'A';
    } else if (numClients >= MAXNUMFILES) {
        // Server reached its capacity
        fprintf(stderr, "Server quota reached. Sending error response.\n");
        resPdu.type = 'E';
        strncpy(resPdu.data, "Server Quota reached.", sizeof("Server Quota reached."));
    } else {
        // File already exists
        fprintf(stderr, "File already exists. Sending error response.\n");
        resPdu.type = 'E';
        strncpy(resPdu.data, "File already exists", sizeof("File already exists"));
    }

    return resPdu; // Return the response PDU
}


struct pdu deregister_client_server(struct pdu req) {
    int i, unique_indx, j;
    struct pdu resPdu; // Response PDU
    char peerName[10]; // Peer name
    char fileName[10]; // File name

    // Extract file name and peer name from the request PDU
    strncpy(fileName, req.data, sizeof(fileName));
    strncpy(peerName, req.data + 11, sizeof(peerName));

    fprintf(stderr, "\nDeregistering File.\n");
    fprintf(stderr, "Filename: %s\nPeer: %s\n", fileName, peerName);

    // Find the index of the record
    int index = findIndexOfRecord(peerName, fileName);
    if (index > -1) {
        // Shift subsequent records to remove the entry
        for (i = index; i < numClients - 1; i++) {
            strncpy(content_name_values[i], content_name_values[i + 1], sizeof(content_name_values[i + 1]));
            strncpy(peer_name_values[i], peer_name_values[i + 1], sizeof(peer_name_values[i + 1]));
            strncpy(ip_values[i], ip_values[i + 1], sizeof(ip_values[i + 1]));
            client_port_values[i] = client_port_values[i + 1];
            num_times_read[i] = num_times_read[i + 1];
        }

        // Clear the last entry and update client count
        memset(content_name_values[numClients - 1], '\0', sizeof(content_name_values[numClients - 1]));
        memset(peer_name_values[numClients - 1], '\0', sizeof(peer_name_values[numClients - 1]));
        memset(ip_values[numClients - 1], '\0', sizeof(ip_values[numClients - 1]));
        client_port_values[numClients - 1] = 0;
        num_times_read[numClients - 1] = 0;
        numClients--;

        // Check if the file is no longer in use and update unique file list
        if (findIndexOfFilename(fileName) < 0) {
            unique_indx = findIndexOfUniqueFileName(fileName);
            if (unique_indx >= 0) {
                for (j = unique_indx; j < numuniqueVals - 1; j++) {
                    strncpy(unique_content_name_values[j], unique_content_name_values[j + 1],
                            sizeof(unique_content_name_values[j + 1]));
                }
                memset(unique_content_name_values[numuniqueVals - 1], '\0', sizeof(unique_content_name_values[numuniqueVals - 1]));
                numuniqueVals--;
            }
        }

        fprintf(stderr, "File Deregistered Successfully... Sending Acknowledgment.\n");
        resPdu.type = 'A'; // Acknowledgment
    } else {
        fprintf(stderr, "File not found. Sending error response.\n");
        resPdu.type = 'E'; // Error response
        strncpy(resPdu.data, "File not found", sizeof("File not found"));
    }

    return resPdu; // Return the response PDU
}


struct pdu deregister_all_client_server(struct pdu req) {
	int i, unique_indx, j, index;
	struct pdu resPdu;
	char peerName[11], fileName[11];
	strncpy(peerName, req.data, sizeof(peerName));
	fprintf(stderr, "\nDeregistering File.\n");
	fprintf(stderr, "Peer is: %s\n", peerName);
	while((index = findIndexOfPeerName(peerName)) > -1){
		//if non-last value is deregistered move all values back one index.
		strncpy(fileName, content_name_values[i], sizeof(content_name_values[i]));
		for(i = index; i < numClients -1; i++) {
			strncpy(content_name_values[i], content_name_values[i+1], sizeof(content_name_values[i+1]));
			strncpy(peer_name_values[i], peer_name_values[i+1], sizeof(peer_name_values[i+1]));
			strncpy(ip_values[i], ip_values[i+1], sizeof(ip_values[i+1]));
			client_port_values[i] = client_port_values[i+1];
			num_times_read[i] = num_times_read[i+1];
		}
		//clear last value of array and decrement numclients
		memset(content_name_values[numClients-1], '\0', sizeof(content_name_values[numClients-1]));
		memset(peer_name_values[numClients-1], '\0', sizeof(peer_name_values[numClients-1]));
		memset(ip_values[numClients-1], '\0', sizeof(ip_values[numClients-1]));
		client_port_values[numClients-1] = 0;
		num_times_read[numClients-1] = 0;
		numClients--;
		
		//ammends unique name array if no more copies of file exist in index records.
		if(findIndexOfFilename(fileName) < 0) {
			unique_indx = findIndexOfUniqueFileName(fileName);
			if(unique_indx >= 0) {
				for(j=unique_indx; j < numuniqueVals; j++) {
					strncpy(unique_content_name_values[j], unique_content_name_values[j+1], sizeof(unique_content_name_values[j+1]));
				}
				memset(unique_content_name_values[unique_indx], '\0', sizeof(unique_content_name_values[unique_indx]));
				numuniqueVals--;
			}
		}
	}
		fprintf(stderr,"Completed Client Deregistertion. Sending Acknowledgment to client \n");
		resPdu.type = 'A';
	return resPdu;
}


struct pdu find_client_server_for_file(char fileName[10]) {
    int i;
    struct pdu resPdu; // Response PDU
    in_port_t testport;
    int lastIndx = -1;         // Index of the least-used content server
    int lastNumTimesRead = -1; // Minimum number of downloads so far

    fprintf(stderr, "\nSearching For File\n");
    fprintf(stderr, "File name: %s\n", fileName);

    // Search for the file in the content records
    for (i = 0; i < numClients; i++) {
        if (strcmp(content_name_values[i], fileName) == 0) {
            if (lastNumTimesRead == -1 || lastNumTimesRead > num_times_read[i]) {
                lastIndx = i;
                lastNumTimesRead = num_times_read[i];
            }
        }
    }

    if (lastIndx > -1) {
        // File found, prepare the response PDU
        fprintf(stderr, "File has been found. Peer: %s\nSending details to client.\n", peer_name_values[lastIndx]);
        resPdu.type = 'S'; // Search response
        memcpy(resPdu.data, ip_values[lastIndx], sizeof(ip_values[lastIndx])); // Add IP
        memcpy(resPdu.data + 10, &client_port_values[lastIndx], sizeof(client_port_values[lastIndx])); // Add port
        memcpy(&testport, resPdu.data + 10, sizeof(testport));
        fprintf(stderr, "IP: %s\nPort: %d\n", resPdu.data, ntohs(testport));
        num_times_read[lastIndx]++; // Increment usage count
    } else {
        // File not found
        fprintf(stderr, "File has not been found. Sending error response.\n");
        resPdu.type = 'E'; // Error response
        strcpy(resPdu.data, "File not found");
    }

    return resPdu; // Return the response PDU
}

struct pdu list_files_in_library() {
	int i, j,h=0, total_indx=0, str_len;
	size_t offset = 0;
	struct pdu tmpPdu;
	tmpPdu.type = 'O';
	fprintf(stderr, "\nListing Files...\n");
	fprintf(stderr, "Files In Library: %d\n", numuniqueVals);
    for (i = 0; i < numuniqueVals; ++i) {
		str_len = strlen(unique_content_name_values[i]);
		for(j = 0; j < str_len; j++) {
			tmpPdu.data[h++] = unique_content_name_values[i][j];
		}
		if(i < numuniqueVals-1) tmpPdu.data[h++] = ':';
		fprintf(stderr, "%d. %s\n", i+1, unique_content_name_values[i]);
    }
	tmpPdu.data[h] = '\0';
	return tmpPdu;
}


int main(int argc, char *argv[]) {
    struct sockaddr_in fsin; // Client address
    int sock, alen;
    struct sockaddr_in sin;  // Server address
    int s, port = 3000;

    // Configure server address and port
    memset(&sin, 0, sizeof(sin));
    sin.sin_family = AF_INET;
    sin.sin_port = htons(port);
    sin.sin_addr.s_addr = INADDR_ANY;

    // Create and bind a UDP socket
    s = socket(AF_INET, SOCK_DGRAM, 0);
    if (bind(s, (struct sockaddr *)&sin, sizeof(sin)) < 0) {
        fprintf(stderr, "Can't bind to port %d\n", port);
        exit(1);
    }

    fprintf(stderr, "Server running on port %d\n", port);

    // Main loop to handle requests
    while (1) {
        alen = sizeof(fsin);
        if (recvfrom(s, req_buffer, sizeof(req_buffer), 0, (struct sockaddr *)&fsin, &alen) < 0) {
            fprintf(stderr, "Error Receiving Request\n");
        }

        // Deserialize the request PDU
        req_pdu.type = req_buffer[0];
        memcpy(req_pdu.data, req_buffer + 1, BUFLEN - 1);

        // Handle the request based on its type
        switch (req_pdu.type) {
            case 'R': // Registration
                res_pdu = register_client_server(req_pdu, fsin);
                break;
            case 'T': // Deregistration
                res_pdu = deregister_client_server(req_pdu);
                break;
            case 'S': // Search
                res_pdu = find_client_server_for_file(req_pdu.data);
                break;
            case 'O': // List content
                res_pdu = list_files_in_library();
                break;
            default: // Invalid request
                fprintf(stderr, "An Invalid Request Was Received.\n");
                res_pdu.type = 'E';
                strncpy(res_pdu.data, "Invalid request", sizeof("Invalid request"));
        }

        // Send the response
        serialize(res_pdu.type, res_pdu.data, res_buffer);
        sendto(s, res_buffer, sizeof(res_buffer), 0, (struct sockaddr *)&fsin, alen);
    }
}

