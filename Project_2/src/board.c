#include "../include/board.h"
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

static int debug_fd = -1;

/**
 * @brief Helper private function to find and kill pacman at specific position.
 * @param board Pointer to the game board structure.
 * @param new_x The x-coordinate to check.
 * @param new_y The y-coordinate to check.
 * @return DEAD_PACMAN if pacman is found and killed, VALID_MOVE otherwise.
 */
static int find_and_kill_pacman(board_t *board, int new_x, int new_y) {
  for (int p = 0; p < board->n_pacmans; p++) {
    pacman_t *pac = &board->pacmans[p];
    if (pac->pos_x == new_x && pac->pos_y == new_y && pac->alive) {
      pac->alive = 0;
      kill_pacman(board, p);
      return DEAD_PACMAN;
    }
  }
  return VALID_MOVE;
}

/**
 * @brief Helper private function for getting board position index.
 * @param board Pointer to the game board structure.
 * @param x The x-coordinate.
 * @param y The y-coordinate.
 * @return The linear index in the board array.
 */
static inline int get_board_index(board_t *board, int x, int y) {
  return y * board->width + x;
}

/**
 * @brief Helper private function for checking valid position.
 * @param board Pointer to the game board structure.
 * @param x The x-coordinate.
 * @param y The y-coordinate.
 * @return 1 if position is valid, 0 otherwise.
 */
static inline int is_valid_position(board_t *board, int x, int y) {
  return (x >= 0 && x < board->width) &&
         (y >= 0 && y < board->height); // Inside of the board boundaries
}

/**
 * @brief Checks if a cell is valid for placement of game elements.
 * @param board Pointer to the game board structure.
 * @param x The x-coordinate.
 * @param y The y-coordinate.
 * @return 1 if playable, 0 otherwise.
 */
static int is_playable_cell(board_t *board, int x, int y) {
  if (!is_valid_position(board, x, y))
    return 0;
  int idx = get_board_index(board, x, y);
  char cell = board->board[idx].content;
  if (cell == 'X' || cell == 'W' || cell == 'M' || cell == 'C') {
    return 0;
  }
  if (board->board[idx].has_portal) {
    return 0;
  }
  return 1;
}

/**
 * @brief Finds the first available playable cell on the board.
 * @param board Pointer to the game board structure.
 * @param x Pointer to store the x-coordinate.
 * @param y Pointer to store the y-coordinate.
 * @return 1 if found, 0 otherwise.
 */
static int find_first_playable_cell(board_t *board, int *x, int *y) {
  for (int row = 0; row < board->height; row++) {
    for (int col = 0; col < board->width; col++) {
      if (is_playable_cell(board, col, row)) {
        *x = col;
        *y = row;
        return 1;
      }
    }
  }
  return 0;
}

/**
 * @brief Sleeps for a specified number of milliseconds.
 * @param milliseconds Duration to sleep in milliseconds.
 */
void sleep_ms(int milliseconds) {
  struct timespec ts;
  ts.tv_sec = milliseconds / 1000;
  ts.tv_nsec = (milliseconds % 1000) * 1000000;
  nanosleep(&ts, NULL);
}

/**
 * @brief Moves the Pacman based on the command.
 * @param board Pointer to the game board structure.
 * @param pacman_index Index of the pacman to move.
 * @param command Pointer to the command structure.
 * @return Result of the move (VALID_MOVE, INVALID_MOVE, DEAD_PACMAN,
 * REACHED_PORTAL).
 */
