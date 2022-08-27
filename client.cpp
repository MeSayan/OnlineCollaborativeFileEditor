/*
Name : Assignment 3 (Client)
Creator : Sayan Mahapatra 
Date : 06-02-2022
Description : In this file implementation of client is given
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

#include "common.cpp"

/*
----------------------------------------------------------------------------------------------------
                    FUNCTIONS TO PREPARE REQUEST FILES TO BE SENT TO SERVER
----------------------------------------------------------------------------------------------------
*/

int prepare_register_file(char filename[TEMP_FILE_LEN]) {
    FILE *fp = fopen(filename, "w");
    fprintf(fp, "COMMAND=REGISTER\n");
    fclose(fp);
    return 0;
}

int prepare_exit_file(char filename[TEMP_FILE_LEN], int cid) {
    FILE *fp = fopen(filename, "w");
    fprintf(fp, "COMMAND=EXIT\n");
    fprintf(fp, "ID=%d\n", cid);
    fclose(fp);
    return 0;
}

int prepare_invite_file(char tmpfn[TEMP_FILE_LEN], int scid, int tcid, char filename[FILE_NAME_LEN],
                        char perm, char *resp) {
    FILE *tfp = fopen(tmpfn, "w");
    if (tfp != NULL) {
        fprintf(tfp, "COMMAND=INVITE\n");
        fprintf(tfp, "FROM=%d\n", scid);
        fprintf(tfp, "TO=%d\n", tcid);
        fprintf(tfp, "FILE=%s\n", filename);
        fprintf(tfp, "PERMISSION=%c\n", perm);
        if (resp != NULL) fprintf(tfp, "CLIENT_RESPONSE=%c\n", *resp);
        fclose(tfp);
        return SUCCESS;
    }
    return ERR_FILE_ERROR;
}

int prepare_upload_file(char tmpfn[TEMP_FILE_LEN], char *filename, int cid) {
    FILE *fp = fopen(tmpfn, "w");
    FILE *ifp = fopen(filename, "r");

    if (!fp) {
        fprintf(stderr, "Failed to open temp file\n");
        return ERR_FILE_ERROR;
    }

    if (!ifp) {
        fprintf(stderr, "Failed to open input file\n");
        return ERR_FILE_ERROR;
    }

    fprintf(fp, "COMMAND=UPLOAD\n");
    fprintf(fp, "ID=%d\n", cid);
    fprintf(fp, "NAME=%s\n", filename);
    char linebuff[LINESIZE];
    while ((fgets(linebuff, LINESIZE, ifp)) != NULL) {
        make_line_terminated(linebuff);
        fprintf(fp, "%s", linebuff);
    }

    fclose(fp);
    fclose(ifp);

    return SUCCESS;
}

int prepare_download_file(char tmpfn[TEMP_FILE_LEN], char filename[FILE_NAME_LEN], int cid) {
    FILE *fp = fopen(tmpfn, "w");
    if (!fp) return ERR_FILE_ERROR;
    fprintf(fp, "COMMAND=DOWNLOAD\n");
    fprintf(fp, "ID=%d\n", cid);
    fprintf(fp, "NAME=%s\n", filename);
    fclose(fp);
    return SUCCESS;
}

int prepare_read_file(char tmpfn[TEMP_FILE_LEN], char filename[FILE_NAME_LEN], int cid, int *sindex,
                      int *eindex) {
    FILE *fp = fopen(tmpfn, "w");
    if (!fp) return ERR_FILE_ERROR;
    fprintf(fp, "COMMAND=READ\n");
    fprintf(fp, "ID=%d\n", cid);
    fprintf(fp, "NAME=%s\n", filename);
    if (sindex != NULL) {
        fprintf(fp, "STARTINDX=%d\n", *sindex);
    } else {
        fprintf(fp, "STARTINDX=\n");
    }
    if (eindex != NULL) {
        fprintf(fp, "ENDINDX=%d\n", *eindex);
    } else {
        fprintf(fp, "ENDINDX=\n");
    }
    fclose(fp);
    return SUCCESS;
}

int prepare_delete_file(char tmpfn[TEMP_FILE_LEN], char filename[FILE_NAME_LEN], int cid,
                        int *sindex, int *eindex) {
    FILE *fp = fopen(tmpfn, "w");
    if (!fp) return ERR_FILE_ERROR;
    fprintf(fp, "COMMAND=DELETE\n");
    fprintf(fp, "ID=%d\n", cid);
    fprintf(fp, "NAME=%s\n", filename);
    if (sindex != NULL) {
        fprintf(fp, "STARTINDX=%d\n", *sindex);
    } else {
        fprintf(fp, "STARTINDX=\n");
    }
    if (eindex != NULL) {
        fprintf(fp, "ENDINDX=%d\n", *eindex);
    } else {
        fprintf(fp, "ENDINDX=\n");
    }
    fclose(fp);
    return SUCCESS;
}

