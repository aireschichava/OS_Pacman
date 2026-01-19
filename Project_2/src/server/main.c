/**
 * @file main.c
 * @brief PacmanIST Server - Multi-client game server with Producer-Consumer
 * @author Aires Chichava
 * @author Eric Muthami
 */

#include "../../include/board.h"
#include "../../include/game.h"
#include "../../include/protocol.h"
#include <dirent.h>
#include <fcntl.h>
#include <pthread.h>
#include <semaphore.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

/* Global configuration */
char *global_fifo_name = NULL;
char *global_levels_dir = NULL;

/* Producer-Consumer buffer */
typedef struct {
  char req_pipe[PIPE_NAME_SIZE];
  char notif_pipe[PIPE_NAME_SIZE];
} game_session_t;

game_session_t *session_buffer = NULL;
int buffer_size = 0;
int buffer_in = 0;
int buffer_out = 0;

/* Synchronization primitives */
pthread_mutex_t buffer_mutex = PTHREAD_MUTEX_INITIALIZER;
sem_t sem_empty;
sem_t sem_full;

/* Scoreboard for SIGUSR1 logging */
#define MAX_SCOREBOARD 100

typedef struct {
  int client_id;
  int score;
  int active;
} score_entry_t;

score_entry_t scoreboard[MAX_SCOREBOARD];
pthread_mutex_t scoreboard_mutex = PTHREAD_MUTEX_INITIALIZER;
int next_client_id = 1;

/**
 * @brief Comparator function for qsort to sort scores in descending order.
 *
 * @param a Pointer to the first score_entry_t element.
 * @param b Pointer to the second score_entry_t element.
 * @return int Positive if b > a, negative if a > b, 0 if equal.
 */
static int compare_scores(const void *a, const void *b) {
  const score_entry_t *ea = (const score_entry_t *)a;
  const score_entry_t *eb = (const score_entry_t *)b;
  return eb->score - ea->score;
}

/**
 * @brief Sorts level filenames alphabetically using bubble sort.
 *
 * @param files Matrix of filenames to sort.
 * @param count Number of files in the matrix.
 */
static void sort_level_files(char files[][512], int count) {
  for (int i = 0; i < count - 1; i++) {
    for (int j = i + 1; j < count; j++) {
      if (strcmp(files[i], files[j]) > 0) {
        char temp[512];
        strncpy(temp, files[i], 512);
        strncpy(files[i], files[j], 512);
        strncpy(files[j], temp, 512);
      }
    }
  }
}

/**
 * @brief Signal handler for SIGINT and SIGTERM.
 *
 * Performs graceful server shutdown: unlinks the registration FIFO,
 * destroys synchronization primitives, and exits.
 *
 * @param sig Signal number (SIGINT or SIGTERM).
 */
void handle_cleanup(int sig) {
  (void)sig;
  if (global_fifo_name != NULL) {
    unlink(global_fifo_name);
  }
  sem_destroy(&sem_empty);
  sem_destroy(&sem_full);
  pthread_mutex_destroy(&buffer_mutex);
  printf("\nServer shutdown complete.\n");
  exit(EXIT_SUCCESS);
}

/**
 * @brief Signal handler for SIGUSR1.
 *
 * Sorts the scoreboard and writes the top 5 active/completed client scores
 * to score_log.txt. Thread-safe via scoreboard_mutex.
 *
 * @param sig Signal number (unused, always SIGUSR1).
 */
void handle_sigusr1(int sig) {
  (void)sig;
  pthread_mutex_lock(&scoreboard_mutex);

  score_entry_t sorted[MAX_SCOREBOARD];
  memcpy(sorted, scoreboard, sizeof(scoreboard));
  qsort(sorted, MAX_SCOREBOARD, sizeof(score_entry_t), compare_scores);

  FILE *f = fopen("score_log.txt", "w");
  if (f) {
    fprintf(f, "=== TOP 5 SCORES ===\n");
    int count = 0;
    for (int i = 0; i < MAX_SCOREBOARD && count < 5; i++) {
      if (sorted[i].score > 0 || sorted[i].active) {
        fprintf(f, "%d. Client %d: %d points%s\n", count + 1,
                sorted[i].client_id, sorted[i].score,
                sorted[i].active ? " (playing)" : "");
        count++;
      }
    }
    if (count == 0) {
      fprintf(f, "No scores recorded yet.\n");
    }
    fclose(f);
  }

  pthread_mutex_unlock(&scoreboard_mutex);
}

