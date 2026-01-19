# Compiler variables
CC = gcc
CFLAGS = -I $(INCLUDE_DIR) -g -Wall -Wextra -Werror -Wno-deprecated-declarations -std=c17 -D_POSIX_C_SOURCE=200809L
LDFLAGS = -lpthread

# Directory variables
SRC_DIR = src
OBJ_DIR = obj
BIN_DIR = bin
INCLUDE_DIR = include

# Executables
SERVER = PacmanIST
CLIENT = client

# Object files
SERVER_OBJS = $(OBJ_DIR)/server_main.o $(OBJ_DIR)/server_game.o $(OBJ_DIR)/board.o
CLIENT_OBJS = $(OBJ_DIR)/client_main.o $(OBJ_DIR)/display.o $(OBJ_DIR)/board.o

all: $(BIN_DIR)/$(SERVER) $(BIN_DIR)/$(CLIENT)

# Link Server
$(BIN_DIR)/$(SERVER): $(SERVER_OBJS) | folders
	$(CC) $(CFLAGS) $^ -o $@ $(LDFLAGS)

# Link Client (Needs ncurses)
$(BIN_DIR)/$(CLIENT): $(CLIENT_OBJS) | folders
	$(CC) $(CFLAGS) $^ -o $@ $(LDFLAGS) -lncurses

# Compile Server Main
$(OBJ_DIR)/server_main.o: $(SRC_DIR)/server/main.c | folders
	$(CC) -I $(INCLUDE_DIR) $(CFLAGS) -c $< -o $@

# Compile Server Game Logic (Headless)
$(OBJ_DIR)/server_game.o: $(SRC_DIR)/server/game.c | folders
	$(CC) -I $(INCLUDE_DIR) $(CFLAGS) -c $< -o $@

# Compile Client Main
$(OBJ_DIR)/client_main.o: $(SRC_DIR)/client/main.c | folders
	$(CC) -I $(INCLUDE_DIR) $(CFLAGS) -c $< -o $@

# Compile Shared Board Logic
$(OBJ_DIR)/board.o: $(SRC_DIR)/board.c | folders
	$(CC) -I $(INCLUDE_DIR) $(CFLAGS) -c $< -o $@

# Compile Display Logic (Client only)
$(OBJ_DIR)/display.o: $(SRC_DIR)/client/display.c | folders
	$(CC) -I $(INCLUDE_DIR) $(CFLAGS) -c $< -o $@

folders:
	mkdir -p $(OBJ_DIR)
	mkdir -p $(BIN_DIR)

clean:
	rm -rf $(OBJ_DIR) $(BIN_DIR)

.PHONY: all clean folders
