/*
Creator : Sayan Mahapatra
Date : 06-02-2022
Description : In this file implementation of common functionality shared
between client and server is given
*/

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include <time.h>

#include "constants.hpp"

int send_signal(int dest, char signal[MAX_SIGNAL_LEN]) {
    char buff[MAX_SIGNAL_LEN];
    memcpy(buff, signal, MAX_SIGNAL_LEN);

    int bs = send(dest, buff, 10, 0);
    while (bs < MAX_SIGNAL_LEN) {
        if (bs < 0) return ERR_SENDING_SIGNAL;
        bs += send(dest, buff + bs, MAX_SIGNAL_LEN - bs, 0);
    }

    return SUCCESS;
}

// Receive with timeout
int recv_t(int socket, char *buffer, int nbytes, int flgs, struct timeval *tv) {
    fd_set f;
    FD_ZERO(&f);
    FD_SET(socket, &f);

    int ret = select(socket + 1, &f, NULL, NULL, tv);
    if (ret == 0) return ERR_TIMEOUT;
    else if (ret < 0)
        return ret;
    else {
        return recv(socket, buffer, nbytes, flgs);
    }
}

int wait_for_signal(int src, char dest[MAX_SIGNAL_LEN], int *timeout) {
    bzero(dest, MAX_SIGNAL_LEN);
    struct timeval *tv;
    if (timeout != NULL) {
        tv->tv_sec = *timeout;
        tv->tv_usec = 0;
    } else {
        tv = NULL;
    }

    int br = recv_t(src, dest, MAX_SIGNAL_LEN, 0, tv);
    while (br < MAX_SIGNAL_LEN) {
        if (br <= 0) return br;
        br += recv_t(src, dest + br, MAX_SIGNAL_LEN - br, 0, tv);
    }

    return SUCCESS;
}

// Sends size bytes from buff in one go
int send_all_bytes(int dest_soc, char *buff, int size) {
    int bytes_written;
    do {
        bytes_written = send(dest_soc, buff, size, 0);
        buff += bytes_written;
        size -= bytes_written;

        // Check for completion
        if (size == 0) return SUCCESS;

    } while (bytes_written < size && bytes_written >= 0);

    // Some error occured
    return ERR_TX;
}

// Writes size bytes from buff in one go to dest fd
int write_all_bytes(int dest, char *buff, int size) {
    int bytes_written;
    do {
        bytes_written = write(dest, buff, size);
        buff += bytes_written;
        size -= bytes_written;

        // Check for completion
        if (size == 0) return SUCCESS;

    } while (bytes_written < size && bytes_written >= 0);

    // Some error occured
    return ERR_TX;
}

int get_file_size(char *filename) {
    FILE *fp = fopen(filename, "r");
    if (!fp) return -1;
    fseek(fp, 0L, SEEK_END);
    return ftell(fp);
}

/* Sends file atomically with handshaking. Timeout = NULL => Blocking*/
int send_file(int dest_soc, char *filename, int *timeout) {
    char buffer[BUFFSIZE];
    bzero(buffer, BUFFSIZE);
    char cbuff[BUFFSIZE];
    bzero(cbuff, BUFFSIZE);

    int ret;
    if ((ret = send_signal(dest_soc, ACK_SIGNAL)) < 0) {
        fprintf(stderr, "Unable to begin sending file: %d\n", ret);
        return ERR_FILE_NOT_SENT;
    }

    if ((ret == wait_for_signal(dest_soc, cbuff, timeout)) < 0 ||
        strncmp(cbuff, ACK_SIGNAL, strlen(ACK_SIGNAL)) != 0) {
        fprintf(stderr, "Didnot receive handshake: %d\n", ret);
        return ERR_FILE_NOT_SENT;
    }

    // Handshaking done begin file transmission
    int fd = open(filename, O_RDONLY);
    if (fd < 0) {
        perror("ERROR Opening file");
        return ERR_FILE_ERROR;
    }

    // First send filesize
    int size = get_file_size(filename);
    int size_n = htonl(size);
    send_all_bytes(dest_soc, (char *)&size_n, 4);

    int bytes_read = 0;
    // Start sending the file
    while (bytes_read < size) {
        int br = read(fd, buffer, BUFFSIZE);
        if (br == 0) break;
        if (br < 0) {
            close(fd);
            perror("ERROR reading from file");
            return ERR_FILE_NOT_SENT;
        }

        if (send_all_bytes(dest_soc, buffer, br) < 0) return ERR_FILE_NOT_SENT;
        bytes_read += br;
    }

    if (bytes_read != size) {
        fprintf(stderr, "Could only send %d of %d bytes of file %s\n", bytes_read, size, filename);
        return ERR_FILE_NOT_SENT;
    }

    if ((ret = send_signal(dest_soc, ACK_SIGNAL)) < 0) {
        fprintf(stderr, "Unable to send end of file: %d\n", ret);
        return ERR_FILE_NOT_SENT;
    }

    if ((ret == wait_for_signal(dest_soc, cbuff, timeout)) < 0 ||
        strncmp(cbuff, ACK_SIGNAL, strlen(ACK_SIGNAL)) != 0) {
        fprintf(stderr, "Didnot receive handshake: %d\n", ret);
        return ERR_FILE_NOT_SENT;
    }

    // printf("Sent %d bytes in total\n", bytes_read);
    return SUCCESS;
}

