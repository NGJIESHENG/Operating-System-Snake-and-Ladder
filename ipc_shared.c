#include "ipc_shared.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>
#include <time.h>

int create_shared_memory(const char *name, size_t size) {
    shm_unlink(name);
    int shm_fd = shm_open(name, O_CREAT | O_RDWR, 0666);
    if (shm_fd == -1) return -1;
    if (ftruncate(shm_fd, size) == -1) return -1;
    return shm_fd;
}

void *attach_shared_memory(int shm_fd, size_t size) {
    void *addr = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd, 0);
    return (addr == MAP_FAILED) ? NULL : addr;
}

void detach_shared_memory(void *addr, size_t size) {
    munmap(addr, size);
}

void destroy_shared_memory(const char *name, int shm_fd) {
    if (shm_fd != -1) close(shm_fd);
    shm_unlink(name);
}

int initialize_sync_primitives(SharedGameData *data) {
    if (!data) return -1;
    memset(data, 0, sizeof(SharedGameData));
    
    pthread_mutexattr_t mutex_attr;
    pthread_mutexattr_init(&mutex_attr);
    pthread_mutexattr_setpshared(&mutex_attr, PTHREAD_PROCESS_SHARED);
    
    pthread_mutex_init(&data->game_mutex, &mutex_attr);
    pthread_mutex_init(&data->turn_mutex, &mutex_attr);
    pthread_mutex_init(&data->player_mutex, &mutex_attr);
    pthread_mutex_init(&data->board_mutex, &mutex_attr);
    pthread_mutex_init(&data->score_mutex, &mutex_attr); // Initialize score mutex
    pthread_mutex_init(&data->log_mutex, &mutex_attr); // NEW
    pthread_mutexattr_destroy(&mutex_attr);
    
    sem_init(&data->log_sem, 1, 0);
    sem_init(&data->turn_sem, 1, 0);
    
    data->game_state = GAME_WAITING;
    data->winner_index = -1;
    data->game_count = 0;
    data->current_player = 0;
    data->turn_number = 0;
    data->turn_start_time = 0; // Initialize timer
    data->active_players = 0;
    data->server_running = true;
    data->unique_players_in_history = 0;
    data->log_head = 0;
    data->log_tail = 0;

    for (int i = 0; i < MAX_PLAYERS; i++) {
        data->players[i].state = PLAYER_DISCONNECTED;
        data->players[i].socket_fd = -1;
    }
    return 0;
}

void cleanup_sync_primitives(SharedGameData *data) {
    if (!data) return;
    pthread_mutex_destroy(&data->game_mutex);
    pthread_mutex_destroy(&data->turn_mutex);
    pthread_mutex_destroy(&data->player_mutex);
    pthread_mutex_destroy(&data->board_mutex);
    pthread_mutex_destroy(&data->score_mutex);
    pthread_mutex_destroy(&data->log_mutex);
    sem_destroy(&data->log_sem);
    sem_destroy(&data->turn_sem);
}

void init_game_board(SharedGameData *data) {
    pthread_mutex_lock(&data->board_mutex);
    // Snakes
    data->snakes[0] = (SnakeLadder){98, 78};
    data->snakes[1] = (SnakeLadder){95, 75};
    data->snakes[2] = (SnakeLadder){93, 73};
    data->snakes[3] = (SnakeLadder){87, 24};
    data->snakes[4] = (SnakeLadder){64, 60};
    data->snakes[5] = (SnakeLadder){62, 19};
    data->snakes[6] = (SnakeLadder){54, 34};
    data->snakes[7] = (SnakeLadder){17, 7};
    data->num_snakes = 8;
    // Ladders
    data->ladders[0] = (SnakeLadder){1, 38};
    data->ladders[1] = (SnakeLadder){4, 14};
    data->ladders[2] = (SnakeLadder){9, 31};
    data->ladders[3] = (SnakeLadder){21, 42};
    data->ladders[4] = (SnakeLadder){28, 84};
    data->ladders[5] = (SnakeLadder){36, 44};
    data->ladders[6] = (SnakeLadder){51, 67};
    data->ladders[7] = (SnakeLadder){71, 91};
    data->num_ladders = 8;
    pthread_mutex_unlock(&data->board_mutex);
}

