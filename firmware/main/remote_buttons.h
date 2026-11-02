#pragma once
#include <stdint.h>
#include <stdbool.h>
/* bits: plus, minus, walk, cycle. Active-high pressed mask. */
typedef struct {
    unsigned candidate, stable;
    uint64_t since;
    bool released, minus_hold, shutdown;
    unsigned event; /* mask of the last accepted press edge; the caller clears it */
} remote_buttons_t;
void remote_buttons_reset(remote_buttons_t *s, uint64_t now);
/* Debounces the whole mask; accepts only single-button edges after all released.
   Startup/disabled holds must be released before they can select assistance. */
bool remote_buttons_poll(remote_buttons_t *s, uint64_t now, unsigned pressed, bool enabled,
                         unsigned current, unsigned *requested);
