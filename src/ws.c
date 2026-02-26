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

#include <base64.h>
#include <hashmap.h>
#include <ws.h>

#define WS_BUFFER_SML 128
#define WS_BUFFER_BIG 1024
#define WS_SPECIAL_KEY "258EAFA5-E914-47DA-95CA-C5AB0DC85B11"
#define WS_SOCKET_BACKLOG 32
#define WS_EVENTS_PER_LOOP 32

#define WS_FIN_BIT_END 0x80
#define WS_OPCODE_TEXT 0x1
#define WS_OPCODE_CLOSE 0x8
#define WS_OPCODE_PING 0x9

//This is the compare function for the connections hashmap, the keys are of type int
static int compareConnections(void const * key1_int, void const * key2_int) {
  return *(int *)key1_int == *(int *)key2_int;
}

static int comparePaths(void const * key1_str, void const * key2_str) {
  int isEqual = 1;
  for (size_t i = 0;((char *)key1_str)[i] != '\0' && ((char *)key2_str)[i] != '\0'; i++)
    if (((char *)key1_str)[i] != ((char *)key2_str)[i]) {
      isEqual = 0;
      break;
    }

  return isEqual;
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

static void freeConnectionResourcesForEachWrapper(void * clientPtr, void * contextPtr) {
  WSSocket * socketInfo = (WSSocket *)contextPtr;
  WSConnection * client = (WSConnection *)clientPtr;
  freeConnectionResources(socketInfo, client);
}

static int isHTTPUpgrade(char const * const request, ssize_t length, char * path, char * key) {
  enum HTTPCHECK {
    GET,
    PATH,
    HTTP,
    KEY,
  };
  int foundConnection = 0;
  int foundUpgrade = 0;
  int foundKey = 0;
  enum HTTPCHECK state = GET;
  for (char const * curr = request;;) {
    switch (state) {
      case GET:;
        char get[] = "GET";
        for (size_t i = 0; i < 3; i++)
          if (*(curr++) != get[i])
            return 0;
        
        state = HTTP;
        break;
      case PATH:;
        size_t pathLenght = 0;
        char const * pathStart = ++curr;
        while (*(curr++) != ' ') pathLenght++;

        path = malloc((pathLenght + 1) * sizeof(char));
        for (size_t i = 0; i < pathLenght; i++)
          path[i] = pathStart[i];
        path[pathLenght] = '\0';

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
          if (!foundConnection && *curr == connection[0]) {
            size_t i = 0;
            for (; i < sizeof(connection); i++) {
             if (curr[i] == connection[i])
               continue;
             break;
            }
            if (i == sizeof(connection)) foundConnection = 1;
          }
          if (!foundUpgrade && *curr == upgrade[0]) {
            size_t i = 0;
            for (; i < sizeof(upgrade); i++) {
              if (curr[i] == upgrade[i])
                continue;
              break;
            }
            if (i == sizeof(upgrade)) foundUpgrade = 1;
          }
          if (!foundKey && *curr == wskey[0]) {
            size_t i = 0;
            for (; i < sizeof(wskey); i++) {
              if (curr[i] == wskey[i])
                continue;
              break;
            }
            if (i == sizeof(wskey)) {
              foundKey = 1;
              size_t keyLength = 24;
              char const * keyStart = curr + i + 1;

              key = malloc((keyLength + 1) * sizeof(char));
              for (size_t j = 0; j < keyLength; j++)
                key[j] = keyStart[j];
              key[keyLength] = '\0';
            }
          }
        }
        break;
    }
  }

  if (foundConnection && foundUpgrade && foundKey)
    return 1;
  else {
    free(path);
    free(key);
    path = NULL;
    key = NULL;
    return 0;
  }
}