GameState get_game_state(SharedGameData *data) {
    pthread_mutex_lock(&data->game_mutex);
    GameState state = data->game_state;
    pthread_mutex_unlock(&data->game_mutex);
    return state;
}

void set_game_state(SharedGameData *data, GameState state) {
    pthread_mutex_lock(&data->game_mutex);
    data->game_state = state;
    pthread_mutex_unlock(&data->game_mutex);
}

int get_current_player(SharedGameData *data) {
    pthread_mutex_lock(&data->turn_mutex);
    int player = data->current_player;
    pthread_mutex_unlock(&data->turn_mutex);
    return player;
}

void advance_turn(SharedGameData *data) {
    pthread_mutex_lock(&data->turn_mutex);
    int next = get_next_active_player(data, data->current_player);
    if (next != -1) {
        data->current_player = next;
        data->turn_number++;
        data->turn_start_time = time(NULL); // RESET TIMER
    }
    pthread_mutex_unlock(&data->turn_mutex);
}

int get_player_position(SharedGameData *data, int player_index) {
    if (player_index < 0 || player_index >= MAX_PLAYERS) return -1;
    pthread_mutex_lock(&data->player_mutex);
    int pos = data->players[player_index].position;
    pthread_mutex_unlock(&data->player_mutex);
    return pos;
}

void set_player_position(SharedGameData *data, int player_index, int position) {
    if (player_index < 0 || player_index >= MAX_PLAYERS) return;
    pthread_mutex_lock(&data->player_mutex);
    data->players[player_index].position = position;
    pthread_mutex_unlock(&data->player_mutex);
}

int check_snake_ladder(SharedGameData *data, int position) {
    pthread_mutex_lock(&data->board_mutex);
    int new_pos = position;
    for(int i=0; i<data->num_snakes; i++) 
        if(data->snakes[i].start == position) new_pos = data->snakes[i].end;
    for(int i=0; i<data->num_ladders; i++) 
        if(data->ladders[i].start == position) new_pos = data->ladders[i].end;
    pthread_mutex_unlock(&data->board_mutex);
    return new_pos;
}

int add_player(SharedGameData *data, const char *name, pid_t pid, int socket_fd) {
    pthread_mutex_lock(&data->player_mutex);
    int idx = -1;
    for(int i=0; i<MAX_PLAYERS; i++) {
        if(data->players[i].state == PLAYER_DISCONNECTED) {
            idx = i;
            data->players[i].pid = pid;
            data->players[i].socket_fd = socket_fd;
            strncpy(data->players[i].name, name, MAX_NAME_LEN - 1);
            
            data->players[i].state = PLAYER_WAITING; 
            
            data->players[i].position = 0;
            data->players[i].is_active = true;
            data->active_players++;
            data->total_players++;
            break;
        }
    }
    pthread_mutex_unlock(&data->player_mutex);
    return idx;
}

void remove_player(SharedGameData *data, int player_index) {
    if (player_index < 0 || player_index >= MAX_PLAYERS) return;
    pthread_mutex_lock(&data->player_mutex);
    if (data->players[player_index].state != PLAYER_DISCONNECTED) {
        data->players[player_index].state = PLAYER_DISCONNECTED;
        data->players[player_index].is_active = false;
        if(data->active_players > 0) data->active_players--;
    }
    pthread_mutex_unlock(&data->player_mutex);
}

