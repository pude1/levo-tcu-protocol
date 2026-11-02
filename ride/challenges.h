#pragma once
/* Medal ladders and personal bests for detected tricks. Pure C. */
#include <stdbool.h>
#include "tricks.h"

typedef enum { MEDAL_NONE, MEDAL_BRONZE, MEDAL_SILVER, MEDAL_GOLD } medal_t;

typedef struct {
    trick_type_t type;
    const char *metric; /* "s", "cm" */
    float tiers[3];     /* bronze, silver, gold */
} challenge_def_t;

typedef struct {
    float best[TRICK_COUNT];      /* scored metric */
    uint16_t count[TRICK_COUNT];  /* this session */
    uint16_t medals[TRICK_COUNT][4];
    uint32_t points;
} challenge_book_t;

typedef struct {
    trick_type_t type;
    float value;          /* scored metric for this attempt */
    medal_t medal;
    bool personal_best;
    float next_target;    /* next tier above this attempt, 0 if gold */
    bool spin180, spin360;
    uint32_t points;      /* awarded for this attempt */
} challenge_result_t;

const challenge_def_t *challenge_def(trick_type_t t);
float challenge_metric(const trick_event_t *e);
void challenge_book_reset(challenge_book_t *b);
challenge_result_t challenge_score(challenge_book_t *b, const trick_event_t *e);
const char *medal_name(medal_t m);
