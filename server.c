#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <jpeglib.h>
#include <pthread.h>
#include <png.h>
#include <webp/encode.h>
#include <webp/decode.h>

#define PORT 8080
#define BUFFER_SIZE 1024
#define END_OF_FILE_MARKER "EOF"
#define CREDENTIALS_FILE "credentials.txt"


typedef struct {
    int socket;
    struct sockaddr_in address;
    pthread_t thread;
} ClientInfo;

ClientInfo clients[FD_SETSIZE];
int client_count = 0;
pthread_mutex_t clients_mutex = PTHREAD_MUTEX_INITIALIZER;

void *handle_client(void *arg) {
    ClientInfo *client = (ClientInfo *)arg;
    int valread;
    char buffer[BUFFER_SIZE] = {0};

    while ((valread = read(client->socket, buffer, BUFFER_SIZE)) > 0) {
        buffer[valread] = '\0';

        // Handle admin requests
        if (strncmp(buffer, "login", 5) == 0) {
            char *username = strtok(buffer + 6, " ");
            char *password = strtok(NULL, " ");
            FILE *file = fopen(CREDENTIALS_FILE, "r");
            char file_username[128], file_password[128];
            int authenticated = 0;
            while (fscanf(file, "%s %s", file_username, file_password) != EOF) {
                if (strcmp(username, file_username) == 0 && strcmp(password, file_password) == 0) {
                    authenticated = 1;
                    break;
                }
            }
            fclose(file);
            if (authenticated) {
                send(client->socket, "success", strlen("success"), 0);
            } else {
                send(client->socket, "failure", strlen("failure"), 0);
            }
        }
       else if (strcmp(buffer, "list_connections") == 0) {
            pthread_mutex_lock(&clients_mutex);
            char response[BUFFER_SIZE] = "Active connections:\n";
            for (int i = 0; i < client_count; ++i) {
                char client_info[128];
                snprintf(client_info, sizeof(client_info), "Client %d - IP: %s, Port: %d\n", i,
                         inet_ntoa(clients[i].address.sin_addr), ntohs(clients[i].address.sin_port));
                strncat(response, client_info, sizeof(response) - strlen(response) - 1);
            }
            pthread_mutex_unlock(&clients_mutex);
            send(client->socket, response, strlen(response), 0);
        } else if (strncmp(buffer, "get_status", 10) == 0) {
            int client_id = atoi(buffer + 11);
            pthread_mutex_lock(&clients_mutex);
            if (client_id >= 0 && client_id < client_count) {
                char client_info[128];
                snprintf(client_info, sizeof(client_info), "Client %d - IP: %s, Port: %d\n", client_id,
                         inet_ntoa(clients[client_id].address.sin_addr), ntohs(clients[client_id].address.sin_port));
                send(client->socket, client_info, strlen(client_info), 0);
            } else {
                send(client->socket, "Invalid client ID\n", 19, 0);
            }
            pthread_mutex_unlock(&clients_mutex);
        } else if (strncmp(buffer, "disconnect", 10) == 0) {
            int client_id = atoi(buffer + 11);
            pthread_mutex_lock(&clients_mutex);
            if (client_id >= 0 && client_id < client_count) {
                close(clients[client_id].socket);
                clients[client_id] = clients[--client_count];
                send(client->socket, "Client disconnected\n", 20, 0);
            } else {
                send(client->socket, "Invalid client ID\n", 19, 0);
            }
            pthread_mutex_unlock(&clients_mutex);
        } else {
            // Handle normal client requests
        }
    }

    pthread_mutex_lock(&clients_mutex);
    for (int i = 0; i < client_count; ++i) {
        if (clients[i].socket == client->socket) {
            clients[i] = clients[--client_count];
            break;
        }
    }
    pthread_mutex_unlock(&clients_mutex);
    close(client->socket);
    free(client);
    return NULL;
}

