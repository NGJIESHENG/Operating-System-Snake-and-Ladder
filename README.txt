======================================================================
CSN6214 Operating Systems: Concurrent Networked Board Game Interpreter
PROJECT: Snake and Ladder (Hybrid Concurrency Model)
======================================================================

1. PROJECT OVERVIEW
-------------------
This project implements a multiplayer Snake and Ladder game for 3 to 5 players[cite: 8, 24, 52].
It utilizes a Hybrid Concurrency Model:
- Multiprocessing: fork() is used for client session isolation[cite: 33, 53].
- Multithreading: pthreads are used for the internal Round Robin Scheduler 
  and the Concurrent Logger in the parent process [cite: 35-38, 54, 68].
- IPC: POSIX Shared Memory is used to maintain game state across processes[cite: 55, 62].

2. PREREQUISITES
----------------
- OS: Linux (required for POSIX shared memory and fork support)[cite: 56].
- Compiler: GCC[cite: 56].
- Libraries: pthread (threading) and rt (real-time shared memory)[cite: 57].

3. HOW TO COMPILE (make)
------------------------
Use the provided Makefile to compile both the server and client components[cite: 58].
Command:
    make

This will generate two executables: 'server' and 'client'[cite: 59].
To clean the directory of executables and object files:
    make clean [cite: 60]

4. HOW TO RUN & EXAMPLE COMMANDS
--------------------------------
Step 1: Start the Server (Run this first)
    ./server

Step 2: Connect Clients (Run in 3 to 5 separate terminal windows)
    ./client

Step 3: Following Prompts
    - Enter your name when prompted.
    - Press 'Enter' to roll the die when it is your turn.

5. GAME RULES SUMMARY
---------------------
- Player Count: Supports exactly 3 to 5 concurrent players[cite: 24, 60].
- Objective: Be the first player to reach square 100 exactly[cite: 64].
- Board Dynamics:
    - Snakes: Land on a head and slide down to the tail (8 snakes total)[cite: 62].
    - Ladders: Land on a base and climb up to the top (8 ladders total)[cite: 63].
- Turn Management (Round Robin):
    - Each player has a 20-second time limit per turn[cite: 65].
    - If a player times out, the Scheduler skips their turn[cite: 31, 37, 66].
- Persistence: Winning stats are saved to 'scores.txt'[cite: 69, 75].

6. MODE SUPPORTED
-----------------
- Deployment Mode: Multi-machine mode supported via TCP/IP Sockets (IPv4)[cite: 67].
- Communication: POSIX Shared Memory for internal process coordination[cite: 55, 68].
- Logging: Concurrent logging to 'game.log' via a dedicated thread[cite: 70].

7. TEAM MEMBERS & ROLES
-----------------------
NG JIE SHENG - Server Core, Forking, Process Management
[Member 2]   - Client/Game Logic, Round Robin Scheduler
[Member 3]   - Shared Memory, Networking, IPC
[Member 4]   - Logger, Persistence, scores.txt management
======================================================================