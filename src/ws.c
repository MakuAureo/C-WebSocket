#define _GNU_SOURCE
#include <sys/socket.h>

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <arpa/inet.h>
#include <openssl/sha.h>
#include <sys/epoll.h>
#include <unistd.h>

#include "base64.h"
#include "hashmap.h"
#include "dstring.h"
#include "ws.h"

#define WS_BUFFER_SML 128
#define WS_BUFFER_BIG 1024
#define WS_SOCKET_BACKLOG 32
#define WS_EVENTS_PER_LOOP 32
#define WS_SPECIAL_KEY "258EAFA5-E914-47DA-95CA-C5AB0DC85B11"

#define WS_MAX_CONNECTIONS 1024 // This should be set the system's File Descriptor Limit (from ulimit -n), 1024 for testing purposes

#define WS_FIN_BIT_END 0x80 // 1000 0000
#define WS_OPCODE_TEXT 0x01 // 0000 0001
#define WS_OPCODE_CLOSE 0x08// 0000 1000
#define WS_OPCODE_PING 0x09 // 0000 1001
#define WS_OPCODE_PONG 0x0A // 0000 1001

static int8_t comparePaths(void const * key1_dstr, void const * key2_dstr) {
  return dstrcmp((DString const *)key1_dstr, (DString const *)key2_dstr);
}

static uint32_t hashString(void * key, size_t length) {
  (void)length;
  uint8_t * bytes = (uint8_t *)((DString *)key)->string;
  uint32_t hash = 2166136261u;

  for (size_t i = 0; i < ((DString *)key)->length; i++) {
    hash ^= bytes[i];
    hash *= 16777619;
  }

  return hash;
}

static int8_t acceptNewConnection(WSSocket * const socketInfo, WSConnection * const client) {
  socklen_t addrLen = sizeof(struct sockaddr_in);

  memset(client, 0, sizeof(WSConnection));
  if ((client->clientFD = accept4(socketInfo->socketFD, (struct sockaddr *)&(client->addrInfo), &addrLen, SOCK_NONBLOCK)) == -1) {
    printf("New client connection failed: %s\n", strerror(errno));
    return -1;
  }
  char addr[INET_ADDRSTRLEN];
  inet_ntop(AF_INET, &(client->addrInfo.sin_addr), addr, INET_ADDRSTRLEN);
  client->needsHandshake = 1;

  struct epoll_event newClientEvent = {
    .data.fd = client->clientFD,
    .events = EPOLLIN | EPOLLET
  };
  if (epoll_ctl(socketInfo->socketEventPoll, EPOLL_CTL_ADD, client->clientFD, &newClientEvent) == -1) {
    printf("(%s): Could not track event for new client: %s\n", addr, strerror(errno));
    close(client->clientFD);
    memset(client, 0, sizeof(WSConnection));
    return -1;
  }

  socketInfo->connections[client->clientFD] = calloc(1, sizeof(WSConnection));
  memcpy(socketInfo->connections[client->clientFD], client, sizeof(WSConnection));

  printf("(%s): Client connected.\n", addr);
  return 0;
}

static void sendCloseFrameTo(WSConnection const * const client, uint16_t closeCode) {
  socklen_t addrLen = sizeof(struct sockaddr_in);
  closeCode = htons(closeCode);
  uint8_t * closeCodeBits = (uint8_t *)(&closeCode);

  uint8_t closeFrame[4] = {0x88, 0x2, 0x0, 0x0};
  memcpy(closeFrame + 2, closeCodeBits, 2 * sizeof(uint8_t));
  sendto(client->clientFD, closeFrame, 4, 0, (struct sockaddr *)&(client->addrInfo), addrLen);
}

static void freeConnectionResources(WSSocket * const socketInfo, WSConnection * const client, uint16_t const closeCode) {
  sendCloseFrameTo(client, closeCode);

  free(client->recvBuffer);
  free(client->sendBuffer);

  epoll_ctl(socketInfo->socketEventPoll, EPOLL_CTL_DEL, client->clientFD, NULL);
  
  shutdown(client->clientFD, SHUT_RDWR);
  close(client->clientFD);
  
  memset(socketInfo->connections[client->clientFD], 0, sizeof(WSConnection));
  free(socketInfo->connections[client->clientFD]);
  socketInfo->connections[client->clientFD] = NULL;
}

