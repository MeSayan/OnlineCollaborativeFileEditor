/*
Creator : Sayan Mahapatra
Date : 06-02-2022
Description : This file contains macro definitions for constants used in project
*/

#ifndef CONSTANTS_HEADER_GUARD
#define CONSTANTS_HEADER_GUARD

#define SERVER_PORT 5003
#define SUCCESS 0
#define ERR_TX -2
#define ERR_CONNECTION_CLOSED -3
#define ERR_FILE_NOT_SENT -4
#define ERR_FILE_NOT_READ -4
#define ERR_FILE_ERROR -5

#define BUFFSIZE 256
#define LINESIZE 512
#define MAX_LINES_IN_FILE 5000
#define MAX_FILES 200

#define ERR_INVALID_FILE -6
#define ERR_INVALID_DATE -7
#define ERR_INVALID_FIELD -8
#define ERR_UNSORTED_FILE -9

#define ERR_SENDING_SIGNAL -10
#define ERR_RECEIVING_SIGNAL -11
#define ERR_FILE_NOT_FOUND -12
#define ERR_SERVING_REQ -13

#define ERR_TIMEOUT -14
#define ERR_FILE_EXISTS -15

#define ERR_PERMISSION_ERR -16

#define ERR_CLIENT_NOT_REGISTERED -17
#define ERR_INVALID_COMMAND -18

#define ERR_CLIENT_DISCONNECT -19

#define DEFAULT_TIMEOUT 60

#define MAX_COMMAND_LEN 20
#define MAX_SIGNAL_LEN 10

#define MAX_FAILED_ATTEMPTS 3

#define TEMP_FILE_LEN 60
#define FILE_NAME_LEN 60

// Signal definitinos
#define ACK_SIGNAL (char *)"ACK"
#define FAIL_SIGNAL (char *)"FAIL"
#define FINISH_SIGNAL (char *)"FINISH"
#define FAILURE_SIGNAL (char *)"FAILURE"
#define MAX_CLIENTS_REACHED (char *)"MCR"
#define CONNECTION_SUCCESS (char *)"CSC"
#define PING_SIGNAL (char *)"PING"

#define CLIENT_EXIT -15

#define SERVER_DIRECTORY (char *)"server_files"
#define CLIENT_DIRECTORY (char *)"client_files"

#define CLIENT_RECORDS_FILENAME (char *)"server_files/client_records.txt"
#define CLIENT_PERMISSIONS_FILENAME (char *)"server_files/client_permissions.txt"

#define FILE_TX_START (char)2 // ASCII Non Printable character STX
#define FILE_TX_STOP (char)3  // ASCII Non Printable character ETX

#define MAX_CONNECTIONS 5

#endif
