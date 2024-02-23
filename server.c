#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <stdbool.h>

#define PORT 8082
#define MAX_CLIENTS 10
#define BUFFER_SIZE 1024
#define MESSAGE_FILE "messages.txt"

void store_message(int sender_uid, int receiver_uid, char *msg);
void send_stored_messages(int sockfd, int uid, int mode);

typedef struct {
    int sender_uid;
    int receiver_uid;
    char message[BUFFER_SIZE];
    long file_position;
} MessageInfo;

typedef struct {
    struct sockaddr_in address;
    int sockfd;
    int uid;
} client_t;


client_t *clients[MAX_CLIENTS];
pthread_mutex_t clients_mutex = PTHREAD_MUTEX_INITIALIZER;


//@brief: add contact to user's contact list
//@param: user_id: user's id
//@param: contact_id: contact's id
void add_contact(int user_id, int contact_id) {
    char filename[30];
    snprintf(filename, sizeof(filename), "contacts_%d.txt", user_id);

    FILE *file = fopen(filename, "a");
    if (file == NULL) {
        perror("Error opening contact file");
        return;
    }

    fprintf(file, "%d\n", contact_id);
    fclose(file);
}

//@brief: send contact list to user
//@param: sockfd: socket file descriptor
//@param: user_id: user's id
void send_contact_list(int sockfd, int user_id) {
    char contact_filename[30];
    snprintf(contact_filename, sizeof(contact_filename), "contacts_%d.txt", user_id);

    FILE *contact_file = fopen(contact_filename, "r");
    if (contact_file == NULL) {
        char *no_contacts = "No contacts found.";
        send(sockfd, no_contacts, strlen(no_contacts), 0);
        char *end_of_contacts = "END_OF_CONTACTS";
        send(sockfd, end_of_contacts, strlen(end_of_contacts), 0);
        return;
    }

    char line[10];
    char formatted_message[BUFFER_SIZE];
    while (fgets(line, sizeof(line), contact_file) != NULL) {
        int contact_id;
        sscanf(line, "%d", &contact_id);

        char user_filename[30];
        snprintf(user_filename, sizeof(user_filename), "user_%d.txt", contact_id);
        FILE *user_file = fopen(user_filename, "r");

        char name[100], phone[15];
        if (user_file) {
            // Read the name and phone number from the file
            if (fscanf(user_file, "%s %s", name, phone) == 2) {
                snprintf(formatted_message, BUFFER_SIZE, "Contact ID: %d, Name: %s, Phone: %s", contact_id, name, phone);
            } else {
                snprintf(formatted_message, BUFFER_SIZE, "Contact ID: %d, Unable to read user file", contact_id);
            }
            fclose(user_file);
        } else {
            snprintf(formatted_message, BUFFER_SIZE, "Contact ID: %d, User file not found", contact_id);
        }
        send(sockfd, formatted_message, strlen(formatted_message), 0);
        usleep(100000);  // Prevents messages from being concatenated
    }

    fclose(contact_file);

    char *end_of_contacts = "END_OF_CONTACTS";
    send(sockfd, end_of_contacts, strlen(end_of_contacts), 0);
}

//@brief: add client to clients array
//@param: cl: client to be added
void add_client(client_t *cl) {
    pthread_mutex_lock(&clients_mutex);

    for (int i = 0; i < MAX_CLIENTS; ++i) {
        if (!clients[i]) {
            clients[i] = cl;
            break;
        }
    }

    pthread_mutex_unlock(&clients_mutex);
}

//@brief: remove client from clients array
//@param: uid: client's id
void remove_client(int uid) {
    pthread_mutex_lock(&clients_mutex);

    for (int i = 0; i < MAX_CLIENTS; ++i) {
        if (clients[i]) {
            if (clients[i]->uid == uid) {
                clients[i] = NULL;
                break;
            }
        }
    }

    pthread_mutex_unlock(&clients_mutex);
}

