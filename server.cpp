/*
Name : Assignment 3 (Server)
Creator : Sayan Mahapatra
Date : 06-02-2022
Description : In this file implementation of server is given
*/

#include <arpa/inet.h>
#include <errno.h>
#include <netdb.h>
#include <netinet/in.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ipc.h>
#include <sys/sem.h>
#include <sys/shm.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#include "common.cpp"
#include "constants.hpp"

struct sembuf pop, vop;

// Semaphores
int c_sem, cid_sem, map_sem, crf_sem, cpf_sem;

#define P(s) semop(s, &pop, 1)
#define V(s) semop(s, &vop, 1)

// Shared
int *cid_s, *nconnections; // Initial cid

// Map storing client id to socket mapping
int *map_cid, *map_soc; // need to store 5 cids at a time

void setup() {
    int shmid;

    // Shared memory for nconnections
    shmid = shmget(IPC_PRIVATE, sizeof(int), 0777 | IPC_CREAT);
    nconnections = (int *)shmat(shmid, 0, 0);

    // Shared memory for cid
    shmid = shmget(IPC_PRIVATE, sizeof(int), 0777 | IPC_CREAT);
    cid_s = (int *)shmat(shmid, 0, 0);
    *(cid_s) = 9999; // inital cid

    // Shared memory for map
    shmid = shmget(IPC_PRIVATE, 10 * sizeof(int), 0777 | IPC_CREAT);
    map_cid = (int *)shmat(shmid, 0, 0);

    shmid = shmget(IPC_PRIVATE, 10 * sizeof(int), 0777 | IPC_CREAT);
    map_soc = (int *)shmat(shmid, 0, 0);

    // Initialise map
    for (int i = 0; i < 10; ++i) {
        map_soc[i] = -1;
        map_cid[i] = -1;
    }

    // Set up c_sem and cid_sem and map_sem
    c_sem = semget(IPC_PRIVATE, 1, 0777 | IPC_CREAT); // connections sem
    semctl(c_sem, 0, SETVAL, 1);

    cid_sem = semget(IPC_PRIVATE, 1, 0777 | IPC_CREAT); // cid sem
    semctl(cid_sem, 0, SETVAL, 1);

    map_sem = semget(IPC_PRIVATE, 1, 0777 | IPC_CREAT); // map sem
    semctl(map_sem, 0, SETVAL, 1);

    // Semaphore for CLIENT_RECORDS_FILE
    crf_sem = semget(IPC_PRIVATE, 1, 0777 | IPC_CREAT); // map sem
    semctl(crf_sem, 0, SETVAL, 1);

    // Semaphore for CLIENT_PERMISSIONS_FILE
    cpf_sem = semget(IPC_PRIVATE, 1, 0777 | IPC_CREAT); // map sem
    semctl(cpf_sem, 0, SETVAL, 1);

    // Set up pop and vop
    pop.sem_num = vop.sem_num = 0;
    pop.sem_flg = vop.sem_flg = SEM_UNDO;
    pop.sem_op = -1;
    vop.sem_op = 1;
}

int get_client_id() {
    P(cid_sem);
    if (*cid_s == 99999) (*cid_s) = 10000; // wrap around
    else
        (*cid_s)++;
    int t = (*cid_s);
    V(cid_sem);
    return t;
}

int prepare_register_reply_file(char filename[TEMP_FILE_LEN], int status, char *err, int cid) {
    FILE *fp = fopen(filename, "w");
    fprintf(fp, "COMMAND=REGISTER\n");
    if (status == 0) {
        fprintf(fp, "STATUS=SUCCESS\n");
        fprintf(fp, "MESSAGE=%d registered\n", cid);
    } else {
        fprintf(fp, "STATUS=FAILED\n");
        fprintf(fp, "ERROR=%s\n", err);
    }

    fclose(fp);
    return 0;
}

int prepare_exit_reply_file(char filename[TEMP_FILE_LEN], int status, char *err, int cid) {
    FILE *fp = fopen(filename, "w");
    fprintf(fp, "COMMAND=EXIT\n");
    if (status == SUCCESS) {
        fprintf(fp, "STATUS=SUCCESS\n");
        fprintf(fp, "MESSAGE=%d removed from active clients\n", cid);
    } else {
        fprintf(fp, "STATUS=FAILED\n");
        fprintf(fp, "ERROR=%s\n", err);
    }

    fclose(fp);
    return 0;
}

int prepare_upload_reply_file(char filename[TEMP_FILE_LEN], char uploadfile[FILE_NAME_LEN],
                              int status, char *err, int cid) {
    FILE *fp = fopen(filename, "w");
    fprintf(fp, "COMMAND=UPLOAD\n");
    if (status == SUCCESS) {
        fprintf(fp, "STATUS=SUCCESS\n");
        fprintf(fp, "NAME=%s\n", uploadfile);
        fprintf(fp, "MESSAGE=Upload to server succesful by owner %d\n", cid);
    } else {
        fprintf(fp, "STATUS=FAILED\n");
        fprintf(fp, "ERROR=%s\n", err);
    }

    fclose(fp);
    return 0;
}

int prepare_download_reply_file(char filename[TEMP_FILE_LEN], char downloadfile[FILE_NAME_LEN],
                                int status, char *err, int cid) {
    FILE *fp = fopen(filename, "w");
    FILE *ifp = fopen(downloadfile, "r");
    fprintf(fp, "COMMAND=DOWNLOAD\n");
    if (status == SUCCESS && (ifp != NULL) && (fp != NULL)) {
        fprintf(fp, "STATUS=SUCCESS\n");
        char *p = strrchr(downloadfile, '/') + 1;
        fprintf(fp, "NAME=%s\n", p);
        fprintf(fp, "CID=%d\n", cid);
        fprintf(fp, "MESSAGE=Downloaded successfuly from server\n");
        // Contents of the file
        char linebuff[LINESIZE];
        while (fgets(linebuff, LINESIZE, ifp) != NULL) {
            make_line_terminated(linebuff);
            fprintf(fp, "%s", linebuff);
        }
        fclose(ifp);
    } else {
        fprintf(fp, "STATUS=FAILED\n");
        fprintf(fp, "ERROR=%s\n", err);
    }

    fclose(fp);
    return 0;
}