void add_client(int new_socket, struct sockaddr_in address) {
    pthread_mutex_lock(&clients_mutex);
    clients[client_count].socket = new_socket;
    clients[client_count].address = address;
    pthread_create(&clients[client_count].thread, NULL, handle_client, &clients[client_count]);
    client_count++;
    pthread_mutex_unlock(&clients_mutex);
}


void convertJpgToPng(unsigned char *jpg_buffer, int jpg_size, const char *png_filename) {
    struct jpeg_decompress_struct cinfo;
    struct jpeg_error_mgr jerr;

    FILE *outfile;
    JSAMPARRAY buffer;
    int row_stride;

    cinfo.err = jpeg_std_error(&jerr);

    jpeg_create_decompress(&cinfo);
    jpeg_mem_src(&cinfo, jpg_buffer, jpg_size);

    (void) jpeg_read_header(&cinfo, TRUE);
    (void) jpeg_start_decompress(&cinfo);

    row_stride = cinfo.output_width * cinfo.output_components;
    buffer = (*cinfo.mem->alloc_sarray)((j_common_ptr) &cinfo, JPOOL_IMAGE, row_stride, 1);

    outfile = fopen(png_filename, "wb");
    png_structp png_ptr = png_create_write_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
    png_infop info_ptr = png_create_info_struct(png_ptr);

    png_init_io(png_ptr, outfile);
    png_set_IHDR(png_ptr, info_ptr, cinfo.output_width, cinfo.output_height,
                 8, PNG_COLOR_TYPE_RGB, PNG_INTERLACE_NONE,
                 PNG_COMPRESSION_TYPE_BASE, PNG_FILTER_TYPE_BASE);

    png_write_info(png_ptr, info_ptr);

    while (cinfo.output_scanline < cinfo.output_height) {
        (void) jpeg_read_scanlines(&cinfo, buffer, 1);
        png_write_row(png_ptr, buffer[0]);
    }

    jpeg_finish_decompress(&cinfo);
    jpeg_destroy_decompress(&cinfo);
    png_write_end(png_ptr, NULL);
    png_destroy_write_struct(&png_ptr, &info_ptr);
    fclose(outfile);
    printf("Conversion to PNG completed. Output file: %s\n", png_filename);
}

void invertJpgColors(unsigned char *jpg_buffer, int jpg_size, const char *output_filename) {
    struct jpeg_decompress_struct cinfo;
    struct jpeg_compress_struct cinfo_out;
    struct jpeg_error_mgr jerr;

    FILE *outfile;
    JSAMPARRAY buffer;
    int row_stride;

    cinfo.err = jpeg_std_error(&jerr);
    jpeg_create_decompress(&cinfo);
    jpeg_mem_src(&cinfo, jpg_buffer, jpg_size);

    if (jpeg_read_header(&cinfo, TRUE) != JPEG_HEADER_OK) {
        fprintf(stderr, "Error reading JPEG header\n");
        return;
    }

    jpeg_start_decompress(&cinfo);
    row_stride = cinfo.output_width * cinfo.output_components;
    buffer = (*cinfo.mem->alloc_sarray)((j_common_ptr) &cinfo, JPOOL_IMAGE, row_stride, 1);

    cinfo_out.err = jpeg_std_error(&jerr);
    jpeg_create_compress(&cinfo_out);
    outfile = fopen(output_filename, "wb");
    if (outfile == NULL) {
        perror("Error opening output file");
        return;
    }
    jpeg_stdio_dest(&cinfo_out, outfile);

    cinfo_out.image_width = cinfo.image_width;
    cinfo_out.image_height = cinfo.image_height;
    cinfo_out.input_components = cinfo.num_components;
    cinfo_out.in_color_space = cinfo.out_color_space;
    jpeg_set_defaults(&cinfo_out);

    jpeg_start_compress(&cinfo_out, TRUE);

    while (cinfo.output_scanline < cinfo.output_height) {
        jpeg_read_scanlines(&cinfo, buffer, 1);
        for (int i = 0; i < row_stride; i++) {
            buffer[0][i] = 255 - buffer[0][i];
        }
        jpeg_write_scanlines(&cinfo_out, buffer, 1);
    }

    jpeg_finish_decompress(&cinfo);
    jpeg_destroy_decompress(&cinfo);
    jpeg_finish_compress(&cinfo_out);
    jpeg_destroy_compress(&cinfo_out);
    fclose(outfile);
    printf("Color inversion completed. Output file: %s\n", output_filename);
}

