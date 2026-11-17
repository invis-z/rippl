#pragma once

#include <stdint.h>

#include "device/core1_stream.h"

void core1_source_use_ring(Core1Source *source, uint32_t bytes);
void core1_source_stop(Core1Source *source);
void core1_source_begin(Core1Source *source, AudioTrackInfo *track);
