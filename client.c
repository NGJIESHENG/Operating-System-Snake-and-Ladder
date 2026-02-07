#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <arpa/inet.h>

#define PORT 8080
#define BUFFER_SIZE 4096 

int main() {
    int sock = 0, valread;
    struct sockaddr_in serv_addr;
    char buffer[BUFFER_SIZE] = {0};
    char input[100];

    if ((sock = socket(AF_INET, SOCK_STREAM, 0)) < 0) return -1;
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(PORT);
    inet_pton(AF_INET, "127.0.0.1", &serv_addr.sin_addr);

    if (connect(sock, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0) {
        printf("Connection Failed.\n");
        return -1;
    }

    printf("Connected! Follow the prompts.\n");

    while (1) {
        memset(buffer, 0, BUFFER_SIZE);
        valread = recv(sock, buffer, BUFFER_SIZE, 0);
        if (valread <= 0) break;

        
        if (strncmp(buffer, "YOUR_TURN|", 10) == 0) {
            printf("\n%s", buffer + 10); 
            fgets(input, sizeof(input), stdin);
            send(sock, "ROLL", 4, 0);
        } 
        
        else if (strncmp(buffer, "RESULT|", 7) == 0) {
            printf("\n--------------------------------\n");
            printf("%s", buffer + 7);
            printf("\n--------------------------------\n");

           
            char *game_over_ptr = strstr(buffer, "GAME_OVER|");
            if (game_over_ptr) {
                printf("\n********************************\n");
                printf("%s", game_over_ptr + 10);
                printf("\n********************************\n");
            }
        }
     
        else if (strncmp(buffer, "GAME_OVER|", 10) == 0) {
            printf("\n********************************\n");
            printf("%s", buffer + 10);
            printf("\n********************************\n");
        }
 
        else {
            printf("%s", buffer);
            if (strstr(buffer, "Enter Name")) {
                fgets(input, sizeof(input), stdin);
                send(sock, input, strlen(input), 0);
            }
        }
    }
    close(sock);
    return 0;
}