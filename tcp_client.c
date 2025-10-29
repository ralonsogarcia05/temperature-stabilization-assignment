#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include "utils.h"

// Client loops until server sends DONE via Index == -1
// External update: (3*external + 2*central) / 5

int main (int argc, char *argv[])
{
    if (argc < 3) {
        fprintf(stderr, "Usage: %s <externalIndex 1..4> <initialTemperature>\n", argv[0]);
        return 1;
    }

    int externalIndex = atoi(argv[1]);
    float externalTemp = atof(argv[2]);

    int socket_desc = socket(AF_INET, SOCK_STREAM, 0);
    if (socket_desc < 0) { perror("socket"); return 1; }

    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port   = htons(2000);
    server_addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK); // 127.0.0.1

    if (connect(socket_desc, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        perror("connect");
        return 1;
    }

    printf("[Client %d] Connected. Initial T=%.6f\n", externalIndex, externalTemp);

    while (1) {
        // Send current external temperature to server
        struct msg out = prepare_message(externalIndex, externalTemp);
        if (send(socket_desc, (const void*)&out, sizeof(out), 0) < 0) {
            perror("send");
            break;
        }

        // Receive server response (central temp or DONE)
        struct msg in;
        int n = recv(socket_desc, (void*)&in, sizeof(in), 0);
        if (n <= 0) { if (n < 0) perror("recv"); else fprintf(stderr, "[Client %d] Server closed\n", externalIndex); break; }

        if (in.Index == -1) {
            // DONE signal
            printf("[Client %d] DONE. Final T=%.6f (central=%.6f)\n",
                   externalIndex, externalTemp, in.T);
            break;
        }

        // Update external using: (3*external + 2*central)/5
        float centralTemp = in.T;
        externalTemp = (3.0f * externalTemp + 2.0f * centralTemp) / 5.0f;
    }

    close(socket_desc);
    return 0;
}
