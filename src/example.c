#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <string.h>
#include <unistd.h>

#include <hashmap.h>
#include <ws.h>

#define PORT 21455

void sigintHandler(int sig);
void acceptNewClient(WSConnection const * const client);
void handshakeNewClient(WSConnection const * const client);
size_t processClientMessage(WSConnection const * const client, char const * const incData, char ** outData);

WSSocket socketInfo;

int main(int argc, char **argv) {
  if (initSocket(&socketInfo) == -1)
    exit(EXIT_FAILURE);

  if (bindSocket(&socketInfo, PORT) == -1) {
    close(socketInfo.socketFD);
    exit(EXIT_FAILURE);
  }

  signal(SIGINT, sigintHandler);

  eventLoop(&socketInfo, acceptNewClient, handshakeNewClient, processClientMessage);
}

void sigintHandler(int sig) {
  const char msg[] = "Closing open connections and free-ing allocated memory\n";
  write(STDOUT_FILENO, msg, strlen(msg));
  closeSocket(&socketInfo);
  exit(EXIT_SUCCESS);
}

void acceptNewClient(WSConnection const * const client) {
  return;
}

void handshakeNewClient(WSConnection const * const client) {
  return;
}

size_t processClientMessage(WSConnection const * const client, char const * const incData, char ** outData) {
  char testString[] = 
    "Lorem ipsum dolor sit amet, consectetur adipiscing elit. Donec fringilla ligula ut magna congue dapibus. "
    "Vestibulum ante ipsum primis in faucibus orci luctus et ultrices posuere cubilia curae; "
    "Integer et consectetur mi. Nam feugiat, eros fringilla feugiat hendrerit, elit velit.";
  size_t size = strlen(testString);
  char * data = realloc(*outData, size + 1);
  strcpy(data, testString);
  *outData = data;
  return size + 1;
}