/*
static void freeConnectionResourcesForEachWrapper(void * clientFDPtr, void * wsConnectionPtr, void * contextPtr) {
  (void)clientFDPtr; //unused
  
  WSSocket * socketInfo = (WSSocket *)contextPtr;
  WSConnection * client = (WSConnection *)wsConnectionPtr;
  freeConnectionResources(socketInfo, client, 1001);
}
*/
static void freeConnectionPathForEachWrapper(void * pathPtr, void * pathHandlerPtr, void * contextPtr) {
  (void)pathHandlerPtr; //unused
  (void)contextPtr; //unused

  dstrfree(pathPtr);
}

static uint8_t isHTTPUpgrade(char const * const request, ssize_t length, char ** path, char ** key) {
  enum HTTPCHECK {
    GET,
    PATH,
    HTTP,
    KEY,
  };

  enum FOUND {
    FOUND_NONE = 0,
    FOUND_CONNECTION = 1,
    FOUND_UPGRADE = (1 << 1),
    FOUND_KEY = (1 << 2)
  };
  uint8_t found = FOUND_NONE;

  enum HTTPCHECK state = GET;
  for (char const * curr = request; curr < request + length;) {
    switch (state) {
      case GET:;
        char get[] = "GET";
        for (size_t i = 0; i < 3; i++)
          if (*(curr++) != get[i])
            return 0;
        
        state = PATH;
        break;
      case PATH:;
        size_t pathLenght = 0;
        char const * pathStart = ++curr;
        while (*(curr++) != ' ') pathLenght++;

        *path = malloc((pathLenght + 1) * sizeof(char));
        memcpy(*path, pathStart, pathLenght);
        (*path)[pathLenght] = '\0';

        state = HTTP;
        break;
      case HTTP:;
        char http[] = "HTTP/1.1";
        for (size_t i = 0; i < 8; i++)
          if (*(curr++) != http[i]) {
            free(path);
            path = NULL;
            return 0;
          }

        state = KEY;
        break;
      case KEY:;
        char connection[] = "Connection: Upgrade";
        char upgrade[] = "Upgrade: websocket";
        char wskey[] = "Sec-WebSocket-Key";

        for (;curr < request + length; curr++) {
          if (!(found & FOUND_CONNECTION) && *curr == connection[0]) {
            size_t i = 0;
            for (; i < sizeof(connection); i++) {
             if (curr[i] == connection[i])
               continue;
             break;
            }
            if (i == sizeof(connection) - 1) {
              found |= FOUND_CONNECTION;
              curr += i;
            }
          }
          if (!(found & FOUND_UPGRADE) && *curr == upgrade[0]) {
            size_t i = 0;
            for (; i < sizeof(upgrade); i++) {
              if (curr[i] == upgrade[i])
                continue;
              break;
            }
            if (i == sizeof(upgrade) - 1) {
              found |= FOUND_UPGRADE;
              curr += i;
            }
          }
          if (!(found & FOUND_KEY) && *curr == wskey[0]) {
            size_t i = 0;
            for (; i < sizeof(wskey); i++) {
              if (curr[i] == wskey[i])
                continue;
              break;
            }
            if (i == sizeof(wskey) - 1) {
              found |= FOUND_KEY;
              size_t keyLength = 24;
              char const * keyStart = curr + i + 2;

              *key = malloc((keyLength + 1) * sizeof(char));
              memcpy(*key, keyStart, keyLength);
              (*key)[keyLength] = '\0';
              curr += i;
            }
          }
        }
        goto exit;
    }
  }

  exit:
  if (found == (FOUND_CONNECTION | FOUND_UPGRADE | FOUND_KEY))
    return 1;
  else {
    free(*path);
    free(*key);
    *path = NULL;
    *key = NULL;
    return 0;
  }
}