int prepare_read_reply_file(char filename[TEMP_FILE_LEN], char rfname[FILE_NAME_LEN], int status,
                            char *err, int cid, int *sindex, int *eindex) {
    int ef = 0;
    char merr[BUFFSIZE];
    FILE *fp = fopen(filename, "w");
    FILE *ifp = fopen(rfname, "r");
    char *crfname = strrchr(rfname, '/') + 1; // client side file name
    fprintf(fp, "COMMAND=READ\n");
    if (status == SUCCESS) {
        int n = get_lines_in_file(rfname);
        if (n >= 0) {
            int s, e;
            if (sindex != NULL && eindex != NULL) {
                s = *sindex;
                e = *eindex;
            } else if (sindex != NULL && eindex == NULL) {
                s = e = *sindex; // Read one line
            } else {
                // Read full file
                s = 0;
                e = n - 1;
            }

            // Check valid index
            if (s >= -n && s < n && e >= -n && e < n) {
                // Convert to positive index
                if (s < 0) s = n + s;
                if (e < 0) e = n + e;

                fprintf(fp, "STATUS=SUCCESS\n");
                fprintf(fp, "MESSAGE=%s read\n", crfname);
                char linebuff[LINESIZE];
                int lindex = 0;
                while (fgets(linebuff, LINESIZE, ifp) != NULL) {
                    if (lindex >= s && lindex <= e) {
                        make_line_terminated(linebuff);
                        fprintf(fp, "%s", linebuff);
                    }
                    lindex++;
                }
            } else if ((n == 0) && (s == 0) && (e == -1)) {
                fprintf(fp, "STATUS=SUCCESS\n");
                fprintf(fp, "MESSAGE=%s read\n", crfname);
            } else {
                ef = 1;
                sprintf(merr, "Invalid Index specified for file %s", crfname);
            }
        } else {
            ef = 1;
            strcpy(merr, "File Error");
        }
    }

    if (ef == 1 || status != SUCCESS) {
        fprintf(fp, "STATUS=FAILED\n");
        if (status != SUCCESS) {
            fprintf(fp, "ERR=%s\n", err);
        } else {
            fprintf(fp, "ERR=%s\n", merr);
        }
    }

    if (fp != NULL) fclose(fp);
    if (ifp != NULL) fclose(ifp);
    return SUCCESS;
}

int prepare_delete_reply_file(char filename[TEMP_FILE_LEN], char dfname[FILE_NAME_LEN], int status,
                              char *err, int cid, int *sindex, int *eindex) {
    int ef = 0;
    char merr[BUFFSIZE];
    FILE *fp = fopen(filename, "w");
    FILE *ifp = fopen(dfname, "r");
    char *cdfname = strrchr(dfname, '/') + 1; // client side file name

    fprintf(fp, "COMMAND=DELETE\n");
    if (status == SUCCESS) {
        int n = get_lines_in_file(dfname);
        if (n >= 0) {
            int s, e;
            if (sindex != NULL && eindex != NULL) {
                s = *sindex;
                e = *eindex;
            } else if (sindex != NULL && eindex == NULL) {
                s = e = *sindex; // Read one line
            } else {
                // Read full file
                s = 0;
                e = n - 1;
            }

            // Check valid index
            if (s >= -n && s < n && e >= -n && e < n) {
                // Convert to positive index
                if (s < 0) s = n + s;
                if (e < 0) e = n + e;

                fprintf(fp, "STATUS=SUCCESS\n");
                fprintf(fp, "MESSAGE=Successful delete on %s\n", cdfname);

                char tmpfn[TEMP_FILE_LEN];
                get_temp_file(SERVER_DIRECTORY, tmpfn);
                FILE *tfp = fopen(tmpfn, "w");
                char linebuff[LINESIZE];
                int lindex = 0;
                while (fgets(linebuff, LINESIZE, ifp) != NULL) {
                    if (!(lindex >= s && lindex <= e)) {
                        make_line_terminated(linebuff);
                        fprintf(fp, "%s", linebuff);
                        fprintf(tfp, "%s", linebuff);
                    }
                    lindex++;
                }

                fclose(tfp);
                // fclose(ifp);
                // sleep(1);
                remove(dfname);
                if (rename(tmpfn, dfname) < 0) {
                    ef = 1;
                    sprintf(merr, "Failed to perform delete request on %s", dfname);
                }
            } else if ((n == 0) && (s == 0) && (e == -1)) {
                fprintf(fp, "STATUS=SUCCESS\n");
                fprintf(fp, "MESSAGE=Successful delete on %s\n", cdfname);
            } else {
                ef = 1;
                sprintf(merr, "Invalid Index specified for file %s", cdfname);
            }
        } else {
            ef = 1;
            strcpy(merr, "File Error");
        }
    }

    if (ef == 1 || status != SUCCESS) {
        fprintf(fp, "STATUS=FAILED\n");
        if (status != SUCCESS) {
            fprintf(fp, "ERR=%s\n", err);
        } else {
            fprintf(fp, "ERR=%s\n", merr);
        }
    }

    if (fp != NULL) fclose(fp);
    if (ifp != NULL) fclose(ifp);
    return SUCCESS;
}

int prepare_insert_reply_file(char filename[TEMP_FILE_LEN], char ifname[FILE_NAME_LEN], int status,
                              char *err, int cid, int *indx, char msg[LINESIZE]) {
    int ef = 0;
    char merr[BUFFSIZE];
    FILE *fp = fopen(filename, "w");
    FILE *ifp = fopen(ifname, "r");
    char *cifname = strrchr(ifname, '/') + 1; // client side file name
    fprintf(fp, "COMMAND=INSERT\n");
    if (status == SUCCESS) {
        int n = get_lines_in_file(ifname);
        if (n >= 0) {
            int i;
            if (indx != NULL) {
                i = *indx;
            } else {
                i = n; // insert at end            }
            }

            // Check valid index
            if (i >= -n && i < n || (indx == NULL && i == n)) {
                // Convert to positive index
                if (i < 0) i = n + i;

                fprintf(fp, "STATUS=SUCCESS\n");
                fprintf(fp, "MESSAGE=Successful insert on %s\n", cifname);

                char tmpfn[TEMP_FILE_LEN];
                get_temp_file(SERVER_DIRECTORY, tmpfn);
                FILE *tfp = fopen(tmpfn, "w");
                char linebuff[LINESIZE];
                int lindex = 0;
                int wrote = 0;
                while (fgets(linebuff, LINESIZE, ifp) != NULL) {
                    if (lindex == i) {
                        make_line_terminated(msg);
                        fprintf(fp, "%s", msg);
                        fprintf(tfp, "%s", msg);
                        wrote = 1;
                    }
                    make_line_terminated(linebuff);
                    fprintf(fp, "%s", linebuff);
                    fprintf(tfp, "%s", linebuff);
                    lindex++;
                }

                if (wrote == 0) {
                    // Add at end of file
                    make_line_terminated(msg);
                    fprintf(fp, "%s", msg);
                    fprintf(tfp, "%s", msg);
                }

                fclose(tfp);
                // fclose(ifp);
                // sleep(1);
                remove(ifname);
                if (rename(tmpfn, ifname) < 0) {
                    ef = 1;
                    sprintf(merr, "Failed to perform insert request on %s", ifname);
                }
            } else {
                ef = 1;
                sprintf(merr, "Invalid Index specified for file %s", ifname);
            }
        } else {
            ef = 1;
            strcpy(merr, "File Error");
        }
    }

    if (ef == 1 || status != SUCCESS) {
        fprintf(fp, "STATUS=FAILED\n");
        if (status != SUCCESS) {
            fprintf(fp, "ERR=%s\n", err);
        } else {
            fprintf(fp, "ERR=%s\n", merr);
        }
    }

    if (fp != NULL) fclose(fp);
    if (ifp != NULL) fclose(ifp);
    return SUCCESS;
}

