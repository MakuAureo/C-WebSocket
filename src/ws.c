#define _GNU_SOURCE

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <openssl/sha.h>
#include <sys/epoll.h>
#include <unistd.h>

#include <base64.h>
#include <ws.h>
#include <hashmap.h>

#define BUFFER_SML 128
#define BUFFER_BIG 1024
#define WS_SPECIAL_KEY "258EAFA5-E914-47DA-95CA-C5AB0DC85B11"
#define SOCKET_BACKLOG 32
#define MAX_EVENTS_PER_LOOP 32

#define FIN_BIT_END 0x80
#define OPCODE_TEXT 0x1
#define OPCODE_CLOSE 0x8
#define OPCODE_PING 0x9

//This is the compare function for the connections hashmap, the keys are of type int
static int compareConnections(void const * key1_int, void const * key2_int) {
  return *(int *)key1_int == *(int *)key2_int;
}

static int acceptNewConnection(WSSocket * const socketInfo, WSConnection * const client) {
  socklen_t addrLen = sizeof(struct sockaddr_in);

  memset(client, 0, sizeof(WSConnection));
  if ((client->socketFD = accept4(socketInfo->socketFD, (struct sockaddr *)&(client->addrInfo), &addrLen, SOCK_NONBLOCK)) == -1) {
    printf("New client connection failed: %s\n", strerror(errno));
    return -1;
  }
  char addr[INET_ADDRSTRLEN];
  inet_ntop(AF_INET, &(client->addrInfo.sin_addr), addr, INET_ADDRSTRLEN);
  client->needsHandshake = 1;

  struct epoll_event newClientEvent = {
    .data.fd = client->socketFD,
    .events = EPOLLIN | EPOLLET
  };
  if (epoll_ctl(socketInfo->socketEventPoll, EPOLL_CTL_ADD, client->socketFD, &newClientEvent) == -1) {
    printf("(%s): Could not track event for new client: %s\n", addr, strerror(errno));
    close(client->socketFD);
    memset(client, 0, sizeof(WSConnection));
    return -1;
  }

  mapPut(&(socketInfo->connections), &(client->socketFD), client);

  printf("(%s): Client connected.\n", addr);
  return 0;
}

static void freeConnectionResources(WSSocket * const socketInfo, WSConnection * const client) {
  free(client->recvBuffer);
  free(client->sendBuffer);
  free(client->connectionPath);
  epoll_ctl(socketInfo->socketEventPoll, EPOLL_CTL_DEL, client->socketFD, NULL);
  close(client->socketFD);
  mapRemove(&(socketInfo->connections), &(client->socketFD));
}

static int performHandshake(WSSocket * const socketInfo, WSConnection * const client) {
  char addr[INET_ADDRSTRLEN];
  inet_ntop(AF_INET, &(client->addrInfo.sin_addr), addr, INET_ADDRSTRLEN);
  socklen_t addrLen = sizeof(struct sockaddr_in);

  char recvBuf[BUFFER_BIG];
  int recvSize;
  if ((recvSize = recvfrom(client->socketFD, recvBuf, BUFFER_BIG - 1, 0, (struct sockaddr *)&(client->addrInfo), &addrLen)) == -1) {
    printf("(%s): Could not read message: %s\n", addr, strerror(errno));
    goto handshakeFail;
  }
  recvBuf[recvSize] = '\0';

  char * keyStart;
  char * pathStart, * pathEnd;
  if ((pathStart = strstr(recvBuf, "GET")) == NULL || 
      (pathEnd = strstr(recvBuf, "HTTP/1.1")) == NULL || 
      strstr(recvBuf, "Connection: Upgrade") == NULL || 
      strstr(recvBuf, "Upgrade: websocket") == NULL ||
      (keyStart = strstr(recvBuf, "Sec-WebSocket-Key")) == NULL) {
    printf("(%s): Invalid websocket upgrade request.\n", addr);
    goto handshakeFail;
  }
  keyStart += 19;
  pathStart += 5;

  int pathSize = pathEnd - pathStart;
  char * path = malloc((pathSize + 1) * sizeof(char));
  strncpy(path, pathStart, pathSize);
  path[pathSize] = '\0';
  client->connectionPath = path;

  char wsKey[25];
  memset(wsKey, 0, 25 * sizeof(char));
  strncpy(wsKey, keyStart, 24);
  wsKey[24] = '\0';

  char appKey[BUFFER_SML];
  sprintf(appKey, "%s%s", wsKey, WS_SPECIAL_KEY);
  unsigned char sha1hash[SHA_DIGEST_LENGTH];
  SHA1((unsigned char*)appKey, strlen(appKey), sha1hash);
  size_t outLen;

  unsigned char * finalKey = base64_encode(sha1hash, SHA_DIGEST_LENGTH, &outLen);
  char response[BUFFER_BIG];
  sprintf(response, 
      "HTTP/1.1 101 Switching Protocols\r\n"
      "Upgrade: websocket\r\n"
      "Connection: Upgrade\r\n"
      "Sec-WebSocket-Accept: %s\r\n\r\n", 
      finalKey);
  free(finalKey);

  send(client->socketFD, response, strlen(response), 0);
  client->needsHandshake = 0;

  printf("(%s): Succeful handshake on path %s\n", addr, client->connectionPath);

  client->recvBuffer = malloc(BUFFER_SML);
  client->sendBuffer = malloc(BUFFER_SML);
  return 0;

  handshakeFail:
    freeConnectionResources(socketInfo, client);
    return -1;
}

