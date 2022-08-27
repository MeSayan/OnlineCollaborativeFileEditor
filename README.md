# About the Design

Every request is a file. Similar to HTTP packet, each (request / response) file has
all details necessary to serve the request or indicate the error. And each request /response is always atomic.

These files are exchanged between server and client via a HANDSHAKING mechanism for fault tolerance. Before sending / receving file, a PING signal must be sent to the other
side to go into send / receiving state.

If the client disconnects while server is on, the server will detect this and remove files from the client
updating permissions.

If the client fails to reach the server, it will keep track of failed attempts and after
MAX_FAILED_ATTEMPTS the client will exit.

Both server and clients run select command to poll connections. Client runs select on stdin, and the socket connection to server.
Server runs select on all its socket connections to clients, and the listening socket to accept a new connection, maintaining upto 5 concurrent connectios and letting other connection requests know that it has reached max clients.

## Description of source files
1. server.cpp - This file contains the implementations of server
2. client.cpp - This file contains the implementation of client
3. common.cpp - Contains common functionality, like checking if a file exists, get a temp file, send / recevie file etc
4. constant.cpp - This file contains all constants and error definitions

## Usage

### COMMANDS TO RUN THE FILE

Server
```
g++ -g server.cpp -o server.out && ./server.out
```

Client
```
g++ client.cpp -o client.out && ./client.out
```

**Note** The programs will automatically create there own directories, server_files/ and /client_files.
Before running the server / clinet please ensure that there directories are
not present by running these (if needed)
```
rm -rf server_files/
rm -rf client_files/
```

Files uploaded to server are saved in server_files.
Files downloaded from server are saved in current working directory.
For uploading files the file must be present in the same directory as client.cpp

# Send File & Receive File Handshaking Process

|SEND FILE                        |             RECEIVE FILE      |
|---------------------------------|-------------------------------|
|SEND ACK                         |                               |
|                                 |            WAIT FOR ACK       |
|                                 |            SEND ACK           |
|WAIT FOR ACK                     |                               |
|Send 4 bytes for file size       |                               |
|Send remaining bytes of file (b) |                               |
|                                 |             Read (b+4) bytes  |
|SEND ACK                         |                               |
|                                 |            WAIT FOR ACK       |
|                                 |            SEND ACK           |
|WAIT FOR ACK                     |                               |
|SEND COMPLETE                    |           RECEIVE COMPLETE    | 

## Merits of the Design:

1. Wrapping up the sending and receiving files in a handshaking setup 
2. Ensuring every request / response is itself a file. For example request file for upload is shown below

# Sample Request-Response Format

## Read Request File Format
```
COMMAND=READ
ID=10002
NAME=file_2.txt
STARTINDX=
ENDINDX=
```

## Response Request File Format
```
COMMAND=READ
STATUS=SUCCESS
MESSAGE=file_2.txt read
This is some line with line no 1
This is some line with line no 2
This is some line with line no 3
This is some line with line no 4
This is some line with line no 5
This is some line with line no 6
This is some line with line no 7
This is some line with line no 8
```

Similarly for any command,  Request-Response  pair file formats have been
fixed in design. Clients and Servers send these files and parse the file
to obtain parameters or print errror messages

```
COMMAND=READ
STATUS=FAILED
ERR=File file_2.txt not found on server
```

```
COMMAND=INSERT
STATUS=FAILED
ERR=Insufficient Permission to access the file file_2.txt
```

Client IDs  are stored in server_files/client_records.txt file
Persmissions are stored in server_files/client_persmission.txt file.
Each entry in the permissions file is of the format

```
10000##!##file_1.txt##!##O
```

During program execution temporary files are created in server_files, client_files directory. 
**ALL** temporary files are of the format
````
[server_files | client_files]/<pid>_<unix timestamp>_<random 5 digit number>.tmp
````
For example a temp file - **server_files/14098_1644557791_68860.tmp**


These temp files are Request/Response Encapsulations.

### Usage of semaphores:
1. cid_sem : Used to maintain mutual exclusion on 5 digit connection id
2. crf_sem : Used to maintain mutual exclusion on client records file
3. cpf_sem : Used to maintain mutual exclusion on client permission file
4. map_sem : Used to maintain mutual exclusion on map_cid, map_soc which are 2, 10 length arrays for maintainig mapping between cid and socket id
