﻿#pragma once

#include "io/files-util.h"

typedef struct player_type player_type;
void print_tomb(player_type *dead_ptr);
void show_death_info(player_type *creature_ptr, display_player_pf display_player);