static int receiveDataFrom(WSSocket * const socketInfo, WSConnection * const client) {
  char addr[INET_ADDRSTRLEN];
  inet_ntop(AF_INET, &(client->addrInfo.sin_addr), addr, INET_ADDRSTRLEN);
  socklen_t addrLen = sizeof(struct sockaddr_in);
  unsigned char closeFrame[4] = {0x88, 0x2, 0x0, 0x0};
  unsigned char * closeCodeBits;

  unsigned char dataHeader_2B[2];
  recvfrom(client->socketFD, dataHeader_2B, 2, 0, (struct sockaddr *)&(client->addrInfo), &addrLen);
  uint8_t finBit = dataHeader_2B[0] & 0xF0;
  uint8_t opcode = dataHeader_2B[0] & 0x0F;
  uint16_t closeCode;

  if (finBit != FIN_BIT_END) {
    printf("(%s): Refusing to read fragmented data. Closing connection.\n", addr);
    closeCode = htons(1003);
    goto closeConnection;
  }
  if (opcode == OPCODE_CLOSE) {
    printf("(%s): Client asked to close connection.\n", addr);
    closeCode = htons(1000);
    goto closeConnection;
  }
  if (opcode != OPCODE_TEXT && opcode != OPCODE_PING) {
    printf("(%s): Refusing to read non-text data. Closing connection.\n", addr);
    closeCode = htons(1003);
    goto closeConnection;
  }

  uint8_t maskBit = (dataHeader_2B[1] & 0x80) >> 7;
  if (maskBit != 1) {
    printf("(%s): Bad message maskBit (protocol violation). Closing connection.\n", addr);
    closeCode = htons(1002);
    goto closeConnection;
  }

  uint64_t payloadLen = dataHeader_2B[1] & 0x7F;
  if (payloadLen == 126) {
    unsigned char extraLen[2];
    recvfrom(client->socketFD, extraLen, 2, 0, (struct sockaddr *)&(client->addrInfo), &addrLen);
    payloadLen = ntohs(*(uint16_t *)extraLen);
  } else if (payloadLen == 127) {
    unsigned char extraLen[8];
    recvfrom(client->socketFD, extraLen, 8, 0, (struct sockaddr *)&(client->addrInfo), &addrLen);
    payloadLen = ntohl(*(uint64_t *)extraLen);
  }

  int isPing = (opcode == OPCODE_PING) ? 1 : 0;
  unsigned char mask_4B[4];
  recvfrom(client->socketFD, mask_4B, 4, 0, (struct sockaddr *)&(client->addrInfo), &addrLen);

  if (payloadLen > 0) {
    char * payloadAlloc = realloc(client->recvBuffer, (payloadLen + 1) * sizeof(char));
    if (payloadAlloc == NULL) {
      closeCode = htons(1001);
      goto closeConnection;
    }
    client->recvBuffer = payloadAlloc;
    recvfrom(client->socketFD, client->recvBuffer, payloadLen, 0, (struct sockaddr *)&(client->addrInfo), &addrLen);
    for (int i = 0; i < payloadLen; i++)
      client->recvBuffer[i] ^= mask_4B[i % 4];
    client->recvBuffer[payloadLen] = '\0';
  }

  if (isPing) {
    char pong[payloadLen + 2];
    pong[0] = 0x8A;
    pong[1] = dataHeader_2B[1] & 0x7F;
    if (payloadLen > 0)
      strncpy(pong + 2, client->recvBuffer, payloadLen);
    sendto(client->socketFD, pong, payloadLen + 2, 0, (struct sockaddr *)&(client->addrInfo), addrLen);
  } else {
    printf("(%s): \"%s\"\n", addr, client->recvBuffer);
  }

  return isPing;

  closeConnection:
    closeCodeBits = (unsigned char *)&closeCode;
    closeFrame[2] = closeCodeBits[0];
    closeFrame[3] = closeCodeBits[1];
    sendto(client->socketFD, closeFrame, 4, 0, (struct sockaddr *)&(client->addrInfo), addrLen);
    freeConnectionResources(socketInfo, client);
    return ntohs(closeCode);
}

