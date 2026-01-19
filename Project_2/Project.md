To get the best mark on Part 2 of the PacmanIST project, you need to implement a **multi-client server architecture** exactly as specified, ensuring robust synchronization and handling all edge cases (like client disconnects).

Here is exactly what is required and a checklist to ensure you get full marks.

### 1. Core Requirements (What you must build)

You are transforming the standalone game into a **Server** that manages game logic and accepts multiple remote **Clients** that visualize the game and send commands.

*   **Server (`PacmanIST`):**
    *   Must run as a single process that manages multiple concurrent game sessions.
    *   **Arguments:** `levels_dir` (directory of levels), `max_games` (limit of active sessions), `fifo_name` (path to the registration FIFO).
    *   **Architecture:**
        *   **Host Task:** A main thread that listens on the registration FIFO for new client connections. It hands off valid requests to worker tasks via a Producer-Consumer buffer.
        *   **Worker Tasks:** A pool of threads (size `max_games`) that pick up requests from the buffer and run the game logic for that specific client session.
*   **Client (`client`):**
    *   **Arguments:** `id`, `registration_fifo`, `input_file` (optional).
    *   Must create two private FIFOs (Requests and Notifications) based on its ID.
    *   Connects to the server, then spawns two threads: one to read input (keyboard/file) and send commands, and another to receive board updates and draw to the screen.

### 2. The "Perfect Mark" Checklist

To maximize your grade, you must implement the specific technical details outlined below. The grading often relies on automated tests, so binary protocol precision is non-negotiable.

#### A. Protocol & Communication (Strict Grading)
You must adhere to the byte-level protocol defined in the manual,,.
*   [ ] **Connect Request:** Format `[OP_CODE=1 (char)] | [req_pipe (40 chars)] | [notif_pipe (40 chars)]`. Strings shorter than 40 chars **must** be padded with `\0`.
*   [ ] **Connect Response:** Format `[OP_CODE=1 (char)] | [result (char)]`. `result` is 0 for success.
*   [ ] **Game Updates:** The server sends the board state using `OP_CODE=4`. Ensure you serialize the data (width, height, points, etc.) exactly as integers before the character array of the board.
*   [ ] **Disconnect:** The client sends `OP_CODE=2` to quit. The server must cleanup resources immediately upon receiving this or detecting a closed pipe.

#### B. Synchronization (Concurrency Grading)
The interaction between the Host Task and Worker Tasks determines your concurrency score.
*   [ ] **Producer-Consumer Buffer:** You **must** implement a buffer (size `max_games`) where the Host Task puts connection requests and Workers take them.
*   [ ] **Semaphores & Mutexes:** Use a mutex to protect buffer access. Use two semaphores: `sem_full` (items in buffer) and `sem_empty` (free slots). Do **not** use busy waiting (loops that constantly check values).
*   [ ] **Thread Safety:** Ensure `ncurses` drawing is thread-safe (only one thread usually handles the display) or protected by mutexes, as the library is not thread-safe by default.

#### C. Signal Handling (SIGUSR1)
*   [ ] **Signal Masking:** Only the **Host Task** is allowed to receive `SIGUSR1`. All other threads (workers) must block this signal using `pthread_sigmask` immediately after creation.
*   [ ] **The Log:** When `SIGUSR1` arrives, the Host Task must write a text file listing the IDs of the top 5 active clients with the highest scores.
*   [ ] **SIGPIPE:** You must ignore `SIGPIPE` (`signal(SIGPIPE, SIG_IGN)`). If a client crashes, the server will try to write to a broken pipe; without this protection, the server will crash, costing you significant points.

#### D. Resource Management
*   [ ] **Cleanup:** When a game ends (win/loss/disconnect), the Worker thread must free all memory for that session and return to the pool to wait for the next request.
*   [ ] **FIFOs:** The server creates the registration FIFO. The client creates its own private FIFOs. Use `unlink` to remove them when the programs exit.

Would you like to start by implementing the **`protocol.h` header file** to ensure your structures match the byte alignment requirements?