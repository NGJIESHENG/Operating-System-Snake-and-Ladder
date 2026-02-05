#include "ipc_shared.h"
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <pthread.h>
#include <signal.h>
#include <sys/wait.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <string.h>
#include <time.h>
#include <sys/time.h>
#include <errno.h>
#include <sys/select.h>

#define PORT 8080

int g_server_fd = -1;

// Function Prototypes
void log_event(SharedGameData *data, const char *event);
void* logger_thread(void* arg);
void handle_client(int client_sock, SharedGameData *shm_ptr);
void* admin_thread(void* arg);
void save_scores(SharedGameData *data);

SharedGameData *g_shm_ptr = NULL;

void sigchld_handler(int s) { while(waitpid(-1, NULL, WNOHANG) > 0); }

// --- Requirement: Saving upon server shutdown ---
void cleanup_handler(int sig) {
    printf("\n[SERVER] Signal received. Saving scores and shutting down...\n");
    if (g_shm_ptr) {
        save_scores(g_shm_ptr); // Save to disk
        g_shm_ptr->server_running = false;
        cleanup_sync_primitives(g_shm_ptr);
    }
    shm_unlink(SHM_NAME);
    exit(0);
}

int get_random_roll() {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    
    // Use microseconds for better randomness
    unsigned int seed = tv.tv_usec ^ getpid() ^ (tv.tv_sec << 20);
    srand(seed);
    
    return (rand() % 6) + 1;
}