static int sendDataTo(WSConnection const * const client, char const * buffer, size_t size) {
  if (size == 0)
    return 0;

  socklen_t addrLen = sizeof(struct sockaddr_in);
  unsigned char headerSize = 2;
  if (size > 125)
    headerSize += 2;
  if (size > 65535)
    headerSize += 6;
  
  char message[size + headerSize];
  message[0] = 0x81;
  if (headerSize == 2)
    message[1] = (char)size;
  else if (headerSize == 4) {
    message[1] = 126;
    uint16_t netSize = htons(size);
    memcpy(message + 2, &netSize, 2 * sizeof(char));
  }
  else if (headerSize == 10) {
    message[1] = 127;
    uint64_t netSize = htonl(size);
    memcpy(message + 2, &netSize, 8 * sizeof(char));
  }

  strncpy(message + headerSize, buffer, size);
  sendto(client->socketFD, message, size + headerSize, 0, &(client->addrInfo), addrLen);
  return size;
}


int initSocket(WSSocket * socketInfo) {
  memset(socketInfo, 0, sizeof(WSSocket));

  if ((socketInfo->socketFD = socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK, 0)) == -1) {
    printf("Could not start a new socket: %s\n", strerror(errno));
    return -1;
  }

  if ((socketInfo->socketEventPoll = epoll_create(8)) == -1) {
    printf("Could not create event poll for new socket: %s\n", strerror(errno));
    goto closeSocket;
  }

  if (setsockopt(socketInfo->socketFD, SOL_SOCKET, SO_REUSEADDR | SO_REUSEPORT, &(socketInfo->socketOpts), sizeof(socketInfo->socketOpts)) == -1) {
    printf("Could not set socket options for new socket: %s\n", strerror(errno));
    goto closeSocket;
  }

  initMap(&(socketInfo->connections), sizeof(int), sizeof(WSConnection), compareConnections);

  struct epoll_event socketEvent = {
    .data.fd = socketInfo->socketFD,
    .events = EPOLLIN
  };
  if (epoll_ctl(socketInfo->socketEventPoll, EPOLL_CTL_ADD, socketInfo->socketFD, &socketEvent) == -1) {
    printf("Could not track event for new socket: %s\n", strerror(errno));
    goto closeSocket;
  }

  return 0;

  closeSocket:
    closeSocket(socketInfo);
    return -1;
}

int bindSocket(WSSocket * socketInfo, unsigned int const port) {
  socklen_t addrLen = sizeof(struct sockaddr_in);

  memset(&(socketInfo->addrInfo), 0, addrLen);
  socketInfo->addrInfo.sin_family = AF_INET;
  socketInfo->addrInfo.sin_port = htons(port);
  socketInfo->addrInfo.sin_addr.s_addr = INADDR_ANY;

  if (bind(socketInfo->socketFD, (struct sockaddr *)&(socketInfo->addrInfo), addrLen) == -1) {
    printf("Could not bind new socket to port (%d): %s\n", port, strerror(errno));
    goto closeSocket;
  }

  if (listen(socketInfo->socketFD, SOCKET_BACKLOG) == -1) {
    printf("Could not start listening on new socket: %s\n", strerror(errno));
    goto closeSocket;
  }

  return 0;

  closeSocket:
    closeSocket(socketInfo);
    return -1;
}

static void freeConnectionResourcesIteratorWrapper(void * clientPtr, void * contextPtr) {
  WSSocket * socketInfo = (WSSocket *)contextPtr;
  WSConnection * client = (WSConnection *)clientPtr;
  freeConnectionResources(socketInfo, client);
}

void closeSocket(WSSocket * socketInfo) {
  mapForEach(&(socketInfo->connections), socketInfo, freeConnectionResourcesIteratorWrapper);
  freeMap(&(socketInfo->connections));
  close(socketInfo->socketFD);
  memset(socketInfo, 0, sizeof(WSSocket));
  socketInfo = NULL;
}

void eventLoop(WSSocket * const socketInfo, void (*onConnected)(WSConnection const * const client), void (*onHandshake)(WSConnection const * const client), size_t (*onMessage)(WSConnection const * const client, char const * const incData, char ** const outData)) {
  struct epoll_event eventsTriggered[MAX_EVENTS_PER_LOOP];
  for (;;) {
    int events = epoll_wait(socketInfo->socketEventPoll, eventsTriggered, MAX_EVENTS_PER_LOOP, -1);
    for (int i = 0; i < events; i++) {
      if (eventsTriggered[i].data.fd == socketInfo->socketFD) {
        WSConnection client;
        acceptNewConnection(socketInfo, &client);
        onConnected(mapGet(&(socketInfo->connections), &(client.socketFD)));
      } else {
        WSConnection * const connection = mapGet(&(socketInfo->connections), &(eventsTriggered[i].data.fd));
        if (connection == NULL) {
          printf("Connection already closed.\n");
          continue;
        }
        if (connection->needsHandshake) {
          if (performHandshake(socketInfo, connection) == -1) {
            continue;
          }
          onHandshake(connection);
        } else {
          if (receiveDataFrom(socketInfo, connection) > 0) {
            continue;
          }
          size_t size = onMessage(connection, connection->recvBuffer, &(connection->sendBuffer));
          sendDataTo(connection, connection->sendBuffer, size);
        }
      }
    }
  }
}
