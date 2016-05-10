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

int *create_socket(int *in_socket);
int bind_socket(int *in_socket, struct sockaddr_in *);

thread_pool *build_thread_pool(void);
void init_worker_thread(thread_pool *tpool);

int begin_listening(int *in_socket);
int accept_connection(int *in_socket, struct sockaddr_in *address, socklen_t *addrlen, struct thread_pool *tpool);

void serve_request(int out_socketfd);
char *read_socket(int out_socketfd);
char *get_url(char *content_buffer);

int send_the_file(int file, int out_socketfd);
int write_socket(int out_socketfd);
void send_default(int out_socketfd);

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
    printf("Thread %p is Serving Request\n", pthread_self());
    read_socket(socketfd);
    write_socket(socketfd);
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

char *read_socket(int socketfd){
    int offset, max_input = 2056;
    char content_buffer[max_input], *url;
    offset = recv(socketfd, content_buffer, max_input, 0);
    content_buffer[offset] = '\0';
    printf("%s", content_buffer);
    //url = get_url(content_buffer);
    char *def = "";
    return def;
}

char *get_url(char *content_buffer) {
    int i = 0, j = 0;
    char *url;
    while (content_buffer[i++] != ' ');
    i++;
    while (content_buffer[i] != ' ') {
       url[j++] = content_buffer[i++];
    }
    url[j] = '\0';
    return url;
}

int write_socket(int socketfd){
      /*int file;
      if ((file = open(url, O_RDONLY)) >= 0) {
        if (!send_the_file(file, socketfd)) {
          return 0;
        }
      } */
      send_default(socketfd);
      return 0;
}

int send_the_file(int file, int socketfd) {
    off_t offset = 0, size = 150000;
    printf("Found requested file...\n\n");
    if (sendfile(file, socketfd, offset, &size, 0,0) < 0) {
        printf("Error Sending File");
        return -1;
    }
    close(file);
    return 0;
}

void send_default(int socketfd) {
    printf("Send default page\n");
    char status[MAX_HEADER_LEN] = "HTTP/1.1 400 Not Found\n";
    char message[MAX_HEADER_LEN] = "<html><body><h1>Page doesn't exist</h1></body></html>\n\n";
    char ctnt_type_hdr[MAX_HEADER_LEN] = "Content-Type: text/html\n\n";
    char *ctnt_len_hdr = "Content-Length: ";
    char ctnt_len_val[20];
    sprintf(ctnt_len_val, "%d\n", (int) strlen(message));

    send(socketfd, &status, strlen(status), 0);
    send(socketfd, &ctnt_len_hdr, strlen(ctnt_len_hdr), 0);
    send(socketfd, &ctnt_len_val, strlen(ctnt_len_val), 0);
    send(socketfd, &ctnt_type_hdr, strlen(ctnt_type_hdr), 0);
    send(socketfd, &message, strlen(message), 0);
}