int prepare_users_reply_file(char tmpfn[TEMP_FILE_LEN], int status, char *err, int cid) {
    FILE *fp = fopen(tmpfn, "w");
    fprintf(fp, "COMMAND=USERS\n");
    if (status == SUCCESS) {
        char linebuff[LINESIZE];
        int users[99999 - 10000 + 1], ic = 0, fcid;
        P(crf_sem);
        FILE *ifp = fopen(CLIENT_RECORDS_FILENAME, "r");
        while (fgets(linebuff, LINESIZE, ifp) != NULL) {
            int fs = sscanf(linebuff, "%d", &fcid);
            if (fs == 1) users[ic++] = fcid; // Optimise
        }
        fclose(ifp);
        fprintf(fp, "STATUS=SUCESS\n");
        fprintf(fp, "MESSAGE=List of users (cids) given below\n");
        for (int i = 0; i < ic; ++i) {
            fprintf(fp, "%d\n", users[i]);
        }
        fclose(fp);
        V(crf_sem);
    } else {
        fprintf(fp, "STATUS=FAILED\n");
        fprintf(fp, "ERR=%s\n", err);
    }
    return SUCCESS;
}

int prepare_files_reply_file(char tmpfn[TEMP_FILE_LEN], int status, char *err, int cid) {
    FILE *fp = fopen(tmpfn, "w");
    fprintf(fp, "COMMAND=FILES\n");
    if (status == SUCCESS) {
        char linebuff[LINESIZE];
        char files[MAX_FILES][FILE_NAME_LEN];
        char nlines[MAX_FILES];
        int fc = 0;
        P(cpf_sem);
        FILE *ifp = fopen(CLIENT_PERMISSIONS_FILENAME, "r");
        while (fgets(linebuff, LINESIZE, ifp) != NULL) {
            int fs, c;
            char fname[LINESIZE], fname2[2 * LINESIZE], perm;
            fs = sscanf(linebuff, "%d##!##%[^#]##!##%c ", &c, fname, &perm);
            if ((fs == 3) && perm == 'O') {
                strcpy(files[fc], fname);
                sprintf(fname2, "%s/%s", SERVER_DIRECTORY, fname);
                nlines[fc] = get_lines_in_file(fname2);
                fc++;
            }
        }
        if (ifp != NULL) fclose(ifp);
        fprintf(fp, "STATUS=SUCESS\n");
        fprintf(fp, "MESSAGE=Files on Server\n");
        fflush(fp);

        for (int i = 0; i < fc; ++i) {
            ifp = fopen(CLIENT_PERMISSIONS_FILENAME, "r");
            fprintf(fp, "File Name: %s No of lines: %d\n", files[i], nlines[i]);
            while (fgets(linebuff, LINESIZE, ifp) != NULL) {
                int fs, c;
                char fname[LINESIZE], fname2[LINESIZE], perm;
                fs = sscanf(linebuff, "%d##!##%[^#]##!##%c ", &c, fname, &perm);
                if (fs == 3 && (strcmp(fname, files[i]) == 0)) {
                    fprintf(fp, "\t Client : %d Permission: %c\n", c, perm);
                }
            }
            if (ifp != NULL) fclose(ifp);
            fflush(fp);
        }
        fclose(fp);
        V(cpf_sem);
    } else {
        fprintf(fp, "STATUS=FAILED\n");
        fprintf(fp, "ERR=%s\n", err);
    }
    return SUCCESS;
}

int add_client_to_file(int cid) {
    P(crf_sem);
    FILE *fp = fopen(CLIENT_RECORDS_FILENAME, "a");
    // fseek(fp, 0l, SEEK_END);
    fprintf(fp, "%d\n", cid);
    fclose(fp);
    V(crf_sem);
    return 0;
}

int remove_client_from_file(int cid) {
    P(crf_sem);
    FILE *fp = fopen(CLIENT_RECORDS_FILENAME, "r+");
    char tempfp[TEMP_FILE_LEN];
    get_temp_file(SERVER_DIRECTORY, tempfp);
    FILE *fpt = fopen(tempfp, "w");

    char linebuff[LINESIZE];
    char cidstring[20];
    sprintf(cidstring, "%d\n", cid);
    while (fgets(linebuff, LINESIZE, fp) != NULL) {
        if (strcmp(linebuff, cidstring) != 0) {
            if (linebuff[strlen(linebuff) - 1] != '\n') strcat(linebuff, "\n");
            fprintf(fpt, "%s", linebuff);
        }
    }

    if (fpt != NULL) fclose(fpt);
    if (fp != NULL) fclose(fp);

    remove(CLIENT_RECORDS_FILENAME);
    int r = rename(tempfp, CLIENT_RECORDS_FILENAME);
    V(crf_sem);
    return r;
}

int disconnect_client(int cid) {
    remove_client_from_file(cid);
    // Update map
    P(map_sem);
    for (int i = 0; i < 10; ++i) {
        if (map_cid[i] == cid) {
            map_soc[i] = -1;
            map_cid[i] = -1;
        }
    }
    V(map_sem);

    char fc = 0;
    P(crf_sem);
    char tmpfn[TEMP_FILE_LEN];
    FILE *fp = fopen(CLIENT_PERMISSIONS_FILENAME, "r");
    get_temp_file(SERVER_DIRECTORY, tmpfn);
    FILE *ofp = fopen(tmpfn, "w");
    char linebuff[LINESIZE], fname[TEMP_FILE_LEN], sfname[TEMP_FILE_LEN * 2];
    char deleted_flist[MAX_FILES][FILE_NAME_LEN];
    int dfl = 0;
    while (fgets(linebuff, LINESIZE, fp) != NULL) {
        make_line_terminated(linebuff);
        int c, fs;
        char p;
        fs = sscanf(linebuff, "%d##!##%[^#]##!##%c ", &c, fname, &p);
        // printf("%d %d %s %c\n", fs, c, fname, p);
        if ((fs == 3) && (c == cid) && (p == 'O')) {
            // cid is owner -> delete file and upate permissions (dont write)
            sprintf(sfname, "%s/%s", SERVER_DIRECTORY, fname);
            if (remove(sfname) < 0) {
                perror(NULL);
            }
            printf("Removed %s\n", sfname);
            strcpy(deleted_flist[dfl++], fname);
        } else {
            int dont_write = 0;
            for (int i = 0; i < dfl; ++i) {
                if (strcmp(deleted_flist[i], fname) == 0) {
                    // File marked for delete, update crf file
                    // dont write back
                    dont_write = 1;
                    break;
                }
            }
            if (dont_write == 0) fprintf(ofp, "%s", linebuff);
        }
    }
    fclose(ofp);
    fclose(fp);

    remove(CLIENT_PERMISSIONS_FILENAME);
    rename(tmpfn, CLIENT_PERMISSIONS_FILENAME);
    V(crf_sem);

    return SUCCESS;
}

