#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <stdbool.h>
#include <math.h>
#include "utils.h"

#define NUM_EXTERNALS 4
#define EPS 1e-3f

// Accept 4 clients on 127.0.0.1:2000 and return the listening fd
static int listen_and_accept_four(int client_socket[NUM_EXTERNALS]) {
    int listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (listen_fd < 0) { perror("socket"); exit(1); }

    int opt = 1;
    setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port   = htons(2000);
    server_addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK); // 127.0.0.1

    if (bind(listen_fd, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) { perror("bind"); exit(1); }
    if (listen(listen_fd, NUM_EXTERNALS) < 0) { perror("listen"); exit(1); }
    printf("[Server] Listening on 127.0.0.1:2000 ...\n");

    socklen_t client_size = sizeof(struct sockaddr_in);
    struct sockaddr_in client_addr;

    for (int i = 0; i < NUM_EXTERNALS; i++) {
        client_socket[i] = accept(listen_fd, (struct sockaddr*)&client_addr, &client_size);
        if (client_socket[i] < 0) { perror("accept"); exit(1); }
        printf("[Server] Client connected from %s:%d (slot %d)\n",
               inet_ntoa(client_addr.sin_addr), ntohs(client_addr.sin_port), i+1);
    }
    return listen_fd; // keep open (or close later)
}

int main(int argc, char *argv[])
{
    if (argc < 2) {
        fprintf(stderr, "Usage: %s <initialCentralTemperature>\n", argv[0]);
        return 1;
    }

    float centralTemp = atof(argv[1]);
    printf("[Server] Start. Initial central T=%.6f\n", centralTemp);

    int client_socket[NUM_EXTERNALS];
    int listen_fd = listen_and_accept_four(client_socket);

    float prevExternal[NUM_EXTERNALS];
    for (int i = 0; i < NUM_EXTERNALS; i++) prevExternal[i] = 1e9f; // force iteration

    bool stable = false;

    while (!stable) {
        // 1) Receive current temps from all externals
        float external[NUM_EXTERNALS];
        for (int i = 0; i < NUM_EXTERNALS; i++) {
            struct msg in;
            int n = recv(client_socket[i], (void*)&in, sizeof(in), 0);
            if (n <= 0) { perror("recv"); exit(1); }
            external[i] = in.T;
            printf("[Server] Got T=%.6f from client %d\n", external[i], in.Index);
        }

        // 2) Compute new central: (2*central + sum(externals)) / 6
        float sumExt = 0.0f;
        for (int i = 0; i < NUM_EXTERNALS; i++) sumExt += external[i];
        float newCentral = (2.0f * centralTemp + sumExt) / 6.0f;

        // 3) Check stability: externals AND central change < EPS
        bool externalsStable = true;
        for (int i = 0; i < NUM_EXTERNALS; i++) {
            if (fabsf(external[i] - prevExternal[i]) >= EPS) { externalsStable = false; break; }
        }
        bool centralStable = fabsf(newCentral - centralTemp) < EPS;

        if (externalsStable && centralStable) {
            // 4) Broadcast DONE: set Index = -1, T = final central
            struct msg done; done.T = newCentral; done.Index = -1;
            for (int i = 0; i < NUM_EXTERNALS; i++) {
                if (send(client_socket[i], (const void*)&done, sizeof(done), 0) < 0) perror("send DONE");
            }
            printf("[Server] STABILIZED. Final central T=%.6f\n", newCentral);
            stable = true;
            break;
        }

        // 5) Not stable: broadcast new central (Index=0)
        struct msg out; out.T = newCentral; out.Index = 0;
        for (int i = 0; i < NUM_EXTERNALS; i++) {
            if (send(client_socket[i], (const void*)&out, sizeof(out), 0) < 0) { perror("send"); exit(1); }
        }

        // 6) Prepare next iteration
        for (int i = 0; i < NUM_EXTERNALS; i++) prevExternal[i] = external[i];
        centralTemp = newCentral;
    }

    for (int i = 0; i < NUM_EXTERNALS; i++) close(client_socket[i]);
    close(listen_fd);
    return 0;
}
