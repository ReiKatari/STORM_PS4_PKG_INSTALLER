#pragma once

#include "Common.h"

#include <netinet/in.h>

class Server {
private:
    int sockfd;
    int port;
    bool running;

public:
    Server(int port);
    ~Server();

    bool Init();
    // Non-blocking check for new connections/data
    // Returns true if a message was received, filling outBuffer.
    bool CheckForMessage(char* outBuffer, int maxLen);
};