int move_pacman(board_t *board, int pacman_index, command_t *command) {
  pthread_rwlock_wrlock(&board->state_lock);
  if (pacman_index < 0 || !board->pacmans[pacman_index].alive) {
    pthread_rwlock_unlock(&board->state_lock);
    return DEAD_PACMAN; // Invalid or dead pacman
  }

  pacman_t *pac = &board->pacmans[pacman_index];
  int new_x = pac->pos_x;
  int new_y = pac->pos_y;

  // check passo
  if (pac->waiting > 0) {
    pac->waiting -= 1;
    pthread_rwlock_unlock(&board->state_lock);
    return VALID_MOVE;
  }
  pac->waiting = pac->passo;

  char direction = toupper(command->command);

  if (direction == 'R') {
    char directions[] = {'W', 'S', 'A', 'D'};
    direction = directions[rand() % 4];
  }

  // Calculate new position based on direction
  switch (direction) {
  case 'W': // Up
    new_y--;
    break;
  case 'S': // Down
    new_y++;
    break;
  case 'A': // Left
    new_x--;
    break;
  case 'D': // Right
    new_x++;
    break;
  case 'T': // Wait
    if (command->turns_left == 1) {
      pac->current_move += 1; // move on
      command->turns_left = command->turns;
    } else
      command->turns_left -= 1;
    pthread_rwlock_unlock(&board->state_lock);
    return VALID_MOVE;
  default:
    pthread_rwlock_unlock(&board->state_lock);
    return INVALID_MOVE; // Invalid direction
  }

  // Logic for the WASD movement
  pac->current_move += 1;

  // Check boundaries
  if (!is_valid_position(board, new_x, new_y)) {
    pthread_rwlock_unlock(&board->state_lock);
    return INVALID_MOVE;
  }

  int new_index = get_board_index(board, new_x, new_y);
  int old_index = get_board_index(board, pac->pos_x, pac->pos_y);
  char target_content = board->board[new_index].content;

  if (board->board[new_index].has_portal) {
    board->board[old_index].content = ' ';
    board->board[new_index].content = 'C';
    pac->pos_x = new_x;
    pac->pos_y = new_y;
    board->level_finished = 1;
    pthread_rwlock_unlock(&board->state_lock);
    return REACHED_PORTAL;
  }

  // Check for walls
  if (target_content == 'W' || target_content == 'X') {
    pthread_rwlock_unlock(&board->state_lock);
    return INVALID_MOVE;
  }

  // Check for ghosts
  if (target_content == 'M') {
    kill_pacman(board, pacman_index);
    pthread_rwlock_unlock(&board->state_lock);
    return DEAD_PACMAN;
  }

  // Collect points
  if (board->board[new_index].has_dot) {
    pac->points += new_index;
    board->board[new_index].has_dot = 0;
  }
  // ---> EXERCISE: COSTLY STEP <---
  // pac->points -= 1;

  board->board[old_index].content = ' ';
  pac->pos_x = new_x;
  pac->pos_y = new_y;
  board->board[new_index].content = 'C';

  pthread_rwlock_unlock(&board->state_lock);
  return VALID_MOVE;
}

/**
 * @brief Helper private function for charged ghost movement in one direction.
 * @param board Pointer to the game board structure.
 * @param ghost Pointer to the ghost structure.
 * @param direction The direction to move.
 * @param new_x Pointer to store the new x coordinate.
 * @param new_y Pointer to store the new y coordinate.
 * @return Result of the move.
 */
static int move_ghost_charged_direction(board_t *board, ghost_t *ghost,
                                        char direction, int *new_x,
                                        int *new_y) {
  int x = ghost->pos_x;
  int y = ghost->pos_y;
  *new_x = x;
  *new_y = y;

  switch (direction) {
  case 'W': // Up
    if (y == 0)
      return INVALID_MOVE;
    *new_y = 0; // In case there is no colision
    for (int i = y - 1; i >= 0; i--) {
      char target_content = board->board[get_board_index(board, x, i)].content;
      if (target_content == 'W' || target_content == 'X' ||
          target_content == 'M') {
        *new_y = i + 1; // stop before colision
        return VALID_MOVE;
      } else if (target_content == 'C') {
        *new_y = i;
        return find_and_kill_pacman(board, *new_x, *new_y);
      }
    }
    break;

  case 'S': // Down
    if (y == board->height - 1)
      return INVALID_MOVE;
    *new_y = board->height - 1; // In case there is no colision
    for (int i = y + 1; i < board->height; i++) {
      char target_content = board->board[get_board_index(board, x, i)].content;
      if (target_content == 'W' || target_content == 'X' ||
          target_content == 'M') {
        *new_y = i - 1; // stop before colision
        return VALID_MOVE;
      }
      if (target_content == 'C') {
        *new_y = i;
        return find_and_kill_pacman(board, *new_x, *new_y);
      }
    }
    break;

  case 'A': // Left
    if (x == 0)
      return INVALID_MOVE;
    *new_x = 0; // In case there is no colision
    for (int j = x - 1; j >= 0; j--) {
      char target_content = board->board[get_board_index(board, j, y)].content;
      if (target_content == 'W' || target_content == 'X' ||
          target_content == 'M') {
        *new_x = j + 1; // stop before colision
        return VALID_MOVE;
      }
      if (target_content == 'C') {
        *new_x = j;
        return find_and_kill_pacman(board, *new_x, *new_y);
      }
    }
    break;

  case 'D': // Right
    if (x == board->width - 1)
      return INVALID_MOVE;
    *new_x = board->width - 1; // In case there is no colision
    for (int j = x + 1; j < board->width; j++) {
      char target_content = board->board[get_board_index(board, j, y)].content;
      if (target_content == 'W' || target_content == 'X' ||
          target_content == 'M') {
        *new_x = j - 1; // stop before colision
        return VALID_MOVE;
      }
      if (target_content == 'C') {
        *new_x = j;
        return find_and_kill_pacman(board, *new_x, *new_y);
      }
    }
    break;
  default:
    debug("DEFAULT CHARGED MOVE - direction = %c\n", direction);
    return INVALID_MOVE;
  }
  return VALID_MOVE;
}

