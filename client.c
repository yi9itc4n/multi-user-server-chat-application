#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <pthread.h>
#include <errno.h>

#define SERVER_PORT 8082
#define BUFFER_SIZE 1024

int user_id;

//@brief: add contact to the contact list
//@param: sockfd: socket file descriptor
void add_contact(int sockfd) {
    int contact_id;
    printf("Enter the UserID of the contact to add: ");
    scanf("%d", &contact_id);
    getchar(); // Consume newline character after scanf

    char buffer[BUFFER_SIZE];
    snprintf(buffer, BUFFER_SIZE, "ADD_CONTACT:%d", contact_id);
    send(sockfd, buffer, strlen(buffer), 0);
}

//@brief: request contact list from the server
//@param: sockfd: socket file descriptor
void request_contacts(int sockfd) {
    char buffer[BUFFER_SIZE] = "REQUEST_CONTACTS";
    send(sockfd, buffer, strlen(buffer), 0);

    printf("Contact List:\n");
    int loop=0;
    while (loop<30) {
        int nbytes = recv(sockfd, buffer, BUFFER_SIZE, 0);
        if (nbytes > 0) {
            buffer[nbytes] = '\0';
            if (strcmp(buffer, "END_OF_CONTACTS") == 0) {
                printf("\nEnd of contact list.\n");
                break; // Break out of the loop when end of contacts is reached
            }
            printf("%s\n", buffer);
        } else {
            if (nbytes < 0 && errno != EWOULDBLOCK) {
                perror("recv failed");
                break;
            }
        }
        loop++;
    }
}


//@brief: request all messages from the server
//@param: sockfd: socket file descriptor
void request_all_messages(int sockfd) {
    char buffer[BUFFER_SIZE];

    // Send a special command to request all messages
    strcpy(buffer, "CHECK_ALL_MESSAGES");
    send(sockfd, buffer, strlen(buffer), 0);

    // Wait and print incoming messages
    printf("All messages:\n");
    while (1) {
        int nbytes = recv(sockfd, buffer, BUFFER_SIZE, 0);
        if (nbytes > 0) {
            buffer[nbytes] = '\0';
            if (strcmp(buffer, "END_OF_MESSAGES") == 0) {
                break; // End of messages marker received, exit loop
            }
            printf("%s\n", buffer);
        } else {
            if (nbytes < 0 && errno != EWOULDBLOCK) {
                perror("recv failed");
                break;
            }
        }
    }
}

//@brief: send message to the server
//@param: sockfd: socket file descriptor
void send_message(int sockfd) {
    char buffer[BUFFER_SIZE];
    printf("Enter the UserID of the receiver and the message (format: UserID:Message): ");
    fgets(buffer, BUFFER_SIZE, stdin);
    buffer[strcspn(buffer, "\n")] = 0; // Remove newline
    send(sockfd, buffer, strlen(buffer), 0);
}

//@brief: check messages from the server
//@param: sockfd: socket file descriptor
void check_messages(int sockfd) {
    char buffer[BUFFER_SIZE];

    // Send a special command to check messages
    strcpy(buffer, "CHECK_MESSAGES");
    send(sockfd, buffer, strlen(buffer), 0);

    // Wait and print incoming messages
    while (1) {
        int nbytes = recv(sockfd, buffer, BUFFER_SIZE, 0);
        if (nbytes > 0) {
            buffer[nbytes] = '\0';
            if (strcmp(buffer, "END_OF_MESSAGES") == 0) {
                break; // End of messages marker received, exit loop
            }
            printf("%s\n", buffer);
        } else {
            break; // No more messages or error
        }
    }
}

//@brief: check messages from the server in the background
//@param: arg: socket file descriptor
void *check_messages_background(void *arg) {
    int sockfd = *(int *)arg;
    char buffer[BUFFER_SIZE];

    while (1) {
        // Send a special command to check messages
        strcpy(buffer, "CHECK_MESSAGES");
        send(sockfd, buffer, strlen(buffer), 0);

        // Wait and print incoming messages
        while (1) {
            int nbytes = recv(sockfd, buffer, BUFFER_SIZE, 0);
            if (nbytes > 0) {
                buffer[nbytes] = '\0';
                if (strcmp(buffer, "END_OF_MESSAGES") == 0) {
                    break; // End of messages marker received, exit loop
                }
                printf("\n\n\n\n\n\n\n\n\n\n\n\n\n");
                printf("\n*******************************************************        New Messages        *******************************************************\n%s\n\n", buffer);
                printf("\n1. Send Message\n2. Show All My Messages\n3. Add Contact\n4. Request Contacts\n5. Exit\n");
                printf("Enter your choice: ");
            } else {
                break; // No more messages or error
            }
        }
        sleep(2); // Adjust the sleep time as needed
    }
    return NULL;
}

//@brief: send user id to the server
//@param: sockfd: socket file descriptor
void send_ID(int sockfd) {
    char buffer[BUFFER_SIZE];

    // Correctly format the SET_ID message
    snprintf(buffer, BUFFER_SIZE, "SET_ID:%d", user_id);
    send(sockfd, buffer, strlen(buffer), 0);

    // Check for server response
    int nbytes = recv(sockfd, buffer, BUFFER_SIZE, 0);
    if (nbytes > 0) {
        buffer[nbytes] = '\0';
        if (strncmp(buffer, "NEW_USER:", 9) == 0) {
            printf("%s\n", buffer + 9); // Print the prompt message
            fgets(buffer, BUFFER_SIZE, stdin);
            buffer[strcspn(buffer, "\n")] = 0; // Remove newline
            send(sockfd, buffer, strlen(buffer), 0);
        }
    }
}



int main() {

    printf("enter user id:");
    scanf("%d",&user_id);
    getchar();

    int sockfd;
    struct sockaddr_in server_addr;

    // Create a socket
    sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0) {
        perror("Socket creation failed");
        exit(EXIT_FAILURE);
    }

    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(SERVER_PORT);
    server_addr.sin_addr.s_addr = inet_addr("127.0.0.1");

    // Connect to the server
    if (connect(sockfd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        perror("Connection to the server failed");
        exit(EXIT_FAILURE);
    }
    send_ID(sockfd);
    printf("Connected to server.\n");


    // Start the background message checking thread
    pthread_t msg_thread;
    if (pthread_create(&msg_thread, NULL, check_messages_background, (void *)&sockfd) != 0) {
        perror("Failed to create message checking thread");
        exit(EXIT_FAILURE);
    }

    int choice;
    while (1) {
        printf("\n1. Send Message\n2. Show All My Messages\n3. Create New Contact\n4. Show Contact List\n5. Exit\n");
        printf("Enter your choice: ");
        scanf("%d", &choice);
        getchar(); // Consume newline character after scanf

        pthread_t msg_thread;
        if (pthread_create(&msg_thread, NULL, check_messages_background, (void *)&sockfd) != 0) {
            perror("Failed to create message checking thread");
            exit(EXIT_FAILURE);
        }

        switch (choice) {
            case 1:
                pthread_cancel(msg_thread);
                send_message(sockfd);
                break;
            case 2:
                pthread_cancel(msg_thread);
                request_all_messages(sockfd);
                break;
            case 3:
                pthread_cancel(msg_thread);
                add_contact(sockfd);
                break;
            case 4:
                pthread_cancel(msg_thread);
                request_contacts(sockfd);
                break;
            case 5:
                pthread_cancel(msg_thread);
                close(sockfd);
                return 0;
            default:
                printf("Invalid choice. Please try again.\n");
        }
    }

    return 0;
}
