#pragma once

#include <stdint.h>

#include "ui/theme.h"

uint8_t *theme_fixture_on_card(const char *fixture, const char *card_name, uint32_t *size);
uint8_t *theme_fixture_placed(const char *card_name, const ThemeLayout *wide,
                              const ThemeLayout *tall, uint32_t *size);
