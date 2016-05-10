#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h> // http://pubs.opengroup.org/onlinepubs/9699919799/basedefs/sys_types.h.html
#include <unistd.h>

int main() {
    int create_socket, new_socket;
    socklen_t addrlen;  // socklen_t is an integer type; data buffer len
    int bufsize = 1024;
    char *buffer = malloc(bufsize);
    
    struct sockaddr_in address; // http://beej.us/net2/html/structs.html 
   
    // socket fn creates an unbound socket and returns an fd that can be used to operate on the socket
    // takes domain in which socket is to be created, type of socket to be created, protocol to be used
    // AF_INET: Address Family (IPv4 addrs)
    // SOCK_STREAM: connection based protocal (versus datagram-based protocol)
    // Passing in a protocol of "0" to have socket() choose the correct protocol based on the type
    if ((create_socket = socket(AF_INET, SOCK_STREAM, 0)) > 0) {
        printf("The socket was created.\n");
    }

    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY; // we will accept a connection on any Internet interface 
    address.sin_port = htons(15000); // host to network short (ie prot number)

    // bind fn assigns local socket address to the create_socket (identified by fd returned from socket()). On success, returns 0
    // we must specify the length of the socket address structure (which depends on the address family)
    // bind(socket, address, address_len)
    if (bind(create_socket, (struct sockaddr *) &address, sizeof(address)) == 0) {
        printf("Binding Socket\n");
    }

    while (1) {
        
        // listen is used by the server to get ready for accepting connection requests from a client
        // takes the socket fd and backlog (specifies the number of requests that cna be queued by the system before the server executes the accept system call)
        // listen(socketfd, int backlog)
        if (listen(create_socket, 10) < 0) {
            perror("server: listen");
            exit(1);
        }
        
        // accept is used by connection-oriented server to set up an actual connection with a client process
        // You call accept and tell the server to get the pending connection, it will return a brand new socket fd for the single connection
        // This means you have two socket fds - the origin one is still listening on your port and the newly created one is ready to send() and recieve()
        // accept (listening socket fd, pointer to local sockaddr struct, which is where the information about the incoming connection will go, the address length so accept will not 
        // put more than that many bytes into the address.
        if ((new_socket = accept(create_socket, (struct sockaddr *) &address, &addrlen)) < 0) {
            perror("server: accept");
            exit(1);
        } 

        if (new_socket > 0) {
            printf("The Client is connected...\n");
        }

        // takes the socket fd to read from, the buffer to read the information into, the maximum len of the buffer, and ny flags)  
        recv(new_socket, buffer, bufsize, 0);
        printf("%s\n", buffer); // print out the contents of buffer
        

        // send(int sockfd, const void *msg, int len, int flags);
        // sock fd is the socket descriptor you want to send data to. msg is a pointer to the data you want to send, and len is the length of the data in bytes. 
        send(new_socket, "HTTP/1.1 200 OK\n", 16, 0);
        send(new_socket, "X-Is-Working: yes!\n",19, 0);
        send(new_socket, "Content-length: 46\n", 19, 0);
        send(new_socket, "Content-Type: text/html\n\n", 25, 0);
        send(new_socket, "<html><body><h1>Hello world<h1></body></html>", 46, 0);
        close(new_socket);
    }
    close(create_socket);
    printf("Closed socket\n");
    return 0;
}

