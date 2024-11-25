#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <arpa/inet.h>
#include <unistd.h>

#include <sys/select.h>
#include <errno.h>

#define BUFFER_SIZE 1024
#define FIXED_PORT 19000 // Change to 20000 for Peer2

// Utility function to log raw data
void log_raw_data(const char *prefix, const char *data, int length) {
    printf("%s [Raw Data]: ", prefix);
    for (int i = 0; i < length; i++) {
        printf("%02X ", (unsigned char)data[i]);
    }
    printf("\n");
}

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
    int bytes_received = recv(client_sock, buffer, BUFFER_SIZE, 0);
    if (bytes_received < 0) {
        perror("Failed to receive content request");
        close(client_sock);
        return;
    }

    printf("Requested content: %s\n", buffer);
    log_raw_data("Received", buffer, bytes_received);

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

        int bytes_sent = send(client_sock, buffer, bytes_read, 0);
        if (bytes_sent < 0) {
            perror("Failed to send file data");
            break;
        }
        log_raw_data("Sent", buffer, bytes_sent);
    }

    printf("File transfer completed.\n");
    fclose(file);
    close(client_sock);
}

// Function to register content
void register_content(int udp_sock, struct sockaddr_in *index_server_addr, const char *content_name, int tcp_sock) {
    struct sockaddr_in tcp_addr;
    socklen_t len = sizeof(tcp_addr);

    // Get the dynamically assigned port
    if (getsockname(tcp_sock, (struct sockaddr *)&tcp_addr, &len) < 0) {
        perror("Failed to retrieve TCP socket details");
        return;
    }
    int assigned_port = ntohs(tcp_addr.sin_port);

    // Prepare the registration message
    char buffer[BUFFER_SIZE] = {0};
    buffer[0] = 'R'; // 'R' for registration
    snprintf(buffer + 1, BUFFER_SIZE - 1, "%s,%d", content_name, assigned_port);

    // Send registration to the Index Server
    int bytes_sent = sendto(udp_sock, buffer, strlen(buffer), 0, (struct sockaddr *)index_server_addr, sizeof(*index_server_addr));
    if (bytes_sent < 0) {
        perror("Failed to send registration data");
        return;
    }
    log_raw_data("Sent Registration", buffer, bytes_sent);

    // Receive acknowledgment
    int bytes_received = recvfrom(udp_sock, buffer, BUFFER_SIZE, 0, NULL, NULL);
    if (bytes_received < 0) {
        perror("Failed to receive acknowledgment");
        return;
    }

    printf("Index server response: %s\n", buffer);
    log_raw_data("Received Acknowledgment", buffer, bytes_received);
}

void search_content(int udp_sock, struct sockaddr_in *index_server_addr, const char *content_name) {
    char buffer[BUFFER_SIZE] = {0}; // Buffer to hold the request and response
    char server_ip[16] = {0};       // To store the extracted server IP
    int server_port = 0;            // To store the extracted server port

    // Prepare the search request message
    snprintf(buffer, BUFFER_SIZE, "S%s", content_name); // 'S' for Search

    // Send search request to the Index Server
    int bytes_sent = sendto(udp_sock, buffer, strlen(buffer), 0, (struct sockaddr *)index_server_addr, sizeof(*index_server_addr));
    if (bytes_sent < 0) {
        perror("Failed to send search request");
        return;
    }
    log_raw_data("Sent Search Request", buffer, bytes_sent);

    // Receive response from Index Server
    int bytes_received = recvfrom(udp_sock, buffer, BUFFER_SIZE, 0, NULL, NULL);
    if (bytes_received < 0) {
        perror("Failed to receive search response");
        return;
    }
    buffer[bytes_received] = '\0'; // Null-terminate the received string
    printf("Index server response: %s\n", buffer);
    log_raw_data("Received Search Response", buffer, bytes_received);

    // Parse the response to extract IP and port
    if (sscanf(buffer, "Content server: IP %15[^,], Port %d", server_ip, &server_port) == 2) {
        printf("Connecting to Content Server: IP=%s, Port=%d\n", server_ip, server_port);

        // Validate the port range
        if (server_port < 0 || server_port > 65535) {
            fprintf(stderr, "Invalid port number received: %d\n", server_port);
            return;
        }

        // Establish TCP connection with the content server
        int tcp_sock = socket(AF_INET, SOCK_STREAM, 0);
        if (tcp_sock < 0) {
            perror("Failed to create TCP socket");
            return;
        }

        struct sockaddr_in content_server_addr;
        memset(&content_server_addr, 0, sizeof(content_server_addr));
        content_server_addr.sin_family = AF_INET;
        content_server_addr.sin_port = htons(server_port);
        if (inet_pton(AF_INET, server_ip, &content_server_addr.sin_addr) <= 0) {
            perror("Invalid IP address");
            close(tcp_sock);
            return;
        }

        if (connect(tcp_sock, (struct sockaddr *)&content_server_addr, sizeof(content_server_addr)) < 0) {
            perror("TCP connection failed");
            close(tcp_sock);
            return;
        }

        // Request the content
        write(tcp_sock, content_name, strlen(content_name));
        printf("Downloading content: %s\n", content_name);

        // Open a file to write the received content
        FILE *fp = fopen(content_name, "wb");
        if (!fp) {
            perror("Failed to open file");
            close(tcp_sock);
            return;
        }

        // Receive and write the file content
        while (1) {
            int bytes_received = read(tcp_sock, buffer, BUFFER_SIZE);
            if (bytes_received <= 0) break; // End of file or error
            fwrite(buffer, 1, bytes_received, fp);
        }

        fclose(fp);
        close(tcp_sock);
        printf("Download completed.\n");
    } else {
        fprintf(stderr, "Failed to parse Index Server response: %s\n", buffer);
    }
}
    