/*Return permission, returns X if entry not found*/
int get_permission(int cid, char filename[FILE_NAME_LEN], char *res) {
    char linebuff[LINESIZE];
    P(cpf_sem);
    FILE *fp = fopen(CLIENT_PERMISSIONS_FILENAME, "r");
    if (!fp) {
        V(cpf_sem);
        *res = 'X';
        return ERR_FILE_ERROR;
    }

    int found = 0;
    while (fgets(linebuff, LINESIZE, fp) != NULL) {
        int fs, c;
        char fname[LINESIZE], perm;
        fs = sscanf(linebuff, "%d##!##%[^#]##!##%c ", &c, fname, &perm);
        if ((fs == 3) && (c == cid) && (strcmp(fname, filename) == 0)) {
            *res = perm;
            found = 1;
            break;
        }
    }

    if (!found) *res = 'X';

    V(cpf_sem);
    return SUCCESS;
}

/* Adds the entry <cid filename perm> to the file. Permissions are upgraded
for existing if needed. Permission downgrades ignored. Permission entries removed if perm = X*/
int update_permission_file(int cid, char filename[FILE_NAME_LEN], char perm) {
    char linebuff[LINESIZE];
    char tmpfn[TEMP_FILE_LEN];
    get_temp_file(SERVER_DIRECTORY, tmpfn);
    P(cpf_sem);
    FILE *ifp = fopen(CLIENT_PERMISSIONS_FILENAME, "r");
    FILE *ofp = fopen(tmpfn, "w");

    if (!ifp || !ofp) {
        V(cpf_sem);
        return ERR_FILE_ERROR;
    }

    int found = 0;
    while (fgets(linebuff, LINESIZE, ifp) != NULL) {
        int c, fs;
        char f[LINESIZE], p;
        fs = sscanf(linebuff, "%d##!##%[^#]##!##%c ", &c, f, &p);
        if (fs == 3) {
            if (c == cid && (strcmp(filename, f) == 0)) {
                if (perm == 'X' && p == 'O') {
                    found = 1; // remove permission only if owner
                } else if (p == 'V' && perm == 'E') {
                    fprintf(ofp, "%d##!##%s##!##%c\n", cid, filename, perm);
                    found = 1;
                } else if (p == 'E' && perm == 'V') {
                    fprintf(ofp, "%d##!##%s##!##%c\n", cid, filename, perm);
                    found = 1;
                }
            } else {
                // Copy as it is
                fprintf(ofp, "%s", linebuff);
            }
        }
    }

    if (found == 0) {
        fprintf(ofp, "%d##!##%s##!##%c\n", cid, filename, perm);
    }

    fclose(ofp);
    fclose(ifp);

    remove(CLIENT_PERMISSIONS_FILENAME);
    int r = rename(tmpfn, CLIENT_PERMISSIONS_FILENAME);
    V(cpf_sem);
    return r;
}