/**
 * @brief Moves the ghost in a charged manner.
 * @param board Pointer to the game board structure.
 * @param ghost_index Index of the ghost to move.
 * @param direction The direction to move.
 * @return Result of the move.
 */
int move_ghost_charged(board_t *board, int ghost_index, char direction) {
  ghost_t *ghost = &board->ghosts[ghost_index];
  int x = ghost->pos_x;
  int y = ghost->pos_y;
  int new_x = x;
  int new_y = y;

  ghost->charged = 0; // uncharge
  int result =
      move_ghost_charged_direction(board, ghost, direction, &new_x, &new_y);
  if (result == INVALID_MOVE) {
    debug("DEFAULT CHARGED MOVE - direction = %c\n", direction);
    return INVALID_MOVE;
  }

  // Get board indices
  int old_index = get_board_index(board, ghost->pos_x, ghost->pos_y);
  int new_index = get_board_index(board, new_x, new_y);

  // Update board - clear old position
  board->board[old_index].content = ' ';
  // Update ghost position
  ghost->pos_x = new_x;
  ghost->pos_y = new_y;
  // Update board - set new position
  board->board[new_index].content = 'M';
  return result;
}

/**
 * @brief Moves the ghost based on the command.
 * @param board Pointer to the game board structure.
 * @param ghost_index Index of the ghost to move.
 * @param command Pointer to the command structure.
 * @return Result of the move.
 */
int move_ghost(board_t *board, int ghost_index, command_t *command) {
  pthread_rwlock_wrlock(&board->state_lock);
  ghost_t *ghost = &board->ghosts[ghost_index];
  int new_x = ghost->pos_x;
  int new_y = ghost->pos_y;

  // check passo
  if (ghost->waiting > 0) {
    ghost->waiting -= 1;
    pthread_rwlock_unlock(&board->state_lock);
    return VALID_MOVE;
  }

  // if (ghost->waiting > 0 && board->pacmans[0].points <= 10) {
  //     ghost->waiting -= 1;
  //     pthread_rwlock_unlock(&board->state_lock);
  //     return VALID_MOVE;
  // }
  ghost->waiting = ghost->passo;

  char direction = command->command;

  if (direction == 'R') {
    char directions[] = {'W', 'S', 'A', 'D'};
    direction = directions[rand() % 4];
  }

  // Calculate new position based on direction
  switch (direction) {
  case 'W': // Up
    new_y--;
    break;
  case 'S': // Down
    new_y++;
    break;
  case 'A': // Left
    new_x--;
    break;
  case 'D': // Right
    new_x++;
    break;
  case 'C': // Charge
    ghost->current_move += 1;
    ghost->charged = 1;
    pthread_rwlock_unlock(&board->state_lock);
    return VALID_MOVE;
  case 'T': // Wait
    if (command->turns_left == 1) {
      ghost->current_move += 1; // move on
      command->turns_left = command->turns;
    } else
      command->turns_left -= 1;
    pthread_rwlock_unlock(&board->state_lock);
    return VALID_MOVE;
  default:
    pthread_rwlock_unlock(&board->state_lock);
    return INVALID_MOVE; // Invalid direction
  }

  // Logic for the WASD movement
  ghost->current_move++;
  if (ghost->charged) {
    int res = move_ghost_charged(board, ghost_index, direction);
    pthread_rwlock_unlock(&board->state_lock);
    return res;
  }

  // Check boundaries
  if (!is_valid_position(board, new_x, new_y)) {
    pthread_rwlock_unlock(&board->state_lock);
    return INVALID_MOVE;
  }

  // Check board position
  int new_index = get_board_index(board, new_x, new_y);
  int old_index = get_board_index(board, ghost->pos_x, ghost->pos_y);
  char target_content = board->board[new_index].content;

  // Check for walls and ghosts
  if (target_content == 'W' || target_content == 'X' || target_content == 'M') {
    pthread_rwlock_unlock(&board->state_lock);
    return INVALID_MOVE;
  }

  int result = VALID_MOVE;
  // Check for pacman
  if (target_content == 'C') {
    result = find_and_kill_pacman(board, new_x, new_y);
  }

  // Update board - clear old position (restore what was there)
  board->board[old_index].content =
      ' '; // Or restore the dot if ghost was on one

  // Update ghost position
  ghost->pos_x = new_x;
  ghost->pos_y = new_y;

  // Update board - set new position
  board->board[new_index].content = 'M';
  pthread_rwlock_unlock(&board->state_lock);
  return result;
}

