#include <iostream>
#include <stdlib.h>
#include <string>
#include <cstring>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <time.h>
#include <dirent.h>
#include <iostream>
#include <queue>
#include <pthread.h>
#include <deque>
#include <map>

// Custom print error function
#define perror2(s, e) fprintf(stderr, "%s: %s\n", s, strerror(e))

using namespace std;

// Custom print error function
void perror_exit(string message){
    perror(message.c_str());
    exit(EXIT_FAILURE);
}

// An item in the queue contains the name of the file and the socket to which it must be sent
typedef struct queue_item{
    char file_name[4096];
    FILE* sock_fp;
} queueItem;


///////////////////////////////////////////////////////////////////////////////////////////////////////
////////// GLOBAL VARIABLES TO BE SHARED AMONG ALL THREADS AND FUNCTIONS //////////////////////////////
///////////// AN APPROACH USING ARGUMENTS WAS MUCH MUCH MORE COMPLEX //////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////////////////////////
                                                                                                    ///
int block_size = 0; // Block size in bytes for the files that the workers send                      ///
                                                                                                    ///
pthread_mutex_t queue_lock = PTHREAD_MUTEX_INITIALIZER; // mutex for accessing the queue            ///
int queue_size = 0;                                                                                 ///
queue<queueItem> files_queue;   // The queue containing the files to be sent                        ///
                                                                                                    ///
// A map to match sockets with the number of files that must be sent through each socket            ///
// We need it in order to know when to close the socket (i.e. when files for this socket reach 0)   ///
map<FILE*, int> files_per_socket;                                                                   ///
                                                                                                    ///
///////////////////////////////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////////////////////////


// send a file fp to a client through sock_fp using write() (size bytes in every write() call)
int write_file_to_client(FILE* sock_fp, FILE* fp, size_t size){

    int n;
    int sent = 0;
    int fd = fileno(sock_fp);   // get file descriptor of the socket
    char* str = new char[size];
    while(fgets(str, size+1, fp)){  // read contents of the file per size bytes
        n = write(fd, str, size);   // write size bytes to client
        sent += n;
        memset(&str[0], 0, sizeof(str));    // empty buffer
    }
    delete[] str;
    return sent;
}

// explore files in a directory recursively and add them to the queue
void exploreFilesRecursively(char *base_path, queue<queueItem> &files_queue, FILE* sock_fp){
    int err;
    char path[4096];
    memset(&path[0], 0, sizeof(path));
    struct dirent *dp;
    DIR* dir_stream = opendir(base_path);    // open the desired directory stream

    if (!dir_stream)    // return if NULL (directory stream could not be opened)
        return;

    while ((dp = readdir(dir_stream)) != NULL){ // get next directory entry
        if (strcmp(dp->d_name, ".") != 0 && strcmp(dp->d_name, "..") != 0){ // if it is not the current or the previous directory

            // create new path based on the base_path given by the user and the directory entry currently read
            strcpy(path, base_path);
            strcat(path, "/");
            strcat(path, dp->d_name);

            if (dp->d_type != DT_DIR){  // if it is a file and not a directory
                while (1){
                    // try to lock the queue
                    if (err = pthread_mutex_lock(&queue_lock)){
                        perror2("pthread_mutex_lock", err);
                        exit(1);
                    }
                    if(files_queue.size() < queue_size){    // and if there is empty space
                        queueItem queue_item1;  // create a new entry - struct
                        memset(&(queue_item1.file_name)[0], 0, 4096);   // initialize file name as empty
                        strcpy(queue_item1.file_name, path);    // copy the path to the new entry
                        queue_item1.sock_fp = sock_fp;  // copy the socket file pointer to the new entry
                        files_queue.push(queue_item1);  // and add it to the queue
                        // also update the map containing the number of files for this socket file pointer
                        files_per_socket.insert(pair<FILE*, int>(sock_fp, files_per_socket[sock_fp]+=1));
                        // try to unlock the queue
                        if (err = pthread_mutex_unlock(&queue_lock)){
                            perror2("pthread_mutex_unlock", err);
                            exit(1);
                        }
                        break;  // also assures that unlock is not performed twice
                    }
                    // try to unlock the queue
                    if (err = pthread_mutex_unlock(&queue_lock)){
                        perror2("pthread_mutex_unlock", err);
                        exit(1);
                    }
                }
            }

            exploreFilesRecursively(path, files_queue, sock_fp);    // continue resursive exploring according to new path
        }
    }

    closedir(dir_stream);
}

// code for communication threads
void* communication_thread(void* arg){

    // get client socket file pointer and try to open it for both reading and writing
    FILE* sock_fp;
    int csock = *(int*)arg;
    if ((sock_fp = fdopen(csock, "r+")) == NULL)
        perror_exit("fdopen");

    // read from the socket the name of the directory that we want to explore and send
    char dirname[4096];
    if (fgets(dirname, BUFSIZ, sock_fp) == NULL)
        perror_exit("reading dirname");
    dirname[strcspn(dirname, "\n")] = '\0'; // find first '\n' occurence and terminate the string there

    exploreFilesRecursively(dirname, files_queue, sock_fp);

    // now that all the directory has been explored recursively, wait for the workers to finish (i.e. 0 files for the socket)
    // and close the connection. Also erase the socket file pointer from the map. 
    while(1){
        if (files_per_socket[sock_fp] == 0){
            if (fclose(sock_fp) != 0){
                perror2("close file", errno);
            }
            map<FILE*, int>::iterator it;
            it=files_per_socket.find(sock_fp);
            files_per_socket.erase(it);
            break;
        }
    }

    // let the thread release its resources when it terminates
    int err;
    if (err = pthread_detach(pthread_self())){
        perror2("pthread_detach", err);
        exit(1);
    }

    pthread_exit(NULL);
}