int parse_request_file(int connection, char *fname) {
    char linebuff[LINESIZE];
    char tmpfn[TEMP_FILE_LEN];
    FILE *fp = fopen(fname, "r");
    int dt = DEFAULT_TIMEOUT;

    // Read first line to figure out type of command
    if (fgets(linebuff, LINESIZE, fp) != NULL) {
        strip_newline(linebuff);
        char *k = strtok(linebuff, "=");
        char *v = strtok(NULL, "=");
        if ((strcmp(k, "COMMAND") == 0) && (strcmp(v, "REGISTER") == 0)) {
            int cid = get_client_id();
            get_temp_file(SERVER_DIRECTORY, tmpfn);
            prepare_register_reply_file(tmpfn, 0, NULL, cid);
            if (send_file(connection, tmpfn, NULL) < 0) {
                fprintf(stderr, "Failed to register client\n");
                P(cid_sem);
                (*cid_s)--;
                V(cid_sem);
                return ERR_FILE_NOT_SENT;
            }
            int v = add_client_to_file(cid);
            // Add socket to map
            P(map_sem);
            for (int i = 0; i < 10; ++i) {
                if (map_cid[i] == -1) {
                    map_soc[i] = connection;
                    map_cid[i] = cid;
                    break;
                }
            }
            V(map_sem);
            printf("Client Registered. CID = %d Socket = %d\n", cid, connection);
            remove_temp_file(tmpfn);
            return SUCCESS;
        } else if ((strcmp(k, "COMMAND") == 0) && (strcmp(v, "USERS") == 0)) {
            // Get CID from second line
            fgets(linebuff, LINESIZE, fp);
            int cid, fs;
            fs = sscanf(linebuff, "ID=%d\n", &cid);
            if (fs != 1 || !(cid >= 10000 && cid <= 99999)) {
                fprintf(stderr, "Invalid File\n");
                fclose(fp);
                char err[2 * LINESIZE];
                get_temp_file(SERVER_DIRECTORY, tmpfn);
                sprintf(err, "Invalid File");
                prepare_users_reply_file(tmpfn, ERR_INVALID_FILE, err, cid);
                send_file(connection, tmpfn, NULL);
                remove_temp_file(tmpfn);
                return ERR_INVALID_FILE;
            }
            get_temp_file(SERVER_DIRECTORY, tmpfn);
            prepare_users_reply_file(tmpfn, SUCCESS, NULL, cid);
            send_file(connection, tmpfn, NULL);
            remove_temp_file(tmpfn);
            return SUCCESS;
        } else if ((strcmp(k, "COMMAND") == 0) && (strcmp(v, "FILES") == 0)) {
            // Get CID from second line
            fgets(linebuff, LINESIZE, fp);
            int cid, fs;
            fs = sscanf(linebuff, "ID=%d\n", &cid);
            if (fs != 1 || !(cid >= 10000 && cid <= 99999)) {
                fprintf(stderr, "Invalid File\n");
                fclose(fp);
                char err[2 * LINESIZE];
                get_temp_file(SERVER_DIRECTORY, tmpfn);
                sprintf(err, "Invalid File");
                prepare_files_reply_file(tmpfn, ERR_INVALID_FILE, err, cid);
                send_file(connection, tmpfn, NULL);
                remove_temp_file(tmpfn);
                return ERR_INVALID_FILE;
            }
            get_temp_file(SERVER_DIRECTORY, tmpfn);
            prepare_files_reply_file(tmpfn, SUCCESS, NULL, cid);
            send_file(connection, tmpfn, NULL);
            remove_temp_file(tmpfn);
            return SUCCESS;
        } else if ((strcmp(k, "COMMAND") == 0) && (strcmp(v, "EXIT") == 0)) {
            // Get CID from second line
            fgets(linebuff, LINESIZE, fp);
            int cid, fs;
            fs = sscanf(linebuff, "ID=%d\n", &cid);
            if (fs != 1 || !(cid >= 10000 && cid <= 99999)) {
                fprintf(stderr, "Invalid File\n");
                fclose(fp);
                char err[2 * LINESIZE];
                get_temp_file(SERVER_DIRECTORY, tmpfn);
                sprintf(err, "Invalid File");
                prepare_exit_reply_file(tmpfn, ERR_INVALID_FILE, err, cid);
                send_file(connection, tmpfn, NULL);
                remove_temp_file(tmpfn);
                return ERR_INVALID_FILE;
            }

            int ret = disconnect_client(cid);
            get_temp_file(SERVER_DIRECTORY, tmpfn);
            // prepare_exit_reply_file(tmpfn, SUCCESS, NULL, cid);
            // send_file(connection, tmpfn, NULL);
            // remove_temp_file(tmpfn);
            return CLIENT_EXIT;
        } else if ((strcmp(k, "COMMAND") == 0) && (strcmp(v, "UPLOAD") == 0)) {
            // Get CID from second line
            fgets(linebuff, LINESIZE, fp);
            int cid, fs;
            fs = sscanf(linebuff, "ID=%d\n", &cid);
            if (fs != 1 || !(cid >= 10000 && cid <= 99999)) {
                fprintf(stderr, "Invalid File\n");
                fclose(fp);
                char err[2 * LINESIZE];
                get_temp_file(SERVER_DIRECTORY, tmpfn);
                sprintf(err, "Invalid File");
                prepare_upload_reply_file(tmpfn, NULL, ERR_INVALID_FILE, err, cid);
                send_file(connection, tmpfn, NULL);
                remove_temp_file(tmpfn);
                return ERR_INVALID_FILE;
            }

            // Get File name form third line
            fgets(linebuff, LINESIZE, fp);
            strip_newline(linebuff);
            char ufname[2 * LINESIZE], ufname2[LINESIZE];
            fs = sscanf(linebuff, "NAME=%s", ufname2);
            fs = sprintf(ufname, "%s/%s", SERVER_DIRECTORY, ufname2);
            // Check if file exists
            if (check_file_present(ufname) == SUCCESS) {
                // File already present
                get_temp_file(SERVER_DIRECTORY, tmpfn);
                prepare_upload_reply_file(tmpfn, ufname2, ERR_FILE_EXISTS,
                                          (char *)"File exists at server", cid);
                send_file(connection, tmpfn, NULL);
                remove_temp_file(tmpfn);
                return SUCCESS;
            } else {
                get_temp_file(SERVER_DIRECTORY, tmpfn);
                // Copy lines from line 4 onwards
                FILE *ofp = fopen(ufname, "w");
                while (fgets(linebuff, LINESIZE, fp) != NULL) {
                    make_line_terminated(linebuff);
                    fprintf(ofp, "%s", linebuff);
                }
                fclose(ofp);
                prepare_upload_reply_file(tmpfn, ufname2, SUCCESS, NULL, cid);
                send_file(connection, tmpfn, NULL);
                remove_temp_file(tmpfn);
                int ret = update_permission_file(cid, ufname2, 'O');
                if (ret < 0) fprintf(stderr, "Error updating permission file %d\n", ret);
                return SUCCESS;
            }
        } else if ((strcmp(k, "COMMAND") == 0) && (strcmp(v, "DOWNLOAD") == 0)) {
            // Get CID from second line
            fgets(linebuff, LINESIZE, fp);
            int cid, fs;
            fs = sscanf(linebuff, "ID=%d\n", &cid);
            if (fs != 1 || !(cid >= 10000 && cid <= 99999)) {
                fprintf(stderr, "Invalid File\n");
                fclose(fp);
                char err[2 * LINESIZE];
                get_temp_file(SERVER_DIRECTORY, tmpfn);
                sprintf(err, "Invalid File");
                prepare_download_reply_file(tmpfn, NULL, ERR_INVALID_FILE, err, cid);
                send_file(connection, tmpfn, NULL);
                remove_temp_file(tmpfn);
                return ERR_INVALID_FILE;
            }

            // Get File name form third line
            fgets(linebuff, LINESIZE, fp);
            strip_newline(linebuff);
            char ufname[2 * LINESIZE], ufname2[LINESIZE];
            fs = sscanf(linebuff, "NAME=%s", ufname2);
            fs = sprintf(ufname, "%s/%s", SERVER_DIRECTORY, ufname2);

            if (check_file_present(ufname) == SUCCESS) {
                // check permissions
                char p;
                if ((get_permission(cid, ufname2, &p) == SUCCESS) &&
                    (p == 'V' || p == 'E' || p == 'O')) {
                    get_temp_file(SERVER_DIRECTORY, tmpfn);
                    prepare_download_reply_file(tmpfn, ufname, SUCCESS, NULL, cid);
                    send_file(connection, tmpfn, NULL);
                    remove_temp_file(tmpfn);
                    return SUCCESS;

                } else {
                    char err[2 * LINESIZE];
                    get_temp_file(SERVER_DIRECTORY, tmpfn);
                    sprintf(err, "Insufficient Permission to access the file %s", ufname2);
                    prepare_download_reply_file(tmpfn, ufname2, ERR_PERMISSION_ERR, err, cid);
                    send_file(connection, tmpfn, NULL);
                    remove_temp_file(tmpfn);
                    return ERR_PERMISSION_ERR;
                }
            } else {
                get_temp_file(SERVER_DIRECTORY, tmpfn);
                char err[2 * LINESIZE];
                sprintf(err, "File %s not found on server", ufname2);
                prepare_download_reply_file(tmpfn, ufname2, ERR_FILE_ERROR, err, cid);
                send_file(connection, tmpfn, NULL);
                remove_temp_file(tmpfn);
                return ERR_FILE_NOT_FOUND;
            }
        } else if ((strcmp(k, "COMMAND") == 0) && (strcmp(v, "READ") == 0)) {
            // Get CID from second line
            fgets(linebuff, LINESIZE, fp);
            int cid;
            int fs1 = sscanf(linebuff, "ID=%d\n", &cid);

            // Get File name form third line
            fgets(linebuff, LINESIZE, fp);
            strip_newline(linebuff);
            char ufname[2 * LINESIZE], ufname2[LINESIZE];
            int fs2 = sscanf(linebuff, "NAME=%s", ufname2);
            sprintf(ufname, "%s/%s", SERVER_DIRECTORY, ufname2);

            // Get s and end index
            int sindex, eindex;
            int *psindex = &sindex, *peindex = &eindex;
            char c, d;

            fgets(linebuff, LINESIZE, fp);
            int fs3 = sscanf(linebuff, "STARTINDX%c%d", &c, &sindex);
            if (fs3 == 1) psindex = NULL;

            fgets(linebuff, LINESIZE, fp);
            int fs4 = sscanf(linebuff, "ENDINDX%c%d", &d, &eindex);
            if (fs4 == 1) peindex = NULL;

            if (fs1 != 1 || fs2 != 1 || fs3 < 1 || fs4 < 1 || c != '=' || d != '=' ||
                !(cid >= 10000 && cid <= 99999)) {
                fprintf(stderr, "Invalid File\n");
                fclose(fp);
                char err[2 * LINESIZE];
                get_temp_file(SERVER_DIRECTORY, tmpfn);
                sprintf(err, "Invalid File");
                prepare_read_reply_file(tmpfn, NULL, ERR_INVALID_FILE, err, cid, NULL, NULL);
                send_file(connection, tmpfn, NULL);
                remove_temp_file(tmpfn);
                return ERR_INVALID_FILE;
            }

            if (check_file_present(ufname) == SUCCESS) {
                // check permissions
                char p;
                if ((get_permission(cid, ufname2, &p) == SUCCESS) &&
                    (p == 'V' || p == 'E' || p == 'O')) {
                    get_temp_file(SERVER_DIRECTORY, tmpfn);
                    prepare_read_reply_file(tmpfn, ufname, SUCCESS, NULL, cid, psindex, peindex);
                    send_file(connection, tmpfn, NULL);
                    remove_temp_file(tmpfn);
                    return SUCCESS;
                } else {
                    char err[2 * LINESIZE];
                    get_temp_file(SERVER_DIRECTORY, tmpfn);
                    sprintf(err, "Insufficient Permission to access the file %s", ufname2);
                    prepare_read_reply_file(tmpfn, ufname2, ERR_PERMISSION_ERR, err, cid, &sindex,
                                            &eindex);
                    send_file(connection, tmpfn, NULL);
                    remove_temp_file(tmpfn);
                    return ERR_PERMISSION_ERR;
                }
            } else {
                get_temp_file(SERVER_DIRECTORY, tmpfn);
                char err[2 * LINESIZE];
                sprintf(err, "File %s not found on server", ufname2);
                prepare_read_reply_file(tmpfn, ufname2, ERR_FILE_ERROR, err, cid, &sindex, &eindex);
                send_file(connection, tmpfn, NULL);
                remove_temp_file(tmpfn);
                return ERR_FILE_NOT_FOUND;
            }
        } else if ((strcmp(k, "COMMAND") == 0) && (strcmp(v, "DELETE") == 0)) {
            // Get CID from second line
            fgets(linebuff, LINESIZE, fp);
            int cid;
            int fs1 = sscanf(linebuff, "ID=%d\n", &cid);

            // Get File name form third line
            fgets(linebuff, LINESIZE, fp);
            strip_newline(linebuff);
            char ufname[2 * LINESIZE], ufname2[LINESIZE];
            int fs2 = sscanf(linebuff, "NAME=%s", ufname2);
            sprintf(ufname, "%s/%s", SERVER_DIRECTORY, ufname2);

            // Get s and end index
            int sindex, eindex;
            int *psindex = &sindex, *peindex = &eindex;
            char c, d;

            fgets(linebuff, LINESIZE, fp);
            int fs3 = sscanf(linebuff, "STARTINDX%c%d", &c, &sindex);
            if (fs3 == 1) psindex = NULL;

            fgets(linebuff, LINESIZE, fp);
            int fs4 = sscanf(linebuff, "ENDINDX%c%d", &d, &eindex);
            if (fs4 == 1) peindex = NULL;

            if (fs1 != 1 || fs2 != 1 || fs3 < 1 || fs4 < 1 || c != '=' || d != '=' ||
                !(cid >= 10000 && cid <= 99999)) {
                fprintf(stderr, "Invalid File\n");
                fclose(fp);
                char err[2 * LINESIZE];
                get_temp_file(SERVER_DIRECTORY, tmpfn);
                sprintf(err, "Invalid File");
                prepare_delete_reply_file(tmpfn, NULL, ERR_INVALID_FILE, err, cid, NULL, NULL);
                send_file(connection, tmpfn, NULL);
                remove_temp_file(tmpfn);
                return ERR_INVALID_FILE;
            }

            if (check_file_present(ufname) == SUCCESS) {
                // check permissions
                char p;
                if ((get_permission(cid, ufname2, &p) == SUCCESS) && (p == 'E' || p == 'O')) {
                    get_temp_file(SERVER_DIRECTORY, tmpfn);
                    prepare_delete_reply_file(tmpfn, ufname, SUCCESS, NULL, cid, psindex, peindex);
                    send_file(connection, tmpfn, NULL);
                    remove_temp_file(tmpfn);
                    return SUCCESS;
                } else {
                    char err[2 * LINESIZE];
                    get_temp_file(SERVER_DIRECTORY, tmpfn);
                    sprintf(err, "Insufficient Permission to access the file %s", ufname2);
                    prepare_delete_reply_file(tmpfn, ufname2, ERR_PERMISSION_ERR, err, cid, &sindex,
                                              &eindex);
                    send_file(connection, tmpfn, NULL);
                    remove_temp_file(tmpfn);
                    return ERR_PERMISSION_ERR;
                }
            } else {
                get_temp_file(SERVER_DIRECTORY, tmpfn);
                char err[2 * LINESIZE];
                sprintf(err, "File %s not found on server", ufname2);
                prepare_delete_reply_file(tmpfn, ufname2, ERR_FILE_ERROR, err, cid, &sindex,
                                          &eindex);
                send_file(connection, tmpfn, NULL);
                remove_temp_file(tmpfn);
                return ERR_FILE_NOT_FOUND;
            }
        } else if ((strcmp(k, "COMMAND") == 0) && (strcmp(v, "INSERT") == 0)) {
            // Get CID from second line
            fgets(linebuff, LINESIZE, fp);
            int cid;
            int fs1 = sscanf(linebuff, "ID=%d\n", &cid);

            // Get File name form third line
            fgets(linebuff, LINESIZE, fp);
            strip_newline(linebuff);
            char ufname[2 * LINESIZE], ufname2[LINESIZE];
            int fs2 = sscanf(linebuff, "NAME=%s", ufname2);
            sprintf(ufname, "%s/%s", SERVER_DIRECTORY, ufname2);

            // Get s and end index
            int index;
            int *pindex = &index;
            char c, d;

            fgets(linebuff, LINESIZE, fp);
            int fs3 = sscanf(linebuff, "INDX%c%d", &c, &index);
            if (fs3 == 1) pindex = NULL;

            char msg[LINESIZE];
            fgets(linebuff, LINESIZE, fp);
            int fs4 = sscanf(linebuff, "MSG%c\"%[^\"]\"", &d, msg);

            if (fs1 != 1 || fs2 != 1 || fs3 < 1 || fs4 < 1 || d != '=' || c != '=' ||
                !(cid >= 10000 && cid <= 99999)) {
                fprintf(stderr, "Invalid File\n");
                fclose(fp);
                char err[2 * LINESIZE];
                get_temp_file(SERVER_DIRECTORY, tmpfn);
                sprintf(err, "Invalid File");
                prepare_insert_reply_file(tmpfn, NULL, ERR_INVALID_FILE, err, cid, NULL, NULL);
                send_file(connection, tmpfn, NULL);
                remove_temp_file(tmpfn);
                return ERR_INVALID_FILE;
            }

            if (check_file_present(ufname) == SUCCESS) {
                // check permissions
                char p;
                if ((get_permission(cid, ufname2, &p) == SUCCESS) && (p == 'E' || p == 'O')) {
                    get_temp_file(SERVER_DIRECTORY, tmpfn);
                    prepare_insert_reply_file(tmpfn, ufname, SUCCESS, NULL, cid, pindex, msg);
                    send_file(connection, tmpfn, NULL);
                    remove_temp_file(tmpfn);
                    return SUCCESS;
                } else {
                    char err[2 * LINESIZE];
                    get_temp_file(SERVER_DIRECTORY, tmpfn);
                    sprintf(err, "Insufficient Permission to access the file %s", ufname2);
                    prepare_insert_reply_file(tmpfn, ufname2, ERR_PERMISSION_ERR, err, cid, NULL,
                                              NULL);
                    send_file(connection, tmpfn, NULL);
                    remove_temp_file(tmpfn);
                    return ERR_PERMISSION_ERR;
                }
            } else {
                get_temp_file(SERVER_DIRECTORY, tmpfn);
                char err[2 * LINESIZE];
                sprintf(err, "File %s not found on server", ufname2);
                prepare_insert_reply_file(tmpfn, ufname2, ERR_FILE_ERROR, err, cid, NULL, NULL);
                send_file(connection, tmpfn, NULL);
                remove_temp_file(tmpfn);
                return ERR_FILE_NOT_FOUND;
            }
        } else if ((strcmp(k, "COMMAND") == 0) && (strcmp(v, "INVITE") == 0)) {
            // Get FROM CID from second line
            fgets(linebuff, LINESIZE, fp);
            int scid;
            int fs1 = sscanf(linebuff, "FROM=%d\n", &scid);

            // Get TO CID from third line
            fgets(linebuff, LINESIZE, fp);
            int tcid;
            int fs2 = sscanf(linebuff, "TO=%d\n", &tcid);

            // Get File name form 4th line
            fgets(linebuff, LINESIZE, fp);
            strip_newline(linebuff);
            char ufname[2 * LINESIZE], ufname2[LINESIZE];
            int fs3 = sscanf(linebuff, "FILE=%s", ufname2);
            sprintf(ufname, "%s/%s", SERVER_DIRECTORY, ufname2);

            // Get Permission from 5th line
            fgets(linebuff, LINESIZE, fp);
            char perm;
            int fs4 = sscanf(linebuff, "PERMISSION=%c", &perm);

            if (fs1 < 1 || fs2 < 1 || fs3 < 1 || fs4 < 1 || !(perm == 'E' || perm == 'V') ||
                (scid == tcid)) {
                fprintf(stderr, "Invalid Invite Request\n");
                char tmpfn[TEMP_FILE_LEN];
                get_temp_file(SERVER_DIRECTORY, tmpfn);
                FILE *tfp = fopen(tmpfn, "w");
                fprintf(tfp, "COMMAND=INVITE\n");
                fprintf(tfp, "STATUS=Failed\n");
                fprintf(tfp, "ERR=Invalid Invite File\n");
                fclose(tfp);
                send_file(connection, tmpfn, NULL);
                remove_temp_file(tmpfn);
                return ERR_INVALID_FILE;
            }

            // Check if file exists on server
            if (check_file_present(ufname) != SUCCESS) {
                fprintf(stderr, "File not found on server\n");
                char tmpfn[TEMP_FILE_LEN];
                get_temp_file(SERVER_DIRECTORY, tmpfn);
                FILE *tfp = fopen(tmpfn, "w");
                fprintf(tfp, "COMMAND=INVITE\n");
                fprintf(tfp, "STATUS=Failed\n");
                fprintf(tfp, "ERR=File not found at server\n");
                fclose(tfp);
                send_file(connection, tmpfn, NULL);
                remove_temp_file(tmpfn);
                return ERR_FILE_ERROR;
            }

            // All ok => Forward request file to client
            int targetSoc; // socket id for recipient client
            P(map_sem);
            for (int i = 0; i < 10; ++i) {
                if (map_cid[i] == tcid) {
                    targetSoc = map_soc[i];
                    break;
                }
            }
            V(map_sem);

            // Check Persmissions
            char res;
            get_permission(scid, ufname2, &res);
            if (res != 'O') {
                fprintf(stderr, "Client %d has insufficient permissions on file %s\n", scid,
                        ufname2);
                char tmpfn[TEMP_FILE_LEN];
                get_temp_file(SERVER_DIRECTORY, tmpfn);
                FILE *tfp = fopen(tmpfn, "w");
                fprintf(tfp, "COMMAND=INVITE\n");
                fprintf(tfp, "STATUS=Failed\n");
                fprintf(tfp, "ERR=Insufficient permission on file %s\n", ufname2);
                fclose(tfp);
                send_file(connection, tmpfn, NULL);
                remove_temp_file(tmpfn);
                return ERR_PERMISSION_ERR;
            }

            printf("Forwarding request to client %d on socket %d\n", tcid, targetSoc);
            if (send_file(targetSoc, fname, NULL) == SUCCESS) {
                char cresf[TEMP_FILE_LEN];
                get_temp_file(SERVER_DIRECTORY, cresf);
                // Wait for response and parse it
                if (receive_file(targetSoc, cresf, NULL) == SUCCESS) {
                    FILE *cresfp = fopen(cresf, "r");
                    char linebuff[LINESIZE], match[2 * LINESIZE];
                    fgets(linebuff, LINESIZE, cresfp);
                    if (strcmp(linebuff, "COMMAND=INVITE\n") == 0) {
                        fgets(linebuff, LINESIZE, cresfp);
                        sprintf(match, "FROM=%d\n", scid);
                        if (strcmp(linebuff, match) == 0) {
                            fgets(linebuff, LINESIZE, cresfp);
                            sprintf(match, "TO=%d\n", tcid);
                            if (strcmp(linebuff, match) == 0) {
                                fgets(linebuff, LINESIZE, cresfp);
                                sprintf(match, "FILE=%s\n", ufname2);
                                if (strcmp(linebuff, match) == 0) {
                                    fgets(linebuff, LINESIZE, cresfp);
                                    sprintf(match, "PERMISSION=%c\n", perm);
                                    if (strcmp(linebuff, match) == 0) {
                                        fgets(linebuff, LINESIZE, cresfp);
                                        sprintf(match, "CLIENT_RESPONSE=Y\n");
                                        if (strcmp(linebuff, match) == 0) {
                                            fclose(cresfp);
                                            // All ok notify both clients
                                            char tmpfn2[TEMP_FILE_LEN];
                                            get_temp_file(SERVER_DIRECTORY, tmpfn2);
                                            FILE *tmpfp2 = fopen(tmpfn2, "w");
                                            fprintf(tmpfp2, "COMMAND=INVITE_SUCCESS\n");
                                            fprintf(
                                                tmpfp2,
                                                "MESSAGE=Client %d has been given permission %c on "
                                                "file %s by client %d\n",
                                                tcid, perm, ufname2, scid);
                                            fclose(tmpfp2);
                                            update_permission_file(tcid, ufname2, perm);
                                            send_file(targetSoc, tmpfn2, NULL);
                                            send_file(connection, tmpfn2, NULL);
                                        } else {
                                            fclose(cresfp);
                                            // All ok notify both clients
                                            char tmpfn2[TEMP_FILE_LEN];
                                            get_temp_file(SERVER_DIRECTORY, tmpfn2);
                                            FILE *tmpfp2 = fopen(tmpfn2, "w");
                                            fprintf(tmpfp2, "COMMAND=INVITE_FAILED\n");
                                            fprintf(tmpfp2,
                                                    "MESSAGE=Client %d has NOT been given "
                                                    "permission %c on "
                                                    "file %s by client %d\n",
                                                    tcid, perm, ufname2, scid);
                                            fclose(tmpfp2);
                                            send_file(targetSoc, tmpfn2, NULL);
                                            send_file(connection, tmpfn2, NULL);
                                        }
                                    }
                                }
                            }
                        }
                    }
                    return SUCCESS;
                } else {
                    fprintf(stderr, "Error reaching client %d on socket %d\n", tcid, targetSoc);
                    char tmpfn[TEMP_FILE_LEN];
                    get_temp_file(SERVER_DIRECTORY, tmpfn);
                    FILE *tfp = fopen(tmpfn, "w");
                    fprintf(tfp, "COMMAND=INVITE\n");
                    fprintf(tfp, "STATUS=Failed\n");
                    fprintf(tfp, "ERR=Failed to reach recipient\n");
                    fclose(tfp);
                    send_file(connection, tmpfn, NULL);
                    remove_temp_file(tmpfn);
                    return ERR_FILE_NOT_READ;
                }
            } else {
                fprintf(stderr, "Error reaching client %d on socket %d\n", tcid, targetSoc);
                char tmpfn[TEMP_FILE_LEN];
                get_temp_file(SERVER_DIRECTORY, tmpfn);
                FILE *tfp = fopen(tmpfn, "w");
                fprintf(tfp, "COMMAND=INVITE\n");
                fprintf(tfp, "STATUS=Failed\n");
                fprintf(tfp, "ERR=Failed to reach recipient\n");
                fclose(tfp);
                send_file(connection, tmpfn, NULL);
                remove_temp_file(tmpfn);
                return ERR_FILE_NOT_SENT;
            }
        }
    }

    fprintf(stderr, "Invalid File\n");
    get_temp_file(SERVER_DIRECTORY, tmpfn);
    FILE *tfp = fopen(tmpfn, "w");
    fprintf(tfp, "COMMAND=INVITE");
    fprintf(tfp, "STATUS=FAILED\n");
    fprintf(tfp, "ERR=Invalid Request\n");
    fclose(tfp);
    send_file(connection, tmpfn, NULL);
    remove_temp_file(tmpfn);

    fclose(fp);
    return ERR_INVALID_FILE;
}