/**
 * @brief Kills a pacman and removes it from the board.
 * @param board Pointer to the game board structure.
 * @param pacman_index Index of the pacman to kill.
 */
void kill_pacman(board_t *board, int pacman_index) {
  debug("Killing %d pacman\n\n", pacman_index);
  pacman_t *pac = &board->pacmans[pacman_index];
  int index = pac->pos_y * board->width + pac->pos_x;

  // Remove pacman from the board
  board->board[index].content = ' ';

  // Mark pacman as dead
  pac->alive = 0;
}

/**
 * @brief Loads the pacman into the board.
 * @param board Pointer to the game board structure.
 * @param points Initial points for the pacman.
 * @return 0 on success.
 */
int load_pacman(board_t *board, int points) {
  board->board[1 * board->width + 1].content = 'C'; // Pacman
  board->pacmans[0].pos_x = 1;
  board->pacmans[0].pos_y = 1;
  board->pacmans[0].alive = 1;
  board->pacmans[0].points = points;
  return 0;
}

/**
 * @brief Loads the ghosts into the board.
 * @param board Pointer to the game board structure.
 * @return 0 on success.
 */
int load_ghost(board_t *board) {
  // Ghost 0
  board->board[3 * board->width + 1].content = 'M'; // Monster
  board->ghosts[0].pos_x = 1;
  board->ghosts[0].pos_y = 3;
  board->ghosts[0].passo = 0;
  board->ghosts[0].waiting = 0;
  board->ghosts[0].current_move = 0;
  board->ghosts[0].n_moves = 16;
  for (int i = 0; i < 8; i++) {
    board->ghosts[0].moves[i].command = 'D';
    board->ghosts[0].moves[i].turns = 1;
  }
  for (int i = 8; i < 16; i++) {
    board->ghosts[0].moves[i].command = 'A';
    board->ghosts[0].moves[i].turns = 1;
  }

  // Ghost 1
  board->board[2 * board->width + 4].content = 'M'; // Monster
  board->ghosts[1].pos_x = 4;
  board->ghosts[1].pos_y = 2;
  board->ghosts[1].passo = 1;
  board->ghosts[1].waiting = 1;
  board->ghosts[1].current_move = 0;
  board->ghosts[1].n_moves = 1;
  board->ghosts[1].moves[0].turns = 1;
  return 0;
}

/**
 * @brief Helper to read a line (without the trailing newline) from a file
 * descriptor.
 */
static int read_line_raw(int fd, char *buffer, int max_len) {
  int i = 0;
  char c;
  while (i < max_len - 1) {
    int n = read(fd, &c, 1);
    if (n <= 0) {
      break;
    }
    if (c == '\n') {
      break;
    }
    if (c == '\r') {
      // Skip carriage returns to be tolerant of CRLF
      continue;
    }
    buffer[i++] = c;
  }
  buffer[i] = '\0';
  return i;
}

/**
 * @brief Returns true if the line is empty or starts with '#'.
 */
static int is_comment_or_empty(const char *line) {
  const char *p = line;
  while (*p == ' ' || *p == '\t')
    p++;
  return (*p == '\0' || *p == '#');
}

static int is_comment(const char *line) {
  const char *p = line;
  while (*p == ' ' || *p == '\t')
    p++;
  return (*p != '\0' && *p == '#');
}

/**
 * @brief Reads only comment lines from file, skipping everything else.
 */
