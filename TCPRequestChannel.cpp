#include <netdb.h>
#include "common.h"
#include "TCPRequestChannel.h"
using namespace std;

TCPRequestChannel::TCPRequestChannel (const string host_name, const string port_no) {
    if(host_name.empty()) {
        //Server
        // listen on sock_fd, new connection on new_fd
        struct addrinfo hints, *serv;
        int rv;

        memset(&hints, 0, sizeof hints);
        hints.ai_family = AF_UNSPEC;
        hints.ai_socktype = SOCK_STREAM;
        hints.ai_flags = AI_PASSIVE; // use my IP

        if ((rv = getaddrinfo(NULL, port_no.c_str(), &hints, &serv)) != 0) {
            cerr  << "getaddrinfo: " << gai_strerror(rv) << endl;
            return;
        }
        if ((sockfd = socket(serv->ai_family, serv->ai_socktype, serv->ai_protocol)) == -1) {
            perror("server: socket");
            return;
        }
        int enable = 1;
        if (setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &enable, sizeof(int)) < 0) {
            perror("server: setsockopt");
            return;
        }
        if (bind(sockfd, serv->ai_addr, serv->ai_addrlen) == -1) {
            perror("server: bind");
            return;
        }
        freeaddrinfo(serv); // all done with this structure

        if (listen(sockfd, 20) == -1) {
            perror("listen");
            return;
        }
    }
    else {
        //Client
        struct addrinfo hints, *res;

        // first, load up address structs with getaddrinfo():
        memset(&hints, 0, sizeof hints);
        hints.ai_family = AF_UNSPEC;
        hints.ai_socktype = SOCK_STREAM;
        int status;
        //getaddrinfo("www.example.com", "3490", &hints, &res);
        if ((status = getaddrinfo(host_name.c_str(), port_no.c_str(), &hints, &res)) != 0) {
            cerr << "getaddrinfo: " << gai_strerror(status) << endl;
            return;
        }

        // make a socket:
        sockfd = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
        if (sockfd < 0){
            perror ("Cannot create socket");
            return;
        }

        // connect!
        if (connect(sockfd, res->ai_addr, res->ai_addrlen) < 0) {
            perror("Cannot Connect");
            return;
        }
        cout << "Connected " << endl;
        // now it is time to free the memory dynamically allocated onto the "res" pointer by the getaddrinfo function
        freeaddrinfo (res);
    }
}

TCPRequestChannel::TCPRequestChannel(int socket) {
    sockfd = socket;
}

TCPRequestChannel::~TCPRequestChannel() {
    close(sockfd);
}

int TCPRequestChannel::cread(void* msgbuf, int buflen) {
    int nbytes = recv(sockfd, msgbuf, buflen, 0);
    if(nbytes < 0){
        perror("client: Receive failure");
    }
    return nbytes;
}

int TCPRequestChannel::cwrite(void* msgbuf, int msglen) {
    while(msglen > 0) {
        int nbytes = send(sockfd, msgbuf, msglen, 0);
        if (nbytes == -1) {
            perror("client: Send failure");
            return nbytes;
        }
        msglen -= nbytes;
    }
}

int TCPRequestChannel::getfd() {
    return sockfd;
}

