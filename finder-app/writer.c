#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <errno.h>
#include <syslog.h>
// Function to create a directory if it doesn't exist
int create_directory(const char *path) {
    struct stat st = {0};

    if (stat(path, &st) == -1) {
        if (mkdir(path, 0700) == -1) {
            if (errno != EEXIST) {
                perror("mkdir");
                return -1;
            }
        }
    }
    return 0;
}

// Function to create all directories in a path
int create_directories(const char *path) {
    char *dir = strdup(path);
    char *p;
    struct stat st = {0};

    // Split the path and create each directory
    for (p = dir + 1; *p; p++) {
        if (*p == '/') {
            *p = 0;
            if (stat(dir, &st) == -1) {
                if (mkdir(dir, 0700) == -1) {
                    if (errno != EEXIST) {
                        perror("mkdir");
                        free(dir);
                        return -1;
                    }
                }
            }
            *p = '/';
        }
    }

    // Create the last directory
    if (stat(dir, &st) == -1) {
        if (mkdir(dir, 0700) == -1) {
            if (errno != EEXIST) {
                perror("mkdir");
                free(dir);
                return -1;
            }
        }
    }

    free(dir);
    return 0;
}

// Function to create a file and ensure its directory exists
int create_file(const char *file_path,const char *data) {
    // Extract the directory part of the file path
    char *dir = strdup(file_path);
    char *last_slash = strrchr(dir, '/');
    if (last_slash) {
        *last_slash = 0;
        if (create_directories(dir) == -1) {
            free(dir);
            return -1;
        }
    }
    free(dir);

    // Create the file
    FILE *fptr = fopen(file_path, "wb");
    if (fptr == NULL) {
        perror("fopen");
        return -1;
    }
    fprintf(fptr,data);
    fclose(fptr);

    return 0;
}

int main(int argc,char *argv[]) {
    openlog("writer", LOG_PID | LOG_CONS, LOG_USER);
    if(argc < 3) {
    	syslog(LOG_ERR,"Number of parameters is not 2");
	return 1;
    }
    syslog(LOG_DEBUG,"Writing %s to %s",argv[2],argv[1]);
    const char *file_path = argv[1];
    if (create_file(file_path,argv[2]) == 0) {
        printf("File created successfully.\n");
    } else {
        printf("Failed to create file.\n");
    }

    return 0;
}
