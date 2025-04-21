#define _GNU_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <signal.h>
#include <syslog.h>
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

typedef struct{
    int client_sock;
    volatile sig_atomic_t finished;
}thread_arg_t;

typedef struct slist_data_s slist_data_t;

struct slist_data_s {
    thread_arg_t *thread_arg;
    pthread_t thread_id;
    SLIST_ENTRY(slist_data_s) entries;
};

SLIST_HEAD(slisthead, slist_data_s) head;

// a function that add a new node to the list
void add_node(struct slisthead *head, thread_arg_t *thread_arg,pthread_t thread_id)
{
    slist_data_t *new_node = malloc(sizeof(slist_data_t));
    if (!new_node) {
        syslog(LOG_ERR, "Memory allocation failed");
        return;
    }
    new_node->thread_arg = thread_arg;
    new_node->thread_id = thread_id;
    SLIST_INSERT_HEAD(head, new_node, entries);
}
// a function that remove a node from the list
void remove_node(struct slisthead *head, slist_data_t *node)
{
    SLIST_REMOVE(head, node, slist_data_s, entries);
    free(node->thread_arg);
    free(node);
}
void *connector(void *arg)
{
    thread_arg_t *thread_arg = (thread_arg_t *)arg;
    int client_sock = thread_arg->client_sock;
    // free(thread_arg); // argument is freed in the process

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
            syslog(LOG_ERR, "Memory allocation failed");
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
        pthread_mutex_lock(&file_mutex);
        fwrite(message_buf, sizeof(char), total_len, file_fp);
        fflush(file_fp);
        rewind(file_fp);

        char file_buf[BUFFER_SIZE];
        size_t read_bytes;
        while ((read_bytes = fread(file_buf, 1, sizeof(file_buf), file_fp)) > 0) {
            send(client_sock, file_buf, read_bytes, 0);
        }
        pthread_mutex_unlock(&file_mutex);
        free(message_buf);
    }
    thread_arg->finished = 1;
    close(client_sock);
    return NULL;
}
// we need a thread that writes “timestamp: time ” where time is specified by the RFC 2822 compliant strftime format , followed by newline. 
// The timestamp should be written to the file every 10 seconds.
// where the string includes the year, month, day, hour (in 24 hour format) minute and second representing the system wall clock time.
void *timestamp_thread(void *arg) {
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);  // Initialize with current wall time

    while (!exit_requested) {
        // Schedule the next 10-second timestamp
        ts.tv_sec += 10;

        // Sleep until the next absolute 10s point
        clock_nanosleep(CLOCK_REALTIME, TIMER_ABSTIME, &ts, NULL);

        if (exit_requested)
            break;

        // Generate timestamp string
        time_t now = time(NULL);
        struct tm *tm_info = localtime(&now);

        char timestamp[128];
        strftime(timestamp, sizeof(timestamp), "timestamp: %Y-%m-%d %H:%M:%S\n", tm_info);

        // Write to shared file with mutex protection
        pthread_mutex_lock(&file_mutex);
        fwrite(timestamp, sizeof(char), strlen(timestamp), file_fp);
        fflush(file_fp);  // Ensure it's flushed immediately
        pthread_mutex_unlock(&file_mutex);
    }

    return NULL;
}
pthread_t timestamp_thread_id;
void start_timestamp_thread() {
    if (pthread_create(&timestamp_thread_id, NULL, timestamp_thread, NULL) != 0) {
        syslog(LOG_ERR, "Failed to create timestamp thread: %s", strerror(errno));
        exit(EXIT_FAILURE);
    }
}
void cleanup()
{   
    // join all threads
    slist_data_t *slist_data = NULL;
    slist_data_t *tmp = NULL;
    SLIST_FOREACH_SAFE(slist_data, &head, entries,tmp) {
        pthread_join(slist_data->thread_id, NULL);
        remove_node(&head, slist_data);
    }
    // join the timestamp thread
    if (timestamp_thread_id) {
        pthread_join(timestamp_thread_id, NULL);
    }

    if (sockfd != -1) {
        close(sockfd);
    }
    if (file_fp) {
        fclose(file_fp);
    }
    unlink(FILE_PATH);
    syslog(LOG_INFO, "Cleaned up and exiting");
    closelog();
}

