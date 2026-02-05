======================================================================
CSN6214 Operating Systems: Concurrent Networked Board Game Interpreter
PROJECT: Snake and Ladder (Hybrid Concurrency Model)
======================================================================

1. PROJECT OVERVIEW
-------------------
This project implements a multiplayer Snake and Ladder game for 3 to 5 players[cite: 8, 24]. 
It utilizes a Hybrid Concurrency Model:
- Multiprocessing: fork() is used for client session isolation [cite: 32-33].
- Multithreading: pthreads are used for the internal Round Robin Scheduler 
  and the Concurrent Logger [cite: 35-38].
- IPC: POSIX Shared Memory is used to maintain game state across processes[cite: 62].

2. PREREQUISITES
----------------
- OS: Linux (required for POSIX shared memory and fork support)[cite: 89].
- Compiler: GCC[cite: 89].
- Libraries: pthread (threading) and rt (real-time shared memory)[cite: 89].

3. COMPILATION
--------------
Use the provided Makefile to compile both the server and client components[cite: 107].
Command:
    make

This will generate two executables: 'server' and 'client'[cite: 87].
To clean the directory:
    make clean

4. HOW TO RUN
-------------
Step 1: Start the Server
    ./server

Step 2: Connect Clients (Open 3 to 5 separate terminal windows)
    ./client

5. GAME RULES
-------------
- Number of Players: Exactly 3 to 5[cite: 24].
- Movement: Players roll a 6-sided die generated server-side[cite: 29].
- Board Logic:
  - Snakes: Slipped down from start to end (8 snakes total).
  - Ladders: Climbed up from start to end (8 ladders total).
- Winning: The first player to reach square 100 exactly wins[cite: 27].
- Round Robin: Each player has a 20-second time limit to move. 
  If a player times out, the Scheduler thread will skip their turn[cite: 15, 37, 59].

6. TECHNICAL FEATURES
---------------------
- Deployment Mode: Multi-machine mode via TCP Sockets (IPv4)[cite: 19, 93, 110].
- Concurrency: Parent process runs the Scheduler and Logger threads. 
  Child processes handle client logic [cite: 20, 131-132].
- Persistence: Player wins are saved in 'scores.txt' and persist across 
  server restarts [cite: 75-77].
- Logging: All game events are recorded in 'game.log' by a dedicated 
  thread using a synchronized queue[cite: 38, 43, 116].

7. TEAM MEMBERS & ROLES
-----------------------
[Insert Your Name/ID] - Server Core, Forking, Process Management [cite: 126]
[Member 2 Name/ID]    - Client/Game Logic, Round Robin Scheduler [cite: 126]
[Member 3 Name/ID]    - Shared Memory, Networking, IPC [cite: 126]
[Member 4 Name/ID]    - Logger, Persistence, scores.txt management [cite: 126]
======================================================================