// --- Requirement: Persistent Storage (Loading) ---
void load_scores(SharedGameData *data) {
    pthread_mutex_lock(&data->score_mutex);
    FILE *file = fopen("scores.txt", "r");
    if (!file) {
        data->unique_players_in_history = 0;
        printf("[PERSISTENCE] No scores found. Created new score table.\n"); 
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
    printf("[PERSISTENCE] Loaded %d player records.\n", i);
    pthread_mutex_unlock(&data->score_mutex);
}

// --- Requirement: Persistent Storage (Saving) ---
void save_scores(SharedGameData *data) {
    pthread_mutex_lock(&data->score_mutex);
    FILE *file = fopen("scores.txt", "w"); // Overwrite mode
    if (file) {
        for (int i = 0; i < data->unique_players_in_history; i++) {
            fprintf(file, "%s %d\n", data->score_table[i].name, data->score_table[i].wins);
        }
        fclose(file);
    }
    pthread_mutex_unlock(&data->score_mutex);
}

// --- Scheduler Thread ---
void* scheduler_thread(void* arg) {
    SharedGameData *data = (SharedGameData*)arg;
    printf("[SCHEDULER] Started. Limit: %ds per turn.\n", TURN_TIME_LIMIT);
    
    while (data->server_running) {
        pthread_mutex_lock(&data->game_mutex);
        GameState state = data->game_state;
        pthread_mutex_unlock(&data->game_mutex);

        if (state == GAME_WAITING) {
            if (prepare_new_game(data) >= MIN_PLAYERS) {
                printf("[SCHEDULER] Players ready. Starting in 5s...\n");
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

// --- Board Generator ---
void generate_board_string(SharedGameData *data, char *board_str, int buffer_size) {
    pthread_mutex_lock(&data->player_mutex);
    pthread_mutex_lock(&data->board_mutex);

    int player_pos[MAX_PLAYERS] = {0};
    for (int i = 0; i < MAX_PLAYERS; i++) {
        if (data->players[i].is_active && data->players[i].state != PLAYER_DISCONNECTED) {
            player_pos[i] = data->players[i].position;
        }
    }

    char temp[30];  // INCREASED SIZE
    char line[256];
    int pos = 100;
    memset(board_str, 0, buffer_size);
    strcat(board_str, "\n=== SNAKE & LADDER ===\n");

    for (int row = 10; row >= 1; row--) {
        memset(line, 0, sizeof(line));
        if (row % 2 == 0) {
            for (int col = 10; col >= 1; col--) {
                int cell = pos;
                char marker[20] = "";  // INCREASED SIZE
                
                // Check players
                for (int p = 0; p < MAX_PLAYERS; p++) 
                    if (player_pos[p] == cell) snprintf(marker, sizeof(marker), "P%d", p+1);
                
                if (strlen(marker) == 0) {
                     // Check snakes/ladders
                     for(int s=0; s<data->num_snakes; s++) 
                        if(data->snakes[s].start == cell) snprintf(marker, sizeof(marker), "S%d", s+1);
                     for(int l=0; l<data->num_ladders; l++) 
                        if(data->ladders[l].start == cell) snprintf(marker, sizeof(marker), "L%d", l+1);
                     if(strlen(marker)==0) snprintf(marker, sizeof(marker), "%d", cell);
                }
                snprintf(temp, sizeof(temp), "[%4s]", marker);  // USING snprintf
                strcat(line, temp);
                pos--;
            }
        } else {
            int start = pos - 9;
            for (int col = 1; col <= 10; col++) {
                int cell = start + col - 1;
                char marker[20] = "";  // INCREASED SIZE
                for (int p = 0; p < MAX_PLAYERS; p++) 
                    if (player_pos[p] == cell) snprintf(marker, sizeof(marker), "P%d", p+1);
                if (strlen(marker) == 0) {
                     for(int s=0; s<data->num_snakes; s++) 
                        if(data->snakes[s].start == cell) snprintf(marker, sizeof(marker), "S%d", s+1);
                     for(int l=0; l<data->num_ladders; l++) 
                        if(data->ladders[l].start == cell) snprintf(marker, sizeof(marker), "L%d", l+1);
                     if(strlen(marker)==0) snprintf(marker, sizeof(marker), "%d", cell);
                }
                snprintf(temp, sizeof(temp), "[%4s]", marker);  // USING snprintf
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

    char buffer[4096]; // Large buffer for board
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
    
    printf("[GAME] P%d (%s) Joined.\n", my_player_index, name);
    log_event(shm_ptr, "PLAYER_JOIN connected.");

    while (shm_ptr->server_running) {
        GameState state = get_game_state(shm_ptr);
        if (state == GAME_WAITING) { sleep(1); continue; }
        
        if (state == GAME_FINISHED) {
             if (!game_over_sent && shm_ptr->winner_index != -1) {
                sprintf(buffer, "GAME_OVER|Winner: P%d!", shm_ptr->winner_index + 1);
                send(client_sock, buffer, strlen(buffer), 0);
                game_over_sent = true;
             }
             sleep(1); continue;
        }

        if (state == GAME_PLAYING) {
            int current = get_current_player(shm_ptr);
            
            if (current == my_player_index) {
                char board_buf[2048];
                generate_board_string(shm_ptr, board_buf, sizeof(board_buf));
                sprintf(buffer, "YOUR_TURN|%s\nYour Turn! Press Enter to Roll...", board_buf);
                send(client_sock, buffer, strlen(buffer), 0);
                
                char temp[10];
                recv(client_sock, temp, sizeof(temp), 0);
                
                if (get_current_player(shm_ptr) != my_player_index) {
                    send(client_sock, "RESULT|Too Slow! Turn Skipped.\n", 30, 0);
                    continue;
                }
                
                int roll = get_random_roll(); // Use your random function
                int pos = get_player_position(shm_ptr, my_player_index);
                int next = pos + roll;
                if (next > 100) next = pos;

                // Check for snake or ladder BEFORE moving
                int original_next = next;
                int final = check_snake_ladder(shm_ptr, next);

                set_player_position(shm_ptr, my_player_index, final);

                generate_board_string(shm_ptr, board_buf, sizeof(board_buf));

                // Build result message with snake/ladder info
                char snake_ladder_msg[100] = "";
                if (final != original_next) {
                    if (final < original_next) {
                        snprintf(snake_ladder_msg, sizeof(snake_ladder_msg), 
                                "Oh no! Snake from %d to %d!\n", original_next, final);
                    } else {
                        snprintf(snake_ladder_msg, sizeof(snake_ladder_msg),
                                "Yay! Ladder from %d to %d!\n", original_next, final);
                    }
                }

                sprintf(buffer, "RESULT|Rolled %d -> Moved to %d\n%s%s", 
                        roll, final, snake_ladder_msg, board_buf);
                send(client_sock, buffer, strlen(buffer), 0);

                char log_msg[64];
                sprintf(log_msg, "MOVE: %s rolled %d to %d", name, roll, final);
                log_event(shm_ptr, log_msg);
                
                if (final == 100) {
                    pthread_mutex_lock(&shm_ptr->game_mutex);
                    shm_ptr->game_state = GAME_FINISHED;
                    shm_ptr->winner_index = my_player_index;
                    
                    // Notify ALL players about the winner
                    char win_msg[256];
                    snprintf(win_msg, sizeof(win_msg), 
                            "GAME_OVER|%s wins! Game will shutdown in 5 seconds.", name);
                    
                    // Broadcast to all connected players
                    for (int i = 0; i < MAX_PLAYERS; i++) {
                        if (shm_ptr->players[i].state != PLAYER_DISCONNECTED && 
                            shm_ptr->players[i].socket_fd != -1) {
                            send(shm_ptr->players[i].socket_fd, win_msg, strlen(win_msg), 0);
                        }
                    }
                    
                    // --- ATOMIC SCORE UPDATE ---
                    pthread_mutex_lock(&shm_ptr->score_mutex);
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
                    // REMOVED: save_scores(shm_ptr); -> Let the parent handle the file I/O safely
                    pthread_mutex_unlock(&shm_ptr->score_mutex);
                    pthread_mutex_unlock(&shm_ptr->game_mutex);

                    log_event(shm_ptr, "GAME_OVER: We have a winner.");

                    // Wait a bit to ensure clients receive the Game Over message
                    sleep(5);

                    // Trigger graceful shutdown in the Parent's main loop
                    shm_ptr->server_running = false;
                    
                } else {
                    advance_turn(shm_ptr);
                }
            } else {
                usleep(500000); 
            }
        }
    }
    remove_player(shm_ptr, my_player_index);
    close(client_sock);
}

void log_event(SharedGameData *data, const char *event) {
    pthread_mutex_lock(&data->log_mutex);
    int next = (data->log_tail + 1) % LOG_QUEUE_SIZE;
    if (next != data->log_head) {
        strncpy(data->log_queue[data->log_tail].message, event, LOG_MSG_LEN - 1);
        data->log_tail = next;
        sem_post(&data->log_sem);
    }
    pthread_mutex_unlock(&data->log_mutex);
}

void* logger_thread(void* arg) {
    SharedGameData *data = (SharedGameData*)arg;
    while (data->server_running) {
        sem_wait(&data->log_sem);
        if (!data->server_running) break;
        pthread_mutex_lock(&data->log_mutex);
        if (data->log_head != data->log_tail) {
            char buffer[LOG_MSG_LEN];
            strcpy(buffer, data->log_queue[data->log_head].message);
            data->log_head = (data->log_head + 1) % LOG_QUEUE_SIZE;
            pthread_mutex_unlock(&data->log_mutex);
            
            FILE *f = fopen("game.log", "a");
            if(f) { fprintf(f, "%ld: %s\n", time(NULL), buffer); fclose(f); }
        } else {
            pthread_mutex_unlock(&data->log_mutex);
        }
    }
    return NULL;
}

void broadcast_to_players(SharedGameData *data, const char *message) {
    pthread_mutex_lock(&data->player_mutex);
    for (int i = 0; i < MAX_PLAYERS; i++) {
        if (data->players[i].state != PLAYER_DISCONNECTED && 
            data->players[i].socket_fd != -1) {
            send(data->players[i].socket_fd, message, strlen(message), 0);
        }
    }
    pthread_mutex_unlock(&data->player_mutex);
}

void* admin_thread(void* arg) {
    SharedGameData *data = (SharedGameData*)arg;
    while (data->server_running) {
        sleep(2);
        pthread_mutex_lock(&data->game_mutex);
        if(data->game_state == GAME_FINISHED) {
            printf("Game Over. Waiting for shutdown...\n");
            pthread_mutex_unlock(&data->game_mutex);
            // Don't trigger shutdown here - let handle_client do it
            break;
        } else {
            pthread_mutex_unlock(&data->game_mutex);
        }
    }
    return NULL;
}

int main() {

    int shm_fd = create_shared_memory(SHM_NAME, sizeof(SharedGameData));
    g_shm_ptr = attach_shared_memory(shm_fd, sizeof(SharedGameData));
    initialize_sync_primitives(g_shm_ptr);
    init_game_board(g_shm_ptr);
    load_scores(g_shm_ptr); // Load scores at startup

    pthread_t t1, t2, t3;
    pthread_create(&t1, NULL, scheduler_thread, g_shm_ptr);
    pthread_create(&t2, NULL, logger_thread, g_shm_ptr);
    pthread_create(&t3, NULL, admin_thread, g_shm_ptr);

    signal(SIGCHLD, sigchld_handler);
    signal(SIGINT, cleanup_handler);

    int new_socket, server_fd;
    server_fd = socket(AF_INET, SOCK_STREAM, 0);
    g_server_fd = server_fd;
    struct sockaddr_in address;
    int opt=1, addrlen=sizeof(address);
    server_fd = socket(AF_INET, SOCK_STREAM, 0);
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    address.sin_family=AF_INET; address.sin_addr.s_addr=INADDR_ANY; address.sin_port=htons(PORT);
    bind(server_fd, (struct sockaddr*)&address, sizeof(address));
    listen(server_fd, MAX_PLAYERS);

    printf("[SERVER] Running on %d...\n", PORT);
    while (g_shm_ptr->server_running) {
        // Set socket to non-blocking
        int flags = fcntl(server_fd, F_GETFL, 0);
        fcntl(server_fd, F_SETFL, flags | O_NONBLOCK);
        
        if ((new_socket = accept(server_fd, (struct sockaddr*)&address, (socklen_t*)&addrlen)) >= 0) {
            if (fork() == 0) { 
                close(server_fd); 
                handle_client(new_socket, g_shm_ptr); 
                exit(0); 
            }
            else close(new_socket);
        }
        
        // Check if shutdown was requested
        if (!g_shm_ptr->server_running) {
            printf("[SERVER] Shutting down...\n");
            break; // Breaks the loop naturally
        }
        usleep(100000);
    }

    // Cleanup before exit
    close(server_fd);
    cleanup_handler(0);
    return 0;
}