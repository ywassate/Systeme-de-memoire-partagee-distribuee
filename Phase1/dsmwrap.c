#include "common_impl.h"

#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <errno.h>

#define PAGE_SIZE (4096)
#define MAX_NAME_SIZE (20)

int main(int argc, char *argv[])
{   
    if (argc < 5) {
        fprintf(stderr, "Usage: %s ip_dsmexec port_dsmexec rang programme [args...]\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    // Récupération des arguments
    char *ip_dsmexec = argv[1];
    int port_dsmexec = atoi(argv[2]);
    int rang = atoi(argv[3]);
    char *programme = argv[4];

    printf("[dsmwrap] Démarrage - IP:%s, Port:%d, Rang:%d, Programme:%s\n", 
           ip_dsmexec, port_dsmexec, rang, programme);

    // Création socket de connexion vers dsmexec
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock == -1) {
        perror("[dsmwrap] socket");
        exit(EXIT_FAILURE);
    }

    // Configuration de l'adresse de connexion
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port_dsmexec);
    if (inet_pton(AF_INET, ip_dsmexec, &addr.sin_addr) != 1) {
        perror("[dsmwrap] inet_pton");
        exit(EXIT_FAILURE);
    }

    // Connexion à dsmexec
    printf("[dsmwrap] Tentative de connexion à %s:%d\n", ip_dsmexec, port_dsmexec);
    if (connect(sock, (struct sockaddr*)&addr, sizeof(addr)) == -1) {
        perror("[dsmwrap] connect");
        exit(EXIT_FAILURE);
    }
    printf("[dsmwrap] Connecté à dsmexec\n");

    // Envoi des informations au lanceur
    char hostname[MAX_NAME_SIZE];
    if (gethostname(hostname, MAX_NAME_SIZE) == -1) {
        perror("[dsmwrap] gethostname");
        exit(EXIT_FAILURE);
    }

    // Création de la socket d'écoute pour les autres processus DSM
    int listen_sock = creer_socket(SOCK_STREAM, NULL, 0);
    if (listen_sock == -1) {
        perror("[dsmwrap] creer_socket");
        exit(EXIT_FAILURE);
    }

    // Récupération du port d'écoute
    struct sockaddr_in sin;
    socklen_t len = sizeof(sin);
    if (getsockname(listen_sock, (struct sockaddr *)&sin, &len) == -1) {
        perror("[dsmwrap] getsockname");
        exit(EXIT_FAILURE);
    }
    int listen_port = ntohs(sin.sin_port);

    // Préparation des informations de connexion
    dsm_proc_conn_t conn_info;
    conn_info.rank = rang;
    strncpy(conn_info.machine, hostname, MAX_NAME_SIZE);
    conn_info.port_num = listen_port;

    // Envoi des informations de connexion
    printf("[dsmwrap] Envoi des informations de connexion (rang=%d, machine=%s, port=%d)\n",
           conn_info.rank, conn_info.machine, conn_info.port_num);
    if (send(sock, &conn_info, sizeof(dsm_proc_conn_t), 0) == -1) {
        perror("[dsmwrap] send conn_info");
        exit(EXIT_FAILURE);
    }

    // Réception du nombre total de processus
    int num_procs;
    if (recv(sock, &num_procs, sizeof(int), 0) == -1) {
        perror("[dsmwrap] recv num_procs");
        exit(EXIT_FAILURE);
    }

    // Réception des informations de connexion des autres processus
    dsm_proc_conn_t *procs_conn = malloc(num_procs * sizeof(dsm_proc_conn_t));
    for (int i = 0; i < num_procs; i++) {
        if (recv(sock, &procs_conn[i], sizeof(dsm_proc_conn_t), 0) == -1) {
            perror("[dsmwrap] recv proc_conn");
            exit(EXIT_FAILURE);
        }
    }

    // Fermeture de la socket de communication avec dsmexec
    close(sock);

    // Préparation des arguments pour le programme
    char **new_argv = malloc((argc - 3) * sizeof(char*));
    for (int i = 4; i < argc; i++) {
        new_argv[i-4] = argv[i];
    }
    new_argv[argc-4] = NULL;

    // Exécution du programme
    printf("[dsmwrap] Lancement du programme %s\n", programme);
    execvp(programme, new_argv);
    perror("[dsmwrap] execvp");
    exit(EXIT_FAILURE);
}