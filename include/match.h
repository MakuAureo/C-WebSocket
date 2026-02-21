#ifndef MATCH_H
#define MATCH_H

typedef enum {
  WAIT,
  DAY1,
  DAY2,
  DAY3,
  END
} MatchState;

typedef struct {
  MatchState state;
  int playerCount;
  int score;
  char * opponentKey;
  char ** playerIDs;
  char ** playerNames;
} TeamInfo;

#endif