/**
 * @brief Worker thread function (Consumer in Producer-Consumer pattern).
 *
 * Waits for game sessions in the shared buffer, retrieves client pipe paths,
 * loads levels, runs game logic, and manages the client scoreboard entry.
 * Blocks SIGUSR1 to ensure only the main thread handles it.
 *
 * @param arg Pointer to an integer containing the worker thread ID.
 * @return void* Always returns NULL.
 */
void *worker_task(void *arg) {
  int thread_id = *(int *)arg;
  free(arg);

  /* Block SIGUSR1 - only main thread handles it */
  sigset_t set;
  sigemptyset(&set);
  sigaddset(&set, SIGUSR1);
  pthread_sigmask(SIG_BLOCK, &set, NULL);

  while (1) {
    /* Wait for session (consumer) */
    sem_wait(&sem_full); // if this is empty we wait(don't run any code bellow)
    pthread_mutex_lock(&buffer_mutex);
    game_session_t session = session_buffer[buffer_out];
    buffer_out = (buffer_out + 1) % buffer_size;
    pthread_mutex_unlock(&buffer_mutex);
    sem_post(&sem_empty);

    /* Load level files */
    char level_files[32][512];
    int level_count = 0;

    DIR *d = opendir(global_levels_dir);
    if (!d) {
      fprintf(stderr, "Worker %d: Cannot open levels directory\n", thread_id);
      continue;
    }

    struct dirent *dir;
    while ((dir = readdir(d)) != NULL && level_count < 32) {
      if (strstr(dir->d_name, ".lvl") || strstr(dir->d_name, ".txt")) {
        snprintf(level_files[level_count], sizeof(level_files[0]), "%s/%s",
                 global_levels_dir, dir->d_name);
        level_count++;
      }
    }
    closedir(d);

    if (level_count == 0) {
      fprintf(stderr, "Worker %d: No level files found\n", thread_id);
      continue;
    }

    /* Sort levels alphabetically */
    sort_level_files(level_files, level_count);

    /* Open client pipes */
    int notif_fd = open(session.notif_pipe, O_WRONLY);
    if (notif_fd == -1) {
      fprintf(stderr, "Worker %d: Failed to open notification pipe\n",
              thread_id);
      continue;
    }

    int req_fd = open(session.req_pipe, O_RDONLY);
    if (req_fd == -1) {
      fprintf(stderr, "Worker %d: Failed to open request pipe\n", thread_id);
      close(notif_fd);
      continue;
    }

    /* Register in scoreboard */
    int my_client_id = 0;
    int my_scoreboard_idx = -1;
    pthread_mutex_lock(&scoreboard_mutex);
    my_client_id = next_client_id++;
    for (int i = 0; i < MAX_SCOREBOARD; i++) {
      if (!scoreboard[i].active) {
        scoreboard[i].client_id = my_client_id;
        scoreboard[i].score = 0;
        scoreboard[i].active = 1;
        my_scoreboard_idx = i;
        break;
      }
    }
    pthread_mutex_unlock(&scoreboard_mutex);

    /* Run game levels */
    int accumulated_points = 0;
    int current_level = 0;
    int game_result = NEXT_LEVEL;

    while (current_level < level_count && game_result == NEXT_LEVEL) {
      board_t board;
      memset(&board, 0, sizeof(board));

      if (load_level(&board, level_files[current_level], accumulated_points) !=
          0) {
        fprintf(stderr, "Worker %d: Failed to load level\n", thread_id);
        break;
      }

      game_result = run_game_logic(&board, notif_fd, req_fd);

      if (board.n_pacmans > 0) {
        accumulated_points = board.pacmans[0].points;
        if (my_scoreboard_idx >= 0) {
          pthread_mutex_lock(&scoreboard_mutex);
          scoreboard[my_scoreboard_idx].score = accumulated_points;
          pthread_mutex_unlock(&scoreboard_mutex);
        }
      }

      unload_level(&board);
      current_level++;
    }

    close(notif_fd);
    close(req_fd);

    /* Finalize scoreboard entry */
    if (my_scoreboard_idx >= 0) {
      pthread_mutex_lock(&scoreboard_mutex);
      scoreboard[my_scoreboard_idx].score = accumulated_points;
      scoreboard[my_scoreboard_idx].active = 0;
      pthread_mutex_unlock(&scoreboard_mutex);
    }
  }
  return NULL;
}