void convertJpgToWebp(unsigned char *jpg_buffer, int jpg_size, const char *webp_filename) {
    struct jpeg_decompress_struct cinfo;
    struct jpeg_error_mgr jerr;
    JSAMPARRAY buffer;
    int row_stride;

    cinfo.err = jpeg_std_error(&jerr);
    jpeg_create_decompress(&cinfo);
    jpeg_mem_src(&cinfo, jpg_buffer, jpg_size);

    if (jpeg_read_header(&cinfo, TRUE) != JPEG_HEADER_OK) {
        fprintf(stderr, "Error reading JPEG header\n");
        return;
    }

    jpeg_start_decompress(&cinfo);
    row_stride = cinfo.output_width * cinfo.output_components;
    buffer = (*cinfo.mem->alloc_sarray)((j_common_ptr) &cinfo, JPOOL_IMAGE, row_stride, 1);

    int width = cinfo.output_width;
    int height = cinfo.output_height;
    int stride = width * 3;
    uint8_t *rgb = (uint8_t *)malloc(stride * height);

    while (cinfo.output_scanline < cinfo.output_height) {
        jpeg_read_scanlines(&cinfo, buffer, 1);
        memcpy(rgb + (cinfo.output_scanline - 1) * stride, buffer[0], stride);
    }

    FILE *outfile = fopen(webp_filename, "wb");
    if (!outfile) {
        fprintf(stderr, "Error opening output file\n");
        return;
    }

    WebPPicture pic;
    WebPPictureInit(&pic);
    pic.width = width;
    pic.height = height;
    pic.use_argb = 0;

    if (!WebPPictureImportRGB(&pic, rgb, stride)) {
        fprintf(stderr, "Error importing RGB data to WebP picture\n");
        free(rgb);
        fclose(outfile);
        return;
    }

    WebPConfig config;
    WebPConfigInit(&config);
    config.quality = 75;

    WebPMemoryWriter writer;
    WebPMemoryWriterInit(&writer);
    pic.writer = WebPMemoryWrite;
    pic.custom_ptr = &writer;

    if (!WebPEncode(&config, &pic)) {
        fprintf(stderr, "Error encoding WebP\n");
        WebPPictureFree(&pic);
        free(rgb);
        fclose(outfile);
        return;
    }

    fwrite(writer.mem, writer.size, 1, outfile);
    WebPPictureFree(&pic);
    WebPMemoryWriterClear(&writer);
    free(rgb);
    fclose(outfile);
    printf("Conversion to WebP completed. Output file: %s\n", webp_filename);
}

void convertWebpToJpg(unsigned char *webp_buffer, int webp_size, const char *jpg_filename) {
    int width, height;
    uint8_t *rgb = WebPDecodeRGB(webp_buffer, webp_size, &width, &height);
    if (!rgb) {
        fprintf(stderr, "Error decoding WebP\n");
        return;
    }

    struct jpeg_compress_struct cinfo;
    struct jpeg_error_mgr jerr;
    cinfo.err = jpeg_std_error(&jerr);
    jpeg_create_compress(&cinfo);

    FILE *outfile = fopen(jpg_filename, "wb");
    if (!outfile) {
        fprintf(stderr, "Error opening output file\n");
        return;
    }

    jpeg_stdio_dest(&cinfo, outfile);
    cinfo.image_width = width;
    cinfo.image_height = height;
    cinfo.input_components = 3;
    cinfo.in_color_space = JCS_RGB;
    jpeg_set_defaults(&cinfo);
    jpeg_start_compress(&cinfo, TRUE);

    JSAMPROW row_pointer[1];
    int row_stride = width * 3;
    while (cinfo.next_scanline < cinfo.image_height) {
        row_pointer[0] = &rgb[cinfo.next_scanline * row_stride];
        jpeg_write_scanlines(&cinfo, row_pointer, 1);
    }

    jpeg_finish_compress(&cinfo);
    jpeg_destroy_compress(&cinfo);
    fclose(outfile);
    WebPFree(rgb);
    printf("Conversion to JPG completed. Output file: %s\n", jpg_filename);
}

