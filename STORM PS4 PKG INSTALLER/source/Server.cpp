#include "../include/Server.h"
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <fcntl.h>
#include <stdlib.h>
#include <stdio.h>

Server::Server(int port) : port(port), sockfd(-1), running(false) {}

Server::~Server() {
    if (sockfd >= 0) close(sockfd);
}

bool Server::Init() {
    sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0) return false;

    // Non-blocking mode
    int flags = fcntl(sockfd, F_GETFL, 0);
    if (flags != -1) {
        fcntl(sockfd, F_SETFL, flags | O_NONBLOCK);
    }

    int reuse = 1;
    setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));

    struct sockaddr_in serverAddr;
    memset(&serverAddr, 0, sizeof(serverAddr));
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_addr.s_addr = htonl(INADDR_ANY);
    serverAddr.sin_port = htons(port);

    if (bind(sockfd, (struct sockaddr*)&serverAddr, sizeof(serverAddr)) != 0) {
        close(sockfd);
        sockfd = -1;
        return false;
    }

    if (listen(sockfd, 5) != 0) {
        close(sockfd);
        sockfd = -1;
        return false;
    }

    running = true;
    return true;
}

bool Server::CheckForMessage(char* outBuffer, int maxLen) {
    if (!running) return false;

    struct sockaddr_in clientAddr;
    socklen_t addrLen = sizeof(clientAddr);
    
    // Accept new connection (non-blocking)
    int connfd = accept(sockfd, (struct sockaddr*)&clientAddr, &addrLen);
    if (connfd >= 0) {
        memset(outBuffer, 0, maxLen);
        
        // Read with timeout or just once since it's a simple install command
        ssize_t len = read(connfd, outBuffer, maxLen - 1);
        
        bool received = false;
        if (len > 0) {
            received = true;
            
            // Send simple response
            const char* response = "{\"status\":\"received\"}\n";
            write(connfd, response, strlen(response));
        }

        close(connfd);
        return received;
    }
    
    return false;
}