void signal_handler(int signo)
{
    if (signo == SIGINT || signo == SIGTERM) {
        exit_requested = 1;
    }
}

void daemonize()
{
    pid_t pid = fork();
    if (pid < 0) {
        perror("fork failed");
        exit(EXIT_FAILURE);
    } else if (pid > 0) {
        exit(EXIT_SUCCESS);  // parent exits
    }

    if (setsid() == -1) {
        perror("setsid failed");
        exit(EXIT_FAILURE);
    }

    pid = fork();
    if (pid < 0) {
        perror("second fork failed");
        exit(EXIT_FAILURE);
    } else if (pid > 0) {
        exit(EXIT_SUCCESS);  // grandparent exits
    }

    umask(0);
    chdir("/");

    close(STDIN_FILENO);
    close(STDOUT_FILENO);
    close(STDERR_FILENO);
}

void setup_signal_handlers()
{
    struct sigaction sa = {0};
    sa.sa_handler = signal_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;

    if (sigaction(SIGINT, &sa, NULL) != 0 ||
        sigaction(SIGTERM, &sa, NULL) != 0) {
        perror("sigaction failed");
        exit(EXIT_FAILURE);
    }
}

int main(int argc, char *argv[])
{
    slist_data_t *slist_data=NULL,*tmp = NULL;
    SLIST_INIT(&head);

    bool is_daemon = false;
    if (argc == 2 && strcmp(argv[1], "-d") == 0) {
        is_daemon = true;
    }

    openlog("aesdsocket", LOG_PID | LOG_CONS, LOG_USER);
    setup_signal_handlers();
    if (is_daemon) {
        daemonize();
    }

    struct sockaddr_in serv_addr = {0};
    struct sockaddr_in client_addr = {0};
    socklen_t client_len = sizeof(client_addr);

    sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd == -1) {
        syslog(LOG_ERR, "Socket creation failed: %s", strerror(errno));
        cleanup();
        exit(EXIT_FAILURE);
    }

    int optval = 1;
    setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(optval));

    serv_addr.sin_family = AF_INET;
    serv_addr.sin_addr.s_addr = INADDR_ANY;
    serv_addr.sin_port = htons(PORT);

    if (bind(sockfd, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) != 0) {
        syslog(LOG_ERR, "Bind failed: %s", strerror(errno));
        cleanup();
        exit(EXIT_FAILURE);
    }

    if (listen(sockfd, 10) != 0) {
        syslog(LOG_ERR, "Listen failed: %s", strerror(errno));
        cleanup();
        exit(EXIT_FAILURE);
    }
    file_fp = fopen(FILE_PATH, "a+");
    if (!file_fp) {
        syslog(LOG_ERR, "File open failed: %s", strerror(errno));
        cleanup();
        exit(EXIT_FAILURE);
    }
    start_timestamp_thread();
    while (!exit_requested) {
        int client_sock = accept(sockfd, (struct sockaddr *)&client_addr, &client_len);
        if (client_sock == -1) {
            if (exit_requested) break;
            syslog(LOG_ERR, "Accept failed: %s", strerror(errno));
            continue;
        }

        char client_ip[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &(client_addr.sin_addr), client_ip, INET_ADDRSTRLEN);
        syslog(LOG_INFO, "Accepted connection from %s", client_ip);

        pthread_t thread_id;
        thread_arg_t *thread_arg = malloc(sizeof(thread_arg_t));
        if (!thread_arg) {
            syslog(LOG_ERR, "Memory allocation failed");
            close(client_sock);
            continue;
        }
        thread_arg->client_sock = client_sock;
        thread_arg->finished = 0;
        if (pthread_create(&thread_id, NULL, connector, thread_arg) != 0) {
            syslog(LOG_ERR, "Thread creation failed: %s", strerror(errno));
            free(thread_arg);
            close(client_sock);
            continue;
        }
        // Add the thread to the list
        add_node(&head, thread_arg, thread_id);
        SLIST_FOREACH_SAFE(slist_data, &head, entries,tmp) {
            // Check if the thread has finished
            if (slist_data->thread_arg->finished) {
                pthread_join(slist_data->thread_id, NULL);
                remove_node(&head, slist_data);
            }
        }
    }

    cleanup();
    return 0;
}
