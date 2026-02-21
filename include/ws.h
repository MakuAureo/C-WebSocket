#ifndef WS_H
#define WS_H

#include <sys/epoll.h>
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

// Returns -1 on error, 0 otherwise
int acceptNewConnection(WSSocket * const socketInfo, WSConnection * const client);

void freeConnectionResources(WSSocket * const socketInfo, WSConnection * client);

// Returns -1 on error, 0 otherwise
int performHandshake(WSSocket * const socketInfo, WSConnection * const client);

/* @return Returns 0 if recvBuffer was updated or 1 if data was a ping
 *
 * Returns closing code otherwise:
 *
 * 1000 - Normal closure
 * 
 * 1001 - Memory Error
 *
 * 1002 - Protocol violation
 *
 * 1003 - Refused to read data (bad formating)
 */
int receiveDataFrom(WSSocket * const socketInfo, WSConnection * const client);

int sendDataTo(WSConnection const * const client, char const * buffer, size_t size);

/* @param `dataHandler(incData, outData)` must return size of buffer `outData` without new line
 * or 0 if incData couldn't be handled
 */
void eventLoop(WSSocket * const socketInfo, size_t (*dataHandler)(WSConnection const * const client, char const * const incData, char ** const outData));

#endif
