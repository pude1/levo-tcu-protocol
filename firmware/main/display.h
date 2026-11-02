#pragma once
#include <stdbool.h>
#include <stdint.h>
void display_start(void);
// Optional short labels for bench/bridge builds; NULL restores sniffer labels.
void display_set_mode(const char *mode, const char *status);
void display_set_bitrate(uint32_t bitrate);
void display_update(uint32_t count, uint32_t missed, uint32_t errors, int rx, int64_t last_frame);
// Terrain AUTO indicator for the header (bias -1..+1).
void display_set_auto(bool enabled, int bias);
