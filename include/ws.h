#ifndef WS_H
#define WS_H

#include <arpa/inet.h>
#include <pthread.h>
#include <hashmap.h>

#define WS_WORKER_THREADS 4

typedef struct {
  int socketFD;
  int needsHandshake;
  char * connectionPath;
  char * recvBuffer;
  char * sendBuffer;
  struct sockaddr_in addrInfo;
} WSConnection;

typedef struct {
  int workerEventPoll;
  void (*onHandshake)(WSConnection const * const client);
  size_t (*onMessage)(WSConnection const * const client, char const * const incData, char ** const outData);
  pthread_t threadId;
} WSWorker;

typedef struct {
  int socketFD;
  int socketOpts;
  int socketEventPoll;
  struct sockaddr_in addrInfo;
  WSWorker workerThreads[WS_WORKER_THREADS];
  Map connections;
} WSSocket;

// Returns -1 on error, 0 otherwise
int initSocket(WSSocket * socketInfo);

// Returns -1 on error, 0 otherwise
int bindSocket(WSSocket * socketInfo, unsigned int const port);

void closeSocket(WSSocket * socketInfo);

void startEventLoop(WSSocket * const socketInfo, 
    void (*onConnect)(WSConnection const * const client), 
    void (*onHandshake)(WSConnection const * const client), 
    size_t (*onMessage)(WSConnection const * const client, char const * const incData, char ** const outData));

#endif
