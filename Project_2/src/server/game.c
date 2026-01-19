#include "../../include/game.h"
#include "../../include/board.h"
#include "../../include/protocol.h"
#include <dirent.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>


/**
 * @brief Argument structure passed to ghost and pacman threads.
 */
typedef struct {
  board_t *board;  /**< Shared game board */
  int ghost_index; /**< Index of the ghost (ignored for pacman) */
  int notif_fd;    /**< Open file descriptor for client updates */
  int req_fd;      /**< Pre-opened request pipe fd (for input listener) */
} thread_arg_t;

/**
 * @brief Thread that listens for movement requests from a specific client.
 *
 * Reads move_req_t structures from the pre-opened request file descriptor.
 * Updates the pacman's next_user_move buffer when a valid move is received.
 *
 * @param arg Pointer to thread_arg_t containing board and req_fd.
 * @return void* Always NULL.
 */
void *input_listener_thread(void *arg) {
  thread_arg_t *i_arg = (thread_arg_t *)arg;
  board_t *board = i_arg->board;
  int fd = i_arg->req_fd; // Use pre-opened fd
  free(i_arg);

  if (fd == -1)
    return NULL;

  move_req_t move;
  while (true) {
    pthread_rwlock_rdlock(&board->state_lock);
    if (board->shutdown) {
      pthread_rwlock_unlock(&board->state_lock);
      break;
    }
    pthread_rwlock_unlock(&board->state_lock);

    ssize_t n = read(fd, &move, sizeof(move_req_t));

    // Handle read errors and EOF
    if (n <= 0) {
      if (n == 0) {
        // Client closed pipe (EOF) - Shutdown game threads
        pthread_rwlock_wrlock(&board->state_lock);
        board->shutdown = 1;
        pthread_rwlock_unlock(&board->state_lock);
        break;
      }
      // Read error - continue trying
      continue;
    }

    // Handle partial reads (malicious client sending incomplete data)
    if (n < (ssize_t)sizeof(move_req_t)) {
      fprintf(stderr,
              "[Listener] Warning: Partial message received (%zd bytes)\n", n);
      continue;
    }

    // Handle different opcodes
    switch (move.op_code) {
    case OP_MOVE:
      pthread_rwlock_wrlock(&board->state_lock);
      board->pacmans[0].next_user_move = move.key;
      pthread_rwlock_unlock(&board->state_lock);
      break;

    case OP_DISCONNECT:
      // Client requested clean disconnect
      pthread_rwlock_wrlock(&board->state_lock);
      board->shutdown = 1;
      pthread_rwlock_unlock(&board->state_lock);
      return NULL;

    default:
      // Unknown opcode - log but don't crash
      fprintf(stderr, "[Listener] Warning: Unknown opcode %d ignored\n",
              move.op_code);
      break;
    }
  }

  // Don't close fd here - owned by worker_task for multi-level reuse
  return NULL;
}

/**
 * @brief Sends a binary game state update to the connected client.
 *
 * Serializes the current board state into a game_state_msg_t structure
 * and writes it to the client's notification pipe.
 *
 * @param board Pointer to the game board.
 * @param notif_fd File descriptor of the client's notification pipe.
 */
void server_send_update(board_t *board, int notif_fd) {
  if (notif_fd == -1)
    return;

  game_state_msg_t msg;
  msg.op_code = OP_UPDATE;
  msg.width = board->width;
  msg.height = board->height;
  msg.points = board->pacmans[0].points;
  msg.lives = board->pacmans[0].alive ? 1 : 0;

  // Set game state
  if (board->level_finished) {
    msg.game_state = GAME_STATE_WIN;
  } else if (!board->pacmans[0].alive) {
    msg.game_state = GAME_STATE_GAME_OVER;
  } else {
    msg.game_state = GAME_STATE_PLAYING;
  }

  // Copy level name
  strncpy(msg.level_name, board->level_name, MAX_LEVEL_NAME - 1);
  msg.level_name[MAX_LEVEL_NAME - 1] = '\0';

  int size = board->width * board->height;
  if (size > MAX_BOARD_SIZE)
    size = MAX_BOARD_SIZE;
  // what are we doing here?
  /**
   * @brief Sends a binary game state update to the connected client.
   *
   * Serializes the current board state into a game_state_msg_t structure
   * and writes it to the client's notification pipe.
   *
   * @param board Pointer to the game board.
   * @param notif_fd File descriptor of the client's notification pipe.
   */
  for (int i = 0; i < size; i++) {
    char visual = board->board[i].content;

    if (visual == 'X' || visual == 'W') {
      visual = '#';
    } else if (visual == ' ' || visual == '\0') {
      if (board->board[i].has_portal) {
        visual = '@';
      } else if (board->board[i].has_dot) {
        visual = '.';
      } else {
        visual = ' ';
      }
    }
    msg.board_data[i] = visual;
  }

  write(notif_fd, &msg, sizeof(game_state_msg_t));
}

