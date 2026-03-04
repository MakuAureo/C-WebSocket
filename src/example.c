#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <string.h>
#include <unistd.h>

#include "ws.h"

#define PORT 21455

void sigintHandler(int sig);
void onConnect(WSConnection const * const client);
void onHandshake(WSConnection const * const client);
size_t onMessage(WSConnection const * const client, char const * const incData, char ** outData);
void onDisconnect(WSConnection const * const client);

WSSocket socketInfo;

int main(int argc, char ** argv) {
  if (initSocket(&socketInfo) != 0)
    exit(EXIT_FAILURE);

  if (bindSocket(&socketInfo, PORT) != 0) {
    close(socketInfo.socketFD);
    exit(EXIT_FAILURE);
  }

  signal(SIGINT, sigintHandler);
  printf("(Server): Socket bound and listening to port: %d\n", PORT);

  if (addValidPath(&socketInfo, "/chat", onHandshake, onDisconnect, onMessage) != 0) {
    printf("(Server): Invalid regex path\n");
    exit(EXIT_FAILURE);
  }

  runSocketLoop(&socketInfo, onConnect);
}

void sigintHandler(int sig) {
  const char msg[] = "\n(Server): Closing open connections and free-ing allocated memory\n";
  write(STDOUT_FILENO, msg, strlen(msg));
  closeSocket(&socketInfo);
  exit(EXIT_SUCCESS);
}

void onConnect(WSConnection const * const client) {
  return;
}

void onHandshake(WSConnection const * const client) {
  return;
}

size_t onMessage(WSConnection const * const client, char const * const incData, char ** outData) {
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

void onDisconnect(WSConnection const * const client) {
  return;
}