int prepare_insert_file(char tmpfn[TEMP_FILE_LEN], char filename[FILE_NAME_LEN], int cid, int *idx,
                        char msg[LINESIZE]) {
    FILE *fp = fopen(tmpfn, "w");
    if (!fp) return ERR_FILE_ERROR;
    fprintf(fp, "COMMAND=INSERT\n");
    fprintf(fp, "ID=%d\n", cid);
    fprintf(fp, "NAME=%s\n", filename);
    if (idx != NULL) {
        fprintf(fp, "INDX=%d\n", *idx);
    } else {
        fprintf(fp, "INDX=\n");
    }
    fprintf(fp, "MSG=\"%s\"\n", msg);
    fclose(fp);
    return SUCCESS;
}

int prepare_users_file(char tmpfn[TEMP_FILE_LEN], int cid) {
    FILE *fp = fopen(tmpfn, "w");
    if (!fp) return ERR_FILE_ERROR;
    fprintf(fp, "COMMAND=USERS\n");
    fprintf(fp, "ID=%d\n", cid);
    fclose(fp);
    return SUCCESS;
}

int prepare_files_file(char tmpfn[TEMP_FILE_LEN], int cid) {
    FILE *fp = fopen(tmpfn, "w");
    if (!fp) return ERR_FILE_ERROR;
    fprintf(fp, "COMMAND=FILES\n");
    fprintf(fp, "ID=%d\n", cid);
    fclose(fp);
    return SUCCESS;
}

int exit_client(int sockfd, int mycid) {
    char fname[TEMP_FILE_LEN];

    send_signal(sockfd, PING_SIGNAL);
    get_temp_file(CLIENT_DIRECTORY, fname);
    prepare_exit_file(fname, mycid);
    if (send_file(sockfd, fname, NULL) < 0) {
        fprintf(stderr, "Error sending request to server\n");
        remove_temp_file(fname);
        return ERR_SENDING_SIGNAL;
    }
    remove_temp_file(fname);

    // get_temp_file(CLIENT_DIRECTORY, fname);
    // if (receive_file(sockfd, fname, NULL) < 0) {
    //     fprintf(stderr, "Error receiving response from server\n");
    //     remove_temp_file(fname);
    //     return ERR_FILE_NOT_READ;
    // }

    // print_file(fname, NULL);
    // remove_temp_file(fname);
    return SUCCESS;
}

int upload_file(int sockfd, int mycid, char *fname) {
    char tmpfn[TEMP_FILE_LEN];

    // Upload file
    send_signal(sockfd, PING_SIGNAL);
    get_temp_file(CLIENT_DIRECTORY, tmpfn);
    prepare_upload_file(tmpfn, fname, mycid);
    if (send_file(sockfd, tmpfn, NULL) < 0) {
        fprintf(stderr, "Failed to request to client\n");
        remove_temp_file(tmpfn);
        return ERR_SENDING_SIGNAL;
    }

    remove_temp_file(tmpfn);
    get_temp_file(CLIENT_DIRECTORY, tmpfn);
    if (receive_file(sockfd, tmpfn, NULL) < 0) {
        fprintf(stderr, "Failed to receive request from server\n");
        remove_temp_file(tmpfn);
        return ERR_FILE_NOT_READ;
    }

    print_file(tmpfn, NULL);
    remove_temp_file(tmpfn);
    return SUCCESS;
}

// Writes to downloaded file (dfname) (if successful) to fname
int download_file(int sockfd, int mycid, char dfname[FILE_NAME_LEN], char fname[FILE_NAME_LEN]) {
    char tmpfn[TEMP_FILE_LEN];

    // Download request
    send_signal(sockfd, PING_SIGNAL);
    get_temp_file(CLIENT_DIRECTORY, tmpfn);
    prepare_download_file(tmpfn, dfname, mycid);
    if (send_file(sockfd, tmpfn, NULL) < 0) {
        fprintf(stderr, "Failed to request to client\n");
        remove_temp_file(tmpfn);
        return ERR_SENDING_SIGNAL;
    }

    // Wait for response
    remove_temp_file(tmpfn);
    get_temp_file(CLIENT_DIRECTORY, tmpfn);
    if (receive_file(sockfd, tmpfn, NULL) < 0) {
        fprintf(stderr, "Failed to receive request from server\n");
        remove_temp_file(tmpfn);
        return ERR_FILE_NOT_READ;
    }

    print_file(tmpfn, NULL);
    remove_temp_file(tmpfn);
    return SUCCESS;
}

