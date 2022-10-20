# FTP-implementation-over-TCP

🗃️ C++ low-level implementation of an FTP (File Transfer Protocol) server-client connection over Transmission Control Protocol/Internet Protocol (TCP/IP) using the Linux Programming Interface.

An implementation of the server-client model. The code for the server can be found in the file `dataServer.cpp`, while the code for the client can be found in the file `remoteClient.cpp`.

The server starts, creates the necessary worker threads and waits for clients to connect to it. When a client is started, it is connected to the server on the predefined port. The server creates a new connection thread for each new server-client connection, and reads the information sent by the client, about the directory that shall be copied. It then iterates through this directory, discovers its structure, and places the files it contains into a queue, along with information about the client to which they should be sent. At the same time, the worker threads read the contents of the queue and send the requested files to the desired client, using an appropriate protocol designed from scratch for this purpose and described in detail in the code comments. The client, then, reads the data sent by the server and separates the protocol header information from the contents of the files. It then creates the appropriate files and writes their contents as they have been sent by the server.

- The synchronization of the above actions is controlled by pthread mutexes, used for access to the queue, so that changes to it are not made at the same time. Queue access mutexes, by extension control writing to sockets so that multiple worker threads do not simultaneously write to a socket. This creates the limitation that each time, only one worker thread writes to the sockets. In order to avoid it, a trylock should be used, which would, however, create a race condition, which would then lead to much larger instability problems in the operation of the program.

- The contents of the files are sent by the server to the clients in blocks of size given by the user. The client, however, reads these contents one byte at a time. An attempt was made to avoid this limitation, but again, greater problems were created without substantial gain.

- The queue is implemented using the C++ queue library and a check is made before adding a new element to it, in order to maintain an upper bound of its size, as given by the user.

The program makes efficient memory management and frees dynamically allocated bytes as soon as possible.

Compile server and client at once, when they are both in the same directory with:

```
make
```
or separately with `make dataServer` and `make remoteClient` respectively within the directory each one is located.

Run the server with:

```
./dataServer -p <port> -s <thread_pool_size> -q <queue_size> -b <block_size>
```
e.g. when run on linux30.di.uoa.gr (Department of Informatics and Telecommuncations / University of Athens linux server)
```
./dataServer -p 5180 -s 2 -q 3 -b 4
```

Run the client with:

```
./remoteClient -i <server_ip> -p <server_port> -d <directory>
```
e.g. when run on linux30.di.uoa.gr (Department of Informatics and Telecommuncations / University of Athens linux server)
```
./remoteClient -i 195.134.65.21 -p 5180 -d test_data
```

Using `make run`, `./dataServer -p 5180 -s 2 -q 3 -b 4` is executed, while a `make clean` deletes any object or executable files created.