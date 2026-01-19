#ifndef PROTOCOL_H
#define PROTOCOL_H

#include <stdint.h>

// --- Operational Codes (OP_CODE) ---
#define OP_CONNECT 1
#define OP_DISCONNECT 2
#define OP_MOVE 3
#define OP_UPDATE 4

// --- Protocol Constants ---
#define PIPE_NAME_SIZE 40

// --- Message Structures ---

// OP_CODE = 1: Connection Request (Client -> Server)
// Size: 1 + 40 + 40 = 81 bytes
typedef struct {
  int8_t op_code;                  // OP_CONNECT
  char req_pipe[PIPE_NAME_SIZE];   // Pipe for sending requests to server
  char notif_pipe[PIPE_NAME_SIZE]; // Pipe for receiving updates from server
} connect_req_t;

// OP_CODE = 1 (Response): Connection Response (Server -> Client)
// Size: 1 + 1 = 2 bytes
typedef struct {
  int8_t op_code; // OP_CONNECT
  int8_t result;  // 0 for success, -1 for failure
} connect_resp_t;

// OP_CODE = 2: Disconnect Request (Client -> Server)
// Size: 1 byte
typedef struct {
  int8_t op_code; // OP_DISCONNECT
} disconnect_req_t;

// OP_CODE = 3: Move Request (Client -> Server)
// Size: 1 + 1 = 2 bytes
typedef struct {
  int8_t op_code; // OP_MOVE
  char key;       // 'w', 'a', 's', 'd'
} move_req_t;

// --- Constants ---
#define MAX_BOARD_SIZE 2400 // Example: 60x40
#define MAX_LEVEL_NAME 32

// Game states for display mode
#define GAME_STATE_PLAYING 0
#define GAME_STATE_WIN 1
#define GAME_STATE_GAME_OVER 2

// OP_CODE = 4: Game Update (Server -> Client)
typedef struct {
  int8_t op_code;    // OP_UPDATE
  int8_t game_state; // GAME_STATE_PLAYING, WIN, or GAME_OVER
  int16_t width;
  int16_t height;
  int16_t points;
  int16_t lives;
  char level_name[MAX_LEVEL_NAME];
  char board_data[MAX_BOARD_SIZE];
} game_state_msg_t;

#endif // PROTOCOL_H