// Writes to downloaded file (dfname) (if successful) to fname (TODO)
int read_file(int sockfd, int mycid, char rfname[FILE_NAME_LEN], int *sindex, int *eindex) {
    char tmpfn[TEMP_FILE_LEN];

    // Read request
    send_signal(sockfd, PING_SIGNAL);
    get_temp_file(CLIENT_DIRECTORY, tmpfn);
    prepare_read_file(tmpfn, rfname, mycid, sindex, eindex);
    if (send_file(sockfd, tmpfn, NULL) < 0) {
        fprintf(stderr, "Failed to send request to client\n");
        remove_temp_file(tmpfn);
        return ERR_SENDING_SIGNAL;
    }

    // Wait for response
    remove_temp_file(tmpfn);
    get_temp_file(CLIENT_DIRECTORY, tmpfn);
    if (receive_file(sockfd, tmpfn, NULL) < 0) {
        fprintf(stderr, "Failed to receive request from server\n");
        remove_temp_file(tmpfn);
        return ERR_FILE_NOT_READ;
    }

    int c = 2;
    print_file(tmpfn, NULL);
    remove_temp_file(tmpfn);
    return SUCCESS;
}

// Writes to downloaded file (dfname) to terminal (TODO)
int delete_file(int sockfd, int mycid, char rfname[FILE_NAME_LEN], int *sindex, int *eindex) {
    char tmpfn[TEMP_FILE_LEN];

    // Download request
    send_signal(sockfd, PING_SIGNAL);
    get_temp_file(CLIENT_DIRECTORY, tmpfn);
    prepare_delete_file(tmpfn, rfname, mycid, sindex, eindex);
    if (send_file(sockfd, tmpfn, NULL) < 0) {
        fprintf(stderr, "Failed to send request to client\n");
        remove_temp_file(tmpfn);
        return ERR_SENDING_SIGNAL;
    }

    // Wait for response
    remove_temp_file(tmpfn);
    get_temp_file(CLIENT_DIRECTORY, tmpfn);
    if (receive_file(sockfd, tmpfn, NULL) < 0) {
        fprintf(stderr, "Failed to receive request from server\n");
        remove_temp_file(tmpfn);
        return ERR_FILE_NOT_READ;
    }

    int c = 2;
    print_file(tmpfn, NULL);
    remove_temp_file(tmpfn);
    return SUCCESS;
}

// Writes to downloaded file (dfname) to terminal (TODO)
int insert_file(int sockfd, int mycid, char ifname[FILE_NAME_LEN], int *sindex,
                char msg[LINESIZE]) {
    char tmpfn[TEMP_FILE_LEN];

    // Send  request
    send_signal(sockfd, PING_SIGNAL);
    get_temp_file(CLIENT_DIRECTORY, tmpfn);
    prepare_insert_file(tmpfn, ifname, mycid, sindex, msg);
    if (send_file(sockfd, tmpfn, NULL) < 0) {
        fprintf(stderr, "Failed to send request to client\n");
        remove_temp_file(tmpfn);
        return ERR_SENDING_SIGNAL;
    }

    // Wait for response
    remove_temp_file(tmpfn);
    get_temp_file(CLIENT_DIRECTORY, tmpfn);
    if (receive_file(sockfd, tmpfn, NULL) < 0) {
        fprintf(stderr, "Failed to receive request from server\n");
        remove_temp_file(tmpfn);
        return ERR_FILE_NOT_READ;
    }

    int c = 2;
    print_file(tmpfn, NULL);
    remove_temp_file(tmpfn);
    return SUCCESS;
}

int get_users(int sockfd, int mycid) {
    char tmpfn[TEMP_FILE_LEN];
    send_signal(sockfd, PING_SIGNAL);
    get_temp_file(CLIENT_DIRECTORY, tmpfn);
    prepare_users_file(tmpfn, mycid);
    if (send_file(sockfd, tmpfn, NULL) < 0) {
        fprintf(stderr, "Failed to send request to client\n");
        remove_temp_file(tmpfn);
        return ERR_SENDING_SIGNAL;
    }

    // Wait for response
    remove_temp_file(tmpfn);

    get_temp_file(CLIENT_DIRECTORY, tmpfn);
    if (receive_file(sockfd, tmpfn, NULL) < 0) {
        fprintf(stderr, "Failed to receive request from server\n");
        remove_temp_file(tmpfn);
        return ERR_FILE_NOT_READ;
    }

    int c = 2;
    print_file(tmpfn, NULL);
    remove_temp_file(tmpfn);
    return SUCCESS;
}

int get_files(int sockfd, int mycid) {
    char tmpfn[TEMP_FILE_LEN];
    send_signal(sockfd, PING_SIGNAL);
    get_temp_file(CLIENT_DIRECTORY, tmpfn);
    prepare_files_file(tmpfn, mycid);
    if (send_file(sockfd, tmpfn, NULL) < 0) {
        fprintf(stderr, "Failed to send request to client\n");
        remove_temp_file(tmpfn);
        return ERR_SENDING_SIGNAL;
    }

    // Wait for response
    remove_temp_file(tmpfn);
    get_temp_file(CLIENT_DIRECTORY, tmpfn);
    if (receive_file(sockfd, tmpfn, NULL) < 0) {
        fprintf(stderr, "Failed to receive request from server\n");
        remove_temp_file(tmpfn);
        return ERR_FILE_NOT_READ;
    }

    int c = 2;
    print_file(tmpfn, NULL);
    remove_temp_file(tmpfn);
    return SUCCESS;
}