static int read_comment_line(int fd, char *buffer, int max_len) {
  int len = 0;
  while ((len = read_line_raw(fd, buffer, max_len)) > 0) {
    if (is_comment(buffer)) {
      return len; // Found a comment!
    }
  }
  return len; // EOF
}
/**
 * @brief Reads a line from file, skipping empty lines and comments.
 * @param fd File descriptor.
 * @param buffer Buffer to store the line.
 * @param max_len Maximum length of the buffer.
 * @return Length of the line, or 0 on EOF, -1 on error.
 */
static int read_effective_line(int fd, char *buffer, int max_len) {
  int len = 0;
  while ((len = read_line_raw(fd, buffer, max_len)) > 0) {
    if (!is_comment_or_empty(buffer)) {
      return len;
    }
  }
  return len;
}

/**
 * @brief Reads only comment lines from file, skipping everything else.
 * @param fd File descriptor.
 * @param buffer Buffer to store the line.
 * @param max_len Maximum length of the buffer.
 * @return Length of the line, or 0 on EOF.
 */

/**
 * @brief Parses a motion definition file (e.g., pacman.p, monster.m).
 * @param filename Path to the motion file.
 * @param moves Array to store parsed commands.
 * @param n_moves Pointer to store the number of moves.
 * @param passo Pointer to store the speed/step configuration.
 * @param pos_x Pointer to store the starting x coordinate (from POS).
 * @param pos_y Pointer to store the starting y coordinate (from POS).
 * @return 0 on success, -1 on failure.
 */
static int parse_motion_file(const char *filename, command_t *moves,
                             int *n_moves, int *passo, int *pos_x, int *pos_y) {
  int fd = open(filename, O_RDONLY);
  if (fd < 0)
    return -1;

  char line[1024];
  *n_moves = 0;
  while (read_effective_line(fd, line, sizeof(line)) > 0) {
    char *saveptr = NULL;
    char *token = strtok_r(line, " \t\r\n", &saveptr);

    while (token != NULL) {
      if (strcmp(token, "PASSO") == 0) {
        char *val = strtok_r(NULL, " \t\r\n", &saveptr);
        if (val != NULL) {
          *passo = atoi(val);
        }
      } else if (strcmp(token, "POS") == 0) {
        char *ly = strtok_r(NULL, " \t\r\n", &saveptr);
        char *lx = strtok_r(NULL, " \t\r\n", &saveptr);
        if (ly != NULL && lx != NULL && pos_x != NULL && pos_y != NULL) {
          *pos_y = atoi(ly);
          *pos_x = atoi(lx);
        }
      } else if (*token != '\0') {
        if (*n_moves < MAX_MOVES) {
          command_t *mv = &moves[*n_moves];
          mv->command = token[0];
          mv->turns = 1;
          if (mv->command == 'T') {
            char *wait_turns = strtok_r(NULL, " \t\r\n", &saveptr);
            if (wait_turns != NULL && *wait_turns != '#') {
              mv->turns = atoi(wait_turns);
            }
          }
          mv->turns_left = mv->turns;
          (*n_moves)++;
        }
      }
      token = strtok_r(NULL, " \t\r\n", &saveptr);
    }
  }
  close(fd);

  return 0;
}

/**
 * @brief Loads Pacman behavior from a file.
 * @param board Pointer to the game board structure.
 * @param pac_idx Index of the pacman.
 * @param filename Path to the behavior file.
 * @return 0 on success, -1 on failure.
 */
static int load_pacman_behavior(board_t *board, int pac_idx,
                                const char *filename) {
  pacman_t *p = &board->pacmans[pac_idx];
  return parse_motion_file(filename, p->moves, &p->n_moves, &p->passo,
                           &p->pos_x, &p->pos_y);
}

/**
 * @brief Loads Ghost behavior from a file.
 * @param board Pointer to the game board structure.
 * @param ghost_idx Index of the ghost.
 * @param filename Path to the behavior file.
 * @return 0 on success, -1 on failure.
 */
static int load_ghost_behavior(board_t *board, int ghost_idx,
                               const char *filename) {
  ghost_t *g = &board->ghosts[ghost_idx];
  return parse_motion_file(filename, g->moves, &g->n_moves, &g->passo,
                           &g->pos_x, &g->pos_y);
}

/**
 * @brief Resets the board structure, freeing allocated memory.
 * @param board Pointer to the game board structure.
 */