/**
 * @brief Dedicated thread for sending periodic updates to the client.
 *
 * Wakes up every tempo milliseconds and sends the current board state.
 * This centralizes updates instead of having each entity thread send updates.
 *
 * @param arg Pointer to thread_arg_t containing board and notif_fd.
 * @return void* Always NULL.
 */
void *update_thread(void *arg) {
  thread_arg_t *u_arg = (thread_arg_t *)arg;
  board_t *board = u_arg->board;
  int notif_fd = u_arg->notif_fd;
  free(u_arg);

  pthread_rwlock_rdlock(&board->state_lock);
  server_send_update(board, notif_fd);
  pthread_rwlock_unlock(&board->state_lock);

  while (true) {
    sleep_ms(board->tempo);

    pthread_rwlock_rdlock(&board->state_lock);
    if (board->shutdown) {
      pthread_rwlock_unlock(&board->state_lock);
      break;
    }
    server_send_update(board, notif_fd);
    pthread_rwlock_unlock(&board->state_lock);
  }
  return NULL;
}

/**
 * @brief Main logic for the Pacman thread.
 *
 * Handles Pacman's movement, interaction with the board (eating dots, portals),
 * and checks for win/loss conditions. Sends updates to the client after moves.
 *
 * @param arg Pointer to thread_arg_t containing board and notif_fd.
 * @return void* Pointer to an integer containing the exit status (NEXT_LEVEL,
 * LOAD_BACKUP, etc.).
 */
void *pacman_thread(void *arg) {
  thread_arg_t *p_arg = (thread_arg_t *)arg;
  board_t *board = p_arg->board;

  pacman_t *pacman = &board->pacmans[0];
  int *retval = malloc(sizeof(int));
  *retval = QUIT_GAME;

  free(p_arg);

  while (true) {
    if (!pacman->alive) {
      *retval = LOAD_BACKUP;
      return (void *)retval;
    }
    if (pacman->points >= 20) {
      sleep_ms(board->tempo * (1 + pacman->passo+1));
    } else {
      sleep_ms(board->tempo * (1 + pacman->passo));
    }

    command_t c = {' ', 0, 0};
    command_t *play = &c;

    pthread_rwlock_wrlock(&board->state_lock);
    if (pacman->next_user_move != ' ') {
      c.command = pacman->next_user_move;
      pacman->next_user_move = ' ';
      play = &c;
    } else if (pacman->n_moves > 0) {
      play = &pacman->moves[pacman->current_move % pacman->n_moves];
    }
    pthread_rwlock_unlock(&board->state_lock);

    int result = move_pacman(board, 0, play);
    // Updates now handled by dedicated update_thread

    if (result == REACHED_PORTAL) {
      *retval = NEXT_LEVEL;
      break;
    }
    if (result == DEAD_PACMAN) {
      *retval = LOAD_BACKUP;
      break;
    }

    pthread_rwlock_rdlock(&board->state_lock);
    if (board->shutdown) {
      pthread_rwlock_unlock(&board->state_lock);
      break;
    }
    pthread_rwlock_unlock(&board->state_lock);
  }
  return (void *)retval;
}

