#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <signal.h>
#include <sys/wait.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <time.h>
#include <sys/time.h>
#include <errno.h>
#include <stdbool.h>
#include <semaphore.h>

#define PORT 8080
#define MAX_PLAYERS 5
#define MIN_PLAYERS 3              
#define MAX_NAME_LEN 32
#define BOARD_SIZE 100          
#define MAX_SNAKES 10
#define MAX_LADDERS 10
#define SHM_NAME "/snakeladders_shm_v13" 
#define TURN_TIME_LIMIT 20  
#define LOG_QUEUE_SIZE 50
#define LOG_MSG_LEN 128



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

typedef struct {
    char name[MAX_NAME_LEN];
    int wins;
} ScoreEntry;

typedef struct {
    char message[LOG_MSG_LEN];
} LogEntry;


typedef struct {
   
    pthread_mutex_t game_mutex;
    pthread_mutex_t turn_mutex;
    pthread_mutex_t player_mutex;
    pthread_mutex_t board_mutex;
    pthread_mutex_t score_mutex; 
    pthread_mutex_t log_mutex;  
    sem_t turn_sem;
    sem_t log_sem;              

    
    GameState game_state;
    int winner_index;
    int game_count;
    bool server_running;
    bool scores_updated_for_game; 
    
    
    int current_player;
    int turn_number;
    time_t turn_start_time;
    
    
    Player players[MAX_PLAYERS];
    int total_players;
    int active_players;
    
    SnakeLadder snakes[MAX_SNAKES];
    SnakeLadder ladders[MAX_LADDERS];
    int num_snakes;
    int num_ladders;

   
    LogEntry log_queue[LOG_QUEUE_SIZE];
    int log_head;
    int log_tail;

   
    ScoreEntry score_table[MAX_PLAYERS * 10]; 
    int unique_players_in_history;
    
} SharedGameData;


SharedGameData *g_shm_ptr = NULL;
int g_server_fd = -1;

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
    pthread_mutex_init(&data->score_mutex, &mutex_attr);
    pthread_mutex_init(&data->log_mutex, &mutex_attr);
    pthread_mutexattr_destroy(&mutex_attr);
    
    sem_init(&data->log_sem, 1, 0);
    sem_init(&data->turn_sem, 1, 0);
    
    data->game_state = GAME_WAITING;
    data->winner_index = -1;
    data->server_running = true;
    data->log_head = 0;
    data->log_tail = 0;
    data->unique_players_in_history = 0;
    data->scores_updated_for_game = false;

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

// --- Logging ---
void log_event(SharedGameData *data, const char *event) {
    pthread_mutex_lock(&data->log_mutex);
    int next = (data->log_tail + 1) % LOG_QUEUE_SIZE;
    if (next != data->log_head) {
        strncpy(data->log_queue[data->log_tail].message, event, LOG_MSG_LEN - 1);
        data->log_queue[data->log_tail].message[LOG_MSG_LEN - 1] = '\0';
        data->log_tail = next;
        sem_post(&data->log_sem);
    }
    pthread_mutex_unlock(&data->log_mutex);
}


void reset_game(SharedGameData *data) {
    pthread_mutex_lock(&data->game_mutex);
    pthread_mutex_lock(&data->player_mutex);
    pthread_mutex_lock(&data->turn_mutex);

    data->game_state = GAME_WAITING;
    data->winner_index = -1;
    data->current_player = 0;
    data->turn_number = 0;
    data->turn_start_time = 0;
    data->scores_updated_for_game = false;
    data->game_count++; 

    
    int count = 0;
    for(int i=0; i<MAX_PLAYERS; i++) {
        if(data->players[i].state != PLAYER_DISCONNECTED) {
            data->players[i].position = 0;
            data->players[i].state = PLAYER_WAITING;
            data->players[i].is_active = true;
            count++;
        }
    }
    data->active_players = count;

    pthread_mutex_unlock(&data->turn_mutex);
    pthread_mutex_unlock(&data->player_mutex);
    pthread_mutex_unlock(&data->game_mutex);
    
    log_event(data, "GAME_RESET: Board cleared for new game.");
}

int get_active_player_count(SharedGameData *data) {
    pthread_mutex_lock(&data->player_mutex);
    int count = data->active_players;
    pthread_mutex_unlock(&data->player_mutex);
    return count;
}

