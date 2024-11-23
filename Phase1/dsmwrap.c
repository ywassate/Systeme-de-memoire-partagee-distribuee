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


/* variables globales */
#define PAGE_SIZE (4096)    // taille d'une page mémoire
#define MAX_NAME_SIZE (20)  // taille maximum du nom d'une machine repertoriée dans machine_file


int main(int argc, char *argv[])
{   
    if (argc < 5) {                                                                                // si nombre d'argument incorrect                
        fprintf(stderr, "Usage: %s ip_dsmexec port_dsmexec rang programme [args...]\n", argv[0]);  // spécifier usage de la fonction
        exit(EXIT_FAILURE);                                                                        // renvoyer échec
    }

    char *ip_dsmexec = argv[1];                                                                    // récupérer ip envoyé par dsmexec
    int port_dsmexec = atoi(argv[2]);                                                              // récupérer port envoyé par dsmexec
    int rang = atoi(argv[3]);                                                                      // récupérer rang du programme envoyé par dsmexec
    char *programme = argv[4];                                                                     // récupérer nom du programme à exécuter par dsmexec

    printf("[dsmwrap] Démarrage - IP:%s, Port:%d, Rang:%d, Programme:%s\n",                        // afficher message de confirmation
           ip_dsmexec, port_dsmexec, rang, programme);

 
    int sock = socket(AF_INET, SOCK_STREAM, 0);                                                    // création de socket vers dsmexec
    if (sock == -1) {                                                                              // si erreur
        perror("[dsmwrap] socket");                                                                // envoyer message
        exit(EXIT_FAILURE);                                                                        // renvoyer échec
    }

    struct sockaddr_in addr;                                                                       // allouer mémoire de la structure d'adressage
    memset(&addr, 0, sizeof(addr));                                                                // mettre tous les bits à 0
    addr.sin_family = AF_INET;                                                                     // paramétrer la structure
    addr.sin_port = htons(port_dsmexec);                                                           // assigner le port
    if (inet_pton(AF_INET, ip_dsmexec, &addr.sin_addr) != 1) {                                     // si conversion du port non fonctionnelle
        perror("[dsmwrap] inet_pton");                                                             // envoyer message
        exit(EXIT_FAILURE);                                                                        // renvoyer échec
    }

    printf("[dsmwrap] Tentative de connexion à %s:%d\n", ip_dsmexec, port_dsmexec);                // afficher message de confirmation
    if (connect(sock, (struct sockaddr*)&addr, sizeof(addr)) == -1) {                              // si tentative de connexion ratée
        perror("[dsmwrap] connect");                                                               // afficher message
        exit(EXIT_FAILURE);                                                                        // renvoyer échec
    }
    printf("[dsmwrap] Connecté à dsmexec\n");                                                      // afficher message de confirmation

    char hostname[MAX_NAME_SIZE];                                                                  // buffer pour le nom d'hôte
    if (gethostname(hostname, MAX_NAME_SIZE) == -1) {                                              // si impossibilité de récupérer le nom d'hôte
        perror("[dsmwrap] gethostname");                                                           // afficher message
        exit(EXIT_FAILURE);                                                                        // renvoyer échec
    }

    int listen_sock = creer_socket(SOCK_STREAM, NULL, 0);                                          // créer la socket d'écoute pour les autres processus
    if (listen_sock == -1) {                                                                       // si échec de création
        perror("[dsmwrap] creer_socket");                                                          // afficher message
        exit(EXIT_FAILURE);                                                                        // renvoyer échec
    }

    struct sockaddr_in sin;                                                                        // allouer mémoire de la structure d'adressage
    socklen_t len = sizeof(sin);                                                                   // récupérer la taille
    if (getsockname(listen_sock, (struct sockaddr *)&sin, &len) == -1) {                           // si échec d'obtention du nom d'hôte
        perror("[dsmwrap] getsockname");                                                           // afficher message
        exit(EXIT_FAILURE);                                                                        // renvoyer échec
    }
    int listen_port = ntohs(sin.sin_port);                                                         // conversion du port d'écoute


    dsm_proc_conn_t conn_info;                                                                     // allouer la structure de connexion
    conn_info.rank = rang;                                                                         // assigner le rang
    strncpy(conn_info.machine, hostname, MAX_NAME_SIZE);                                           // assigner le nom d'hôte
    conn_info.port_num = listen_port;                                                              // assigner le port d'écoute

    printf("[dsmwrap] Envoi des informations de connexion (rang=%d, machine=%s, port=%d)\n",       // afficher message de confirmation
           conn_info.rank, conn_info.machine, conn_info.port_num);              
    if (send(sock, &conn_info, sizeof(dsm_proc_conn_t), 0) == -1) {                                // si impossibilié d'envoyer les informations de connexion
        perror("[dsmwrap] send conn_info");                                                        // afficher message
        exit(EXIT_FAILURE);                                                                        // renvoyer échec
    }

    int num_procs;                                                                                 // assigner variable pour le nombre total de processus distants 
    if (recv(sock, &num_procs, sizeof(int), 0) == -1) {                                            // si impossibilité de récupérer le nombre
        perror("[dsmwrap] recv num_procs");                                                        // afficher message
        exit(EXIT_FAILURE);                                                                        // envoyer échec
    }

    dsm_proc_conn_t *procs_conn = malloc(num_procs * sizeof(dsm_proc_conn_t));                     // allouer structure de connexion
    for (int i = 0; i < num_procs; i++) {                                                          // pour le nombre de processus distants
        if (recv(sock, &procs_conn[i], sizeof(dsm_proc_conn_t), 0) == -1) {                        // si échec de réception des informations de connexion
            perror("[dsmwrap] recv proc_conn");                                                    // afficher message
            exit(EXIT_FAILURE);                                                                    // envoyer échec
        }
    }

    close(sock);                                                                                   // fermeture de la socket de communication avec dsmexec

    char **new_argv = malloc((argc - 3) * sizeof(char*));                                          // créer le tableau d'argument pour exécuter le programme suivant
    for (int i = 4; i < argc; i++) {                                                               // pour le nombre d'arguments
        new_argv[i-4] = argv[i];                                                                   // assigner les arguments
    }
    new_argv[argc-4] = NULL;                                                                       // signifier la fin du tableau d'arguments

    printf("[dsmwrap] Lancement du programme %s\n", programme);                                    // afficher message de confirmation de lancement du programme suivant
    execvp(programme, new_argv);                                                                   // exécuter nouveau programme avec arguments définis
    perror("[dsmwrap] execvp");                                                                    // si échec, afficher message
    exit(EXIT_FAILURE);                                                                            // renvoyer échec
}