static int8_t performHandshake(WSSocket * const socketInfo, WSConnection * const client) {
  char addr[INET_ADDRSTRLEN];
  inet_ntop(AF_INET, &(client->addrInfo.sin_addr), addr, INET_ADDRSTRLEN);
  socklen_t addrLen = sizeof(struct sockaddr_in);

  char recvBuf[WS_BUFFER_BIG];
  ssize_t recvSize;
  if ((recvSize = recvfrom(client->clientFD, recvBuf, WS_BUFFER_BIG - 1, 0, (struct sockaddr *)&(client->addrInfo), &addrLen)) == -1) {
    printf("(%s): Could not read message: %s\n", addr, strerror(errno));
    goto handshakeFail;
  }
  recvBuf[recvSize] = '\0';

  char * key = NULL;
  char * path = NULL;
  if (!isHTTPUpgrade(recvBuf, recvSize, &path, &key)) {
    printf("(%s): Invalid websocket upgrade request.\n", addr);
    goto handshakeFail;
  }
  DString pathDString;
  dstrinit(&pathDString, path, strlen(path));

  WSPathHandler * pathHandler;
  if ((pathHandler = mapGet(&(socketInfo->paths), &pathDString)) == NULL) {
    printf("(%s): Invalid websocket path (%s).\n", addr, path);
    free(path);
    free(key);
    goto handshakeFail;
  }
  dstrfree(&pathDString);
  client->pathHanlder = pathHandler;

  char appKey[WS_BUFFER_SML];
  sprintf(appKey, "%s%s", key, WS_SPECIAL_KEY);
  unsigned char sha1hash[SHA_DIGEST_LENGTH];
  SHA1((unsigned char*)appKey, strlen(appKey), sha1hash);
  free(key);

  size_t outLen;
  unsigned char * finalKey = base64_encode(sha1hash, SHA_DIGEST_LENGTH, &outLen);
  char response[WS_BUFFER_BIG];
  sprintf(response, 
      "HTTP/1.1 101 Switching Protocols\r\n"
      "Upgrade: websocket\r\n"
      "Connection: Upgrade\r\n"
      "Sec-WebSocket-Accept: %s\r\n\r\n", 
      finalKey);
  free(finalKey);

  sendto(client->clientFD, response, strlen(response), 0, &(client->addrInfo), addrLen);
  client->needsHandshake = 0;

  printf("(%s): Succeful handshake on path %s\n", addr, path);
  free(path);

  client->recvBuffer = malloc(WS_BUFFER_SML * sizeof(char));
  client->sendBuffer = malloc(WS_BUFFER_SML * sizeof(char));
  return 0;

  handshakeFail:
    epoll_ctl(socketInfo->socketEventPoll, EPOLL_CTL_DEL, client->clientFD, NULL);
    shutdown(client->clientFD, SHUT_RDWR);
    close(client->clientFD);
    memset(socketInfo->connections[client->clientFD], 0, sizeof(WSConnection));
    free(socketInfo->connections[client->clientFD]);
    socketInfo->connections[client->clientFD] = NULL;
    return -1;
}

