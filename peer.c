#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <sys/select.h>

#define BUFFER_SIZE 1024

// Function to handle content download requests
void handle_download(int tcp_sock) {
    struct sockaddr_in client_addr;
    socklen_t addr_len = sizeof(client_addr);
    char buffer[BUFFER_SIZE] = {0};

    // Accept incoming connection
    int client_sock = accept(tcp_sock, (struct sockaddr *)&client_addr, &addr_len);
    if (client_sock < 0) {
        perror("Failed to accept connection");
        return;
    }
    printf("Accepted connection from %s:%d\n",
           inet_ntoa(client_addr.sin_addr), ntohs(client_addr.sin_port));

    // Receive content name request
    recv(client_sock, buffer, BUFFER_SIZE, 0);
    printf("Requested content: %s\n", buffer);

    // Check if the file exists
    FILE *file = fopen(buffer, "rb");
    if (!file) {
        perror("File not found");
        send(client_sock, "File not found", 14, 0);
        close(client_sock);
        return;
    }

    // Send the file content in chunks
    while (1) {
        int bytes_read = fread(buffer, 1, BUFFER_SIZE, file);
        if (bytes_read <= 0) break;
        send(client_sock, buffer, bytes_read, 0);
    }

    printf("File transfer completed.\n");
    fclose(file);
    close(client_sock);
}

void register_content(int udp_sock, struct sockaddr_in *index_server_addr, const char *content_name, int tcp_sock, int fixed_tcp_port) {
    struct sockaddr_in tcp_addr;

    // Bind the TCP socket to the fixed port
    memset(&tcp_addr, 0, sizeof(tcp_addr));
    tcp_addr.sin_family = AF_INET;
    tcp_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    tcp_addr.sin_port = htons(fixed_tcp_port);

    if (bind(tcp_sock, (struct sockaddr *)&tcp_addr, sizeof(tcp_addr)) < 0) {
        perror("TCP bind failed");
        close(tcp_sock);
        exit(EXIT_FAILURE);
    }

    // Start listening on the fixed TCP port
    if (listen(tcp_sock, 5) < 0) {
        perror("Failed to listen on TCP socket");
        close(tcp_sock);
        exit(EXIT_FAILURE);
    }
    printf("Listening on fixed TCP port: %d\n", fixed_tcp_port);

    // Prepare the registration message
    char buffer[BUFFER_SIZE] = {0};
    buffer[0] = 'R'; // 'R' for registration
    snprintf(buffer + 1, BUFFER_SIZE - 1, "%s,%d", content_name, fixed_tcp_port);

    // Send the registration to the Index Server
    sendto(udp_sock, buffer, strlen(buffer), 0, (struct sockaddr *)index_server_addr, sizeof(*index_server_addr));
    printf("Registered content '%s' on fixed TCP port %d\n", content_name, fixed_tcp_port);
}
// Function to search and download content
void search_content(int udp_sock, struct sockaddr_in *index_server_addr, const char *content_name) {
    char buffer[BUFFER_SIZE] = {0}; // Buffer to store messages

    // Prepare the search request message to send to the Index Server
    snprintf(buffer, BUFFER_SIZE, "S%s", content_name); // 'S' indicates a search request
    sendto(udp_sock, buffer, strlen(buffer), 0, (struct sockaddr *)index_server_addr, sizeof(*index_server_addr));

    // Receive the response from the Index Server
    recvfrom(udp_sock, buffer, BUFFER_SIZE, 0, NULL, NULL);

    // Log the raw response for debugging
    printf("[DEBUG] Raw Index Server response: %s\n", buffer);

    // Check if the response contains "Content server" (indicating the content is found)
    if (strstr(buffer, "Content server")) {
        char server_ip[16];   // To store the content server's IP address
        int server_port;      // To store the content server's port

        // Parse the response to extract the IP address and port
        int result = sscanf(buffer, "Content server: IP %15s, Port %d", server_ip, &server_port);
        if (result != 2) { // Ensure that both IP and port are successfully parsed
            printf("Error: Failed to parse server IP and port from response\n");
            return; // Exit the function if parsing fails
        }

        // Log the parsed IP and port for verification
        printf("[DEBUG] Extracted IP: %s, Port: %d\n", server_ip, server_port);

        // Log the intention to connect to the content server
        printf("Connecting to Content Server: IP=%s, Port=%d\n", server_ip, server_port);

        // Create a new TCP socket for connecting to the content server
        int tcp_sock = socket(AF_INET, SOCK_STREAM, 0);
        if (tcp_sock < 0) { // Check if socket creation failed
            perror("Failed to create TCP socket");
            return; // Exit the function if the socket creation fails
        }

        // Configure the address of the content server
        struct sockaddr_in content_server_addr;
        content_server_addr.sin_family = AF_INET;                // IPv4 address family
        content_server_addr.sin_port = htons(server_port);       // Convert port to network byte order
        inet_pton(AF_INET, server_ip, &content_server_addr.sin_addr); // Convert IP address to binary format

        // Attempt to connect to the content server
        if (connect(tcp_sock, (struct sockaddr *)&content_server_addr, sizeof(content_server_addr)) < 0) {
            perror("TCP connection failed"); // Log connection failure
            close(tcp_sock); // Close the socket
            return; // Exit the function if connection fails
        }

        // Send the content name as the request to the content server
        write(tcp_sock, content_name, strlen(content_name));
        printf("Downloading content: %s\n", content_name);

        // Open a file locally to save the downloaded content
        FILE *fp = fopen(content_name, "wb");
        if (!fp) { // Check if the file could not be created
            perror("Failed to open file");
            close(tcp_sock); // Close the TCP socket
            return; // Exit the function if file creation fails
        }

        // Receive the file content in chunks and write it to the local file
        while (1) {
            int bytes_received = read(tcp_sock, buffer, BUFFER_SIZE); // Read a chunk of data
            if (bytes_received <= 0) break; // Exit loop if end of file or error
            fwrite(buffer, 1, bytes_received, fp); // Write the received chunk to the file
        }

        // Close the file and the TCP socket after the transfer is complete
        fclose(fp);
        close(tcp_sock);
        printf("Download completed.\n"); // Log the successful completion of the download
    } else {
        // If the response does not indicate a content server, log content not found
        printf("Content not found.\n");
    }
}


