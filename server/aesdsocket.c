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
#include <sys/stat.h>
#include <sys/file.h>
#include <stdbool.h>
#include <arpa/inet.h>

#define PORT 9000
#define BUFFER_SIZE 1024
#define FILE_PATH "/var/tmp/aesdsocketdata"

int sockfd = -1, client_sock = -1;
FILE *file_fp = NULL;
volatile sig_atomic_t exit_requested = 0;

void cleanup()
{
    if (client_sock != -1) {
        close(client_sock);
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

    while (!exit_requested) {
        client_sock = accept(sockfd, (struct sockaddr *)&client_addr, &client_len);
        if (client_sock == -1) {
            if (exit_requested) break;
            syslog(LOG_ERR, "Accept failed: %s", strerror(errno));
            continue;
        }

        char client_ip[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &(client_addr.sin_addr), client_ip, INET_ADDRSTRLEN);
        syslog(LOG_INFO, "Accepted connection from %s", client_ip);

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
            file_fp = fopen(FILE_PATH, "a+");
            if (!file_fp) {
                syslog(LOG_ERR, "File open failed: %s", strerror(errno));
                free(message_buf);
                continue;
            }

            flock(fileno(file_fp), LOCK_EX);
            fwrite(message_buf, sizeof(char), total_len, file_fp);
            fflush(file_fp);
            rewind(file_fp);

            char file_buf[BUFFER_SIZE];
            size_t read_bytes;
            while ((read_bytes = fread(file_buf, 1, sizeof(file_buf), file_fp)) > 0) {
                send(client_sock, file_buf, read_bytes, 0);
            }

            flock(fileno(file_fp), LOCK_UN);
            fclose(file_fp);
            file_fp = NULL;
            free(message_buf);
        }

        close(client_sock);
        client_sock = -1;
    }

    cleanup();
    return 0;
}
