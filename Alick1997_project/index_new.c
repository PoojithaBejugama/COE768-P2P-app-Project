#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <stdlib.h>
#include <string.h>
#include <netdb.h>
#include <stdio.h>
#include <time.h>

#define BUFFER_LENGTH 100
#define MAX_FILES 9

char file_names[MAX_FILES][11];
char unique_file_names[MAX_FILES][11];
char client_names[MAX_FILES][11];
char ip_addresses[MAX_FILES][10];
in_port_t client_ports[MAX_FILES];
int access_count[MAX_FILES];
int total_clients = 0;
int unique_file_count = 0;

struct pdu {
    char type;
    char data[99];
};

struct pdu request_pdu, response_pdu;
char request_buffer[BUFFER_LENGTH], response_buffer[BUFFER_LENGTH], temp_buffer[50];
in_port_t listening_port;

void serialize_pdu(char type, char data[99], char buffer[BUFFER_LENGTH]) {
    buffer[0] = type;
    for(int i = 0; i < BUFFER_LENGTH - 1; i++) {
        buffer[i + 1] = data[i];
    }
}

void deserialize_pdu(struct pdu* pdu_, char buffer[BUFFER_LENGTH]) {
    pdu_->type = buffer[0];
    fprintf(stderr, "%c", buffer[0]);
    for(int i = 0; i < BUFFER_LENGTH - 1; i++) {
        pdu_->data[i] = buffer[i + 1];
    }
}

int get_client_file_index(char client_name[10], char file_name[10]) {
    for(int i = 0; i < total_clients; i++) {
        if(strcmp(client_names[i], client_name) == 0 && strcmp(file_names[i], file_name) == 0) {
            return i;
        }
    }
    return -1;
}

int get_file_index(char file_name[10]) {
    for(int i = 0; i < total_clients; i++) {
        if(strcmp(file_names[i], file_name) == 0) {
            return i;
        }
    }
    return -1;
}

int get_client_index(char client_name[10]) {
    for(int i = 0; i < total_clients; i++) {
        if(strcmp(client_names[i], client_name) == 0) {
            return i;
        }
    }
    return -1;
}

int get_unique_file_index(char file_name[10]) {
    for(int i = 0; i < unique_file_count; i++) {
        if(strcmp(unique_file_names[i], file_name) == 0) {
            return i;
        }
    }
    return -1;
}

struct pdu register_client(struct pdu req, struct sockaddr_in sockadd) {
    int i;
    struct pdu res_pdu;
    char client_name[11];
    char file_name[11];
    char ip_address[10];

    strncpy(client_name, request_pdu.data, sizeof(client_name));
    strncpy(file_name, request_pdu.data + 11, sizeof(file_name));
    strncpy(ip_address, request_pdu.data + 22, sizeof(ip_address));
    memcpy(&listening_port, req.data + 32, sizeof(listening_port));

    fprintf(stderr, "\nRegistering File.\n");
    fprintf(stderr, "Filename is: %s\nClient is: %s\nIP is: %s\nPort is: %d\n", file_name, client_name, ip_address, ntohs(listening_port));
    if(get_client_file_index(client_name, file_name) == -1 && total_clients < MAX_FILES) {
        strncpy(client_names[total_clients], client_name, sizeof(client_names[total_clients]));
        strncpy(file_names[total_clients], file_name, sizeof(file_names[total_clients]));
        strncpy(ip_addresses[total_clients], ip_address, sizeof(ip_addresses[total_clients]));
        client_ports[total_clients] = listening_port;
        total_clients++;
        if(get_unique_file_index(file_name) < 0) {
            strncpy(unique_file_names[unique_file_count], file_name, sizeof(unique_file_names[unique_file_count]));
            unique_file_count++;
        }
        fprintf(stderr, "File has been registered -- Acknowledgement sent back.\n");
        res_pdu.type = 'A';
    } else if(total_clients >= MAX_FILES) {
        fprintf(stderr, "Server Quota reached.\n");
        res_pdu.type = 'E';
        strncpy(res_pdu.data, "Server Quota reached.", sizeof("Server Quota reached."));
    } else {
        fprintf(stderr, "This file already exists -- Sending error.\n");
        res_pdu.type = 'E';
        strncpy(res_pdu.data, "File already exists", sizeof("File already exists"));
    }
    return res_pdu;
}