int register_client(int sockfd) {
    char tmpfn[TEMP_FILE_LEN];
    send_signal(sockfd, PING_SIGNAL);

    get_temp_file(CLIENT_DIRECTORY, tmpfn);
    printf("Temp file: %s\n", tmpfn);
    prepare_register_file(tmpfn);
    if (send_file(sockfd, tmpfn, NULL) < 0) {
        remove_temp_file(tmpfn);
        fprintf(stderr, "Failed to send file\n");
        return ERR_SENDING_SIGNAL;
    }
    remove_temp_file(tmpfn);

    // Get response
    get_temp_file(CLIENT_DIRECTORY, tmpfn);
    if (receive_file(sockfd, tmpfn, NULL) < 0) {
        return ERR_FILE_NOT_READ;
    }

    print_file(tmpfn, NULL);

    char linebuf[LINESIZE];
    FILE *fp = fopen(tmpfn, "r");
    fgets(linebuf, LINESIZE, fp);
    if (strcmp(linebuf, "COMMAND=REGISTER\n") != 0) {
        return ERR_CLIENT_NOT_REGISTERED;
    }
    fgets(linebuf, LINESIZE, fp);
    fgets(linebuf, LINESIZE, fp);
    int mycid, fs;
    fs = sscanf(linebuf, "MESSAGE=%d registered\n", &mycid);
    remove_temp_file(tmpfn);

    if (fs == 1 && (mycid >= 10000 && mycid <= 99999)) return mycid;
    else
        return ERR_INVALID_FILE;
}

void invalid_command(char *err) {
    printf("\nINVALID COMMAND\n");
    if (err) printf("%s", err);
    printf("Usage:\n");
    printf("\t/users\n");
    printf("\t/files\n");
    printf("\t/upload <filename>\n");
    printf("\t/download <filename>\n");
    printf("\t/invite <filename> <start_idx> <end_idx> <permission>\n");
    printf("\t/read <filename> <start_idx> <end_idx>\n");
    printf("\t/insert <filename> <idx> \"<message>\"\n");
    printf("\t/delete <filename> <start_idx> <end_idx>\n");
    printf("\t/exit\n\n");
    printf("Permission can E (editor), V (viewer)\n\n");
}

void test_suite(int sockfd) {
    int mycid = register_client(sockfd);
    if (mycid < 0) {
        fprintf(stderr, "Error registering client");
        exit(1);
    }

    printf("==== Users Test ====\n");
    get_users(sockfd, mycid);

    printf("=====Upload tests======\n");

    int ret = upload_file(sockfd, mycid, (char *)"file_1.txt");
    printf("Upload Status: %d\n", ret);

    ret = upload_file(sockfd, mycid, (char *)"file_2.txt");
    printf("Upload Status: %d\n", ret);

    ret = upload_file(sockfd, mycid, (char *)"file_3.txt");
    printf("Upload Status: %d\n", ret);

    printf("=====Download tests======\n");
    ret = download_file(sockfd, mycid, (char *)"file_1.txt", NULL);
    printf("Download Status: %d\n", ret);

    // Doesnt exist
    ret = download_file(sockfd, mycid, (char *)"file_1_2.txt", NULL);
    printf("Download Status: %d\n", ret);

    printf("=====Read tests======\n");
    int s, e;
    s = -8;
    e = -1;

    ret = read_file(sockfd, mycid, (char *)"file_1.txt", NULL, NULL);
    printf("Read Status: %d\n", ret);

    printf("=====Delete tests======\n");
    s = -8;
    e = -5;

    ret = delete_file(sockfd, mycid, (char *)"file_1.txt", &s, &e);
    printf("Delete Status: %d\n", ret);

    printf("=====Insert tests======\n");
    // Insert those 4 deleted lines back
    int i = 0;
    insert_file(sockfd, mycid, (char *)"file_1.txt", &i,
                (char *)"Sapiente minima omnis reiciendis ipsa.");
    i = -4;
    insert_file(sockfd, mycid, (char *)"file_1.txt", &i,
                (char *)"Mollitia sint assumenda nihil et exercitationem ut fugit quam. ");
    i = 2;
    insert_file(sockfd, mycid, (char *)"file_1.txt", &i,
                (char *)"Omnis commodi temporibus earum accusamus rerum mollitia.");
    i = -4;
    insert_file(sockfd, mycid, (char *)"file_1.txt", &i,
                (char *)"Nobis libero temporibus consequatur maiores quidem suscipit ut id.");

    // Insert file at end
    insert_file(sockfd, mycid, (char *)"file_1.txt", NULL,
                (char *)"Dolore dolor voluptas ut eum vero pariatur.");

    printf("=====Users tests======\n");
    get_users(sockfd, mycid);

    printf("===========Files Tests================\n");
    get_files(sockfd, mycid);

    printf("=====Exit tests======\n");
    ret = exit_client(sockfd, mycid);
}

/*
----------------------------------------------------------------------------------------------------
                    FUNCTIONS TO SEND REQUEST FILES TO SERVER
----------------------------------------------------------------------------------------------------
*/

