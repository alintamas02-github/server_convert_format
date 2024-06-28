
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#define PORT 8080
#define SERVER_IP "127.0.0.1"
#define BUFFER_SIZE 1024
#define SMALL_BUFFER_SIZE 128
#define CREDENTIALS_FILE "credentials.txt"

void error(const char *msg) {
    perror(msg);
    exit(1);
}

void send_request(int sock, const char *request) {
    send(sock, request, strlen(request), 0);
}

void receive_response(int sock) {
    char buffer[BUFFER_SIZE] = {0};
    int valread;
    while ((valread = read(sock, buffer, BUFFER_SIZE - 1)) > 0) {
        buffer[valread] = '\0';
        printf("%s", buffer);
        if (valread < BUFFER_SIZE - 1) {
            break;
        }
    }
}

int login(int sock) {
    char username[SMALL_BUFFER_SIZE];
    char password[SMALL_BUFFER_SIZE];
    char request[BUFFER_SIZE];
    
    printf("Username: ");
    scanf("%s", username);
    printf("Password: ");
    scanf("%s", password);

    snprintf(request, sizeof(request), "login %s %s", username, password);
    send_request(sock, request);

    char response[BUFFER_SIZE];
    int valread = read(sock, response, BUFFER_SIZE - 1);
    response[valread] = '\0';

    if (strcmp(response, "success") == 0) {
        printf("Login successful\n");
        return 1;
    } else {
        printf("Login failed\n");
        return 0;
    }
}

void register_account(int sock) {
    char username[SMALL_BUFFER_SIZE];
    char password[SMALL_BUFFER_SIZE];
    char request[BUFFER_SIZE];

    printf("Choose a username: ");
    scanf("%s", username);
    printf("Choose a password: ");
    scanf("%s", password);

    // Send registration request to server
    snprintf(request, sizeof(request), "register %s %s", username, password);
    send_request(sock, request);

    // Receive response from server
    char response[BUFFER_SIZE];
    int valread = read(sock, response, BUFFER_SIZE - 1);
    response[valread] = '\0';
    printf("%s\n", response);

    // Save credentials to local file
    FILE *fp = fopen(CREDENTIALS_FILE, "a");
    if (fp == NULL) {
        perror("Error opening file for appending");
        return;
    }
    fprintf(fp, "%s %s\n", username, password);
    fclose(fp);
}

int main() {
    int sock;
    struct sockaddr_in serv_addr;

    if ((sock = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        error("Socket creation error");
    }

    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(PORT);

    if (inet_pton(AF_INET, SERVER_IP, &serv_addr.sin_addr) <= 0) {
        error("Invalid address/ Address not supported");
    }

    if (connect(sock, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0) {
        error("Connection failed");
    }

    int choice;
    char buffer[SMALL_BUFFER_SIZE];
    char request[BUFFER_SIZE];

    // Login or Register
    int authenticated = 0;
    while (!authenticated) {
        printf("1. Login\n");
        printf("2. Exit\n");
        printf("Enter your choice: ");
        scanf("%d", &choice);

        if (choice == 1) {
            authenticated = login(sock);
        } else if (choice == 2) {
            return 0;
        } else {
            printf("Invalid choice\n");
        }
    }

    // Admin functionalities after login
    while (1) {
        printf("\nAdmin Client Menu:\n");
        printf("1. List active connections\n");
        printf("2. Get status of a specific connection\n");
        printf("3. Disconnect a specific client\n");
        printf("4. Exit\n");
        printf("Enter your choice: ");
        scanf("%d", &choice);

        switch (choice) {
            case 1:
                send_request(sock, "list_connections");
                receive_response(sock);
                break;
            case 2:
                printf("Enter client ID to get status: ");
                scanf("%s", buffer);
                snprintf(request, sizeof(request), "get_status %.118s", buffer);
                send_request(sock, request);
                receive_response(sock);
                break;
            case 3:
                printf("Enter client ID to disconnect: ");
                scanf("%s", buffer);
                snprintf(request, sizeof(request), "disconnect %.118s", buffer);
                send_request(sock, request);
                receive_response(sock);
                break;
            case 4:
                close(sock);
                exit(0);
            default:
                printf("Invalid choice\n");
        }
    }

    return 0;
}