// code for worker threads
void* worker_thread(void* arg){
    int c, err;
    while(1){

        // try to lock the queue
        if (err = pthread_mutex_lock(&queue_lock)){
            perror2("pthread_mutex_lock", err);
            exit(1);
        }

        if (files_queue.size() > 0){    // avoid empty queue

            // get first item of the queue
            queueItem queue_item1 = files_queue.front();

            // open the file described the file name (path)
            FILE* fp;
            fp = fopen(queue_item1.file_name, "r");

            // count file characters (to include it in the protocol)
            int count = 0;
            for (c = getc(fp); c != EOF; c = getc(fp))
                count = count + 1;

            rewind(fp); // get back to the beginning of the file

            int fd = fileno(queue_item1.sock_fp);   // get file descriptor of the socket file pointer

            // We define a protocol in which we deliver each file beginning with a '~', accompanied by the file name
            // the file name ends with '~' which is then followed by the number of characters of the file. Finally
            // one more '~' is sent and then the actual file contents are transferred.
            write(fd, "~", 1);

            write(fd, queue_item1.file_name, 4096);

            write(fd, "~", 1);

            char str[256];  // assume that the number of characters in a file can be converted to a 256 characters string
            memset(&str[0], 0, sizeof(str));
            sprintf(str, "%d", count);
            write(fd, str, sizeof(str));
            
            write(fd, "~", 1);
            
            write_file_to_client(queue_item1.sock_fp, fp, block_size);

            // close the file described the file name (path)
            if (fclose(fp) != 0){
                perror2("close file", errno);
            }

            files_queue.pop();  // pop the queue item we just processed
            // Update the corresponding map by decrementing the files for this socket by one
            files_per_socket.insert(pair<FILE*, int>(queue_item1.sock_fp, files_per_socket[queue_item1.sock_fp]-=1));
        }

        // try to unlock the queue
        if (err = pthread_mutex_unlock(&queue_lock)){
            perror2("pthread_mutex_unlock", err);
            exit(1);
        }
    }

    // let the thread release its resources when it terminates
    if (err = pthread_detach(pthread_self())){
        perror2("pthread_detach", err);
        exit(1);
    }

    pthread_exit(NULL);
}

int main(int argc, char* argv[]){

    int port = 0;
    int thread_pool_size = 0;

    // get command line arguments
    if (argc == 9){
        // iterate through arguments - when position is an odd number we have a option and the next
        // argument is the variable for the option
        for (int i = 0; i < argc; i++){
            if (i % 2){
                if (!strcmp(argv[i], "-p")){
                    port = atoi(argv[i+1]);
                }else if (!strcmp(argv[i], "-s")){
                    thread_pool_size = atoi(argv[i+1]);
                }else if (!strcmp(argv[i], "-q")){
                    queue_size = atoi(argv[i+1]);
                }else if (!strcmp(argv[i], "-b")){
                    block_size = atoi(argv[i+1]);
                }
            }
        }
    }else{
        perror("wrong arguments");
    }

    // keep worker thread ids in a vector for further use
    vector<pthread_t> wtids;

    // create thread_pool_size number of worker threads and add them to the vector
    int err;
    for (int i = 0; i < thread_pool_size; i++){
        pthread_t returned;
        if (err = pthread_create(&returned, NULL, worker_thread, NULL)){
            perror2("pthread_create", err);
            exit(1);
        }
        wtids.push_back(returned);
    }

    int lsock, csock;

    // create listening socket through internet
    if ((lsock = socket(AF_INET, SOCK_STREAM, 0)) == -1) {
        perror("socket");
        exit(-1);
    }

    // set socket options (we want the socket to be reusable itself)
    int enable = 1;
    if (setsockopt(lsock, SOL_SOCKET, SO_REUSEADDR, &enable, sizeof(int)) < 0)
        perror("setsockopt(SO_REUSEADDR) failed");

    struct sockaddr_in myaddr ; // structure for handling internet addresses

    myaddr.sin_addr.s_addr = htonl(INADDR_ANY); // get any address and convert to network byte order
    myaddr.sin_port = htons(port);  // covnert port to network byte order
    myaddr.sin_family = AF_INET;    // we will use Internet Protocol v4 addresses

    if (bind(lsock, (struct sockaddr*)&myaddr, sizeof(myaddr))) // bind address in myaddr to listening socket
        perror_exit("bind");

    // mark as passive socket (will be used to accept incoming connection requests using accept())
    // arbitary queue size = 5
    if (listen(lsock, 5) != 0)
        perror_exit("listen");

    // create a vector to hold communication thread ids
    vector<pthread_t> ctids;

    // wait for new connections, and every time a client is connected to the server
    while (1){
        // extract the first connection request on the queue of pending connections for the listening socket,
        // create a new connected socket, and return a new file descriptor referring to that socket
        if ((csock = accept(lsock, NULL, NULL)) < 0)
            perror_exit("accept");

        // create new communication thread
        pthread_t returned;
        if (err = pthread_create(&returned, NULL, communication_thread, (void*)&csock)){
            perror2("pthread_create", err);
            exit(1);
        }
        ctids.push_back(returned);  // add it to the corresponding vector
    }

    return 0;
}