/**
 * @brief Creates the worker thread pool.
 *
 * Spawns max_games worker threads, each running worker_task.
 * Workers run indefinitely, so thread handles are freed immediately.
 *
 * @param max_games Number of worker threads to create.
 */
void create_threads(int max_games) {
  pthread_t *workers = malloc(max_games * sizeof(pthread_t));
  for (int i = 0; i < max_games; i++) {
    int *arg = malloc(sizeof(int));
    *arg = i;
    if (pthread_create(&workers[i], NULL, worker_task, arg) != 0) {
      perror("Failed to create worker thread");
      exit(EXIT_FAILURE);
    }
  }
  free(workers);
}

/**
 * @brief Main entry point for the PacmanIST server.
 *
 * Parses command-line arguments, initializes the Producer-Consumer buffer
 * and synchronization primitives, sets up signal handlers, creates the
 * registration FIFO, spawns worker threads, and enters the main loop to
 * accept client connections (Producer role).
 *
 * @param argc Number of command-line arguments (expected: 4).
 * @param argv Array of arguments: levels_dir, max_games, fifo_name.
 * @return int Exit status code (0 on success).
 */
int main(int argc, char *argv[]) {
  if (argc != 4) {
    fprintf(stderr, "Usage: %s <levels_dir> <max_games> <fifo_name>\n",
            argv[0]);
    exit(EXIT_FAILURE);
  }

  global_levels_dir = argv[1];
  int max_games = atoi(argv[2]);
  global_fifo_name = argv[3];

  buffer_size = max_games;
  session_buffer = calloc((size_t)buffer_size, sizeof(game_session_t));
  if (session_buffer == NULL) {
    perror("Failed to allocate session buffer");
    exit(EXIT_FAILURE);
  }

  if (sem_init(&sem_empty, 0, (unsigned int)buffer_size) != 0 ||
      sem_init(&sem_full, 0, 0) != 0) {
    perror("Failed to init semaphores");
    exit(EXIT_FAILURE);
  }

  signal(SIGPIPE, SIG_IGN);
  signal(SIGINT, handle_cleanup);
  signal(SIGTERM, handle_cleanup);
  signal(SIGUSR1, handle_sigusr1);

  unlink(global_fifo_name);
  if (mkfifo(global_fifo_name, 0666) == -1) {
    perror("Failed to create registration FIFO");
    exit(EXIT_FAILURE);
  }

  printf("PacmanIST Server started (max %d games) on %s\n", max_games,
         global_fifo_name);

  create_threads(max_games);

  int fifo_fd = open(global_fifo_name, O_RDWR);
  if (fifo_fd == -1) {
    perror("Failed to open FIFO");
    unlink(global_fifo_name);
    exit(EXIT_FAILURE);
  }

  while (1) {
    connect_req_t req;
    ssize_t bytes_read = read(fifo_fd, &req, sizeof(connect_req_t));

    if (bytes_read == 0)
      break;
    if (bytes_read == -1) {
      perror("Read error");
      break;
    }
    if (bytes_read != sizeof(connect_req_t))
      continue;

    if (req.op_code == OP_CONNECT) {
      int client_fd = open(req.notif_pipe, O_WRONLY);
      if (client_fd == -1) {
        perror("Failed to open client pipe");
        continue;
      }

      connect_resp_t resp = {.op_code = OP_CONNECT, .result = 0};
      write(client_fd, &resp, sizeof(connect_resp_t));
      close(client_fd);

      sem_wait(&sem_empty);
      pthread_mutex_lock(&buffer_mutex);
      strncpy(session_buffer[buffer_in].req_pipe, req.req_pipe, PIPE_NAME_SIZE);
      strncpy(session_buffer[buffer_in].notif_pipe, req.notif_pipe,
              PIPE_NAME_SIZE);
      buffer_in = (buffer_in + 1) % buffer_size;
      pthread_mutex_unlock(&buffer_mutex);
      sem_post(&sem_full);
    }
  }

  close(fifo_fd);
  unlink(global_fifo_name);
  return 0;
}
