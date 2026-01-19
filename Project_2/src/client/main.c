/**
 * @file main.c
 * @brief PacmanIST Client - Connects to server and provides game UI.
 */

#include "../../include/display.h"
#include "../../include/protocol.h"
#include <fcntl.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

volatile int client_running = 1;

typedef struct {
  char req_pipe_path[PIPE_NAME_SIZE];
  char moves_file[256];
} client_thread_arg_t;

/**
 * @brief Input thread function.
 *
 * Reads player input from keyboard (using get_input) or from a file if
 * provided. Sends valid move commands (W, A, S, D) to the server via the
 * request pipe, and sends disconnect request on quit (Q) or EOF.
 *
 * @param arg Pointer to client_thread_arg_t containing pipe path and moves
 * file.
 * @return void* Always returns NULL.
 */
void *client_input_thread(void *arg) {
  client_thread_arg_t *c_arg = (client_thread_arg_t *)arg;
  char pipe_path[PIPE_NAME_SIZE];
  char moves_file[256];
  strncpy(pipe_path, c_arg->req_pipe_path, PIPE_NAME_SIZE);
  strncpy(moves_file, c_arg->moves_file, 256);
  free(c_arg);

  int fd = open(pipe_path, O_WRONLY);
  if (fd == -1) {
    perror("Failed to open request pipe");
    return NULL;
  }

  FILE *move_fp = NULL;
  if (moves_file[0] != '\0') {
    move_fp = fopen(moves_file, "r");
  }

  while (client_running) {
    char ch = '\0';

    if (move_fp) {
      char line[16];
      if (fgets(line, sizeof(line), move_fp)) {
        ch = line[0];
        sleep_ms(100);
      } else {
        disconnect_req_t disc = {.op_code = OP_DISCONNECT};
        write(fd, &disc, sizeof(disc));
        client_running = 0;
        break;
      }
    } else {
      ch = get_input();
      if (ch == '\0') {
        sleep_ms(10);
        continue;
      }
    }

    if (ch == 'q' || ch == 'Q') {
      disconnect_req_t disc = {.op_code = OP_DISCONNECT};
      write(fd, &disc, sizeof(disc));
      client_running = 0;
      break;
    }

    if (ch >= 'a' && ch <= 'z') {
      ch = ch - 'a' + 'A';
    }

    if (ch == 'W' || ch == 'A' || ch == 'S' || ch == 'D') {
      move_req_t req = {.op_code = OP_MOVE, .key = ch};
      write(fd, &req, sizeof(move_req_t));
    }
  }

  if (move_fp)
    fclose(move_fp);
  close(fd);
  return NULL;
}

/**
 * @brief Main entry point for the PacmanIST client.
 *
 * Parses command-line arguments, creates private FIFOs for communication,
 * connects to the server via the registration FIFO, initializes the ncurses
 * UI, spawns the input thread, and enters the main loop to receive and
 * render game state updates from the server.
 *
 * @param argc Number of command-line arguments (expected: 3 or 4).
 * @param argv Array of arguments: id, registration_fifo, [input_file].
 * @return int Exit status code (0 on success).
 */