// Function to deregister content
void deregister_content(int udp_sock, struct sockaddr_in *index_server_addr, const char *content_name) {
	char buffer[BUFFER_SIZE] = {0};
	
	// Prepare the deregistration message
	buffer[0] = 'T'; // 'T' for deregistration
	snprintf(buffer + 1, BUFFER_SIZE - 1, "%s", content_name);
	
	// Send deregistration to the Index Server
	int bytes_sent = sendto(udp_sock, buffer, strlen(buffer), 0, (struct sockaddr *)index_server_addr, sizeof(*index_server_addr));
	if (bytes_sent < 0) {
	    perror("Failed to send deregistration data");
	    return;
	}
	log_raw_data("Sent Deregistration", buffer, bytes_sent);
	
	// Receive acknowledgment
	int bytes_received = recvfrom(udp_sock, buffer, BUFFER_SIZE, 0, NULL, NULL);
	if (bytes_received < 0) {
	    perror("Failed to receive acknowledgment");
	    return;
	}
	
	printf("Index server response: %s\n", buffer);
	log_raw_data("Received Acknowledgment", buffer, bytes_received);
}

    
}


// Main function
int main() {
    int udp_sock, tcp_sock;
    struct sockaddr_in index_server_addr, tcp_addr, peer_addr;
    char server_ip[16] = "10.1.1.34"; // Index server IP
    int server_port = 17000; // Index server port

    // Create UDP socket
    udp_sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (udp_sock < 0) {
        perror("UDP socket creation failed");
        exit(EXIT_FAILURE);
    }

    // Bind the UDP socket to a fixed port
    memset(&peer_addr, 0, sizeof(peer_addr));
    peer_addr.sin_family = AF_INET;
    peer_addr.sin_port = htons(FIXED_PORT); // Fixed port for this peer
    peer_addr.sin_addr.s_addr = inet_addr(FIXED_PORT == 19000 ? "10.1.1.37" : "10.1.1.31");

    if (bind(udp_sock, (struct sockaddr *)&peer_addr, sizeof(peer_addr)) < 0) {
        perror("UDP bind failed");
        close(udp_sock);
        exit(EXIT_FAILURE);
    }
    printf("Peer UDP socket bound to IP: %s, Port: %d\n", inet_ntoa(peer_addr.sin_addr), FIXED_PORT);

    // Configure index server address
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

    // Bind the TCP socket with a dynamically assigned port
    memset(&tcp_addr, 0, sizeof(tcp_addr));
    tcp_addr.sin_family = AF_INET;
    tcp_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    tcp_addr.sin_port = htons(0); // Let OS assign a port
    if (bind(tcp_sock, (struct sockaddr *)&tcp_addr, sizeof(tcp_addr)) < 0) {
        perror("TCP bind failed");
        close(tcp_sock);
        exit(EXIT_FAILURE);
    }

    // Listen for incoming connections
    if (listen(tcp_sock, 5) < 0) {
        perror("TCP listen failed");
        close(tcp_sock);
        exit(EXIT_FAILURE);
    }

    printf("TCP socket is listening for incoming connections...\n");

    // Peer menu and event handling
    while (1) {
        printf("\n--- Peer Menu ---\n");
        printf("1. Register Content\n");
        printf("2. Search Content\n");
        printf("3. Quit\n");
        printf("Enter your choice: ");
        int choice;
        scanf("%d", &choice);

        if (choice == 1) {
            char content_name[20];
            printf("Enter content name to register: ");
            scanf("%19s", content_name);
            register_content(udp_sock, &index_server_addr, content_name, tcp_sock);
	// Add the download handling loop after registration 
printf("Waiting for download requests...\n"); while (1) { handle_download(tcp_sock); }
        } else if (choice == 2) {
            char content_name[20];
            printf("Enter content name to search: ");
            scanf("%19s", content_name);
            search_content(udp_sock, &index_server_addr, content_name);
        } 

        else if (choice == 4) {
        char content_name[20];
        printf("Enter content name to deregister: ");
        scanf("%19s", content_name);
        deregister_content(udp_sock, &index_server_addr, content_name);
        }
            
        else if (choice == 3) {
    printf("Deregistering all contents before quitting...\n");

    // Add logic here to deregister all registered content
    char content_list[][20] = {"content1", "content2"}; // Replace with actual registered content
    int content_count = 2; // Update dynamically based on registered contents

    for (int i = 0; i < content_count; i++) {
        deregister_content(udp_sock, &index_server_addr, content_list[i]);
    }

    printf("Quitting...\n");
    break;
}
        
        } else {
            printf("Invalid choice. Try again.\n");
        }
    }

    close(udp_sock);
    close(tcp_sock);
    return 0;
}