//@brief: check if user file exists, if not create one
//@param: user_id: user's id
//@return: true if user file is created, false if user file already exists
bool check_and_create_user_file(int user_id) {
    char filename[30];
    sprintf(filename, "user_%d.txt", user_id);
    FILE *file = fopen(filename, "r");
    bool isNewFile = false;

    if (!file) {
        file = fopen(filename, "w");
        isNewFile = true;
    }
    if (file) {
        fclose(file);
    } else {
        perror("Error creating user file");
    }
    return isNewFile;
}

//@brief: handle client's request
//@param: arg: client to be handled
void *handle_client(void *arg) {
    client_t *cli = (client_t *)arg;
    char buffer[BUFFER_SIZE];
    int nbytes;

    // Receive the first message, which should be the user ID
    nbytes = recv(cli->sockfd, buffer, BUFFER_SIZE, 0);
    if (nbytes > 0) {
        buffer[nbytes] = '\0';
        if (strncmp(buffer, "SET_ID:", 7) == 0) {
            sscanf(buffer + 7, "%d", &cli->uid);
            if (check_and_create_user_file(cli->uid)) {
                char newUserPrompt[] = "NEW_USER:Please enter your name and phone number:";
                send(cli->sockfd, newUserPrompt, strlen(newUserPrompt), 0);

                // Wait for the client's response
                nbytes = recv(cli->sockfd, buffer, BUFFER_SIZE, 0);
                if (nbytes > 0) {
                    buffer[nbytes] = '\0';
                    // Save the received information into the user file
                    char filename[30];
                    sprintf(filename, "user_%d.txt", cli->uid);
                    FILE *file = fopen(filename, "w");
                    if (file) {
                        fputs(buffer, file);
                        fclose(file);
                    } else {
                        perror("Error writing to user file");
                    }
                }
            }
            else {
                char welcomeMessage[] = "WELCOME:Welcome back!";
                send(cli->sockfd, welcomeMessage, strlen(welcomeMessage), 0);
            }
        }
    }




    while ((nbytes = recv(cli->sockfd, buffer, BUFFER_SIZE, 0)) > 0) {
        buffer[nbytes] = '\0';


        if (strncmp(buffer, "ADD_CONTACT:", 12) == 0) {
            int contact_id;
            sscanf(buffer + 12, "%d", &contact_id);
            add_contact(cli->uid, contact_id);
        } else if (strcmp(buffer, "REQUEST_CONTACTS") == 0) {
            send_contact_list(cli->sockfd, cli->uid);
        }
        else if (strcmp(buffer, "CHECK_MESSAGES") == 0) {
            send_stored_messages(cli->sockfd, cli->uid, 0);
        }
        else if(strcmp(buffer, "CHECK_ALL_MESSAGES") == 0){
            send_stored_messages(cli->sockfd, cli->uid, 1);
        }
        else {
            int recipient_uid;
            char message[BUFFER_SIZE];
            sscanf(buffer, "%d:%[^\n]", &recipient_uid, message);
            store_message(cli->uid, recipient_uid, message); // Store sender and receiver UIDs
            printf("User %d sent a message to user %d: %s\n", cli->uid, recipient_uid, message);
        }
    }

    printf("User %d has disconnected.\n", cli->uid);
    close(cli->sockfd);
    remove_client(cli->uid);
    free(cli);
    pthread_detach(pthread_self());

    return NULL;
}


//@brief: store message in file
//@param: sender_uid: sender's id
//@param: receiver_uid: receiver's id
//@param: msg: message to be stored
void store_message_in_file(int sender_uid, int receiver_uid, const char *msg) {
    FILE *file = fopen(MESSAGE_FILE, "a");
    if (file == NULL) {
        perror("Error opening file");
        return;
    }
    // Store the message with sender and receiver UIDs and a '0' to indicate it hasn't been sent
    fprintf(file, "%d:%d:0:%s\n", sender_uid, receiver_uid, msg);
    fclose(file);
}