int send_upload_request(int sockfd, int mycid, char linebuff[LINESIZE]) {
    char *p = strchr(linebuff, ' ');
    if (!p) {
        invalid_command(NULL);
        return ERR_INVALID_COMMAND;
    }
    char fname[FILE_NAME_LEN];
    sscanf(p + 1, "%[^\n]", fname);

    if (check_file_present(fname) != SUCCESS) {
        fprintf(stderr, (char *)"File does not exist at client\n");
        return ERR_FILE_NOT_FOUND;
    }

    char tmpfn[TEMP_FILE_LEN];
    // Upload file
    send_signal(sockfd, PING_SIGNAL);
    get_temp_file(CLIENT_DIRECTORY, tmpfn);
    prepare_upload_file(tmpfn, fname, mycid);
    if (send_file(sockfd, tmpfn, NULL) < 0) {
        fprintf(stderr, "Failed to request to client\n");
        remove_temp_file(tmpfn);
        return ERR_FILE_NOT_SENT;
    }

    remove_temp_file(tmpfn);
    return SUCCESS;
}

int send_download_request(int sockfd, int mycid, char linebuff[LINESIZE]) {
    char *p = strchr(linebuff, ' ');
    if (!p) {
        invalid_command(NULL);
        return ERR_INVALID_COMMAND;
    }
    char fname[FILE_NAME_LEN];
    sscanf(p + 1, "%[^\n]", fname);

    char tmpfn[TEMP_FILE_LEN];
    // Upload file
    send_signal(sockfd, PING_SIGNAL);
    get_temp_file(CLIENT_DIRECTORY, tmpfn);
    prepare_download_file(tmpfn, fname, mycid);
    if (send_file(sockfd, tmpfn, NULL) < 0) {
        fprintf(stderr, "Failed to request to client\n");
        remove_temp_file(tmpfn);
        return ERR_FILE_NOT_SENT;
    }
    remove_temp_file(tmpfn);
    return SUCCESS;
}

int send_read_request(int sockfd, int mycid, char linebuff[LINESIZE]) {
    char tokens[10][LINESIZE];
    int tc = 0;
    strip_newline(linebuff);
    char *tok = strtok(linebuff, " ");
    while ((tok != NULL) && tc < 10) {
        strcpy(tokens[tc++], tok);
        tok = strtok(NULL, " ");
    }

    if (tc <= 1) {
        invalid_command(NULL);
        return ERR_INVALID_COMMAND;
    }

    int sid, eid;
    int *psid, *peid;
    char rfname[FILE_NAME_LEN];
    if (tc == 2) {
        sscanf(tokens[1], "%s", rfname);
        psid = peid = NULL;
    } else if (tc == 3) {
        sscanf(tokens[1], "%s", rfname);
        sscanf(tokens[2], "%d", &sid);
        psid = &sid;
        peid = NULL;
    } else if (tc == 4) {
        sscanf(tokens[1], "%s", rfname);
        sscanf(tokens[2], "%d", &sid);
        sscanf(tokens[3], "%d", &eid);
        psid = &sid;
        peid = &eid;
    } else {
        invalid_command(NULL);
        return ERR_INVALID_COMMAND;
    }

    // Send Read request
    char tmpfn[TEMP_FILE_LEN];
    send_signal(sockfd, PING_SIGNAL);
    get_temp_file(CLIENT_DIRECTORY, tmpfn);
    prepare_read_file(tmpfn, rfname, mycid, psid, peid);
    if (send_file(sockfd, tmpfn, NULL) < 0) {
        fprintf(stderr, "Failed to send request to client\n");
        return ERR_INVALID_COMMAND;
    }
    remove_temp_file(tmpfn);
    return SUCCESS;
}

int send_delete_request(int sockfd, int mycid, char linebuff[LINESIZE]) {
    char tokens[10][LINESIZE];
    int tc = 0;
    strip_newline(linebuff);
    char *tok = strtok(linebuff, " ");
    while ((tok != NULL) && tc < 10) {
        strcpy(tokens[tc++], tok);
        tok = strtok(NULL, " ");
    }

    if (tc <= 1) {
        invalid_command(NULL);
        return ERR_INVALID_COMMAND;
    }

    int sid, eid;
    int *psid, *peid;
    char dfname[FILE_NAME_LEN];
    if (tc == 2) {
        sscanf(tokens[1], "%s", dfname);
        psid = peid = NULL;
    } else if (tc == 3) {
        sscanf(tokens[1], "%s", dfname);
        sscanf(tokens[2], "%d", &sid);
        psid = &sid;
        peid = NULL;
    } else if (tc == 4) {
        sscanf(tokens[1], "%s", dfname);
        sscanf(tokens[2], "%d", &sid);
        sscanf(tokens[3], "%d", &eid);
        psid = &sid;
        peid = &eid;
    } else {
        invalid_command(NULL);
        return ERR_INVALID_COMMAND;
    }

    // Send Read request
    char tmpfn[TEMP_FILE_LEN];
    send_signal(sockfd, PING_SIGNAL);
    get_temp_file(CLIENT_DIRECTORY, tmpfn);
    prepare_delete_file(tmpfn, dfname, mycid, psid, peid);
    if (send_file(sockfd, tmpfn, NULL) < 0) {
        fprintf(stderr, "Failed to send request to client\n");
        remove_temp_file(tmpfn);
        return ERR_FILE_NOT_SENT;
    }
    remove_temp_file(tmpfn);
    return SUCCESS;
}