// Function to deregister content
void deregister_content(int udp_sock, struct sockaddr_in *index_server_addr, const char *content_name) {
    char buffer[BUFFER_SIZE] = {0};
    buffer[0] = 'T';
    snprintf(buffer + 1, BUFFER_SIZE - 1, "%s", content_name);

    // Send deregistration request to the Index Server
    sendto(udp_sock, buffer, strlen(buffer), 0, (struct sockaddr *)index_server_addr, sizeof(*index_server_addr));

    // Receive server response
    recvfrom(udp_sock, buffer, BUFFER_SIZE, 0, NULL, NULL);
    printf("Index server response: %s\n", buffer);
}

// Main function
int main() {
    int udp_sock, tcp_sock;
    struct sockaddr_in index_server_addr;
    char server_ip[16];
    int server_port;

    // Fixed port numbers
    const int udp_port = 8080; // Fixed UDP port
    const int tcp_port = 9090; // Fixed TCP port

    // Ask user for IP and port of the Index Server
    printf("Enter Index Server IP: ");
    scanf("%15s", server_ip);
    printf("Enter Index Server Port: ");
    scanf("%d", &server_port);

    // Create UDP socket
    udp_sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (udp_sock < 0) {
        perror("UDP socket creation failed");
        exit(EXIT_FAILURE);
    }

    // Configure Index Server address
    memset(&index_server_addr, 0, sizeof(index_server_addr));
    index_server_addr.sin_family = AF_INET;
    index_server_addr.sin_port = htons(server_port);
    inet_pton(AF_INET, server_ip, &index_server_addr.sin_addr);

    // Create TCP socket
    tcp_sock = socket(AF_INET, SOCK_STREAM, 0);
    if (tcp_sock < 0) {
        perror("TCP socket creation failed");
        exit(EXIT_FAILURE);
    }

    // Bind the TCP socket to the fixed port
    struct sockaddr_in tcp_addr;
    memset(&tcp_addr, 0, sizeof(tcp_addr));
    tcp_addr.sin_family = AF_INET;
    tcp_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    tcp_addr.sin_port = htons(tcp_port);

    if (bind(tcp_sock, (struct sockaddr *)&tcp_addr, sizeof(tcp_addr)) < 0) {
        perror("TCP bind failed");
        close(tcp_sock);
        exit(EXIT_FAILURE);
    }

    // Start listening on the fixed TCP port
    if (listen(tcp_sock, 5) < 0) {
        perror("TCP listen failed");
        close(tcp_sock);
        exit(EXIT_FAILURE);
    }

    printf("Listening on fixed TCP port: %d\n", tcp_port);

    // Set up select() to handle stdin and incoming TCP connections
    fd_set afds, rfds;
    FD_ZERO(&afds);
    FD_SET(STDIN_FILENO, &afds); // Add stdin to the set
    FD_SET(tcp_sock, &afds);     // Add TCP socket to the set

    while (1) {
        rfds = afds; // Copy afds to rfds for select

        printf("\n--- Peer Menu ---\n");
        printf("1. Register Content\n");
        printf("2. Search Content\n");
        printf("3. Deregister Content\n");
        printf("4. Quit\n");
        printf("Enter your choice: ");

        if (select(FD_SETSIZE, &rfds, NULL, NULL, NULL) < 0) {
            perror("Select failed");
            break;
        }

        // Check if stdin is ready (user input)
        if (FD_ISSET(STDIN_FILENO, &rfds)) {
            int choice;
            char content_name[20];
            scanf("%d", &choice);

            if (choice == 1) {
                printf("Enter content name to register: ");
                scanf("%19s", content_name);
                register_content(udp_sock, &index_server_addr, content_name, tcp_sock, tcp_port);
            } else if (choice == 2) {
                printf("Enter content name to search: ");
                scanf("%19s", content_name);
                search_content(udp_sock, &index_server_addr, content_name);
            } else if (choice == 3) {
                printf("Enter content name to deregister: ");
                scanf("%19s", content_name);
                deregister_content(udp_sock, &index_server_addr, content_name);
            } else if (choice == 4) {
                printf("Quitting...\n");
                break;
            } else {
                printf("Invalid choice. Please try again.\n");
            }
        }

        // Check if TCP socket is ready (incoming download request)
        if (FD_ISSET(tcp_sock, &rfds)) {
            handle_download(tcp_sock);
        }
    }

    close(udp_sock);
    close(tcp_sock);
    return 0;
}