void increment_player_wins(SharedGameData *data, int player_index) {
    if (player_index < 0 || player_index >= MAX_PLAYERS) return;
    pthread_mutex_lock(&data->player_mutex);
    data->players[player_index].total_wins++;
    pthread_mutex_unlock(&data->player_mutex);
}

void reset_game(SharedGameData *data) {
    if (!data) return;
    pthread_mutex_lock(&data->game_mutex);
    pthread_mutex_lock(&data->turn_mutex);
    pthread_mutex_lock(&data->player_mutex);
    
    data->game_state = GAME_WAITING;
    data->winner_index = -1;
    data->current_player = 0;
    data->turn_number = 0;
    data->turn_start_time = 0; // Reset Timer
    data->game_count++;
    
    int active_count = 0;
    int first_active = -1;

    for(int i=0; i<MAX_PLAYERS; i++) {
        if(data->players[i].state != PLAYER_DISCONNECTED && data->players[i].is_active) {
            data->players[i].position = 0;
            data->players[i].state = PLAYER_WAITING;
            data->players[i].is_active = true;
            if(first_active == -1) first_active = i;
            active_count++;
        }
    }
    data->active_players = active_count;
    
    pthread_mutex_unlock(&data->player_mutex);
    pthread_mutex_unlock(&data->turn_mutex);
    pthread_mutex_unlock(&data->game_mutex);
}

int prepare_new_game(SharedGameData *data) {
    if (!data) return -1;
    pthread_mutex_lock(&data->game_mutex);
    if (data->game_state != GAME_WAITING) {
        pthread_mutex_unlock(&data->game_mutex);
        return -1;
    }
    
    pthread_mutex_lock(&data->player_mutex);
    int ready = 0;
    for(int i=0; i<MAX_PLAYERS; i++) 
        if (data->players[i].state == PLAYER_WAITING && data->players[i].is_active) ready++;
    pthread_mutex_unlock(&data->player_mutex);
    
    pthread_mutex_unlock(&data->game_mutex);
    return ready;
}

void cleanup_finished_game(SharedGameData *data) {
    // Optional helper (logic covered in reset_game)
}

int get_active_player_count(SharedGameData *data) {
    if (!data) return 0;
    pthread_mutex_lock(&data->player_mutex);
    int count = data->active_players;
    pthread_mutex_unlock(&data->player_mutex);
    return count;
}

int get_next_active_player(SharedGameData *data, int current) {
    pthread_mutex_lock(&data->player_mutex);
    int next = (current + 1) % MAX_PLAYERS;
    int checked = 0;
    int result = -1;

    while(checked < MAX_PLAYERS) {
        if(data->players[next].is_active && data->players[next].state != PLAYER_DISCONNECTED){
            result = next;
            break;
        }
        next = (next + 1) % MAX_PLAYERS;
        checked++;
    }
    pthread_mutex_unlock(&data->player_mutex);
    return result;
}

const char *player_state_string(PlayerState state) {
    switch(state) {
        case PLAYER_DISCONNECTED: return "DISCONNECTED";
        case PLAYER_CONNECTED: return "CONNECTED";
        case PLAYER_WAITING: return "WAITING";
        case PLAYER_PLAYING: return "PLAYING";
        default: return "UNKNOWN";
    }
}

const char *game_state_string(GameState state) {
    switch(state) {
        case GAME_WAITING: return "WAITING";
        case GAME_READY: return "READY";
        case GAME_PLAYING: return "PLAYING";
        case GAME_FINISHED: return "FINISHED";
        default: return "UNKNOWN";
    }
}

void print_shared_state(SharedGameData *data) {
    pthread_mutex_lock(&data->game_mutex);
    pthread_mutex_lock(&data->player_mutex);
    
    printf("[LOG] State: %s | Active: %d | Turn: P%d\n", 
           game_state_string(data->game_state), 
           data->active_players, 
           data->current_player);
           
    pthread_mutex_unlock(&data->player_mutex);
    pthread_mutex_unlock(&data->game_mutex);
}