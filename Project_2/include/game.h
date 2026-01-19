#ifndef SERVER_GAME_H
#define SERVER_GAME_H

#include "../include/board.h"

/**
 * @brief Entry point for the game logic.
 *
 * Runs the game loop for a single session, managing threads for Pacman
 * and ghosts, and sending updates to the client via 'notif_fd'.
 *
 * @param game_board Pointer to the initialized game board.
 * @param notif_fd Open file descriptor for client updates.
 * @param req_fd Pre-opened file descriptor for reading player input.
 * @return int Exit status (NEXT_LEVEL, QUIT_GAME, etc.)
 */
int run_game_logic(board_t *game_board, int notif_fd, int req_fd);

#endif
