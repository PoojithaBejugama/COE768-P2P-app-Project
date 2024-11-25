#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <arpa/inet.h>
#include <unistd.h>

#define BUFFER_SIZE 1024

// Structure to store registered content
typedef struct {
    char content_name[20];
    struct sockaddr_in content_address;
} ContentInfo;

ContentInfo registry[10]; // Maximum 10 pieces of content
int registry_count = 0;

// Function to find content in the registry
int find_content(const char *content_name) {
    for (int i = 0; i < registry_count; i++) {
        if (strcmp(registry[i].content_name, content_name) == 0) {
            return i;
        }
    }
    return -1;
}

// Function to display the registry
void display_registry() {
    printf("\n--- Registered Content ---\n");
    for (int i = 0; i < registry_count; i++) {
        printf("Content Name: %s, IP: %s, Port: %d\n",
               registry[i].content_name,
               inet_ntoa(registry[i].content_address.sin_addr),
               ntohs(registry[i].content_address.sin_port));
    }
    printf("--------------------------\n");
}

int main() {
    int sockfd;
    struct sockaddr_in server_addr, client_addr;
    char buffer[BUFFER_SIZE];
    socklen_t addr_len = sizeof(client_addr);

    // Create UDP socket
    sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (sockfd < 0) {
        perror("Socket creation failed");
        exit(EXIT_FAILURE);
    }

    // Configure server address
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = inet_addr("10.1.1.34");
    server_addr.sin_port = htons(8080); // Replace with desired port

    // Bind socket
    if (bind(sockfd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        perror("Bind failed");
        close(sockfd);
        exit(EXIT_FAILURE);
    }

    // Retrieve and display the server's IP and port
    socklen_t len = sizeof(server_addr);
    if (getsockname(sockfd, (struct sockaddr *)&server_addr, &len) == -1) {
        perror("getsockname failed");
    } else {
        printf("Index server is running on IP: %s, Port: %d\n",
               inet_ntoa(server_addr.sin_addr), ntohs(server_addr.sin_port));
    }

    while (1) {
        memset(buffer, 0, BUFFER_SIZE); // Clear buffer
        recvfrom(sockfd, buffer, BUFFER_SIZE, 0, (struct sockaddr *)&client_addr, &addr_len);

        char pdu_type = buffer[0];
        char content_name[20];
        strncpy(content_name, buffer + 1, 20);
        content_name[19] = '\0';

        if (pdu_type == 'R') { // Registration
              char received_content[BUFFER_SIZE] = {0};
              int content_port;

      // Split the received message into content name and port number
      sscanf(buffer + 1, "%[^,],%d", received_content, &content_port);
  
      // Check if the content is already registered
      int content_index = find_content(received_content);
      if (content_index == -1 && registry_count < 10) {
          // Register new content
          strcpy(registry[registry_count].content_name, received_content);
  
          // Update the client's port in the address structure
          registry[registry_count].content_address = client_addr;
          registry[registry_count].content_address.sin_port = htons(content_port);
  
          registry_count++;
  
          strcpy(buffer, "Registration successful");
          sendto(sockfd, buffer, strlen(buffer), 0, (struct sockaddr *)&client_addr, addr_len);
  
          display_registry();
      } else {
          if (content_index != -1) {
              strcpy(buffer, "Error: Content already registered");
          } else {
              strcpy(buffer, "Error: Registry full");
          }
          sendto(sockfd, buffer, strlen(buffer), 0, (struct sockaddr *)&client_addr, addr_len);
      }

        } else if (pdu_type == 'S') { // Search
            int content_index = find_content(content_name);
            if (content_index != -1) {
                printf("Content found: %s\n", content_name);
                struct sockaddr_in *content_addr = &registry[content_index].content_address;
                sprintf(buffer, "Content server: IP %s, Port %d",
                        inet_ntoa(content_addr->sin_addr), ntohs(content_addr->sin_port));
            } else {
                strcpy(buffer, "Error: Content not found");
            }
            sendto(sockfd, buffer, strlen(buffer), 0, (struct sockaddr *)&client_addr, addr_len);
        } else if (pdu_type == 'T') { // Deregistration
            int content_index = find_content(content_name);
            if (content_index != -1) {
                for (int i = content_index; i < registry_count - 1; i++) {
                    registry[i] = registry[i + 1];
                }
                registry_count--;

                strcpy(buffer, "Deregistration successful");
            } else {
                strcpy(buffer, "Error: Content not found");
            }
            sendto(sockfd, buffer, strlen(buffer), 0, (struct sockaddr *)&client_addr, addr_len);

            display_registry();
        }
    }

    close(sockfd);
    return 0;
}

