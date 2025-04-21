#define _GNU_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <signal.h>
#include <fcntl.h>
#include <errno.h>
#include <time.h>
#include <sys/un.h>
#include <sys/stat.h>
#include <sys/file.h>
#include <stdbool.h>
#include <arpa/inet.h>
#include <pthread.h>
#include "queue.h"

#define PORT 9000
#define BUFFER_SIZE 1024
#define FILE_PATH "/var/tmp/aesdsocketdata"

int sockfd = -1;
FILE *file_fp = NULL;
volatile sig_atomic_t exit_requested = 0;
pthread_mutex_t file_mutex = PTHREAD_MUTEX_INITIALIZER;

typedef struct {
    int client_sock;
    volatile sig_atomic_t finished;
} thread_arg_t;

typedef struct slist_data_s slist_data_t;

struct slist_data_s {
    thread_arg_t *thread_arg;
    pthread_t thread_id;
    SLIST_ENTRY(slist_data_s) entries;
};

SLIST_HEAD(slisthead, slist_data_s) head;

void log_info(const char *message) {
    printf("[INFO] %s\n", message);
}

void log_error(const char *message) {
    printf("[ERROR] %s\n", message);
}

void add_node(struct slisthead *head, thread_arg_t *thread_arg, pthread_t thread_id) {
    log_info("Adding new node to thread list");
    slist_data_t *new_node = malloc(sizeof(slist_data_t));
    if (!new_node) {
        log_error("Memory allocation failed for new node");
        return;
    }
    new_node->thread_arg = thread_arg;
    new_node->thread_id = thread_id;
    SLIST_INSERT_HEAD(head, new_node, entries);
    log_info("New node added successfully");
}

void remove_node(struct slisthead *head, slist_data_t *node) {
    log_info("Removing node from thread list");
    SLIST_REMOVE(head, node, slist_data_s, entries);
    free(node->thread_arg);
    free(node);
    log_info("Node removed successfully");
}

void *connector(void *arg) {
    log_info("Connection thread started");
    thread_arg_t *thread_arg = (thread_arg_t *)arg;
    int client_sock = thread_arg->client_sock;

    char recv_buf[BUFFER_SIZE] = {0};
    char *message_buf = NULL;
    size_t total_len = 0;

    ssize_t bytes_received;
    while ((bytes_received = recv(client_sock, recv_buf, sizeof(recv_buf), 0)) > 0) {
        char *newline_ptr = memchr(recv_buf, '\n', bytes_received);
        size_t chunk_len = bytes_received;
        if (newline_ptr != NULL) {
            chunk_len = newline_ptr - recv_buf + 1;
        }

        char *temp_buf = realloc(message_buf, total_len + chunk_len + 1);
        if (!temp_buf) {
            log_error("Memory allocation failed in connector thread");
            free(message_buf);
            break;
        }
        message_buf = temp_buf;
        memcpy(message_buf + total_len, recv_buf, chunk_len);
        total_len += chunk_len;
        message_buf[total_len] = '\0';

        if (newline_ptr) break;
    }

    if (message_buf && total_len > 0) {
        log_info("Writing received data to file");
        pthread_mutex_lock(&file_mutex);
        fwrite(message_buf, sizeof(char), total_len, file_fp);
        fflush(file_fp);
        rewind(file_fp);

        char file_buf[BUFFER_SIZE];
        size_t read_bytes;
        log_info("Sending file contents back to client");
        while ((read_bytes = fread(file_buf, 1, sizeof(file_buf), file_fp)) > 0) {
            send(client_sock, file_buf, read_bytes, 0);
        }
        pthread_mutex_unlock(&file_mutex);
        free(message_buf);
    }
    thread_arg->finished = 1;
    close(client_sock);
    log_info("Connection thread completed");
    return NULL;
}

void *timestamp_thread(void *arg) {
    log_info("Timestamp thread started");
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);

    while (!exit_requested) {
        ts.tv_sec += 10;

        clock_nanosleep(CLOCK_REALTIME, TIMER_ABSTIME, &ts, NULL);

        if (exit_requested)
            break;

        time_t now = time(NULL);
        struct tm *tm_info = localtime(&now);

        char timestamp[128];
        strftime(timestamp, sizeof(timestamp), "timestamp: %Y-%m-%d %H:%M:%S\n", tm_info);

        pthread_mutex_lock(&file_mutex);
        log_info("Writing timestamp to file");
        fwrite(timestamp, sizeof(char), strlen(timestamp), file_fp);
        fflush(file_fp);
        pthread_mutex_unlock(&file_mutex);
    }

    log_info("Timestamp thread exiting");
    return NULL;
}

pthread_t timestamp_thread_id;

void start_timestamp_thread() {
    log_info("Starting timestamp thread");
    if (pthread_create(&timestamp_thread_id, NULL, timestamp_thread, NULL) != 0) {
        log_error("Failed to create timestamp thread");
        exit(EXIT_FAILURE);
    }
    log_info("Timestamp thread started successfully");
}