static void reset_board(board_t *board) {
  if (board->lock_initialized) {
    pthread_rwlock_destroy(&board->state_lock);
    board->lock_initialized = 0;
  }

  free(board->board);
  free(board->pacmans);
  free(board->ghosts);

  board->board = NULL;
  board->pacmans = NULL;
  board->ghosts = NULL;
  board->n_pacmans = 0;
  board->n_ghosts = 0;
  board->width = 0;
  board->height = 0;
  board->tempo = 0;
  board->level_finished = 0;
  board->level_name[0] = '\0';
  board->pacman_file[0] = '\0';
  for (int i = 0; i < MAX_GHOSTS; i++) {
    board->ghosts_files[i][0] = '\0';
  }
}

/**
 * @brief Loads a level from a file, parsing dimensions, map, and entities.
 * @param board Pointer to the game board structure.
 * @param filename Path to the level file.
 * @param accumulated_points Points carried over from previous levels.
 * @return 0 on success, -1 on failure.
 */
int load_level(board_t *board, const char *filename, int accumulated_points) {
  int fd = open(filename, O_RDONLY);
  if (fd < 0) {
    return -1;
  }

  reset_board(board);

  char line[1024];
  int rows_read = 0;
  int map_pac_x = -1, map_pac_y = -1;
  int map_ghost_x[MAX_GHOSTS];
  int map_ghost_y[MAX_GHOSTS];
  for (int i = 0; i < MAX_GHOSTS; i++) {
    map_ghost_x[i] = -1;
    map_ghost_y[i] = -1;
  }
  int map_ghost_count = 0;

  while (read_effective_line(fd, line, sizeof(line)) > 0) {
    char original_line[1024];
    strncpy(original_line, line, sizeof(original_line));
    original_line[sizeof(original_line) - 1] = '\0';

    char *saveptr = NULL;
    char *token = strtok_r(line, " \t\r\n", &saveptr);
    if (token == NULL)
      continue;

    if (strcmp(token, "DIM") == 0) {
      char *h = strtok_r(NULL, " \t\r\n", &saveptr);
      char *w = strtok_r(NULL, " \t\r\n", &saveptr);
      if (h != NULL && w != NULL) {
        board->height = atoi(h);
        board->width = atoi(w);
        board->board = calloc((size_t)board->width * (size_t)board->height,
                              sizeof(board_pos_t));
        if (board->board == NULL) {
          reset_board(board);
          close(fd);
          return -1;
        }
        for (int i = 0; i < board->width * board->height; i++) {
          board->board[i].content = ' ';
        }
      }
      continue;
    }

    if (strcmp(token, "TEMPO") == 0) {
      char *t = strtok_r(NULL, " \t\r\n", &saveptr);
      if (t != NULL) {
        board->tempo = atoi(t);
      }
      continue;
    }

    if (strcmp(token, "PAC") == 0) {
      char *pfile = strtok_r(NULL, " \t\r\n", &saveptr);
      if (board->n_pacmans == 0) {
        board->pacmans = calloc(1, sizeof(pacman_t));
        board->n_pacmans = 1;
      }
      if (board->pacmans != NULL) {
        memset(&board->pacmans[0], 0, sizeof(pacman_t));
        board->pacmans[0].alive = 1;
        board->pacmans[0].points = accumulated_points;
        board->pacmans[0].pos_x = -1;
        board->pacmans[0].pos_y = -1;
      }
      if (pfile != NULL) {
        strncpy(board->pacman_file, pfile, MAX_FILENAME - 1);
        board->pacman_file[MAX_FILENAME - 1] = '\0';
      }
      continue;
    }

    if (strcmp(token, "MON") == 0) {
      char *mfile = NULL;
      while ((mfile = strtok_r(NULL, " \t\r\n", &saveptr)) != NULL &&
             board->n_ghosts < MAX_GHOSTS) {
        board->ghosts = realloc(board->ghosts, (size_t)(board->n_ghosts + 1) *
                                                   sizeof(ghost_t));
        if (board->ghosts == NULL) {
          reset_board(board);
          close(fd);
          return -1;
        }
        memset(&board->ghosts[board->n_ghosts], 0, sizeof(ghost_t));
        board->ghosts[board->n_ghosts].pos_x = -1;
        board->ghosts[board->n_ghosts].pos_y = -1;
        strncpy(board->ghosts_files[board->n_ghosts], mfile, MAX_FILENAME - 1);
        board->ghosts_files[board->n_ghosts][MAX_FILENAME - 1] = '\0';
        board->n_ghosts++;
      }
      continue;
    }

    if (board->board != NULL && rows_read < board->height) {
      const char *row = original_line;
      int len = (int)strlen(row);
      for (int c = 0; c < board->width && c < len; c++) {
        char ch = row[c];
        int idx = rows_read * board->width + c;
        switch (ch) {
        case '#':
          // It's a comment char, do nothing or break
          break;
        case 'X':

          // Fall through to W case for board logic
        case 'W':
          board->board[idx].content = 'X';
          break;
        case '.':
        case 'o':

          board->board[idx].content = ' ';
          board->board[idx].has_dot = 1;
          break;
        case '@':

          board->board[idx].has_portal = 1;
          board->board[idx].content = ' ';
          break;
        case 'P':
          board->board[idx].content = ' ';
          map_pac_x = c;
          map_pac_y = rows_read;
          break;
        case 'M':
          board->board[idx].content = ' ';
          if (map_ghost_count < MAX_GHOSTS) {
            map_ghost_x[map_ghost_count] = c;
            map_ghost_y[map_ghost_count] = rows_read;
            map_ghost_count++;
          }
          break;
        default:
          board->board[idx].content = ' ';
          break;
        }
      }
      rows_read++;
    }
  }
  if (strcmp(filename, "Level_99.txt") == 0) {
    fprintf(stdout, "SECREET LEVEL FOUND");
  }

  close(fd);

  if (board->board == NULL || board->width == 0 || board->height == 0) {
    reset_board(board);
    return -1;
  }

  // Ensure at least one pacman exists even without a PAC line.
  if (board->n_pacmans == 0) {
    board->pacmans = calloc(1, sizeof(pacman_t));
    if (board->pacmans == NULL) {
      reset_board(board);
      return -1;
    }
    board->n_pacmans = 1;
    board->pacmans[0].alive = 1;
    board->pacmans[0].points = accumulated_points;
    board->pacmans[0].pos_x = -1;
    board->pacmans[0].pos_y = -1;
  }

  // Determine base directory for relative motion files
  char dir[2048];
  strncpy(dir, filename, sizeof(dir));
  dir[sizeof(dir) - 1] = '\0';
  char *slash = strrchr(dir, '/');
  if (slash)
    *slash = '\0';
  else
    strcpy(dir, ".");

  // Load Pacman behavior (may set starting POS)
  if (strlen(board->pacman_file) > 0) {
    char p_path[2048];
    int n = snprintf(p_path, sizeof(p_path), "%s/%s", dir, board->pacman_file);
    if (n > 0 && (size_t)n < sizeof(p_path)) {
      load_pacman_behavior(board, 0, p_path);
    } else {
      debug("Warning: Pacman file path too long\n");
    }
  }

  // Load Ghost behaviors (may set starting POS)
  for (int i = 0; i < board->n_ghosts; i++) {
    char m_path[2048];
    int n =
        snprintf(m_path, sizeof(m_path), "%s/%s", dir, board->ghosts_files[i]);
    if (n > 0 && (size_t)n < sizeof(m_path)) {
      load_ghost_behavior(board, i, m_path);
    } else {
      debug("Warning: Ghost %d file path too long\n", i);
    }
  }

  // Assign positions from map if behavior files did not override them
  pacman_t *main_pac = &board->pacmans[0];
  if (main_pac->pos_x < 0 || main_pac->pos_y < 0) {
    if (map_pac_x >= 0 && map_pac_y >= 0) {
      main_pac->pos_x = map_pac_x;
      main_pac->pos_y = map_pac_y;
    }
  }

  int ghost_map_idx = 0;
  for (int i = 0; i < board->n_ghosts; i++) {
    ghost_t *g = &board->ghosts[i];
    if (g->pos_x < 0 || g->pos_y < 0) {
      if (ghost_map_idx < map_ghost_count) {
        g->pos_x = map_ghost_x[ghost_map_idx];
        g->pos_y = map_ghost_y[ghost_map_idx];
        ghost_map_idx++;
      }
    }
  }

  // Clear any stale agent marks
  for (int i = 0; i < board->width * board->height; i++) {
    if (board->board[i].content == 'C' || board->board[i].content == 'M' ||
        board->board[i].content == 'P') {
      board->board[i].content = ' ';
    }
  }

  if (!is_playable_cell(board, main_pac->pos_x, main_pac->pos_y)) {
    int fx = 0, fy = 0;
    if (find_first_playable_cell(board, &fx, &fy)) {
      main_pac->pos_x = fx;
      main_pac->pos_y = fy;
    }
  }

  if (is_playable_cell(board, main_pac->pos_x, main_pac->pos_y)) {
    board->board[main_pac->pos_y * board->width + main_pac->pos_x].content =
        'C';
  }

  for (int i = 0; i < board->n_ghosts; i++) {
    ghost_t *g = &board->ghosts[i];
    if (!is_playable_cell(board, g->pos_x, g->pos_y)) {
      int gx = 0, gy = 0;
      if (find_first_playable_cell(board, &gx, &gy)) {
        g->pos_x = gx;
        g->pos_y = gy;
      }
    }

    if (is_playable_cell(board, g->pos_x, g->pos_y)) {
      board->board[g->pos_y * board->width + g->pos_x].content = 'M';
    }
  }

  snprintf(board->level_name, sizeof(board->level_name), "%s", filename);
  pthread_rwlock_init(&board->state_lock, NULL);
  board->lock_initialized = 1;

  int fd2 = open(filename, O_RDONLY);
  if (fd2 < 0)
    return -1;

  char line2[1024];
  char fileout[1024];

  snprintf(fileout, sizeof(fileout), "%s.out", filename);

  FILE *f2 = fopen(fileout, "w");
  if (f2) {
    while (read_comment_line(fd2, line2, sizeof(line2)) > 0) {
      fprintf(f2, "%s", line2);
    }
    fclose(f2);
  }
  close(fd2);

  return 0;
}