/* Received file atomically with debugging */
int receive_file(int src, char *filename, int *timeout) {
    char buffer[BUFFSIZE];
    bzero(buffer, BUFFSIZE);
    char cbuff[MAX_COMMAND_LEN];
    bzero(cbuff, MAX_COMMAND_LEN);

    int ret;
    if ((ret = wait_for_signal(src, cbuff, timeout)) < 0 ||
        strncmp(cbuff, ACK_SIGNAL, strlen(ACK_SIGNAL)) != 0) {
        fprintf(stderr, "Error waiting for handshake: %d\n", ret);
        return ERR_FILE_NOT_READ;
    }

    if ((ret = send_signal(src, ACK_SIGNAL)) < 0) {
        fprintf(stderr, "Error sending handshake: %d\n", ret);
        return ERR_FILE_NOT_READ;
    }

    // First 4 bytes will be file size
    int size, br = 0;
    do {
        int b = recv(src, &size, 4 - br, 0);
        if (b <= 0) return ERR_FILE_NOT_READ;
        br += b;
    } while (br < 4);

    int size_h = ntohl(size);

    int fd = open(filename, O_WRONLY | O_CREAT, 0777);
    if (fd < 0) {
        perror("ERROR Opening file");
        return ERR_FILE_ERROR;
    }

    int bytes_read = 0;
    while (bytes_read < size_h) {
        int to_read = (size_h - bytes_read) > BUFFSIZE ? BUFFSIZE : (size_h - bytes_read);
        int b = recv(src, buffer, to_read, 0);
        if (b <= 0) break;
        bytes_read += b;

        if (write_all_bytes(fd, buffer, b) < 0) {
            fprintf(stderr, "Failed to write bytes to receiving file\n");
            return ERR_FILE_NOT_READ;
        }
    }

    if (bytes_read != size_h) {
        fprintf(stderr, "Failed to read full file\n");
        return ERR_FILE_NOT_READ;
    }

    if ((ret = wait_for_signal(src, cbuff, timeout)) < 0 ||
        strncmp(cbuff, ACK_SIGNAL, strlen(ACK_SIGNAL)) != 0) {
        fprintf(stderr, "Error waiting for handshake: %d\n", ret);
        return ERR_FILE_NOT_READ;
    }

    if ((ret = send_signal(src, ACK_SIGNAL)) < 0) {
        fprintf(stderr, "Error sending handshake: %d\n", ret);
        return ERR_FILE_NOT_READ;
    }

    // printf("Received %d bytes in total\n", bytes_read);
    return SUCCESS;
}

int get_lines_in_file(char filename[FILE_NAME_LEN]) {
    FILE *fp = fopen(filename, "r");
    if (!fp) return ERR_FILE_ERROR;
    char linebuff[LINESIZE];
    int c = 0;
    while (fgets(linebuff, LINESIZE, fp) != NULL) {
        c++;
    }
    fclose(fp);
    return c;
}

// Prints file on screen. skiplines = NULL => print all lines
void print_file(char *filename, int *skiplines) {
    FILE *fp = fopen(filename, "r");
    if (!fp) {
        printf("%s not found", filename);
        return;
    }

    char linebuf[LINESIZE];
    int c;
    if (skiplines) c = *skiplines;
    while (fgets(linebuf, LINESIZE, fp) != NULL) {
        if (skiplines != NULL && c > 0) c--;
        else
            fputs(linebuf, stdout);
    }

    fclose(fp);
}

int check_file_present(char *filename) {
    FILE *fp = fopen(filename, "r");
    if (fp != NULL) {
        fclose(fp);
        return SUCCESS;
    }
    return ERR_FILE_NOT_FOUND;
}

/* Returns name for a temporary file*/
int get_temp_file(char *prefix, char filename[TEMP_FILE_LEN]) {
    bzero(filename, TEMP_FILE_LEN);
    int p = getpid();
    int t = time(NULL);
    int r = rand() % 100000;
    if (prefix) return sprintf(filename, "%s/%d_%d_%d.tmp", prefix, p, t, r);
    else
        return sprintf(filename, "%d_%d.tmp", p, t);
}

void remove_temp_file(char fname[TEMP_FILE_LEN]) {
    if (fname != NULL) {
        remove(fname);
    }
}

// Remove newline character from end of string if present
void strip_newline(char *s) {
    if (s[strlen(s) - 1] == '\n') s[strlen(s) - 1] = '\0';
}

/* Add newline character to s if not present */
void make_line_terminated(char *s) {
    if (s[strlen(s) - 1] != '\n') {
        strcat(s, "\n");
    }
}