int send_insert_request(int sockfd, int mycid, char linebuff[LINESIZE]) {
    char ifname[FILE_NAME_LEN], msg[LINESIZE];
    char c1, c2, c3, c4, c5;
    int id, *pid;

    int fs =
        sscanf(linebuff, "%*[^ ]%c%s%c%d%c%c%[^\"]%c", &c1, ifname, &c2, &id, &c3, &c4, msg, &c5);

    if (fs == 8 && c1 == ' ' && c2 == ' ' && c3 == ' ' && c4 == '"' && c5 == '"') {
        pid = &id;
    } else {
        fs = sscanf(linebuff, "%*[^ ]%c%s%c%c%[^\"]%c", &c1, ifname, &c2, &c3, msg, &c4);
        if (fs == 6 && c1 == ' ' && c2 == ' ' && c3 == '"' && c4 == '"') {
            pid = NULL;
        } else {
            invalid_command(NULL);
            return ERR_INVALID_COMMAND;
        }
    }

    // Send  request
    char tmpfn[TEMP_FILE_LEN];
    send_signal(sockfd, PING_SIGNAL);
    get_temp_file(CLIENT_DIRECTORY, tmpfn);
    prepare_insert_file(tmpfn, ifname, mycid, pid, msg);
    if (send_file(sockfd, tmpfn, NULL) < 0) {
        fprintf(stderr, "Failed to send request to client\n");
        remove_temp_file(tmpfn);
        return ERR_FILE_NOT_SENT;
    }
    remove_temp_file(tmpfn);
    return SUCCESS;
}

int send_invite_request(int sockfd, int mycid, char linebuff[LINESIZE]) {
    char tokens[10][LINESIZE];
    int tc = 0;
    strip_newline(linebuff);
    char *tok = strtok(linebuff, " ");
    while ((tok != NULL) && tc < 10) {
        strcpy(tokens[tc++], tok);
        tok = strtok(NULL, " ");
    }

    if (tc == 4) {
        char fname[LINESIZE], perm;
        int tcid;
        strcpy(fname, tokens[1]);
        int fs1 = sscanf(tokens[2], "%d", &tcid);
        int fs2 = sscanf(tokens[3], "%c", &perm);
        if ((fs1 == 1) && (fs2 == 1)) {
            char tmpfn[TEMP_FILE_LEN];
            // Send  request
            send_signal(sockfd, PING_SIGNAL);
            get_temp_file(CLIENT_DIRECTORY, tmpfn);
            prepare_invite_file(tmpfn, mycid, tcid, fname, perm, NULL);
            if (send_file(sockfd, tmpfn, NULL) < 0) {
                fprintf(stderr, "Failed to send request to client\n");
                remove_temp_file(tmpfn);
                return ERR_FILE_NOT_SENT;
            }
            remove_temp_file(tmpfn);
            return SUCCESS;
        } else {
            invalid_command(NULL);
            return ERR_INVALID_COMMAND;
        }
    } else {
        invalid_command(NULL);
        return ERR_INVALID_COMMAND;
    }
    return SUCCESS;
}

int PS = 1;

