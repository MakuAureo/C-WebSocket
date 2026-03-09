#ifndef WS_H
#define WS_H

#include <arpa/inet.h>
#include <pthread.h>

#include "hashmap.h"

#define WS_MAX_THREADS 4

typedef struct WSPathHandler WSPathHandler;
typedef struct WSConnection WSConnection;
typedef struct WSWorker WSWorker;
typedef struct WSSocket WSSocket;

struct WSPathHandler {
  void (*onHandshake)(WSConnection const * const client);
  void (*onDisconnect)(WSConnection const * const client);
  size_t (*onMessage)(WSConnection const * const client, char const * const incData, char ** const outData);
};

struct WSConnection {
  int32_t clientFD;
  int8_t needsHandshake;
  char * recvBuffer;
  char * sendBuffer;
  struct sockaddr_in addrInfo;
  WSPathHandler * pathHanlder;
};

struct WSWorker {
  pthread_t thread;
  int32_t workerOpts;
  int32_t workerEventPoll;
};

struct WSSocket {
  int32_t socketFD;
  int32_t socketOpts;
  int32_t socketEventPoll;
  struct sockaddr_in addrInfo;
  WSWorker threads[WS_MAX_THREADS];
  WSConnection ** connections;
  Map paths;
};

// Returns 0 on success, -1 otherwise
int8_t initSocket(WSSocket * socketInfo);

// Returns 0 on success, -1 otherwise
int8_t bindSocket(WSSocket * socketInfo, unsigned int const port);

void closeSocket(WSSocket * socketInfo);

// Returns 0 on success
int8_t addValidPath(WSSocket * const socketInfo, char const * const path,
    void (*onHandshake)(WSConnection const * const client),
    void (*onDisconnect)(WSConnection const * const client),
    size_t (*onMessage)(WSConnection const * const client, char const * const incData, char ** const outData));

void runSocketLoop(WSSocket * const socketInfo, void (*onConnect)(WSConnection const * const client));

#endif