//@brief: send stored messages from file
//@param: sockfd: socket file descriptor
//@param: uid: user's id
//@param: mode: 0 for unread messages, 1 for all messages
void send_stored_messages_from_file(int sockfd, int uid, int mode) {
    FILE *file = fopen(MESSAGE_FILE, "r+");
    if (file == NULL) {
        perror("Error opening file");
        return;
    }

    MessageInfo messages[MAX_CLIENTS]; // Adjust size as needed
    int message_count = 0;
    char line[BUFFER_SIZE];

    while (fgets(line, sizeof(line), file) != NULL) {
        int sender_uid, receiver_uid, sent;
        char message[BUFFER_SIZE];
        sscanf(line, "%d:%d:%d:%[^\n]", &sender_uid, &receiver_uid, &sent, message);

        if ((receiver_uid == uid && !sent) || (receiver_uid == uid && mode == 1)) {
            messages[message_count].sender_uid = sender_uid;
            messages[message_count].receiver_uid = receiver_uid;
            strcpy(messages[message_count].message, message);
            messages[message_count].file_position = ftell(file) - strlen(line);
            message_count++;
        }
    }

    for (int i = 0; i < message_count; i++) {
        char sender_filename[30];
        snprintf(sender_filename, sizeof(sender_filename), "user_%d.txt", messages[i].sender_uid);
        FILE *sender_file = fopen(sender_filename, "r");

        char name[100], phone[15];
        if (sender_file) {
            if (fscanf(sender_file, "%s %s", name, phone) == 2) {
                fclose(sender_file);

                // Calculate the required buffer length
                int required_length = snprintf(NULL, 0, "From %s (%s): %s", name, phone, messages[i].message) + 1;

                // Dynamically allocate memory
                char *formatted_message = (char *)malloc(required_length * sizeof(char));
                if (formatted_message != NULL) {
                    snprintf(formatted_message, required_length, "From %s (%s): %s", name, phone, messages[i].message);
                    send(sockfd, formatted_message, strlen(formatted_message), 0);
                    usleep(100000); // Prevents messages from being concatenated
                    free(formatted_message);
                } else {
                    perror("Memory allocation failed");
                }
            } else {
                perror("Error reading user file");
                fclose(sender_file);
            }
        } else {
            perror("Sender file not found");
        }
    }

    fseek(file, 0, SEEK_SET);
    for (int i = 0; i < message_count; i++) {
        fseek(file, messages[i].file_position, SEEK_SET);
        fprintf(file, "%d:%d:1:%s\n", messages[i].sender_uid, messages[i].receiver_uid, messages[i].message);
    }

    fclose(file);

    char *end_of_messages = "END_OF_MESSAGES";
    send(sockfd, end_of_messages, strlen(end_of_messages), 0);
}


void store_message(int sender_uid, int receiver_uid, char *msg) {
    store_message_in_file(sender_uid, receiver_uid, msg);
}

void send_stored_messages(int sockfd, int uid, int mode) {
    send_stored_messages_from_file(sockfd, uid, mode);
}

int main() {
    int sockfd, new_sock;
    struct sockaddr_in server_addr, client_addr;
    pthread_t tid;

    sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd == -1) {
        perror("Socket creation failed");
        exit(EXIT_FAILURE);
    }

    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(PORT);

    if (bind(sockfd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        perror("Socket bind failed");
        exit(EXIT_FAILURE);
    }

    if (listen(sockfd, 10) < 0) {
        perror("Socket listen failed");
        exit(EXIT_FAILURE);
    }

    printf("Server started on port %d...\n", PORT);

    int uid = 0;
    while (1) {
        socklen_t clilen = sizeof(client_addr);
        new_sock = accept(sockfd, (struct sockaddr *)&client_addr, &clilen);

        if (new_sock < 0) {
            perror("Accept failed");
            continue;
        }

        // Client settings
        client_t *cli = (client_t *)malloc(sizeof(client_t));
        cli->address = client_addr;
        cli->sockfd = new_sock;
        cli->uid = -1;

        add_client(cli);
        pthread_create(&tid, NULL, &handle_client, (void*)cli);

    }

    return 0;
}