static int32_t receiveDataFrom(WSConnection * const client) {
  char addr[INET_ADDRSTRLEN];
  inet_ntop(AF_INET, &(client->addrInfo.sin_addr), addr, INET_ADDRSTRLEN);
  socklen_t addrLen = sizeof(struct sockaddr_in);

  uint8_t dataHeader_2B[2];
  recvfrom(client->clientFD, dataHeader_2B, 2, 0, (struct sockaddr *)&(client->addrInfo), &addrLen);
  uint8_t finBit = dataHeader_2B[0] & 0xF0;
  uint8_t opcode = dataHeader_2B[0] & 0x0F;
  uint16_t closeCode = 0;

  if (finBit != WS_FIN_BIT_END) {
    printf("(%s): Refusing to read fragmented data. Closing connection.\n", addr);
    closeCode = 1003;
    return closeCode;
  }
  if (opcode == WS_OPCODE_CLOSE) {
    printf("(%s): Client asked to close connection.\n", addr);
    closeCode = 1000;
    return closeCode;
  }
  if (opcode != WS_OPCODE_TEXT && opcode != WS_OPCODE_PING) {
    printf("(%s): Refusing to read non-text data. Closing connection.\n", addr);
    closeCode = 1003;
    return closeCode;
  }

  uint8_t maskBit = (dataHeader_2B[1] & 0x80) >> 7;
  if (maskBit != 1) {
    printf("(%s): Bad message maskBit (protocol violation). Closing connection.\n", addr);
    closeCode = 1002;
    return closeCode;
  }

  uint64_t payloadLen = dataHeader_2B[1] & 0x7F;
  if (payloadLen == 126) {
    uint8_t extraLen[2];
    recvfrom(client->clientFD, extraLen, 2, 0, (struct sockaddr *)&(client->addrInfo), &addrLen);
    payloadLen = ntohs(*(uint16_t *)extraLen);
  } else if (payloadLen == 127) {
    uint8_t extraLen[8];
    recvfrom(client->clientFD, extraLen, 8, 0, (struct sockaddr *)&(client->addrInfo), &addrLen);
    payloadLen = ntohl(*(uint64_t *)extraLen);
  }

  uint8_t isPing = (opcode == WS_OPCODE_PING) ? 1 : 0;
  uint8_t mask_4B[4];
  recvfrom(client->clientFD, mask_4B, 4, 0, (struct sockaddr *)&(client->addrInfo), &addrLen);

  if (payloadLen > 0) {
    char * payloadAlloc = realloc(client->recvBuffer, (payloadLen + 1) * sizeof(char));
    if (payloadAlloc == NULL) {
      closeCode = 1001;
      return closeCode;
    }
    client->recvBuffer = payloadAlloc;
    recvfrom(client->clientFD, client->recvBuffer, payloadLen, 0, (struct sockaddr *)&(client->addrInfo), &addrLen);
    for (uint64_t i = 0; i < payloadLen; i++)
      client->recvBuffer[i] ^= mask_4B[i % 4];
    client->recvBuffer[payloadLen] = '\0';
  }

  if (isPing) {
    uint8_t pong[payloadLen + 2];
    pong[0] = WS_FIN_BIT_END | WS_OPCODE_PONG;
    pong[1] = dataHeader_2B[1] & (0x7F);
    if (payloadLen > 0)
      memcpy(pong + 2, client->recvBuffer, payloadLen);
    sendto(client->clientFD, pong, payloadLen + 2, 0, (struct sockaddr *)&(client->addrInfo), addrLen);
    printf("(%s): ping.\n", addr);
  } else {
    printf("(%s): \"%s\"\n", addr, client->recvBuffer);
  }

  return isPing;
}

static size_t sendDataTo(WSConnection const * const client, char const * buffer, size_t size) {
  if (size == 0)
    return 0;

  socklen_t addrLen = sizeof(struct sockaddr_in);
  uint8_t headerSize = 2;
  if (size > 125)
    headerSize += 2;
  if (size > 65535)
    headerSize += 6;
  
  uint8_t message[size + headerSize];
  message[0] = WS_FIN_BIT_END | WS_OPCODE_TEXT;
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

  memcpy(message + headerSize, buffer, size);
  sendto(client->clientFD, message, size + headerSize, 0, &(client->addrInfo), addrLen);
  return size;
}

int8_t initSocket(WSSocket * socketInfo) {
  memset(socketInfo, 0, sizeof(WSSocket));

  if ((socketInfo->socketFD = socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK, 0)) == -1) {
    printf("Could not start a new socket: %s\n", strerror(errno));
    return -1;
  }

  if ((socketInfo->socketEventPoll = epoll_create1(0)) == -1) {
    printf("Could not create event poll for new socket: %s\n", strerror(errno));
    goto closeSocket;
  }

  if (setsockopt(socketInfo->socketFD, SOL_SOCKET, SO_REUSEADDR | SO_REUSEPORT, &(socketInfo->socketOpts), sizeof(socketInfo->socketOpts)) == -1) {
    printf("Could not set socket options for new socket: %s\n", strerror(errno));
    goto closeSocket;
  }

  socketInfo->connections = calloc(WS_MAX_CONNECTIONS, sizeof(WSConnection *));
  initMap(&(socketInfo->paths), sizeof(DString), sizeof(WSPathHandler), comparePaths, hashString);

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

