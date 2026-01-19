#include "../../include/display.h"
#include <ncurses.h>

/**
 * @brief Initializes the ncurses terminal settings.
 */
// Define color pairs
#define COLOR_PACMAN 1
#define COLOR_GHOST 2
#define COLOR_WALL 3
#define COLOR_DOT 4
#define COLOR_UI 5
#define COLOR_PORTAL 6

/**
 * @brief Initializes the ncurses terminal settings.
 */
void terminal_init(void) {
  // ncurses setup so the game can refresh the screen freely.
  initscr();
  cbreak();
  noecho();
  keypad(stdscr, TRUE);
  curs_set(0);
  nodelay(stdscr, TRUE);

  if (has_colors()) {
    start_color();
    init_pair(COLOR_PACMAN, COLOR_YELLOW, COLOR_BLACK);
    init_pair(COLOR_GHOST, COLOR_RED, COLOR_BLACK);
    init_pair(COLOR_WALL, COLOR_BLUE, COLOR_BLACK);
    init_pair(COLOR_DOT, COLOR_WHITE, COLOR_BLACK);
    init_pair(COLOR_UI, COLOR_GREEN, COLOR_BLACK);
    init_pair(COLOR_PORTAL, COLOR_MAGENTA, COLOR_BLACK);
  }
}

/**
 * @brief Restores the terminal settings and cleans up ncurses.
 */
void terminal_cleanup(void) {
  // Leave the terminal as we found it for the shell.
  endwin();
}

/**
 * @brief Refreshes the ncurses screen buffer.
 */
void refresh_screen(void) { refresh(); }

/**
 * @brief Gets a character input from the user (non-blocking).
 * @return The character input, or '\0' if no input.
 */
char get_input(void) {
  // Non-blocking read prevents the render loop from stalling.
  int ch = getch();
  if (ch == ERR) {
    return '\0';
  }
  return (char)ch;
}

/**
 * @brief Draws the board content to the screen.
 * @param board Pointer to the game board structure.
 * @param mode The mode to draw (Menu, Game, Win, Game Over).
 */
void draw_board(board_t *board, int mode) {
  if (board == NULL || board->board == NULL) {
    return;
  }

  // Note: Client uses temp board without lock - no locking needed

  erase(); // Use erase() instead of clear() to reduce flickering

  // Draw Header
  attron(COLOR_PAIR(COLOR_UI) | A_BOLD);
  mvprintw(0, 0, "=== PACMAN IST ONLINE ===");
  attrset(A_NORMAL); // Reset attributes immediately
  clrtoeol();        // Clear rest of line with normal colors

  // Show level name
  attron(COLOR_PAIR(COLOR_UI));
  mvprintw(1, 0, "Level: %s", board->level_name[0] ? board->level_name : "???");
  attrset(A_NORMAL);
  clrtoeol();

  int start_row = 3;

  for (int y = 0; y < board->height; y++) {
    for (int x = 0; x < board->width; x++) {
      const board_pos_t *cell = &board->board[y * board->width + x];
      char ch = cell->content; // Default

      // Map content to visuals
      if (ch != ' ') {
        // It's an entity (W, P, M, C, etc.)
        // Note: Our server might send 'X' for wall, 'C' for Pacman, 'M' for
        // Monster Wait, did we fix the server to send '#'? In Step 1222 output
        // we saw '#', 'C', 'M'. So we handle those characters.

        if (ch == '#' || ch == 'W' || ch == 'X') {
          attron(COLOR_PAIR(COLOR_WALL));
          mvaddch(start_row + y, x, '#');
          attroff(COLOR_PAIR(COLOR_WALL));
        } else if (ch == 'C' || ch == 'P') {
          attron(COLOR_PAIR(COLOR_PACMAN) | A_BOLD);
          mvaddch(start_row + y, x, 'C');
          attroff(COLOR_PAIR(COLOR_PACMAN) | A_BOLD);
        } else if (ch == 'M') {
          attron(COLOR_PAIR(COLOR_GHOST) | A_BOLD);
          mvaddch(start_row + y, x, 'M');
          attroff(COLOR_PAIR(COLOR_GHOST) | A_BOLD);
        } else {
          mvaddch(start_row + y, x, ch);
        }
      } else {
        // Empty cell, check for items
        if (cell->has_portal) {
          attron(COLOR_PAIR(COLOR_PORTAL) | A_BOLD);
          mvaddch(start_row + y, x, '@');
          attroff(COLOR_PAIR(COLOR_PORTAL) | A_BOLD);
        } else if (cell->has_dot) {
          attron(COLOR_PAIR(COLOR_DOT));
          mvaddch(start_row + y, x, '.');
          attroff(COLOR_PAIR(COLOR_DOT));
        } else {
          mvaddch(start_row + y, x, ' ');
        }
      }
    }
  }

  int info_row = start_row + board->height + 1;

  // Show Score and Lives
  if (board->n_pacmans > 0) {
    attron(COLOR_PAIR(COLOR_UI));
    mvprintw(info_row, 0, "Score: %d  |  Lives: %d", board->pacmans[0].points,
             board->pacmans[0].alive ? 1 : 0);
    attrset(A_NORMAL);
    clrtoeol();
    info_row++;
  }

  move(info_row, 0);
  switch (mode) {
  case DRAW_MENU:
    addstr("Controls: WASD | Quit: Q");
    clrtoeol();
    break;
  case DRAW_WIN:
    attron(COLOR_PAIR(COLOR_UI) | A_BOLD);
    addstr("LEVEL COMPLETE! Loading next...");
    attrset(A_NORMAL);
    clrtoeol();
    break;
  case DRAW_GAME_OVER:
    attron(COLOR_PAIR(COLOR_GHOST) | A_BOLD);
    addstr("GAME OVER - Press Q");
    attrset(A_NORMAL);
    clrtoeol();
    break;
  default:
    break;
  }

  // No unlock needed - client temp board has no lock
}