static int performHandshake(WSSocket * const socketInfo, WSConnection * const client) {
  char addr[INET_ADDRSTRLEN];
  inet_ntop(AF_INET, &(client->addrInfo.sin_addr), addr, INET_ADDRSTRLEN);
  socklen_t addrLen = sizeof(struct sockaddr_in);

  char recvBuf[WS_BUFFER_BIG];
  ssize_t recvSize;
  if ((recvSize = recvfrom(client->socketFD, recvBuf, WS_BUFFER_BIG - 1, 0, (struct sockaddr *)&(client->addrInfo), &addrLen)) == -1) {
    printf("(%s): Could not read message: %s\n", addr, strerror(errno));
    goto handshakeFail;
  }
  recvBuf[recvSize] = '\0';

  char * key = NULL;
  char * path = NULL;
  if (!isHTTPUpgrade(recvBuf, recvSize, key, path)) {
    printf("(%s): Invalid websocket upgrade request.\n", addr);
    goto handshakeFail;
  }
  WSPathHandler * pathHandler;
  if ((pathHandler = mapGet(&(socketInfo->paths), path)) == NULL) {
    printf("(%s): Invalid websocket path.\n", addr);
    free(path);
    free(key);
    goto handshakeFail;
  }
  free(path);
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

  send(client->socketFD, response, strlen(response), 0);
  client->needsHandshake = 0;

  printf("(%s): Succeful handshake on path %s\n", addr, client->connectionPath);

  client->recvBuffer = malloc(WS_BUFFER_SML);
  client->sendBuffer = malloc(WS_BUFFER_SML);
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

  if (finBit != WS_FIN_BIT_END) {
    printf("(%s): Refusing to read fragmented data. Closing connection.\n", addr);
    closeCode = htons(1003);
    goto closeConnection;
  }
  if (opcode == WS_OPCODE_CLOSE) {
    printf("(%s): Client asked to close connection.\n", addr);
    closeCode = htons(1000);
    goto closeConnection;
  }
  if (opcode != WS_OPCODE_TEXT && opcode != WS_OPCODE_PING) {
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

  int isPing = (opcode == WS_OPCODE_PING) ? 1 : 0;
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
    printf("(%s): ping.\n", addr);
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
  initMap(&(socketInfo->paths), sizeof(char *), sizeof(WSPathHandler), comparePaths);

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
  mapForEach(&(socketInfo->connections), socketInfo, freeConnectionResourcesForEachWrapper);
  freeMap(&(socketInfo->connections));
  close(socketInfo->socketFD);
  memset(socketInfo, 0, sizeof(WSSocket));
  socketInfo = NULL;
}

void addValidPath(WSSocket * const socketInfo, char * const path,
    void (*onHandshake)(WSConnection const * const client),
    void (*onDisconnect)(struct WSConnection const * const client),
    size_t (*onMessage)(WSConnection const * const client, char const * const incData, char ** const outData)) {
  WSPathHandler pathHandler = {
    .onHandshake = onHandshake,
    .onDisconnect = onDisconnect,
    .onMessage = onMessage
  };
  // Prob look into this cuz i don't think it works!
  mapPut(&(socketInfo->paths), path, &pathHandler);
}

void startEventLoop(WSSocket * const socketInfo, void (*onConnect)(WSConnection const * const client)) {
  struct epoll_event eventsTriggered[WS_EVENTS_PER_LOOP];
  for (;;) {
    int events = epoll_wait(socketInfo->socketEventPoll, eventsTriggered, WS_EVENTS_PER_LOOP, -1);
    for (int i = 0; i < events; i++) {
      if (eventsTriggered[i].data.fd == socketInfo->socketFD) {
        WSConnection client;
        acceptNewConnection(socketInfo, &client);
        onConnect(mapGet(&(socketInfo->connections), &(client.socketFD)));
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
          connection->pathHanlder->onHandshake(connection);
        } else {
          if (receiveDataFrom(socketInfo, connection) > 0) {
            connection->pathHanlder->onDisconnect(connection);
            continue;
          }
          size_t size = connection->pathHanlder->onMessage(connection, connection->recvBuffer, &(connection->sendBuffer));
          sendDataTo(connection, connection->sendBuffer, size);
        }
      }
    }
  }
}
