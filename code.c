#include <stdio.h>
#include <string.h> // for strlen
#include <stdlib.h>
#include <sys/socket.h>
#include <arpa/inet.h> // for inet_addr
#include <unistd.h>    // for write
#include <pthread.h>   // for threading, link with lpthread
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <semaphore.h>

#define PATH "/home/ozgur/workspace/assignment2/sources" // Last char of this PATH cannot be "/", please mind this. (PATH connot finish "/")
                                                        // For example PATH cannot be "/home/ozgur/workspace/assignment2/sources/"
#define PORT_NO 8888
#define BUFFER_SIZE 1024
#define CONNECTION_NUMBER 10

int thread_count = 0; // Keeps the number of the threads working simultaneously.
sem_t mutex; // To control thread_counter.

void jpeg_handler(int socket, char *file_name) // handle jpeg files
{
    char *buffer;
    char *full_path = (char *)malloc((strlen(PATH) + strlen(file_name)) * sizeof(char));
    int fp;

    strcpy(full_path, PATH); // Merge the file name that requested and path of the root folder
    strcat(full_path, file_name);
    puts(full_path);

    if ((fp=open(full_path, O_RDONLY)) > 0) //FILE FOUND
    {
        puts("Image Found.");
        int bytes;
        char buffer[BUFFER_SIZE];

        send(socket, "HTTP/1.0 200 OK\r\nContent-Type: image/jpeg\r\n\r\n", 45, 0);
	    while ( (bytes=read(fp, buffer, BUFFER_SIZE))>0 ) // Read the file to buffer. If not the end of the file, then continue reading the file
			write (socket, buffer, bytes); // Send the part of the jpeg to client.
    }
    else // If there is not such a file.
    {
        write(socket, "HTTP/1.0 404 Not Found\r\nConnection: close\r\nContent-Type: text/html\r\n\r\n<!doctype html><html><body>404 File Not Found</body></html>", strlen("HTTP/1.0 404 Not Found\r\nConnection: close\r\nContent-Type: text/html\r\n\r\n<!doctype html><html><body>404 File Not Found</body></html>"));
    }

    free(full_path);
    close(fp);
}

void html_handler(int socket, char *file_name) // handle html files
{
    char *buffer;
    char *full_path = (char *)malloc((strlen(PATH) + strlen(file_name)) * sizeof(char));
    FILE *fp;

    strcpy(full_path, PATH); // Merge the file name that requested and path of the root folder
    strcat(full_path, file_name);

    fp = fopen(full_path, "r");
    if (fp != NULL) //FILE FOUND
    {
        puts("File Found.");

        fseek(fp, 0, SEEK_END); // Find the file size.
        long bytes_read = ftell(fp);
        fseek(fp, 0, SEEK_SET);

        send(socket, "HTTP/1.0 200 OK\r\nContent-Type: text/html\r\n\r\n", 44, 0); // Send the header for succesful respond.
        buffer = (char *)malloc(bytes_read * sizeof(char)); 
        
        fread(buffer, bytes_read, 1, fp); // Read the html file to buffer.
        write (socket, buffer, bytes_read); // Send the content of the html file to client.
        free(buffer);
        
        fclose(fp);
    }
    else // If there is not such a file.
    {
        write(socket, "HTTP/1.0 404 Not Found\r\nConnection: close\r\nContent-Type: text/html\r\n\r\n<!doctype html><html><body>404 File Not Found</body></html>", strlen("HTTP/1.0 404 Not Found\r\nConnection: close\r\nContent-Type: text/html\r\n\r\n<!doctype html><html><body>404 File Not Found</body></html>"));
    }

    free(full_path);
}