void set_player_position(SharedGameData *data, int player_index, int position) {
    if (player_index < 0 || player_index >= MAX_PLAYERS) return;
    pthread_mutex_lock(&data->player_mutex);
    data->players[player_index].position = position;
    pthread_mutex_unlock(&data->player_mutex);
}

int get_next_active_player(SharedGameData *data, int current) {
    int next = (current + 1) % MAX_PLAYERS;
    int checked = 0;
    while(checked < MAX_PLAYERS) {
        if(data->players[next].is_active && data->players[next].state != PLAYER_DISCONNECTED){
            return next;
        }
        next = (next + 1) % MAX_PLAYERS;
        checked++;
    }
    return -1;
}

void advance_turn(SharedGameData *data) {
    pthread_mutex_lock(&data->turn_mutex);
    int next = get_next_active_player(data, data->current_player);
    if (next != -1) {
        data->current_player = next;
        data->turn_number++;
        data->turn_start_time = time(NULL); 
    }
    pthread_mutex_unlock(&data->turn_mutex);
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

int prepare_new_game(SharedGameData *data) {
    if (!data) return -1;
    pthread_mutex_lock(&data->game_mutex);
    if (data->game_state != GAME_WAITING) {
        pthread_mutex_unlock(&data->game_mutex);
        return -1;
    }
    pthread_mutex_unlock(&data->game_mutex);
    
    pthread_mutex_lock(&data->player_mutex);
    int ready = 0;
    for(int i=0; i<MAX_PLAYERS; i++) {
        if (data->players[i].state == PLAYER_WAITING && data->players[i].is_active) ready++;
    }
    pthread_mutex_unlock(&data->player_mutex);
    return ready;
}



void load_scores(SharedGameData *data) {
    pthread_mutex_lock(&data->score_mutex);
    FILE *file = fopen("scores.txt", "r");
    
    if (!file) {
        printf("[PERSISTENCE] scores.txt not found. Creating new file...\n");
        file = fopen("scores.txt", "w"); 
        if (file) {
            fclose(file);
            printf("[PERSISTENCE] scores.txt created successfully.\n");
        } else {
            perror("[PERSISTENCE] Failed to create scores.txt");
        }
        data->unique_players_in_history = 0;
        pthread_mutex_unlock(&data->score_mutex);
        return;
    }

    int i = 0;
    while (fscanf(file, "%s %d", data->score_table[i].name, &data->score_table[i].wins) != EOF) {
        i++;
        if(i >= MAX_PLAYERS * 10) break;
    }
    data->unique_players_in_history = i;
    fclose(file);
    pthread_mutex_unlock(&data->score_mutex);
}

void save_scores(SharedGameData *data) {
    FILE *file = fopen("scores.txt", "w");
    if (file) {
        for (int i = 0; i < data->unique_players_in_history; i++) {
            fprintf(file, "%s %d\n", data->score_table[i].name, data->score_table[i].wins);
        }
        fflush(file); 
        fclose(file);
        printf("[PERSISTENCE] Scores saved to file.\n");
    } else {
        perror("[PERSISTENCE] Failed to write scores.txt");
    }
}

void process_score_update(SharedGameData *shm_ptr) {
    if (shm_ptr->winner_index == -1 || shm_ptr->scores_updated_for_game) return;

    pthread_mutex_lock(&shm_ptr->score_mutex);
    
    char name[MAX_NAME_LEN];
    strncpy(name, shm_ptr->players[shm_ptr->winner_index].name, MAX_NAME_LEN);
    
    bool found = false;
    for(int i=0; i<shm_ptr->unique_players_in_history; i++) {
        if(strcmp(shm_ptr->score_table[i].name, name) == 0) {
            shm_ptr->score_table[i].wins++;
            found = true; 
            break;
        }
    }
    if(!found && shm_ptr->unique_players_in_history < MAX_PLAYERS * 10) {
        strcpy(shm_ptr->score_table[shm_ptr->unique_players_in_history].name, name);
        shm_ptr->score_table[shm_ptr->unique_players_in_history].wins = 1;
        shm_ptr->unique_players_in_history++;
    }
    
    save_scores(shm_ptr);
    shm_ptr->scores_updated_for_game = true;
    
    pthread_mutex_unlock(&shm_ptr->score_mutex);
}

void* logger_thread(void* arg) {
    SharedGameData *data = (SharedGameData*)arg;
    printf("[LOGGER] Thread started.\n");
    
    while (data->server_running) {
        sem_wait(&data->log_sem);
        if (!data->server_running) break;

        char buffer[LOG_MSG_LEN];
        bool has_log = false;

        pthread_mutex_lock(&data->log_mutex);
        if (data->log_head != data->log_tail) {
            strcpy(buffer, data->log_queue[data->log_head].message);
            data->log_head = (data->log_head + 1) % LOG_QUEUE_SIZE;
            has_log = true;
        }
        pthread_mutex_unlock(&data->log_mutex);

        if (has_log) {
            FILE *f = fopen("game.log", "a");
            if(f) { 
                time_t now = time(NULL);
                char *t_str = ctime(&now);
                t_str[strlen(t_str)-1] = '\0';
                fprintf(f, "[%s] %s\n", t_str, buffer); 
                fclose(f); 
            }
        }
    }
    return NULL;
}


void* scheduler_thread(void* arg) {
    SharedGameData *data = (SharedGameData*)arg;
    printf("[SCHEDULER] Started. Limit: %ds per turn.\n", TURN_TIME_LIMIT);
    
    while (data->server_running) {
        pthread_mutex_lock(&data->game_mutex);
        GameState state = data->game_state;
        pthread_mutex_unlock(&data->game_mutex);

        if (state == GAME_WAITING) {
            if (prepare_new_game(data) >= MIN_PLAYERS) {
                printf("[SCHEDULER] 3+ Players Ready. Starting in 5s...\n");
                sleep(5); 
                pthread_mutex_lock(&data->game_mutex);
                if (get_active_player_count(data) >= MIN_PLAYERS) {
                    data->game_state = GAME_PLAYING;
                    data->turn_number = 1;
                    data->turn_start_time = time(NULL);
                    printf("[SCHEDULER] Game Started!\n");
                    log_event(data, "GAME_START: New game began.");
                }
                pthread_mutex_unlock(&data->game_mutex);
            }
        }
        else if (state == GAME_FINISHED) {
           
            if (!data->scores_updated_for_game) {
                printf("[SCHEDULER] Processing scores...\n");
                process_score_update(data);
            }
            
            
            printf("[SCHEDULER] Game Finished. Waiting 5s before reset...\n");
            sleep(5); 
            reset_game(data);
            printf("[SCHEDULER] Game Reset complete.\n");
        }
        else if (state == GAME_PLAYING) {
            pthread_mutex_lock(&data->turn_mutex);
            time_t now = time(NULL);
            double elapsed = difftime(now, data->turn_start_time);
            
            if (elapsed > TURN_TIME_LIMIT) {
                int current = data->current_player;
                printf("[SCHEDULER] Timeout! P%d skipped.\n", current);
                log_event(data, "TIMEOUT: Player skipped.");

                int next = get_next_active_player(data, current);
                if (next != -1) {
                    data->current_player = next;
                    data->turn_number++;
                    data->turn_start_time = now;
                }
            }
            pthread_mutex_unlock(&data->turn_mutex);
        }
        sleep(1);
    }
    return NULL;
}

void generate_board_string(SharedGameData *data, char *board_str, int buffer_size) {
    pthread_mutex_lock(&data->player_mutex);
    pthread_mutex_lock(&data->board_mutex);

    int player_pos[MAX_PLAYERS] = {0};
    for (int i = 0; i < MAX_PLAYERS; i++) {
        if (data->players[i].is_active && data->players[i].state != PLAYER_DISCONNECTED) {
            player_pos[i] = data->players[i].position;
        }
    }

    char temp[30];
    char line[256];
    int pos = 100;
    memset(board_str, 0, buffer_size);
    strcat(board_str, "\n=== SNAKE & LADDER ===\n");

    for (int row = 10; row >= 1; row--) {
        memset(line, 0, sizeof(line));
        if (row % 2 == 0) {
            for (int col = 10; col >= 1; col--) {
                int cell = pos;
                char marker[20] = "";
                
                for (int p = 0; p < MAX_PLAYERS; p++) 
                    if (player_pos[p] == cell) {
                        char p_id[5];
                        sprintf(p_id, "P%d", p+1);
                        strcat(marker, p_id);
                    }
                
                if (strlen(marker) == 0) {
                     for(int s=0; s<data->num_snakes; s++) 
                        if(data->snakes[s].start == cell) snprintf(marker, sizeof(marker), "S%d", s+1);
                     for(int l=0; l<data->num_ladders; l++) 
                        if(data->ladders[l].start == cell) snprintf(marker, sizeof(marker), "L%d", l+1);
                     if(strlen(marker)==0) snprintf(marker, sizeof(marker), "%d", cell);
                }
                snprintf(temp, sizeof(temp), "[%4s]", marker);
                strcat(line, temp);
                pos--;
            }
        } else {
            int start = pos - 9;
            for (int col = 1; col <= 10; col++) {
                int cell = start + col - 1;
                char marker[20] = "";
                for (int p = 0; p < MAX_PLAYERS; p++) 
                    if (player_pos[p] == cell) {
                        char p_id[5];
                        sprintf(p_id, "P%d", p+1);
                        strcat(marker, p_id);
                    }
                if (strlen(marker) == 0) {
                     for(int s=0; s<data->num_snakes; s++) 
                        if(data->snakes[s].start == cell) snprintf(marker, sizeof(marker), "S%d", s+1);
                     for(int l=0; l<data->num_ladders; l++) 
                        if(data->ladders[l].start == cell) snprintf(marker, sizeof(marker), "L%d", l+1);
                     if(strlen(marker)==0) snprintf(marker, sizeof(marker), "%d", cell);
                }
                snprintf(temp, sizeof(temp), "[%4s]", marker);
                strcat(line, temp);
            }
            pos -= 10;
        }
        strcat(board_str, line);
        strcat(board_str, "\n");
    }
    pthread_mutex_unlock(&data->board_mutex);
    pthread_mutex_unlock(&data->player_mutex);
}


void handle_client(int client_sock, SharedGameData *shm_ptr) {
    srand(time(NULL) ^ getpid());
    char buffer[4096];
    char name[MAX_NAME_LEN];
    int bytes_read, my_player_index = -1;
    bool game_over_sent = false;

    
    send(client_sock, "Enter Name: ", 12, 0);
    memset(name, 0, MAX_NAME_LEN);
    bytes_read = recv(client_sock, name, MAX_NAME_LEN-1, 0);
    if (bytes_read <= 0) { close(client_sock); return; }
    name[strcspn(name, "\n")] = 0;
    
    
    my_player_index = add_player(shm_ptr, name, getpid(), client_sock);
    if (my_player_index == -1) {
        send(client_sock, "Server Full.\n", 13, 0);
        close(client_sock); return;
    }
    
    printf("[GAME] P%d (%s) Joined.\n", my_player_index + 1, name);
    char log_buf[64];
    snprintf(log_buf, sizeof(log_buf), "PLAYER_JOIN: %s connected.", name);
    log_event(shm_ptr, log_buf);

   
    while (shm_ptr->server_running) {
        pthread_mutex_lock(&shm_ptr->game_mutex);
        GameState state = shm_ptr->game_state;
        pthread_mutex_unlock(&shm_ptr->game_mutex);

        if (state == GAME_WAITING || state == GAME_PLAYING) {
            game_over_sent = false;
        }

        if (state == GAME_WAITING) { 
            usleep(1000000); // Wait 1s
            continue; 
        }
        
        if (state == GAME_FINISHED) {
             if (!game_over_sent) {
                 if (shm_ptr->winner_index != -1) {
                     sprintf(buffer, "GAME_OVER|Winner: P%d! Auto-restarting in 5s...", shm_ptr->winner_index + 1);
                     send(client_sock, buffer, strlen(buffer), 0);
                 }
                 game_over_sent = true;
             }
             sleep(1); continue;
        }

        if (state == GAME_PLAYING) {
            pthread_mutex_lock(&shm_ptr->turn_mutex);
            int current = shm_ptr->current_player;
            pthread_mutex_unlock(&shm_ptr->turn_mutex);
            
            if (current == my_player_index) {
               
                char board_buf[2048];
                generate_board_string(shm_ptr, board_buf, sizeof(board_buf));
                sprintf(buffer, "YOUR_TURN|%s\nYour Turn! Press Enter to Roll...", board_buf);
                send(client_sock, buffer, strlen(buffer), 0);
                
                char temp[10];
                recv(client_sock, temp, sizeof(temp), 0);
                
                pthread_mutex_lock(&shm_ptr->turn_mutex);
                if (shm_ptr->current_player != my_player_index) {
                    pthread_mutex_unlock(&shm_ptr->turn_mutex);
                    send(client_sock, "RESULT|Too Slow! Turn Skipped.\n", 30, 0);
                    continue;
                }
                pthread_mutex_unlock(&shm_ptr->turn_mutex);
                
                int roll = (rand() % 6) + 1;
                
                pthread_mutex_lock(&shm_ptr->player_mutex);
                int pos = shm_ptr->players[my_player_index].position;
                pthread_mutex_unlock(&shm_ptr->player_mutex);
                
                int next = pos + roll;
                if (next > 100) next = pos; 

                int final = check_snake_ladder(shm_ptr, next);
                set_player_position(shm_ptr, my_player_index, final);

                snprintf(log_buf, sizeof(log_buf), "MOVE: %s rolled %d to %d", name, roll, final);
                log_event(shm_ptr, log_buf);

                char event_msg[64] = "";
                if (final > next) sprintf(event_msg, " (LADDER! Up to %d)", final);
                if (final < next) sprintf(event_msg, " (SNAKE! Down to %d)", final);

                generate_board_string(shm_ptr, board_buf, sizeof(board_buf));
                sprintf(buffer, "RESULT|Rolled %d -> Moved to %d%s\n%s", roll, final, event_msg, board_buf);
                send(client_sock, buffer, strlen(buffer), 0);
                
                if (final == 100) {
                    pthread_mutex_lock(&shm_ptr->game_mutex);
                    shm_ptr->game_state = GAME_FINISHED;
                    shm_ptr->winner_index = my_player_index;
                    pthread_mutex_unlock(&shm_ptr->game_mutex);

                    log_event(shm_ptr, "GAME_OVER: We have a winner.");

                    sprintf(buffer, "GAME_OVER|Winner: P%d! Auto-restarting in 5s...", my_player_index + 1);
                    send(client_sock, buffer, strlen(buffer), 0);
                    game_over_sent = true;

                    
                } else {
                    advance_turn(shm_ptr);
                }
            } else {
                usleep(200000); 
            }
        }
    }
    remove_player(shm_ptr, my_player_index);
    close(client_sock);
}

void cleanup_handler(int sig) {
    printf("\n[SERVER] Shutdown signal. Cleaning up...\n");
    if (g_shm_ptr) {
        g_shm_ptr->server_running = false;
        sem_post(&g_shm_ptr->log_sem);
        cleanup_sync_primitives(g_shm_ptr);
    }
    shm_unlink(SHM_NAME);
    if(g_server_fd != -1) close(g_server_fd);
    exit(0);
}

void sigchld_handler(int s) { while(waitpid(-1, NULL, WNOHANG) > 0); }

int main() {
    signal(SIGCHLD, sigchld_handler);
    signal(SIGINT, cleanup_handler);

    int shm_fd = create_shared_memory(SHM_NAME, sizeof(SharedGameData));
    g_shm_ptr = attach_shared_memory(shm_fd, sizeof(SharedGameData));
    if (!g_shm_ptr) { perror("Shared Memory Error"); exit(1); }

    initialize_sync_primitives(g_shm_ptr);
    init_game_board(g_shm_ptr);
    load_scores(g_shm_ptr);

    pthread_t t_sched, t_log;
    pthread_create(&t_sched, NULL, scheduler_thread, g_shm_ptr);
    pthread_create(&t_log, NULL, logger_thread, g_shm_ptr);

    int new_socket;
    struct sockaddr_in address;
    int opt=1, addrlen=sizeof(address);
    
    g_server_fd = socket(AF_INET, SOCK_STREAM, 0);
    setsockopt(g_server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    address.sin_family=AF_INET; address.sin_addr.s_addr=INADDR_ANY; address.sin_port=htons(PORT);
    bind(g_server_fd, (struct sockaddr*)&address, sizeof(address));
    listen(g_server_fd, MAX_PLAYERS);

    printf("[SERVER] Listening on port %d. Waiting for 3 players...\n", PORT);

    while (g_shm_ptr->server_running) {
        if ((new_socket = accept(g_server_fd, (struct sockaddr*)&address, (socklen_t*)&addrlen)) >= 0) {
            if (fork() == 0) { 
                close(g_server_fd); 
                handle_client(new_socket, g_shm_ptr); 
                exit(0); 
            }
            else close(new_socket);
        }
        usleep(100000); 
    }
    cleanup_handler(0);
    return 0;
}