#include <string>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <unistd.h>
#include <string.h>
#include <iostream>
#include <sys/stat.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

// Custom print error function
#define perror2(s, e) fprintf(stderr, "%s: %s\n", s, strerror(e))

using namespace std;

// Custom print error function
void perror_exit(string message){
    perror(message.c_str());
    exit(EXIT_FAILURE);
}

// Custom function to write up to size bytes of buffer buff to a file descriptor fd
int write_all(int fd, const void* buff, size_t size){
    int sent, n;
    for (sent = 0; sent < size; sent += n){
        if ((n = write(fd, (char*)buff + sent, size - sent)) == -1)
            return -1; /* error */
    }
    return sent;
}

// Count occurences of char c in char* string
int count_characters(char* string, char c){
    int count = 0;
    int i = 0;
    while(string[i] != '\0'){
        if(string[i] == c){
            count++;
        }
        i++;
    }
    return count;
}

int main(int argc , char* argv []){

    struct sockaddr_in servadd; // structure for handling internet addresses
    int PORTNUM;    // port number
    struct hostent* hp; // hostent structure pointer to receive returned info from gethostbyaddr
    int sock, n_read;
    char dirname[4096];
    memset(&dirname[0], 0, sizeof(dirname));
    char ip[15] = "";   // IPv4 addresses have a maximum length of 15 characters

    // get command line arguments
    if (argc == 7){
        // iterate through arguments - when position is an odd number we have a option and the next
        // argument is the variable for the option
        for (int i = 0; i < argc; i++){
            if (i % 2){
                if (!strcmp(argv[i], "-i")){
                    strcpy(ip, argv[i+1]);
                }else if (!strcmp(argv[i], "-p")){
                    PORTNUM = atoi(argv[i+1]);
                }else if (!strcmp(argv[i], "-d")){
                    strcpy(dirname, argv[i+1]);
                }
            }
        }
    }else{
        perror("wrong arguments");
    }

    // create socket through internet
    if ((sock = socket(AF_INET, SOCK_STREAM, 0)) == -1)
        perror_exit("socket");

    // set socket options (we want the socket to be reusable itself)
    int enable = 1;
    if (setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &enable, sizeof(int)) < 0)
        perror("setsockopt(SO_REUSEADDR) failed");
    
    // lookup server’s address and connect there
    struct in_addr myaddress ;
    inet_aton(ip, &myaddress);
    if ((hp = gethostbyaddr((const char*)&myaddress, sizeof(myaddress), AF_INET)) == NULL){
        herror("gethostbyname");
        exit(1);
    }

    memcpy(&servadd.sin_addr, hp->h_addr, hp->h_length);    // copy address taken from gethostbyaddr to servadd
    servadd.sin_port = htons(PORTNUM);  // covnert port to network byte order
    servadd.sin_family = AF_INET;   // we will use Internet Protocol v4 addresses

    // connect the socket sock to the address in serveradd
    if (connect(sock, (struct sockaddr*)&servadd, sizeof(servadd)) != 0)
        perror_exit("connect");

    // send desired directory name to the server and end with '\n'
    if (write_all(sock, dirname, strlen(dirname)) == -1)
        perror_exit("write");
    if (write_all(sock, "\n", 1) == -1)
        perror_exit("write");
    
    // initialize a buffer of size 2
    // we will read from server 1 byte at a time and add a terminating '\0'
    int BUFFSIZE = 2;
    char buffer[BUFFSIZE];

    // we need a path_and_metadata array where we store the header of the protocol
    // 4096 bytes for the maximum file path length, 256 bytes for the maximum characters in a file, 3 bytes for the separaing '~'s
    char path_and_metadata[4096+256+3];
    memset(&path_and_metadata[0], 0, sizeof(path_and_metadata));

    int size = 0;   // how far we have reached within the array
    int count = 0;  // how many '~' we have found so far
    int file_characters;    // characters contained in the file

    while ((n_read = read(sock, buffer, 1)) > 0){   // read 1 byte at a time
        if (n_read > 0 && (buffer[0] != '\0')){ // if we have read something valid
            buffer[1] = '\0';   // terminate the buffer
            strcpy(path_and_metadata+size, buffer); // copy the content to the appropriate position within the path_and_metadata array
            size += strlen(buffer); // increment size by one

            // In the protocol that we have created, we deliver each file beginning with a '~', accompanied by the file name
            // the file name ends with '~' which is then followed by the number of characters of the file. Finally
            // one more '~' is sent and then the actual file contents are transferred.
            count = count_characters(path_and_metadata, '~');   // count how many '~' we have found so far
            if (count == 3){    // only when 3 are found the protocol header is read completely
                char *save_ptr, *save_ptr_dir;  // to be used in strtok_r()
                char* token;
                token = strtok_r(path_and_metadata, "~", &save_ptr);    // get path with filename in the end
                char* token_dir;
                token_dir = strtok_r(token, "/", &save_ptr_dir);    // get every different directory of the path by separating with '/'
                char current_path[4096];    // our path so far, by the concatenated strtok_r() results
                memset(&current_path[0], 0, sizeof(current_path));
                while (token_dir != NULL){
                    strcat(current_path, token_dir);    // add strtok_r() result to current path
                    // continute strtok_r() here to avoid creating a directory with the name of file in the end of the path
                    token_dir = strtok_r(NULL, "/", &save_ptr_dir);
                    if (token_dir){ // if not NULL add a '/' and check if directory (with path) exists
                        strcat(current_path, "/");
                        struct stat st = {0};
                        if (stat(current_path, &st) == -1){
                            mkdir(current_path, 0700);  // if it does not exist, create it
                        }
                    }
                }
                token = strtok_r(NULL, "~", &save_ptr);
                file_characters = atoi(token);  // get number of characters in file from protocol header

                remove(current_path);   // remove the file if it already exists
                FILE* fp = fopen(current_path, "w");    // create file in write mode
                int fd = fileno(fp);    // get file descriptor from file pointer

                int characters_written = 0; // characters written to file so far

                while ((n_read = read(sock, buffer, 1)) > 0){   // continue reading from socket
                    if (n_read > 0 && (buffer[0] != '\0')){ // terminate buffer with '/0'
                        buffer[1] = '\0';
                        if (write(fd, buffer, n_read) < n_read) // write to file
                            perror_exit("fwrite");
                        characters_written++;   // one more character written to the file
                        if (characters_written == file_characters)  // check when all the characters are written in the file
                            break;
                    }
                }
                if (fclose(fp) != 0){
                    perror2("close file", errno);
                }

                // reset
                memset(&path_and_metadata[0], 0, sizeof(path_and_metadata));
                count = 0;
                size = 0;
            }
        }
    }

    return 0;
}