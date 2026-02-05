#ifndef SNAKE_LADDERS_IPC_H
#define SNAKE_LADDERS_IPC_H

#include <pthread.h>
#include <semaphore.h>
#include <stdbool.h>
#include <sys/types.h>
#include <time.h> 

#define MAX_PLAYERS 5
#define MIN_PLAYERS 3              
#define MAX_NAME_LEN 32
#define BOARD_SIZE 100          
#define MAX_SNAKES 10
#define MAX_LADDERS 10
#define SHM_NAME "/snakeladders_shm"
#define TURN_TIME_LIMIT 20  

// --- Logging Constants ---
#define LOG_QUEUE_SIZE 50
#define LOG_MSG_LEN 128

// --- Scoreboard Structure ---
typedef struct {
    char name[MAX_NAME_LEN];
    int wins;
} ScoreEntry;

// --- Log Structure ---
typedef struct {
    char message[LOG_MSG_LEN];
} LogEntry;

// --- Game Structures ---
typedef struct {
    int start;
    int end;
} SnakeLadder;

typedef enum {
    PLAYER_DISCONNECTED = 0,
    PLAYER_CONNECTED,
    PLAYER_WAITING,
    PLAYER_PLAYING
} PlayerState;

typedef struct {
    pid_t pid;
    int socket_fd;
    char name[MAX_NAME_LEN];
    PlayerState state;
    int position;
    int total_wins;
    bool is_active;
} Player;

typedef enum {
    GAME_WAITING = 0,
    GAME_READY,
    GAME_PLAYING,
    GAME_FINISHED
} GameState;

// --- Main Shared Memory ---
typedef struct {
    // Synchronization
    pthread_mutex_t game_mutex;
    pthread_mutex_t turn_mutex;
    pthread_mutex_t player_mutex;
    pthread_mutex_t board_mutex;
    pthread_mutex_t log_mutex;  
    pthread_mutex_t score_mutex; //
    sem_t turn_sem;
    sem_t log_sem;              

    // Game State
    GameState game_state;
    int winner_index;
    int game_count;
    
    // Turn Data
    int current_player;
    int turn_number;
    time_t turn_start_time;
    
    // Players & Board
    Player players[MAX_PLAYERS];
    int total_players;
    int active_players;
    
    SnakeLadder snakes[MAX_SNAKES];
    SnakeLadder ladders[MAX_LADDERS];
    int num_snakes;
    int num_ladders;
    
    bool server_running;

    // Logging Queue
    LogEntry log_queue[LOG_QUEUE_SIZE];
    int log_head;
    int log_tail;

    // --- SCOREBOARD DATA ---
    // Stores up to 50 historical player records
    ScoreEntry score_table[MAX_PLAYERS * 10]; 
    int unique_players_in_history;

} SharedGameData;

// Prototypes
int create_shared_memory(const char *name, size_t size);
void *attach_shared_memory(int shm_fd, size_t size);
void detach_shared_memory(void *addr, size_t size);
void destroy_shared_memory(const char *name, int shm_fd);
int initialize_sync_primitives(SharedGameData *data);
void cleanup_sync_primitives(SharedGameData *data);
void init_game_board(SharedGameData *data);
GameState get_game_state(SharedGameData *data);
int get_current_player(SharedGameData *data);
void advance_turn(SharedGameData *data);
int get_player_position(SharedGameData *data, int player_index);
void set_player_position(SharedGameData *data, int player_index, int position);
int check_snake_ladder(SharedGameData *data, int position);
int add_player(SharedGameData *data, const char *name, pid_t pid, int socket_fd);
void remove_player(SharedGameData *data, int player_index);
void reset_game(SharedGameData *data);
int prepare_new_game(SharedGameData *data);
int get_active_player_count(SharedGameData *data);
int get_next_active_player(SharedGameData *data, int current);

#endif