﻿#pragma once

#include "main-win/main-win-define.h"

#include <windows.h>

void load_bg_prefs(void);
void finalize_bg();

void delete_bg(void);
bool load_bg(char *filename);
void draw_bg(HDC hdc, RECT *r);