int8_t bindSocket(WSSocket * socketInfo, uint32_t const port) {
  socklen_t addrLen = sizeof(struct sockaddr_in);

  memset(&(socketInfo->addrInfo), 0, addrLen);
  socketInfo->addrInfo.sin_family = AF_INET;
  socketInfo->addrInfo.sin_port = htons(port);
  socketInfo->addrInfo.sin_addr.s_addr = INADDR_ANY;

  if (bind(socketInfo->socketFD, (struct sockaddr *)&(socketInfo->addrInfo), addrLen) == -1) {
    printf("Could not bind socket to port %d: %s\n", port, strerror(errno));
    goto closeSocket;
  }

  if (listen(socketInfo->socketFD, WS_SOCKET_BACKLOG) == -1) {
    printf("Could not start listening on port %d: %s\n", port, strerror(errno));
    goto closeSocket;
  }

  return 0;

  closeSocket:
    closeSocket(socketInfo);
    return -1;
}

void closeSocket(WSSocket * socketInfo) {
  for (int32_t i = 0; i < WS_MAX_CONNECTIONS; i++)
    if (socketInfo->connections[i] != NULL)
      freeConnectionResources(socketInfo, socketInfo->connections[i], 1001);

  mapForEach(&(socketInfo->paths), NULL, freeConnectionPathForEachWrapper);
  freeMap(&(socketInfo->paths));
  
  shutdown(socketInfo->socketFD, SHUT_RDWR);
  close(socketInfo->socketFD);

  memset(socketInfo, 0, sizeof(WSSocket));
  socketInfo = NULL;
}

int8_t addValidPath(WSSocket * const socketInfo, char const * const path,
    void (*onHandshake)(WSConnection const * const client),
    void (*onDisconnect)(WSConnection const * const client),
    size_t (*onMessage)(WSConnection const * const client, char const * const incData, char ** const outData)) {
  if (mapGet(&(socketInfo->paths), (void*)path) != NULL)
    return -1;
  WSPathHandler pathHandler = {
    .onHandshake = onHandshake,
    .onDisconnect = onDisconnect,
    .onMessage = onMessage
  };
  DString pathDStr;
  dstrinit(&pathDStr, path, strlen(path));
  mapPut(&(socketInfo->paths), &pathDStr, &pathHandler);
  return 0;
}

void runSocketLoop(WSSocket * const socketInfo, void (*onConnect)(WSConnection const * const client)) {
  struct epoll_event eventsTriggered[WS_EVENTS_PER_LOOP];
  for (;;) {
    int32_t events = epoll_wait(socketInfo->socketEventPoll, eventsTriggered, WS_EVENTS_PER_LOOP, -1);
    for (int32_t i = 0; i < events; i++) {
      if (eventsTriggered[i].data.fd == socketInfo->socketFD) {
        WSConnection client;
        acceptNewConnection(socketInfo, &client);
        onConnect(socketInfo->connections[client.clientFD]);
      } else {
        WSConnection * const connection = socketInfo->connections[eventsTriggered[i].data.fd];
        if (connection == NULL) {
          printf("Connection already closed.\n");
          continue;
        }
        if (connection->needsHandshake) {
          if (performHandshake(socketInfo, connection) == -1)
            continue;
          connection->pathHanlder->onHandshake(connection);
        } else {
          int32_t closeCode;
          if ((closeCode = receiveDataFrom(connection)) > 1) {
            connection->pathHanlder->onDisconnect(connection);
            freeConnectionResources(socketInfo, connection, closeCode);
            continue;
          }
          size_t size = connection->pathHanlder->onMessage(connection, connection->recvBuffer, &(connection->sendBuffer));
          sendDataTo(connection, connection->sendBuffer, size);
        }
      }
    }
  }
}
