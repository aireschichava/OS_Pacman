#ifndef DISPLAY_H
#define DISPLAY_H

#include "board.h"

// Draw modes
#define DRAW_MENU 0
#define DRAW_GAME 1
#define DRAW_WIN 2
#define DRAW_GAME_OVER 3

void terminal_init(void);
void terminal_cleanup(void);
void draw_board(board_t *board, int mode);
void refresh_screen(void);
char get_input(void);

#endif