int serve_request(int newsockfd) {
    char cbuff[MAX_COMMAND_LEN];
    int ret;
    if ((ret = wait_for_signal(newsockfd, cbuff, NULL)) < 0 ||
        strncmp(cbuff, PING_SIGNAL, strlen(PING_SIGNAL)) != 0) {
        if (ret == 0) {
            // Client disconnected, Check again?
            ret = recv(newsockfd, cbuff, 1, 0);
            if (ret == 0) {
                // Experimental
                P(map_sem);
                int disconnect_cid;
                for (int i = 0; i < 10; ++i) {
                    if (map_soc[i] == newsockfd) {
                        disconnect_cid = map_cid[i];
                        printf("Detected that client %d has disconnected\n", disconnect_cid);
                        /* Free slot */
                        map_soc[i] = -1;
                        map_cid[i] = -1;
                        break;
                    }
                }
                V(map_sem);
                disconnect_client(disconnect_cid);
                return ERR_CLIENT_DISCONNECT; // client discconect = exit
            }
        }
        return SUCCESS;
    }

    // Receive file
    char fname[TEMP_FILE_LEN];
    get_temp_file(SERVER_DIRECTORY, fname);
    if (receive_file(newsockfd, fname, NULL) < 0) {
        fprintf(stderr, "Failed to receive request file from socket %d\n", newsockfd);
        return ERR_SERVING_REQ;
    }

    printf("========== RECEIVED REQUEST FILE ==============\n");
    print_file(fname, NULL);
    printf("========== END OF REQUEST FILE ==============\n");

    ret = parse_request_file(newsockfd, fname);
    if (ret == 0) printf("SUCCESS\n");
    else if (ret == CLIENT_EXIT) {
        remove_temp_file(fname);
        return ERR_CLIENT_DISCONNECT;
    } else
        printf("Failed to serve the request\n");
    remove_temp_file(fname);

    return SUCCESS;
}