void cleanup() {
    log_info("Starting cleanup process");
    
    slist_data_t *slist_data = NULL;
    slist_data_t *tmp = NULL;
    SLIST_FOREACH_SAFE(slist_data, &head, entries, tmp) {
        log_info("Joining and removing connection thread");
        pthread_join(slist_data->thread_id, NULL);
        remove_node(&head, slist_data);
    }

    if (timestamp_thread_id) {
        log_info("Joining timestamp thread");
        pthread_join(timestamp_thread_id, NULL);
    }

    if (sockfd != -1) {
        log_info("Closing server socket");
        shutdown(sockfd, SHUT_RDWR); 
        close(sockfd);
    }
    if (file_fp) {
        log_info("Closing data file");
        fclose(file_fp);
    }
    
    log_info("Removing data file");
    unlink(FILE_PATH);
    
    log_info("Cleanup completed");
}

void signal_handler(int signo) {
    printf("Received signal: %d\n", signo);
    if (signo == SIGINT || signo == SIGTERM) {
        log_info("Exit requested by signal");
        exit_requested = 1;
    }
}

void daemonize() {
    log_info("Starting daemonization process");
    pid_t pid = fork();
    if (pid < 0) {
        log_error("First fork failed");
        exit(EXIT_FAILURE);
    } else if (pid > 0) {
        log_info("Parent process exiting");
        exit(EXIT_SUCCESS);
    }

    if (setsid() == -1) {
        log_error("setsid failed");
        exit(EXIT_FAILURE);
    }

    pid = fork();
    if (pid < 0) {
        log_error("Second fork failed");
        exit(EXIT_FAILURE);
    } else if (pid > 0) {
        log_info("Grandparent process exiting");
        exit(EXIT_SUCCESS);
    }

    umask(0);
    chdir("/");

    close(STDIN_FILENO);
    close(STDOUT_FILENO);
    close(STDERR_FILENO);
    log_info("Daemonization completed");
}

void setup_signal_handlers() {
    log_info("Setting up signal handlers");
    struct sigaction sa = {0};
    sa.sa_handler = signal_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;

    if (sigaction(SIGINT, &sa, NULL) != 0 ||
        sigaction(SIGTERM, &sa, NULL) != 0) {
        log_error("Failed to set up signal handlers");
        exit(EXIT_FAILURE);
    }
    log_info("Signal handlers set up successfully");
}

int main(int argc, char *argv[]) {
    // exit(EXIT_SUCCESS);
    printf("Starting aesdsocket application\n");
    slist_data_t *slist_data = NULL, *tmp = NULL;
    SLIST_INIT(&head);

    bool is_daemon = false;
    if (argc == 2 && strcmp(argv[1], "-d") == 0) {
        log_info("Running in daemon mode");
        is_daemon = true;
    }

    setup_signal_handlers();
    if (is_daemon) {
        daemonize();
    }

    struct sockaddr_in serv_addr = {0};
    struct sockaddr_in client_addr = {0};
    socklen_t client_len = sizeof(client_addr);

    log_info("Creating server socket");
    sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd == -1) {
        log_error("Socket creation failed");
        cleanup();
        exit(EXIT_FAILURE);
    }

    int optval = 1;
    // Set socket options
    if (setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(optval)) != 0) {
        log_error("setsockopt(SO_REUSEADDR) failed");
        cleanup();
        exit(EXIT_FAILURE);
    }
    // setsockopt(sockfd, SOL_SOCKET, SO_REUSEPORT, &optval, sizeof(optval));

    
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_addr.s_addr = INADDR_ANY;
    serv_addr.sin_port = htons(PORT);

    log_info("Binding socket to port");
    int bind_attempts = 0;
    while (bind(sockfd, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) != 0) {
        if (bind_attempts >= 5) {
            log_error("Bind failed after multiple attempts");
            cleanup();
            exit(EXIT_FAILURE);
        }
        log_info("Bind failed, retrying...");
        sleep(10);
        bind_attempts++;
    }

    log_info("Starting to listen on socket");
    if (listen(sockfd, 10) != 0) {
        log_error("Listen failed");
        cleanup();
        exit(EXIT_FAILURE);
    }

    log_info("Opening data file");
    file_fp = fopen(FILE_PATH, "a+");
    if (!file_fp) {
        log_error("File open failed");
        cleanup();
        exit(EXIT_FAILURE);
    }

    start_timestamp_thread();
    log_info("Server ready to accept connections");

    while (!exit_requested) {
        log_info("Waiting for client connection");
        int client_sock = accept(sockfd, (struct sockaddr *)&client_addr, &client_len);
        if (client_sock == -1) {
            if (exit_requested) break;
            log_error("Accept failed");
            continue;
        }

        char client_ip[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &(client_addr.sin_addr), client_ip, INET_ADDRSTRLEN);
        printf("Accepted connection from %s\n", client_ip);

        pthread_t thread_id;
        thread_arg_t *thread_arg = malloc(sizeof(thread_arg_t));
        if (!thread_arg) {
            log_error("Memory allocation failed for thread argument");
            close(client_sock);
            continue;
        }
        thread_arg->client_sock = client_sock;
        thread_arg->finished = 0;

        log_info("Creating connection thread");
        if (pthread_create(&thread_id, NULL, connector, thread_arg) != 0) {
            log_error("Thread creation failed");
            free(thread_arg);
            close(client_sock);
            continue;
        }

        add_node(&head, thread_arg, thread_id);
        
        SLIST_FOREACH_SAFE(slist_data, &head, entries, tmp) {
            if (slist_data->thread_arg->finished) {
                log_info("Cleaning up finished thread");
                pthread_join(slist_data->thread_id, NULL);
                remove_node(&head, slist_data);
            }
        }
    }

    cleanup();
    printf("aesdsocket application exiting\n");
    return 0;
}