int main() {
    int server_fd, new_socket;
    struct sockaddr_in address;
    int opt = 1;
    int addrlen = sizeof(address);
    char buffer[BUFFER_SIZE];
    int valread;

    if ((server_fd = socket(AF_INET, SOCK_STREAM, 0)) == 0) {
        perror("Socket failed");
        exit(EXIT_FAILURE);
    }

    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR | SO_REUSEPORT, &opt, sizeof(opt))) {
        perror("Setsockopt failed");
        exit(EXIT_FAILURE);
    }

    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(PORT);

    if (bind(server_fd, (struct sockaddr *)&address, sizeof(address)) < 0) {
        perror("Bind failed");
        exit(EXIT_FAILURE);
    }

    if (listen(server_fd, 3) < 0) {
        perror("Listen failed");
        exit(EXIT_FAILURE);
    }

    if ((new_socket = accept(server_fd, (struct sockaddr *)&address, (socklen_t *)&addrlen)) < 0) {
        perror("Accept failed");
        exit(EXIT_FAILURE);
    }

    int conversion = -1;
    unsigned char *image_data = NULL;
    size_t image_size = 0;

    while ((valread = read(new_socket, buffer, BUFFER_SIZE)) > 0) {
        if (conversion == -1) {
            buffer[valread] = '\0';
            if (strcmp(buffer, "convert_jpg_to_png") == 0) {
                conversion = 1;
                image_data = malloc(1);
                image_size = 0;
                continue;
            } else if (strcmp(buffer, "convert_jpg_to_webp") == 0) {
                conversion = 2;
                image_data = malloc(1);
                image_size = 0;
                continue;
            } else if (strcmp(buffer, "convert_webp_to_jpg") == 0) {
                conversion = 3;
                image_data = malloc(1);
                image_size = 0;
                continue;
            } else if (strcmp(buffer, "convert_jpg_to_inv") == 0) {
                conversion = 4;
                image_data = malloc(1);
                image_size = 0;
                continue;
            }
        } else if (conversion > 0) {
            if (strstr(buffer, END_OF_FILE_MARKER) != NULL) {
                size_t marker_pos = strstr(buffer, END_OF_FILE_MARKER) - buffer;
                image_data = realloc(image_data, image_size + marker_pos);
                memcpy(image_data + image_size, buffer, marker_pos);
                image_size += marker_pos;
                break;
            } else {
                image_data = realloc(image_data, image_size + valread);
                memcpy(image_data + image_size, buffer, valread);
                image_size += valread;
            }
        }
    }

    if (conversion == 1 && image_data != NULL) {
        convertJpgToPng(image_data, image_size, "output.png");
    } else if (conversion == 2 && image_data != NULL) {
        convertJpgToWebp(image_data, image_size, "output.webp");
    } else if (conversion == 3 && image_data != NULL) {
        convertWebpToJpg(image_data, image_size, "output.jpg");
    } else if (conversion == 4 && image_data != NULL) {
        invertJpgColors(image_data, image_size, "inverted.jpg");
    }

    if (image_data != NULL) {
        free(image_data);
    }
    while (1) {
        if ((new_socket = accept(server_fd, (struct sockaddr *)&address, (socklen_t *)&addrlen)) < 0) {
            perror("Accept failed");
            exit(EXIT_FAILURE);
        }
        ClientInfo *client = (ClientInfo *)malloc(sizeof(ClientInfo));
        client->socket = new_socket;
        client->address = address;
        add_client(new_socket, address);
    }
    close(new_socket);
    close(server_fd);
    return 0;
}

