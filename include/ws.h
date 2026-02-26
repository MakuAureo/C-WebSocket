#ifndef WS_H
#define WS_H

#include <arpa/inet.h>
#include <hashmap.h>

typedef struct WSPathHandler WSPathHandler;
typedef struct WSConnection WSConnection;
typedef struct WSSocket WSSocket;

struct WSPathHandler {
  void (*onHandshake)(WSConnection const * const client);
  void (*onDisconnect)(struct WSConnection const * const client);
  size_t (*onMessage)(WSConnection const * const client, char const * const incData, char ** const outData);
};

struct WSConnection {
  int socketFD;
  int needsHandshake;
  char * connectionPath;
  char * recvBuffer;
  char * sendBuffer;
  struct sockaddr_in addrInfo;
  WSPathHandler * pathHanlder;
};

struct WSSocket{
  int socketFD;
  int socketOpts;
  int socketEventPoll;
  struct sockaddr_in addrInfo;
  Map connections;
  Map paths;
};

// Returns -1 on error, 0 otherwise
int initSocket(WSSocket * socketInfo);

// Returns -1 on error, 0 otherwise
int bindSocket(WSSocket * socketInfo, unsigned int const port);

void closeSocket(WSSocket * socketInfo);

void startEventLoop(WSSocket * const socketInfo, void (*onConnect)(WSConnection const * const client));

#endif
