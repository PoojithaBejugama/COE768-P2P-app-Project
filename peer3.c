/* peer_client.c - main */

#include <sys/types.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>

#define BUFLEN         100   /* buffer length */
#define FILEBUFFLEN    1024  /* file data buffer length */
#define MAX_FILES      5

struct pdu {
    char type;
    char data[BUFLEN - 1];
};

struct filePdu {
    char type;
    char data[FILEBUFFLEN - 1];
};

void handle_register();
void handle_deregister();
void handle_list_download();
void handle_std_input();
void list_available_content(int udp_socket, struct sockaddr_in server);
void search_content(int file_index);
void download_file(struct sockaddr_in client_addr, char filename[20]);
int upload_file(int tcp_socket, struct sockaddr_in client, char filename[20]);
int listen_for_requests(int tcp_socket, struct sockaddr_in sock_addr, char filename[20]);

void send_udp_request();
void send_tcp_request(int socket);

void serialize();
void deserialize();

char peer_name[20], user_input[100], request_buffer[100], response_buffer[100], ip_address[20], file_names[MAX_FILES][20];
int menu_mode = 0, udp_socket, file_index;
fd_set active_fds, ready_fds;
struct pdu request_pdu, response_pdu;

void serialize() {
    request_buffer[0] = request_pdu.type;
    memcpy(request_buffer + 1, request_pdu.data, sizeof(request_pdu.data));
}

void deserialize() {
    response_pdu.type = response_buffer[0];
    memcpy(response_pdu.data, response_buffer + 1, sizeof(response_pdu.data));
    memset(response_buffer, 0, sizeof(response_buffer));
}

void display_menu() {
    switch (menu_mode) {
        case 0:
            printf("------ Menu ------\n");
            printf("1. Register File\n");
            printf("2. Deregister File\n");
            printf("3. Search and Download Files\n");
            printf("0. Exit\n");
            printf("----------------------\n");
            printf("Enter your choice: \n");
            break;
        case 1:
            printf("------ Register File ------\n");
            printf("Enter the file name: \n");
            break;
        case 2:
            printf("------ Deregister File ------\n");
            printf("Enter the file name: \n");
            break;
        case 3:
            printf("------ Search and Download ------\n");
            handle_list_download();
            break;
    }
}

void handle_std_input() {
    int new_mode;
    switch (menu_mode) {
        case 0:
            scanf("%d", &new_mode);
            if (new_mode >= 0 && new_mode <= 3) {
                menu_mode = new_mode;
            } else {
                printf("Invalid choice. Try again.\n");
            }
            break;
        case 1:
            scanf("%s", user_input);
            handle_register();
            printf("File registered successfully.\n");
            menu_mode = 0;
            break;
        case 2:
            scanf("%s", user_input);
            handle_deregister();
            menu_mode = 0;
            break;
        case 3:
            scanf("%d", &file_index);
            if (file_index == 0) {
                menu_mode = 0;
            } else {
                search_content(file_index - 1);
            }
            break;
    }
}

void handle_register() {
    struct sockaddr_in reg_addr;
    int tcp_socket, addr_len;
    tcp_socket = socket(AF_INET, SOCK_STREAM, 0);
    reg_addr.sin_family = AF_INET;
    reg_addr.sin_port = htons(0);
    reg_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    bind(tcp_socket, (struct sockaddr *)&reg_addr, sizeof(reg_addr));
    addr_len = sizeof(struct sockaddr_in);
    getsockname(tcp_socket, (struct sockaddr *)&reg_addr, (socklen_t *)&addr_len);

    request_pdu.type = 'R';
    snprintf(request_pdu.data, sizeof(request_pdu.data), "%s:%s:%d", peer_name, user_input, ntohs(reg_addr.sin_port));
    serialize();
    send_udp_request();

    if (read(udp_socket, response_buffer, BUFLEN) > 0) {
        deserialize();
        if (response_pdu.type == 'A') {
            printf("Acknowledgment received. Listening for requests...\n");
            if (fork() == 0) {
                exit(listen_for_requests(tcp_socket, reg_addr, user_input));
            }
        } else if (response_pdu.type == 'E') {
            printf("File already registered or error occurred.\n");
        }
    } else {
        printf("Error in registration.\n");
    }
}

int listen_for_requests(int tcp_socket, struct sockaddr_in sock_addr, char filename[20]) {
    struct sockaddr_in client_addr;
    int client_len, new_socket;
    listen(tcp_socket, 5);

    while (1) {
        client_len = sizeof(client_addr);
        new_socket = accept(tcp_socket, (struct sockaddr *)&client_addr, (unsigned int *)&client_len);
        if (new_socket < 0) {
            printf("Failed to accept client connection.\n");
            exit(1);
        } else {
            printf("Client connected.\n");
            if (fork() == 0) {
                char request_type;
                read(new_socket, &request_type, sizeof(request_type));
                if (request_type == 'D') {
                    exit(upload_file(new_socket, client_addr, filename));
                }
                exit(0);
            }
        }
    }
    return 0;
}

int upload_file(int tcp_socket, struct sockaddr_in client, char filename[20]) {
    FILE *file_ptr;
    file_ptr = fopen(filename, "rb");
    if (!file_ptr) {
        write(tcp_socket, "E", 1);
        close(tcp_socket);
        return 1;
    }

    fseek(file_ptr, 0L, SEEK_END);
    int file_size = ftell(file_ptr);
    fseek(file_ptr, 0L, SEEK_SET);
    write(tcp_socket, &file_size, sizeof(file_size));

    char buffer[FILEBUFFLEN - 1];
    int bytes_read;
    while ((bytes_read = fread(buffer, 1, sizeof(buffer), file_ptr)) > 0) {
        write(tcp_socket, buffer, bytes_read);
    }

    fclose(file_ptr);
    close(tcp_socket);
    return 0;
}

void send_udp_request() {
    write(udp_socket, request_buffer, sizeof(request_buffer));
    memset(request_buffer, 0, sizeof(request_buffer));
    memset(request_pdu.data, 0, sizeof(request_pdu.data));
    request_pdu.type = '\0';
}

int main(int argc, char **argv) {
    struct sockaddr_in server_addr;
    int port = 4000;  // Default port
    char *host = "localhost";

    if (argc == 3) {
        host = argv[1];
        port = atoi(argv[2]);
    }

    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(port);
    server_addr.sin_addr.s_addr = inet_addr(host);

    udp_socket = socket(AF_INET, SOCK_DGRAM, 0);
    connect(udp_socket, (struct sockaddr *)&server_addr, sizeof(server_addr));

    printf("Enter your peer name: ");
    scanf("%s", peer_name);
    printf("Enter your IP address: ");
    scanf("%s", ip_address);

    FD_ZERO(&active_fds);
    FD_SET(0, &active_fds);

    while (1) {
        display_menu();
        ready_fds = active_fds;
        if (select(FD_SETSIZE, &ready_fds, NULL, NULL, NULL) > 0) {
            if (FD_ISSET(0, &ready_fds)) {
                handle_std_input();
            }
        }
    }

    return 0;
}