struct pdu deregister_client(struct pdu req) {
    int i, unique_index, j;
    struct pdu res_pdu;
    char client_name[10];
    char file_name[10];
    strncpy(file_name, req.data, sizeof(file_name));
    strncpy(client_name, req.data + 11, sizeof(client_name));
    fprintf(stderr, "\nDeregistering File.\n");
    fprintf(stderr, "Filename is: %s\nClient is: %s\n", file_name, client_name);
    int index = get_client_file_index(client_name, file_name);
    if(index > -1) {
        // If non-last value is deregistered, move all values back one index.
        for(i = index; i < total_clients - 1; i++) {
            strncpy(file_names[i], file_names[i + 1], sizeof(file_names[i + 1]));
            strncpy(client_names[i], client_names[i + 1], sizeof(client_names[i + 1]));
            strncpy(ip_addresses[i], ip_addresses[i + 1], sizeof(ip_addresses[i + 1]));
            client_ports[i] = client_ports[i + 1];
            access_count[i] = access_count[i + 1];
        }
        // Clear last value of array and decrement total_clients
        memset(file_names[total_clients - 1], '\0', sizeof(file_names[total_clients - 1]));
        memset(client_names[total_clients - 1], '\0', sizeof(client_names[total_clients - 1]));
        memset(ip_addresses[total_clients - 1], '\0', sizeof(ip_addresses[total_clients - 1]));
        client_ports[total_clients - 1] = 0;
        access_count[total_clients - 1] = 0;
        total_clients--;

        // Amend unique name array if no more copies of file exist in index records.
        if(get_file_index(file_name) < 0) {
            unique_index = get_unique_file_index(file_name);
            if(unique_index >= 0) {
                for(j = unique_index; j < unique_file_count; j++) {
                    strncpy(unique_file_names[j], unique_file_names[j + 1], sizeof(unique_file_names[j + 1]));
                }
                memset(unique_file_names[unique_index], '\0', sizeof(unique_file_names[unique_index]));
                unique_file_count--;
            }
        }

        fprintf(stderr, "File has been deregistered -- Acknowledgement sent back.\n");
        res_pdu.type = 'A';
    } else {
        fprintf(stderr, "File could not be found. Sending error\n");
        res_pdu.type = 'E';
    }

    return res_pdu;
}

struct pdu deregister_all_clients(struct pdu req) {
    int i, unique_index, j, index;
    struct pdu res_pdu;
    char client_name[11], file_name[11];
    strncpy(client_name, req.data, sizeof(client_name));
    fprintf(stderr, "\nDeregistering File.\n");
    fprintf(stderr, "Client is: %s\n", client_name);
    while((index = get_client_index(client_name)) > -1) {
        // If non-last value is deregistered, move all values back one index.
        strncpy(file_name, file_names[i], sizeof(file_names[i]));
        for(i = index; i < total_clients - 1; i++) {
            strncpy(file_names[i], file_names[i + 1], sizeof(file_names[i + 1]));
            strncpy(client_names[i], client_names[i + 1], sizeof(client_names[i + 1]));
            strncpy(ip_addresses[i], ip_addresses[i + 1], sizeof(ip_addresses[i + 1]));
            client_ports[i] = client_ports[i + 1];
            access_count[i] = access_count[i + 1];
        }
        // Clear last value of array and decrement total_clients
        memset(file_names[total_clients - 1], '\0', sizeof(file_names[total_clients - 1]));
        memset(client_names[total_clients - 1], '\0', sizeof(client_names[total_clients - 1]));
        memset(ip_addresses[total_clients - 1], '\0', sizeof(ip_addresses[total_clients - 1]));
        client_ports[total_clients - 1] = 0;
        access_count[total_clients - 1] = 0;
        total_clients--;

        // Amend unique name array if no more copies of file exist in index records.
        if(get_file_index(file_name) < 0) {
            unique_index = get_unique_file_index(file_name);
            if(unique_index >= 0) {
                for(j = unique_index; j < unique_file_count; j++) {
                    strncpy(unique_file_names[j], unique_file_names[j + 1], sizeof(unique_file_names[j + 1]));
                }
                memset(unique_file_names[unique_index], '\0', sizeof(unique_file_names[unique_index]));
                unique_file_count--;
            }
        }
    }
   
