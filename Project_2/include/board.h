#ifndef BOARD_H
#define BOARD_H

#include <pthread.h>

/** @brief Maximum number of moves in a command sequence */
#define MAX_MOVES 20
/** @brief Maximum number of levels in a single game run */
#define MAX_LEVELS 20
/** @brief Maximum length for filenames */
#define MAX_FILENAME 256
/** @brief Maximum number of ghosts allowed on a board */
#define MAX_GHOSTS 25

/** @brief Game Control Codes */
#define CONTINUE_PLAY 0
#define NEXT_LEVEL 1
#define QUIT_GAME 2
#define LOAD_BACKUP 3
#define CREATE_BACKUP 4

/**
 * @brief Return codes for movement functions.
 */
typedef enum {
  REACHED_PORTAL = 1, /**< Pacman reached the level exit */
  VALID_MOVE = 0,     /**< Move was successful */
  INVALID_MOVE = -1,  /**< Move blocked (e.g., wall) */
  DEAD_PACMAN = -2,   /**< Pacman collided with a ghost */
} move_t;

/**
 * @brief Represents a single movement command or a sequence.
 */
typedef struct {
  char command;   /**< 'w', 'a', 's', 'd' or ' ' */
  int turns;      /**< Total number of turns to execute this command */
  int turns_left; /**< Remaining turns for this specific command */
} command_t;

/**
 * @brief State and attributes of a Pacman character.
 */
typedef struct {
  int pos_x, pos_y; /**< Current coordinates on the board matrix */
  int alive;        /**< Boolean: 1 if alive, 0 if dead */
  int points;       /**< Total score collected by this Pacman */
  int passo;        /**< Movement delay: waits (passo) frames between moves */
  command_t moves[MAX_MOVES]; /**< List of automatic moves from file */
  int current_move;    /**< Index of current move in automatic sequence */
  int n_moves;         /**< Number of automatic moves (0 if manual control) */
  int waiting;         /**< Flag if pacman is currently in a wait state */
  char next_user_move; /**< The next move buffered from client input */
} pacman_t;

/**
 * @brief State and attributes of a Ghost character.
 */
typedef struct {
  int pos_x, pos_y; /**< Current coordinates on the board matrix */
  int passo;        /**< Movement delay: waits (passo) frames between moves */
  command_t moves[MAX_MOVES]; /**< List of movement patterns from file */
  int n_moves;                /**< Total moves in the ghost's pattern */
  int current_move;           /**< Current index in the movement pattern */
  int waiting;                /**< Flag if ghost is currently in a wait state */
  int charged; /**< Potentially for power-ups (e.g. vulnerable ghosts) */
} ghost_t;

/**
 * @brief Data for a single cell on the game board.
 */
typedef struct {
  char content;   /**< Character representation ('P', 'M', 'W', ' ', '.') */
  int has_dot;    /**< 1 if cell contains a point dot */
  int has_portal; /**< 1 if cell is the level exit portal */
} board_pos_t;

/**
 * @brief Global state of a level.
 */
typedef struct {
  int width, height;     /**< Dimensions of the board matrix */
  board_pos_t *board;    /**< Pointer to row-major board array */
  int n_pacmans;         /**< Current number of Pacmans (Usually 1) */
  pacman_t *pacmans;     /**< Array of Pacman structures */
  int n_ghosts;          /**< Total number of ghosts currently on board */
  ghost_t *ghosts;       /**< Array of Ghost structures */
  char level_name[256];  /**< Filename of the current level */
  char pacman_file[256]; /**< Path to file describing Pacman's AI moves */
  char ghosts_files[MAX_GHOSTS][256]; /**< Paths to files for each ghost AI */
  int tempo;          /**< Base tick rate in milliseconds for the level */
  int level_finished; /**< Flag set to 1 when portal is reached */
  int shutdown;       /**< Flag to signal all threads for this board to exit */
  pthread_rwlock_t
      state_lock;       /**< Synchronization for multi-threaded board access */
  int lock_initialized; /**< Safety flag to track if lock is ready */
} board_t;

/**
 * @brief Makes the current thread sleep.
 * @param milliseconds The duration to wait.
 */
void sleep_ms(int milliseconds);

/**
 * @brief Processes a single movement step for Pacman.
 * @param board Pointer to the game board.
 * @param pacman_index Index of the Pacman moving.
 * @param command Pointer to the command to execute.
 * @return move_t Status of the move (VALID, INVALID, DEAD, etc).
 */
int move_pacman(board_t *board, int pacman_index, command_t *command);

/**
 * @brief Processes a single movement step for a Ghost.
 * @param board Pointer to the game board.
 * @param ghost_index Index of the Ghost moving.
 * @param command Pointer to the displacement command.
 * @return move_t Status of the move.
 */
int move_ghost(board_t *board, int ghost_index, command_t *command);

/**
 * @brief Logic for when Pacman dies (collision/traps).
 * @param board Pointer to the board.
 * @param pacman_index Index of the deceased.
 */
void kill_pacman(board_t *board, int pacman_index);

/**
 * @brief Dynamically allocates and adds a pacman to the board.
 * @param board Board pointer.
 * @param points Initial points to award.
 * @return 0 on success.
 */
int load_pacman(board_t *board, int points);

/**
 * @brief Dynamically adds a ghost to the board state.
 */
int load_ghost(board_t *board);

/**
 * @brief Parses a level file and initializes the board_t state.
 * @param board Pointer to populate.
 * @param filename Path to the .txt level file.
 * @param accumulated_points Points carried over from previous levels.
 * @return 0 on success.
 */
int load_level(board_t *board, const char *filename, int accumulated_points);

/**
 * @brief Frees memory and cleans up resources for the level.
 */
void unload_level(board_t *board);

/* --- Debugging Utilities --- */

/** @brief Opens a file for writing debug logs. */
void open_debug_file(char *filename);
/** @brief Closes the debug log file. */
void close_debug_file();
/** @brief Writes a formatted string to the debug log. */
void debug(const char *format, ...);
/** @brief Dumps the current board matrix to the debug log. */
void print_board(board_t *board);

#endif