int main() {
    // creat server_files directory
    if (mkdir(SERVER_DIRECTORY, 0777) < 0) {
        int t = errno;
        if (t != EEXIST) {
            perror("Failed to create server_files_directory");
            exit(4);
        }
    }

    // seed random number generator
    srand(time(NULL));

    // Set up shared memory and semaphore
    setup();

    // create permission file
    FILE *fp = fopen(CLIENT_PERMISSIONS_FILENAME, "w");
    fclose(fp);

    // create records file
    fp = fopen(CLIENT_RECORDS_FILENAME, "w");
    fclose(fp);

    int port = SERVER_PORT;
    struct sockaddr_in serv_addr;
    unsigned short sin_port;
    in_addr sin_addr;
    char sin_zero[8];

    bzero((char *)&serv_addr, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_addr.s_addr = INADDR_ANY;
    serv_addr.sin_port = htons(port);

    int listener;

    listener = socket(AF_INET, SOCK_STREAM, 0);

    int yes = 1;
    setsockopt(listener, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(int));

    if (bind(listener, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0) {
        perror("ERROR on binding");
        exit(1);
    }

    listen(listener, 5);
    printf("Server: Listening for connections on PORT %d\n", port);

    struct sockaddr_in cli_addr;
    socklen_t clilen;
    clilen = sizeof(cli_addr);

    fd_set master;
    fd_set readfds;
    FD_ZERO(&master);
    FD_ZERO(&readfds);
    int fdmax;

    FD_SET(listener, &master);
    fdmax = listener;
    int nconnections_select = 0;

    char buffer[BUFFSIZE];
    while (1) {
        readfds = master;
        if (select(fdmax + 1, &readfds, NULL, NULL, NULL) < 0) {
            perror("Error in selecting connection");
            exit(4);
        }

        for (int i = 0; i <= fdmax; i++) {
            if (FD_ISSET(i, &readfds)) {
                if (i == listener) {
                    int newsockfd = accept(listener, (struct sockaddr *)&cli_addr, &clilen);
                    if (newsockfd == -1) {
                        perror("Error accepting connection");
                    } else {
                        FD_SET(newsockfd, &master);
                        if (newsockfd > fdmax) fdmax = newsockfd;
                        printf("Accepted connection from on %d\n", newsockfd);

                        if (nconnections_select < MAX_CONNECTIONS) {
                            if (send_signal(newsockfd, CONNECTION_SUCCESS) < 0) {
                                fprintf(stderr, "Error sending connection "
                                                "success message\n");
                            }
                            nconnections_select++;
                        } else {
                            if (send_signal(newsockfd, MAX_CLIENTS_REACHED) < 0) {
                                fprintf(stderr, "Error sending max clients"
                                                "message\n");
                            }
                            FD_CLR(newsockfd, &master);
                        }
                    }
                } else {
                    int ret = serve_request(i);
                    if (ret == ERR_CLIENT_DISCONNECT) {
                        printf("Connection closed %d\n", i);
                        close(i);
                        FD_CLR(i, &master);
                        nconnections_select--;
                    }
                }
            }
        }
    }
    return 0;
}