/**
 * @brief Unloads the level and frees memory.
 * @param board Pointer to the game board structure.
 */
void unload_level(board_t *board) { reset_board(board); }

/**
 * @brief Opens the debug file.
 * @param filename Name of the debug file.
 */
void open_debug_file(char *filename) {
  if (debug_fd != -1) {
    close(debug_fd);
    debug_fd = -1;
  }
  const char *env_path = getenv("PACMANIST_DEBUG");
  const char *path = NULL;
  if (env_path != NULL && env_path[0] != '\0') {
    path = env_path;
  } else if (filename != NULL) {
    path = filename;
  }
  if (path == NULL || path[0] == '\0') {
    debug_fd = -1; // Debug disabled
    return;
  }
  debug_fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
}

/**
 * @brief Closes the debug file.
 */
void close_debug_file() {
  if (debug_fd != -1) {
    close(debug_fd);
    debug_fd = -1;
  }
}

/**
 * @brief Writes data to the debug file.
 * @param format Format string (using printf style).
 * @param ... Additional arguments.
 */
void debug(const char *format, ...) {
  if (debug_fd == -1) {
    return;
  }
  va_list args;
  va_start(args, format);
  char buffer[4096];
  int len = vsnprintf(buffer, sizeof(buffer), format, args);
  va_end(args);
  if (len < 0) {
    return;
  }
  if (len > (int)sizeof(buffer)) {
    len = (int)sizeof(buffer);
  }
  write(debug_fd, buffer, (size_t)len);
}