int main(int argc, char *argv[]) {
  if (argc < 3 || argc > 4) {
    fprintf(stderr, "Usage: %s <id> <registration_fifo> [input_file]\n",
            argv[0]);
    return 1;
  }

  char *client_id = argv[1];
  char *server_fifo = argv[2];
  char *moves_file = (argc >= 4) ? argv[3] : NULL;

  /* Create client FIFOs */
  char req_pipe_path[PIPE_NAME_SIZE];
  char notif_pipe_path[PIPE_NAME_SIZE];
  snprintf(req_pipe_path, PIPE_NAME_SIZE, "/tmp/pacman_req_%s", client_id);
  snprintf(notif_pipe_path, PIPE_NAME_SIZE, "/tmp/pacman_notif_%s", client_id);

  unlink(req_pipe_path);
  unlink(notif_pipe_path);

  if (mkfifo(req_pipe_path, 0666) == -1) {
    perror("Failed to create request FIFO");
    return 1;
  }
  if (mkfifo(notif_pipe_path, 0666) == -1) {
    perror("Failed to create notification FIFO");
    unlink(req_pipe_path);
    return 1;
  }

  /* Connect to server */
  int server_fd = open(server_fifo, O_WRONLY);
  if (server_fd == -1) {
    perror("Failed to connect to server");
    unlink(req_pipe_path);
    unlink(notif_pipe_path);
    return 1;
  }

  connect_req_t req = {.op_code = OP_CONNECT};
  strncpy(req.req_pipe, req_pipe_path, PIPE_NAME_SIZE);
  strncpy(req.notif_pipe, notif_pipe_path, PIPE_NAME_SIZE);

  if (write(server_fd, &req, sizeof(connect_req_t)) == -1) {
    perror("Failed to send connection request");
    close(server_fd);
    unlink(req_pipe_path);
    unlink(notif_pipe_path);
    return 1;
  }

  /* Wait for server response */
  int notif_fd = open(notif_pipe_path, O_RDONLY);
  if (notif_fd == -1) {
    perror("Failed to open notification FIFO");
    close(server_fd);
    unlink(req_pipe_path);
    unlink(notif_pipe_path);
    return 1;
  }

  connect_resp_t resp;
  if (read(notif_fd, &resp, sizeof(connect_resp_t)) != sizeof(connect_resp_t)) {
    perror("Failed to read connection response");
    close(notif_fd);
    close(server_fd);
    unlink(req_pipe_path);
    unlink(notif_pipe_path);
    return 1;
  }

  if (resp.result == -1) {
    fprintf(stderr, "Server rejected connection.\n");
    close(notif_fd);
    close(server_fd);
    unlink(req_pipe_path);
    unlink(notif_pipe_path);
    return 1;
  }

  /* Initialize UI and input thread */
  terminal_init();

  pthread_t input_tid;
  client_thread_arg_t *c_arg = malloc(sizeof(client_thread_arg_t));
  strncpy(c_arg->req_pipe_path, req_pipe_path, PIPE_NAME_SIZE);
  c_arg->moves_file[0] = '\0';
  if (moves_file)
    strncpy(c_arg->moves_file, moves_file, 256);
  pthread_create(&input_tid, NULL, client_input_thread, c_arg);

  /* Game loop - receive and render updates */
  game_state_msg_t msg;
  while (client_running) {
    ssize_t bytes_read = read(notif_fd, &msg, sizeof(game_state_msg_t));
    if (bytes_read <= 0) {
      client_running = 0;
      break;
    }

    if (msg.op_code == OP_UPDATE) {
      board_t temp_board;
      temp_board.width = msg.width;
      temp_board.height = msg.height;
      int size = msg.width * msg.height;
      temp_board.board = calloc(size, sizeof(board_pos_t));

      for (int i = 0; i < size; i++) {
        char ch = msg.board_data[i];
        temp_board.board[i].content = ch;
        if (ch == '.') {
          temp_board.board[i].has_dot = 1;
          temp_board.board[i].content = ' ';
        } else if (ch == '@') {
          temp_board.board[i].has_portal = 1;
          temp_board.board[i].content = ' ';
        }
      }

      pacman_t p_dummy = {.points = msg.points, .alive = msg.lives > 0};
      temp_board.pacmans = &p_dummy;
      temp_board.n_pacmans = 1;
      strncpy(temp_board.level_name, msg.level_name, MAX_LEVEL_NAME - 1);
      temp_board.level_name[MAX_LEVEL_NAME - 1] = '\0';

      int display_mode = DRAW_MENU;
      if (msg.game_state == GAME_STATE_WIN)
        display_mode = DRAW_WIN;
      else if (msg.game_state == GAME_STATE_GAME_OVER)
        display_mode = DRAW_GAME_OVER;

      draw_board(&temp_board, display_mode);
      refresh_screen();
      free(temp_board.board);
    }
  }

  /* Cleanup */
  client_running = 0;
  pthread_join(input_tid, NULL);
  terminal_cleanup();

  close(server_fd);
  close(notif_fd);
  unlink(req_pipe_path);
  unlink(notif_pipe_path);

  return 0;
}