/**
 * @brief Main logic for a Ghost thread.
 *
 * Handles a single Ghost's movement and interactions. Ghosts send updates
 * to the client after each move to ensure smooth visuals.
 *
 * @param arg Pointer to thread_arg_t containing board, ghost index, and
 * notif_fd.
 * @return void* Always NULL.
 */
void *ghost_thread(void *arg) {
  thread_arg_t *ghost_arg = (thread_arg_t *)arg;
  board_t *board = ghost_arg->board;
  int ghost_ind = ghost_arg->ghost_index;
  free(ghost_arg);

  ghost_t *ghost = &board->ghosts[ghost_ind];

  while (true) {
    sleep_ms(board->tempo * (1 + ghost->passo));

    pthread_rwlock_rdlock(&board->state_lock);
    if (board->shutdown) {
      pthread_rwlock_unlock(&board->state_lock);
      pthread_exit(NULL);
    }
    pthread_rwlock_unlock(&board->state_lock);

    if (ghost->n_moves > 0) {
      move_ghost(board, ghost_ind,
                 &ghost->moves[ghost->current_move % ghost->n_moves]);
    } else {
      command_t random_move = {'R', 1, 1};
      move_ghost(board, ghost_ind, &random_move);
    }
  }
  return NULL;
}

/**
 * @brief Entry point for the game logic of a single level.
 *
 * Spawns threads for Pacman, Ghosts, and the Input Listener. Waits for the
 * Pacman thread to finish (win/loss) before cleaning up all threads.
 *
 * @param game_board Pointer to the initialized game board.
 * @param notif_fd Open file descriptor for client updates.
 * @param req_fd Open file descriptor for reading client requests.
 * @return int Exit status of the level (e.g., NEXT_LEVEL, QUIT_GAME).
 */
int run_game_logic(board_t *game_board, int notif_fd, int req_fd) {
  pthread_t pacman_tid, listener_tid, update_tid;
  pthread_t *ghost_tids = malloc(game_board->n_ghosts * sizeof(pthread_t));

  game_board->shutdown = 0;

  // Create Update Thread (dedicated for sending periodic state updates)
  thread_arg_t *update_arg = malloc(sizeof(thread_arg_t));
  update_arg->board = game_board;
  update_arg->notif_fd = notif_fd;
  update_arg->req_fd = -1;
  pthread_create(&update_tid, NULL, update_thread, (void *)update_arg);

  // Create Pacman Thread
  thread_arg_t *pac_arg = malloc(sizeof(thread_arg_t));
  pac_arg->board = game_board;
  pac_arg->notif_fd = notif_fd;
  pac_arg->req_fd = -1; // Pacman doesn't use req_fd
  pthread_create(&pacman_tid, NULL, pacman_thread, (void *)pac_arg);

  // Create Listener Thread
  thread_arg_t *list_arg = malloc(sizeof(thread_arg_t));
  list_arg->board = game_board;
  list_arg->req_fd = req_fd; // Pass pre-opened fd
  pthread_create(&listener_tid, NULL, input_listener_thread, (void *)list_arg);

  // Create Ghost Threads
  for (int i = 0; i < game_board->n_ghosts; i++) {
    thread_arg_t *ghost_arg = malloc(sizeof(thread_arg_t));
    ghost_arg->board = game_board;
    ghost_arg->ghost_index = i;
    ghost_arg->notif_fd = notif_fd;
    pthread_create(&ghost_tids[i], NULL, ghost_thread, (void *)ghost_arg);
  }

  // Blocking wait for the player's thread
  int *retval;
  pthread_join(pacman_tid, (void **)&retval);

  // Signalling ghosts and listener to stop
  pthread_rwlock_wrlock(&game_board->state_lock);
  game_board->shutdown = 1;
  pthread_rwlock_unlock(&game_board->state_lock);

  pthread_join(listener_tid, NULL);
  pthread_join(update_tid, NULL);

  for (int i = 0; i < game_board->n_ghosts; i++) {
    pthread_join(ghost_tids[i], NULL);
  }

  free(ghost_tids);
  int result = *retval;
  free(retval); 
  return result;
}