int main() {
    // creat server_files directory
    if (mkdir(CLIENT_DIRECTORY, 0777) < 0) {
        int t = errno;
        if (t != EEXIST) {
            perror("Failed to create server_files_directory");
            exit(4);
        }
    }

    // seed random number generator
    srand(time(NULL));

    char fname[TEMP_FILE_LEN];

    int sockfd, port = SERVER_PORT;
    sockfd = socket(AF_INET, SOCK_STREAM, 0);
    struct hostent *server;
    server = gethostbyname("localhost");
    struct sockaddr_in serv_addr;
    bzero((char *)&serv_addr, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    bcopy((char *)server->h_addr, (char *)&serv_addr.sin_addr.s_addr, server->h_length);
    serv_addr.sin_port = htons(port);
    if (connect(sockfd, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0) {
        perror("ERROR connecting");
        exit(1);
    }

    char command[BUFFSIZE];

    // Check for connectoin success mssg
    char signalbuff[MAX_SIGNAL_LEN];
    int ret = wait_for_signal(sockfd, signalbuff, NULL) < 0;
    if (ret == 0 && strncmp(signalbuff, MAX_CLIENTS_REACHED, strlen(MAX_CLIENTS_REACHED)) == 0) {
        fprintf(stderr, "Server has reached max clients\n");
        close(sockfd);
        exit(1);
    } else if (ret < 0 ||
               strncmp(signalbuff, CONNECTION_SUCCESS, strlen(CONNECTION_SUCCESS)) != 0) {
        fprintf(stderr, "Unable to connect to server\n");
        close(sockfd);
        exit(1);
    }

    printf("Connection Successful\n");

    int mycid = register_client(sockfd);
    if (mycid < 0) {
        fprintf(stderr, "Failed to register client");
        exit(EXIT_FAILURE);
    }

    // test_suite(sockfd);

    // // TODO (Remove)
    // sleep(10000);
    // exit(0);
    // int mycid = 1;

    // TODo (Remove)
    // upload_file(sockfd, mycid, (char *)"file_1.txt");

    int stdinfd = 0; // fd of stdin
    int fdmax = sockfd > stdinfd ? sockfd : stdinfd;
    fd_set f, f_c;
    FD_ZERO(&f);
    FD_ZERO(&f_c);
    FD_SET(stdinfd, &f); // Add stdin to select
    FD_SET(sockfd, &f);
    char linebuff[LINESIZE * 5];
    int failed_attempts = 0;

    // printf(">> ");
    // fflush(stdout);
    int prompt = 1;
    while (1) {
        if (PS) {
            printf(">> ");
            fflush(stdout);
        }
        f_c = f;
        int nready;
        char tmpfn[TEMP_FILE_LEN];
        if ((nready = select(fdmax + 1, &f_c, NULL, NULL, NULL))) {
            if (nready < 0) {
                printf("Timeout\n");
            }
            if (FD_ISSET(stdinfd, &f_c)) {
                // printf(">> ");
                // fflush(stdout);
                int r = read(stdinfd, linebuff, 1024);
                if (r >= 0) {
                    linebuff[r] = '\0';
                    if (strncmp(linebuff, "/users\n", strlen("/users\n")) == 0) {
                        char tmpfn[TEMP_FILE_LEN];
                        int ec = 0;
                        send_signal(sockfd, PING_SIGNAL);
                        get_temp_file(CLIENT_DIRECTORY, tmpfn);
                        prepare_users_file(tmpfn, mycid);
                        if (send_file(sockfd, tmpfn, NULL) < 0) {
                            fprintf(stderr, "Failed to send request to client\n");
                            ec = 1;
                        }
                        remove_temp_file(tmpfn);
                        if (ec == 1) PS = 1; // Claim back prompt
                    } else if (strncmp(linebuff, "/files\n", strlen("/files\n")) == 0) {
                        char tmpfn[TEMP_FILE_LEN];
                        int ec = 0;
                        send_signal(sockfd, PING_SIGNAL);
                        get_temp_file(CLIENT_DIRECTORY, tmpfn);
                        prepare_files_file(tmpfn, mycid);
                        if (send_file(sockfd, tmpfn, NULL) < 0) {
                            fprintf(stderr, "Failed to send request to client\n");
                            ec = 1;
                        }
                        remove_temp_file(tmpfn);
                        if (ec == 1) PS = 1;
                    } else if (strncmp(linebuff, "/upload", strlen("/upload")) == 0) {
                        int ret = send_upload_request(sockfd, mycid, linebuff);
                        if (ret != SUCCESS) PS = 1;
                        else {
                            PS = 0;
                        }
                    } else if (strncmp(linebuff, "/download", strlen("/download")) == 0) {
                        int ret = send_download_request(sockfd, mycid, linebuff);
                        if (ret != SUCCESS) PS = 1;
                        else {
                            PS = 0;
                        }
                    } else if (strncmp(linebuff, "/invite", strlen("/invite")) == 0) {
                        int ret = send_invite_request(sockfd, mycid, linebuff);
                        if (ret != SUCCESS) PS = 1;
                        else {
                            PS = 0;
                        }
                    } else if (strncmp(linebuff, "/read", strlen("/read")) == 0) {
                        int ret = send_read_request(sockfd, mycid, linebuff);
                        if (ret != SUCCESS) PS = 1;
                        else {
                            PS = 0;
                        }
                    } else if (strncmp(linebuff, "/insert", strlen("/insert")) == 0) {
                        int ret = send_insert_request(sockfd, mycid, linebuff);
                        if (ret != SUCCESS) PS = 1;
                        else {
                            PS = 0;
                        }
                    } else if (strncmp(linebuff, "/delete", strlen("/delete")) == 0) {
                        int ret = send_delete_request(sockfd, mycid, linebuff);
                        if (ret != SUCCESS) PS = 1;
                        else {
                            PS = 0;
                        }
                    } else if (strncmp(linebuff, "/exit\n", strlen("/exit\n")) == 0) {
                        char fname[TEMP_FILE_LEN];
                        send_signal(sockfd, PING_SIGNAL);
                        get_temp_file(CLIENT_DIRECTORY, fname);
                        prepare_exit_file(fname, mycid);
                        if (send_file(sockfd, fname, NULL) < 0) {
                            fprintf(stderr, "Error sending request to server\n");
                            remove_temp_file(fname);
                        }
                        remove_temp_file(fname);
                        exit(EXIT_SUCCESS);
                    } else {
                        invalid_command(NULL);
                        PS = 1;
                        continue;
                    }
                }
            } else if (FD_ISSET(sockfd, &f_c)) {
                get_temp_file(CLIENT_DIRECTORY, tmpfn);
                int ret;
                if ((ret = receive_file(sockfd, tmpfn, NULL)) < 0) {
                    fprintf(stderr, "Failed to receive request from server (%d)\n", ret);
                    remove_temp_file(tmpfn);
                    if (ret == ERR_FILE_NOT_READ) failed_attempts++;
                    if (failed_attempts < MAX_FAILED_ATTEMPTS) {
                        continue;
                    } else {
                        printf("\n\nFailed to reach the server %d times. Exiting\n\n",
                               MAX_FAILED_ATTEMPTS);
                        exit(EXIT_SUCCESS);
                    }
                }
                int c = 2;
                printf("\n");
                print_file(tmpfn, NULL);
                printf("\n");

                // Parse request file for collaboration request and download request
                char linebuff[LINESIZE];
                FILE *rfp = fopen(tmpfn, "r");
                fgets(linebuff, LINESIZE, rfp);
                if (strcmp(linebuff, "COMMAND=DOWNLOAD\n") == 0) {
                    bzero(linebuff, LINESIZE);
                    fgets(linebuff, LINESIZE, rfp);
                    if (strcmp(linebuff, "STATUS=SUCCESS\n") == 0) {
                        bzero(linebuff, LINESIZE);
                        fgets(linebuff, LINESIZE, rfp);
                        char fname[LINESIZE];
                        int fs = sscanf(linebuff, "NAME=%s\n", fname);
                        if (fs == 1) {
                            bzero(linebuff, LINESIZE);
                            fgets(linebuff, LINESIZE, rfp);
                            int dcid;
                            fs = sscanf(linebuff, "CID=%d\n", &dcid);
                            if (fs == 1 && (dcid == mycid)) {
                                fgets(linebuff, LINESIZE, rfp);
                                bzero(linebuff, LINESIZE);
                                FILE *ofp = fopen(fname, "w");

                                while ((ofp != NULL) && fgets(linebuff, LINESIZE, rfp) != NULL) {
                                    make_line_terminated(linebuff);
                                    fprintf(ofp, "%s", linebuff);
                                    bzero(linebuff, LINESIZE);
                                }
                                if (ofp != NULL) {
                                    printf("File %s successfuly saved to client\n", fname);
                                    fclose(ofp);
                                } else {
                                    fprintf(stderr, "Error parsing download response file\n");
                                }
                            }
                        }
                    }
                } else if (strcmp(linebuff, "COMMAND=INVITE\n") == 0) {
                    int scid, tcid;
                    char fname[FILE_NAME_LEN], permission;

                    fgets(linebuff, LINESIZE, rfp);
                    int fs1 = sscanf(linebuff, "FROM=%d", &scid);
                    fgets(linebuff, LINESIZE, rfp);
                    int fs2 = sscanf(linebuff, "TO=%d", &tcid);
                    fgets(linebuff, LINESIZE, rfp);
                    int fs3 = sscanf(linebuff, "FILE=%s", fname);
                    fgets(linebuff, LINESIZE, rfp);
                    int fs4 = sscanf(linebuff, "PERMISSION=%c", &permission);

                    if ((fs1 == 1) && (fs2 == 1) && (fs3 == 1) && (fs4 == 1) && (tcid == mycid)) {
                        printf("Client %d wants to give %c permission to you on file %s. Accept ? "
                               "(Y/N) ",
                               scid, permission, fname);
                        fflush(stdout);
                        char r;
                        char linebuf[LINESIZE];
                        fgets(linebuf, LINESIZE, stdin);
                        int fs4 = sscanf(linebuf, "%c", &r);
                        char tmpfn2[TEMP_FILE_LEN];
                        int ef;
                        if (fs4 == 1) {
                            get_temp_file(CLIENT_DIRECTORY, tmpfn2);
                            prepare_invite_file(tmpfn2, scid, tcid, fname, permission, &r);
                        }
                        if (send_file(sockfd, tmpfn2, NULL) == SUCCESS) {
                            remove_temp_file(tmpfn2);
                            get_temp_file(CLIENT_DIRECTORY, tmpfn2);
                            if (receive_file(sockfd, tmpfn2, NULL) == SUCCESS) {
                                print_file(tmpfn2, NULL);
                            } else {
                                ef = 1;
                            }
                        } else {
                            if (ef == 1) {
                                printf("Invite request failed\n");
                            }
                        }
                    }
                }
                if (rfp != NULL) fclose(rfp);
                remove_temp_file(tmpfn);

                PS = 1; // served request give prompt
            }
        }
    }

    // test_suite(sockfd);

    close(sockfd);

    printf("Shutting Down\n");
    return 0;
}
