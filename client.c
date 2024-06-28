#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#define PORT 8080
#define SERVER_IP "127.0.0.1"
#define BUFFER_SIZE 1024
#define END_OF_FILE_MARKER "EOF"

int main(int argc, char *argv[]) {
    if (argc != 3) {
        printf("Usage: %s <Image_file> <convert_jpg_to_png | convert_jpg_to_webp | convert_webp_to_jpg | convert_jpg_to_inv>\n", argv[0]);
        return -1;
    }

    int sock = 0;
    struct sockaddr_in serv_addr;
    char buffer[BUFFER_SIZE] = {0};

    if ((sock = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        printf("\n Socket creation error \n");
        return -1;
    }

    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(PORT);

    if (inet_pton(AF_INET, SERVER_IP, &serv_addr.sin_addr) <= 0) {
        printf("\nInvalid address/ Address not supported \n");
        return -1;
    }

    if (connect(sock, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0) {
        printf("\nConnection Failed \n");
        return -1;
    }

    FILE *fp = fopen(argv[1], "rb");
    if (fp == NULL) {
        printf("Error opening file\n");
        return -1;
    }

    // Send the conversion request message
    send(sock, argv[2], strlen(argv[2]), 0);
    usleep(100000); // Ensure the message is sent separately

    // Send the image data
    size_t bytesRead;
    while ((bytesRead = fread(buffer, 1, BUFFER_SIZE, fp)) > 0) {
        send(sock, buffer, bytesRead, 0);
    }

    // Send end of file marker
    send(sock, END_OF_FILE_MARKER, strlen(END_OF_FILE_MARKER), 0);

    fclose(fp);
    printf("Image sent to server.\n");
    close(sock);
    return 0;
}
