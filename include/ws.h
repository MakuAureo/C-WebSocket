#ifndef WS_H
#define WS_H

#include <arpa/inet.h>
#include <hashmap.h>

typedef struct {
  int socketFD;
  int socketOpts;
  int socketEventPoll;
  struct sockaddr_in addrInfo;
  Map connections;
} WSSocket;

typedef struct {
  int socketFD;
  int needsHandshake;
  char * connectionPath;
  char * recvBuffer;
  char * sendBuffer;
  struct sockaddr_in addrInfo;
} WSConnection;

// Returns -1 on error, 0 otherwise
int initSocket(WSSocket * socketInfo);

// Returns -1 on error, 0 otherwise
int bindSocket(WSSocket * socketInfo, unsigned int const port);

void closeSocket(WSSocket * socketInfo);

void eventLoop(WSSocket * const socketInfo, 
    void (*onConnection)(WSConnection const * const client), 
    void (*onHandshake)(WSConnection const * const client), 
    size_t (*onMessage)(WSConnection const * const client, char const * const incData, char ** const outData));

#endif