void *connection_handler(void *socket_desc)
{
    int request;
    char client_reply[BUFFER_SIZE], *request_lines[3];
    char *file_name;
    char *extension;

    // Get the socket descriptor.
    int sock = *((int *)socket_desc);

    // Get the request.
    request = recv(sock, client_reply, BUFFER_SIZE, 0);

    sem_wait(&mutex);
    thread_count++; 

    if(thread_count > 10) // If there is 10 request at the same time, other request will be refused.
    {
        char *message = "HTTP/1.0 400 Bad Request\r\nContent-Type: text/html\r\n\r\n<!doctype html><html><body>System is busy right now.</body></html>";
        write(sock, message, strlen(message));
        thread_count--; 
        sem_post(&mutex);
        free(socket_desc);
        shutdown(sock, SHUT_RDWR);
        close(sock);
        sock = -1;
        pthread_exit(NULL);
    }
    sem_post(&mutex);

    if (request < 0) // Receive failed.
    {
        puts("Recv failed");
    }
    else if (request == 0) // receive socket closed. Client disconnected upexpectedly.
    {
        puts("Client disconnected upexpectedly.");
    }
    else // Message received.
    {
        printf("%s", client_reply);
        request_lines[0] = strtok(client_reply, " \t\n");
        if (strncmp(request_lines[0], "GET\0", 4) == 0)
        {
            // Parsing the request header.
            request_lines[1] = strtok(NULL, " \t");
            request_lines[2] = strtok(NULL, " \t\n");

            if (strncmp(request_lines[2], "HTTP/1.0", 8) != 0 && strncmp(request_lines[2], "HTTP/1.1", 8) != 0) // Bad request if not HTTP 1.0 or 1.1
            {
                char *message = "HTTP/1.0 400 Bad Request\r\nConnection: close\r\nContent-Type: text/html\r\n\r\n<!doctype html><html><body>400 Bad Request</body></html>";
                write(sock, message, strlen(message));
            }
            else
            {
                char *tokens[2]; // For parsing the file name and extension.

                file_name = (char *)malloc(strlen(request_lines[1]) * sizeof(char));
                strcpy(file_name, request_lines[1]);
                puts(file_name);

                // Getting the file name and extension
                tokens[0] = strtok(file_name, ".");
                tokens[1] = strtok(NULL, "."); 

                if(strcmp(tokens[0], "/favicon") == 0 && strcmp(tokens[1], "ico")) // Discard the favicon.ico requests.
                {
                    sem_wait(&mutex);
                    thread_count--;
                    sem_post(&mutex);
                    free(socket_desc);
                    shutdown(sock, SHUT_RDWR);
                    close(sock);
                    sock = -1;
                    pthread_exit(NULL);
                }
                else if (tokens[0] == NULL || tokens[1] == NULL) // If there is not an extension in request or request to just localhost:8888/
                {
                    char *message = "HTTP/1.0 400 Bad Request\r\nConnection: close\r\n\r\n<!doctype html><html><body>400 Bad Request. (You need to request to jpeg and html files)</body></html>";
                    write(sock, message, strlen(message));
                }
                else
                {

                    if (strcmp(tokens[1], "html") != 0 && strcmp(tokens[1], "jpeg") != 0) // If the request is not to html or jpeg files, it will be respond 400 Bad Request
                    {
                        char *message = "HTTP/1.0 400 Bad Request\r\nConnection: close\r\n\r\n<!doctype html><html><body>400 Bad Request. Not Supported File Type (Suppoerted File Types: html and jpeg)</body></html>";
                        write(sock, message, strlen(message));
                    }
                    else
                    {
                        if (strcmp(tokens[1], "html") == 0)
                        {
                            sem_wait(&mutex); // Prevent two or more thread do some IO operation same time.
                            html_handler(sock, request_lines[1]);
                            sem_post(&mutex);
                        }
                        else if (strcmp(tokens[1], "jpeg") == 0)
                        {
                            sem_wait(&mutex); // Prevent two or more thread do some IO operation same time
                            jpeg_handler(sock, request_lines[1]);
                            sem_post(&mutex);
                        }
                    }
                    free(extension);
                }
                free(file_name);
            }
        }
    }

    //sleep(50); // If you want to see just 10 thread is working simultaneously, you can sleep here.
    // After send 10 request, 11th request will be responded as "System is busy right now".
    
    free(socket_desc);
    shutdown(sock, SHUT_RDWR);
    close(sock);
    sock = -1;
    sem_wait(&mutex);
    thread_count--;
    sem_post(&mutex);
    pthread_exit(NULL);
}

int main(int argc, char *argv[])
{
    sem_init(&mutex, 0, 1); // Inıtialize the mutex from 1.
    int socket_desc, new_socket, c, *new_sock;
    struct sockaddr_in server, client;

    socket_desc = socket(AF_INET, SOCK_STREAM, 0);
    if (socket_desc == -1)
    {
        puts("Could not create socket");
        return 1;
    }

    server.sin_family = AF_INET;
    server.sin_addr.s_addr = INADDR_ANY;
    server.sin_port = htons(PORT_NO);

    if (bind(socket_desc, (struct sockaddr *)&server, sizeof(server)) < 0)
    {
        puts("Binding failed");
        return 1;
    }

    listen(socket_desc, 20);

    puts("Waiting for incoming connections...");
    c = sizeof(struct sockaddr_in);
    while ((new_socket = accept(socket_desc, (struct sockaddr *)&client, (socklen_t *)&c))) // Accept the connection.
    {
        puts("Connection accepted \n");

        pthread_t sniffer_thread;
        new_sock = malloc(1);
        *new_sock = new_socket;

        if (pthread_create(&sniffer_thread, NULL, connection_handler, (void *)new_sock) < 0) // Create a thread for each request.
        {
            puts("Could not create thread");
            return 1;
        }   
    }

    return 0;
}
