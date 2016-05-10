#include <netinet/in.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h> 
#include <unistd.h>

#define MAX_HEADER_LEN 1000
#define MAX_INPUT 2056

typedef struct headers {
    char    url[MAX_HEADER_LEN];
} headers;

typedef struct request {
    char                contents[MAX_INPUT];
    struct headers      headers;
    int                 offset;
    int                 capacity;
    int                 in_socket_fd;
    int                 out_socket_fd;
} request;

struct request *request_init(char *);
int create_socket(struct request *);

int bind_socket(struct request *, struct sockaddr_in *);
int begin_listening(struct request *req);
int accept_connection(struct request *req, struct sockaddr_in *address, socklen_t *addrlen);

void read_socket(struct request *req);
void get_url(struct request *req);

int send_the_file(int file, struct request *req);
int write_socket(struct request *);
void send_default(struct request *);

void clean_up(struct request *);

int main(){
    int new_socket, pid;

    struct request *req = calloc(1, sizeof(request));
    struct sockaddr_in address;
    socklen_t addrlen;

    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(15000);

    if (create_socket(req) < 0) {
        clean_up(req);
        return 1;
    }

    if(bind_socket(req, &address) < 0) {
        clean_up(req);
        return 1;
    }

    begin_listening(req);
    while(1) {
        if(accept_connection(req, &address, &addrlen) < 0) {
            exit(1);
        }
        if ((pid = fork()) == 0) {
            printf("Spawning Child Process...\n");
            close(req->in_socket_fd);
            read_socket(req);
            write_socket(req);
            exit(0);
        } else if(pid == -1) {
            printf("Error Spawning Child\n");
            exit(1);
        }
    };
    clean_up(req);
    return 0;
}

void clean_up(struct request *req) {
    if (req->in_socket_fd) {
        close(req->in_socket_fd);
    }
    if (req->out_socket_fd) {
        close(req->out_socket_fd);
    }
    free(req);
}

int create_socket(struct request *req){
    if ((req->in_socket_fd = socket(AF_INET, SOCK_STREAM, 0)) > 0) {
        printf("The socket was created.\n");
        return 0;
    } else {
        printf("Error creating socket\n");
        return -1;
    };
}

int bind_socket(struct request *req, struct sockaddr_in *address){
     if (bind(req->in_socket_fd, (struct sockaddr *) address, sizeof(*address)) == 0) {
        printf("Binding Socket\n");
    } else {
        printf("Error Binding Socket\n");
        return -1;
    }
    return 0;
}

int begin_listening(struct request *req){

    if (listen(req->in_socket_fd, 10) < 0) {
        perror("server: listen");
        return -1;
    }

    return 0;
}

int accept_connection(struct request *req, struct sockaddr_in *address, socklen_t *addrlen) {
    if ((req->out_socket_fd = accept(req->in_socket_fd, (struct sockaddr *) address, addrlen)) < 0) {
        perror("server: accept");
        return -1;
    }

    if (req->out_socket_fd > 0) {
        printf("The Client is connected...\n");
    }

    return 0;
}

void read_socket(struct request *req) {
    req->offset = recv(req->out_socket_fd, req->contents, MAX_INPUT, 0);
    req->contents[req->offset] = '\0';
    printf("%s", req->contents);
    get_url(req);
}

void get_url(struct request *req) {
    int i = 0, j = 0;
    while (req->contents[i++] != ' ');
    i++;
    while (req->contents[i] != ' ') {
       req->headers.url[j++] = req->contents[i++];
    }
    req->headers.url[j] = '\0';
}

int write_socket(struct request *req){
      int file;
      if ((file = open(req->headers.url, O_RDONLY)) >= 0) {
        if (!send_the_file(file, req)) {
          return 0;
        }
      }
      send_default(req);
      return 0;
}

int send_the_file(int file, struct request *req) {
    off_t offset = 0, size = 150000;
    printf("Found requested file...\n\n");
    if (sendfile(file, req->out_socket_fd, offset, &size, 0,0) < 0) {
        printf("Error Sending File");
        return -1;
    }
    close(file);
    return 0;
}

void send_default(struct request *req) {
    printf("Send default page\n");
    char status[MAX_HEADER_LEN] = "HTTP/1.1 400 Not Found\n";
    char message[MAX_HEADER_LEN] = "<html><body><h1>Page doesn't exist</h1></body></html>\n\n";
    char ctnt_type_hdr[MAX_HEADER_LEN] = "Content-Type: text/html\n\n";
    char *ctnt_len_hdr = "Content-Length: ";
    char ctnt_len_val[20];
    sprintf(ctnt_len_val, "%d\n", (int) strlen(message));

    send(req->out_socket_fd, &status, strlen(status), 0);
    send(req->out_socket_fd, &ctnt_len_hdr, strlen(ctnt_len_hdr), 0);
    send(req->out_socket_fd, &ctnt_len_val, strlen(ctnt_len_val), 0);
    send(req->out_socket_fd, &ctnt_type_hdr, strlen(ctnt_type_hdr), 0);
    send(req->out_socket_fd, &message, strlen(message), 0);
}