/**
 * @brief Prints the board and its state to the debug file.
 * @param board Pointer to the game board structure.
 */
void print_board(board_t *board) {
  if (!board || !board->board) {
    debug("[%d] Board is empty or not initialized.\n", getpid());
    return;
  }

  // Large buffer to accumulate the whole output
  char buffer[8192];
  size_t offset = 0;

  offset += snprintf(buffer + offset, sizeof(buffer) - offset,
                     "=== [%d] LEVEL INFO ===\n"
                     "Dimensions: %d x %d\n"
                     "Tempo: %d\n"
                     "Pacman file: %s\n",
                     getpid(), board->height, board->width, board->tempo,
                     board->pacman_file);

  offset += snprintf(buffer + offset, sizeof(buffer) - offset,
                     "Monster files (%d):\n", board->n_ghosts);

  for (int i = 0; i < board->n_ghosts; i++) {
    offset += snprintf(buffer + offset, sizeof(buffer) - offset, "  - %s\n",
                       board->ghosts_files[i]);
  }

  offset +=
      snprintf(buffer + offset, sizeof(buffer) - offset, "\n=== BOARD ===\n");

  for (int y = 0; y < board->height; y++) {
    for (int x = 0; x < board->width; x++) {
      int idx = y * board->width + x;
      if (offset < sizeof(buffer) - 2) {
        buffer[offset++] = board->board[idx].content;
      }
    }
    if (offset < sizeof(buffer) - 2) {
      buffer[offset++] = '\n';
    }
  }

  offset += snprintf(buffer + offset, sizeof(buffer) - offset,
                     "==================\n");

  buffer[offset] = '\0';

  debug("%s", buffer);
}
