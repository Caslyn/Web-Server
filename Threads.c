#include <netinet/in.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <pthread.h>

#define MAX_HEADER_LEN 1000
#define SHORT_HEADER_LEN 50
#define MAX_CONTENT 2056
#define MAX_THREADS 10
#define MAX_CONNECTIONS 100

 // create a threadpool struct
typedef struct thread_pool {
    pthread_mutex_t thread_lock; // lock so one thread can have exclusive access
    pthread_cond_t signal; // conditional signal to lock/unlock
    pthread_t *threads; // thread pool
    int *connection_queue; // connection queue
    int c_head; // pointer to teh beginning of the connection queue
    int c_tail; // pointer to next available place in connection queue
    int c_count; // number of connections
} thread_pool;

typedef struct headers {
    char method[SHORT_HEADER_LEN];
    char url[SHORT_HEADER_LEN];
    char protocol[SHORT_HEADER_LEN];

} headers;

int *create_socket(int *in_socket);
int bind_socket(int *in_socket, struct sockaddr_in *);

thread_pool *build_thread_pool(void);
void init_worker_thread(thread_pool *tpool);

int begin_listening(int *in_socket);
int accept_connection(int *in_socket, struct sockaddr_in *address, socklen_t *addrlen, struct thread_pool *tpool);

void serve_request(int out_socketfd);
int read_socket(int out_socketfd, struct headers *req_headers);
int parse_headers(char *req_content, struct headers *req_headers);

int send_the_file(int file, int out_socketfd);
int write_socket(int out_socketfd, struct headers *req_headers);

void clean_up_tpool(struct thread_pool *tpool);

int main(){
    thread_pool *tpool;
    struct sockaddr_in address;
    socklen_t addrlen;

    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(15000);

    int in_socket;

    if (create_socket(&in_socket) < 0) {
        return 1;
    }

    if(bind_socket(&in_socket, &address) < 0) {
        return 1;
    }

    begin_listening(&in_socket);

    tpool = build_thread_pool();

    while(1) {
      if((accept_connection(&in_socket, &address, &addrlen, tpool)) < 0) {
        exit(1);
      }
    }
    clean_up_tpool(tpool);
    return 0;
}

thread_pool *build_thread_pool(void) {
    thread_pool *tpool;
    int i;

    // allocate memory for threadpool, threads, connections separately
    tpool = (thread_pool *)malloc(sizeof(thread_pool));
    tpool->threads = (pthread_t *) malloc(sizeof(pthread_t) * MAX_THREADS);
    tpool->connection_queue = (int *) malloc(sizeof(int) * MAX_CONNECTIONS);

    tpool->c_head = 0;
    tpool->c_tail = 0;
    tpool->c_count = 0;

    pthread_mutex_init(&(tpool->thread_lock), NULL); //initialize mutex to create exclusive access
    pthread_cond_init(&(tpool->signal), NULL); // initialize conditional variable

    for (i = 0; i < MAX_THREADS; i++) {
        // create new thread and send into a waiting state
        if (pthread_create(&(tpool->threads[i]), NULL, (void *) init_worker_thread, tpool) != 0) {
            printf("Error Creating Thread");
            return NULL;
        }
    }
    return tpool;
}

void init_worker_thread(thread_pool *tpool) {
    int connection;
    while (1) {

        while (tpool->c_count == 0) {
            printf("Thread %p in Wait State\n", pthread_self());
            pthread_cond_wait(&(tpool->signal), &(tpool->thread_lock)); // release mutex and wait on conditional variable (tpool->signal)
        }
        // get first connection in buffer
        connection = tpool->connection_queue[tpool->c_head++];

        if(tpool->c_head == MAX_THREADS) {
            tpool->c_head = 0;
        }

        tpool->c_count -= 1;
        // process connection (connection has already been accepted).
        serve_request(connection);

   }
   pthread_exit(NULL);
}

void serve_request(int socketfd) {
    struct headers *req_headers = (headers *) malloc(sizeof(struct headers));

    printf("Thread %p is Serving Request\n", pthread_self());
    read_socket(socketfd, req_headers);
    write_socket(socketfd, req_headers);
    free(req_headers);
}

void clean_up_tpool(struct thread_pool *tpool) {
    thread_pool *pool = tpool;
    int i;
    // Join threads in order to free together
    for (i = 0; i < MAX_THREADS; i++) {
        pthread_join(pool->threads[i], NULL);
    }

    free(pool);
}

int *create_socket(int *in_socket){
    if ((*in_socket = socket(AF_INET, SOCK_STREAM, 0)) > 0) {
        printf("The socket was created.\n");
    } else {
        printf("Error creating socket\n");
    }
    return in_socket;
}

int bind_socket(int *in_socket, struct sockaddr_in *address){
     if (bind(*in_socket, (struct sockaddr *) address, sizeof(*address)) == 0) {
        printf("Binding Socket\n");
    } else {
        printf("Error Binding Socket\n");
        return -1;
    }
     return 0;
}

int begin_listening(int *in_socket){

    if (listen(*in_socket, MAX_CONNECTIONS) < 0) {
        perror("server: listen");
        return -1;
    }

    return 0;
}

int accept_connection(int *in_socket, struct sockaddr_in *address, socklen_t *addrlen, struct thread_pool *tpool) {
    int next;
    int socketfd;

    if ((socketfd = accept(*in_socket, (struct sockaddr *) address, addrlen)) > 0) {
        printf("The Client is Connected...\n");
    } else {
        perror("server: accept");
        return -1;
    }
    next = tpool->c_tail + 1;

    if (next == MAX_THREADS) { // the next thread will be the first thread in pool
        next = 0;
     }

     if (tpool->c_count == MAX_CONNECTIONS) {
          printf("Max Connections Reached\n");
          return -1;
     }

    tpool->connection_queue[tpool->c_tail] = socketfd; // add connection to end of connection buffer
    tpool->c_count++;
    tpool->c_tail = next;
    pthread_cond_signal(&(tpool->signal)); // signal to threadpool that connection is waiting
    return 0;
}

int read_socket(int socketfd, struct headers *req_headers){
    int offset;
    char req_content[MAX_CONTENT];

    if ((offset = recv(socketfd, req_content, MAX_CONTENT, 0)) > 0) {
      req_content[offset] = '\0';
      parse_headers(req_content, req_headers); // parse headers, passing in content
      printf("%s", req_content);
      return 0;
    } else {
        printf("Error Recieving Request");
        return -1;
    }
}

int parse_headers(char *req_content, struct headers *req_headers) {
    int i;

    for(i = 0; *req_content != ' '; i++) {
        req_headers->method[i] = *req_content++;
    }
    req_headers->method[i] = '\0';
    req_content = req_content + 2; // skip past space and pre-emptive '/'

    for(i = 0; *req_content != ' '; i++) {
      req_headers->url[i] = *req_content++;
    }
    req_headers->url[i] = '\0';
    return 0;
}

int write_socket(int socketfd, struct headers *req_headers){
      int file;
      if ((file = open(req_headers->url, O_RDONLY)) >= 0) {
        if (!send_the_file(file, socketfd)) {
          return 0;
        }
      } else {
        file = open("views/not_found.html", O_RDONLY);
        send_the_file(file, socketfd);
      }
      return 0;
}

int send_the_file(int file, int socketfd) {
    off_t offset = 0, size = 150000;
    if (sendfile(file, socketfd, offset, &size, 0,0) < 0) {
        printf("Error Sending File");
        return -1;
    }
    close(file);
    return 0;
}
