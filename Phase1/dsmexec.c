#include "common_impl.h"

#include <sys/types.h>       // Définitions de types de base
#include <sys/socket.h>      // Définitions pour les sockets (inclut SOCK_STREAM)
#include <netinet/in.h>      // Définitions pour les adresses Internet (inclut sockaddr_in)
#include <arpa/inet.h>       // Fonctions pour les conversions d'adresses (ex., htons, ntohs)
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <errno.h>

/* variables globales */
#define PAGE_SIZE (4096)    // taille d'une page mémoire
#define MAX_NAME_SIZE (20)  // taille maximum du nom d'une machine repertoriée dans machine_file

/* un tableau gerant les infos d'identification */
/* des processus dsm */
dsm_proc_t *proc_array = NULL; 

/* le nombre de processus effectivement crees */
volatile int num_procs_creat = 0;

void usage(void) {                                                             // fonction pour signifier l'usage du script dsmexec          
  fprintf(stdout,"Usage : dsmexec machine_file executable arg1 arg2 ...\n");   // écrire nom du script, arguments
  fflush(stdout);                                                              // vider le buffer
  exit(EXIT_FAILURE);                                                          // s'arrêter
}


int main(int argc, char *argv[]) {
    if (argc < 3){                                                              // si pas le bon nombre d'arguments
        usage();                                                                // signifier usage de la fonction
    }
    pid_t pid;                                                                  // variable de stockage des pid des processus enfants
    int num_procs = 0;                                                          // nombre de processus à créer
    int i;                                                                      // variable pour les boucle for
    
    signal(SIGCHLD, sigchld_handler);                                           // gérer les processus zombies

    char **machines = read_machine_file(argv[1]);                               // tableau qui contient le nombre de processus puis les noms des machines
    num_procs = atoi(machines[0]);                                              // récupérer le nombre de processus

    proc_array = malloc(num_procs * sizeof(dsm_proc_t));                        // allouer la mémoire pour proc_array
    if (!proc_array) {                                                          // si échec
        perror("malloc proc_array");                                            // afficher message
        exit(EXIT_FAILURE);                                                     // s'arrêter
    }

    for(i = 0; i < num_procs; i++) {                                            // initialisation du tableau proc array
        proc_array[i].pid = -1;                                                 // assigner le pid
        proc_array[i].rank = i;                                                 // assigner le rang
        proc_array[i].machine_name = strdup(machines[i + 1]);                   // assigner le nom de la machine
    }

    int listen_socket = creer_socket(SOCK_STREAM, NULL, 0);                     // paramètrer la socket d'écoute
    struct sockaddr_in sin;                                                     // structure d'adressage
    socklen_t len = sizeof(sin);                                                // taille de la structure

    if (getsockname(listen_socket, (struct sockaddr *)&sin, &len) == -1) {      // remplir la structure d'adressage
        perror("getsockname");                                                  // si échec, envoyer message
        exit(EXIT_FAILURE);                                                     // s'arrêter
    }
   
    int listen_port = ntohs(sin.sin_port);                                      // convertir le port d'écoute
    char listen_ip[INET_ADDRSTRLEN];                                            // buffer pour l'adresse IP
    inet_ntop(AF_INET, &(sin.sin_addr), listen_ip, INET_ADDRSTRLEN);            // récupérer l'adresse IP

    printf("[DEBUG] Socket d'écoute créée sur %s:%d\n", listen_ip, listen_port);

    for(i = 0; i < num_procs ; i++) {                                           // pour le nombre de processus
        int stdout_pipe[2], stderr_pipe[2];                                     // initialiser les pipes
        
        if (pipe(stdout_pipe) == -1 || pipe(stderr_pipe) == -1) {               // si échec de création
            perror("pipe");                                                     // envoyer message
            exit(EXIT_FAILURE);                                                 // s'arrêter
        }

        pid = fork();                                                           // créer processus enfant
        if (pid == -1) {                                                        // si échec
            perror("fork");                                                     // envoyer message
            exit(EXIT_FAILURE);                                                 // s'arrêter
        }
      
        if (0 == pid) {                                                         // si processus enfant

            close(stdout_pipe[0]);                                              // fermer lecture stdout
            close(stderr_pipe[0]);                                              // fermer lecture stderr

            close(fileno(stdout));                                              // fermer stdout
            if (dup(stdout_pipe[1]) == -1) {                                    // dupliquer stdout
                perror("dup2 stdout");                                          // si échec
                exit(EXIT_FAILURE);                                             // s'arrêter
            }

            close(fileno(stderr));                                              // fermer stderr
            if (dup(stderr_pipe[1]) == -1) {                                    // dupliquer stderr
                perror("dup2 stderr");                                          // si échec
                exit(EXIT_FAILURE);                                             // s'arrêter
            }

            char *dsm_bin = getenv("DSM_BIN");                                  // récupérer DSM_BIN
            if (!dsm_bin) {                                                     // si non défini
                fprintf(stderr, "[ERROR] DSM_BIN non défini\n");                // message d'erreur
                exit(EXIT_FAILURE);                                             // s'arrêter
            }

            char port_str[10];                                                  // chaîne pour le port
            sprintf(port_str, "%d", listen_port);                               // convertir port en chaîne

            char rank_str[10];                                                  // chaîne pour le rang
            sprintf(rank_str, "%d", i);                                         // convertir rang en chaîne


            char remote_cmd[4096];                                              // buffer pour la ligne de commande du processus distant
            snprintf(remote_cmd, sizeof(remote_cmd),                            // créer la ligne de commande
                "export DSM_BIN=%s; "                                           // exporter variable
                "export PATH=$DSM_BIN:$PATH; "                                  // exporter le PATH
                "%s/dsmwrap %s %d %d %s",                                       // ligne d'exécution de dsmwrap et arguments
                dsm_bin,                                                        // variable dsm_bin
                dsm_bin,                                                        // variable dsm_bin
                "0.0.0.0",                                                      // adresse IP
                listen_port,                                                    // port d'écoute
                i,                                                              // rang du processus
                argv[2]);                                                       // programme à exécuter


            printf("[DEBUG][Enfant %d] DSM_BIN = %s\n", i, dsm_bin);            // afficher message de confirmation
            printf("[DEBUG][Enfant %d] Commande complète: %s\n", i, remote_cmd);

            char *ssh_args[] = {                                                // créer tableau d'arguments pour la commande ssh
                "ssh",                                                          // ssh
                "-v",                                                           // ajout du mode verbose pour SSH
                proc_array[i].machine_name,                                     // machine sur laquelle se connecter
                remote_cmd,                                                     // ligne de commande à exécuter
                NULL                                                            // signifier la fin du tableau d'arguments
            };

            printf("[DEBUG][Enfant %d] Exécution SSH avec arguments:\n", i);    // afficher le rang du processus
            for(int j = 0; ssh_args[j] != NULL; j++) {                          // pour l'ensemble du tableau
            printf("  arg[%d] = %s\n", j, ssh_args[j]);                         // afficher l'argument
            }

            execvp("ssh", ssh_args);                                            // exécuter ssh
            perror("execvp");                                                   // si échec
            exit(EXIT_FAILURE);                                                 // s'arrêter
        }

        else {
            proc_array[i].pid = pid;                                            // stocker pid
            proc_array[i].stdout_fd = stdout_pipe[0];                           // stocker fd stdout
            proc_array[i].stderr_fd = stderr_pipe[0];                           // stocker fd stderr
            close(stdout_pipe[1]);                                              // fermer écriture stdout
            close(stderr_pipe[1]);                                              // fermer écriture stderr
      
            printf("[DEBUG] Processus %d créé avec pid %d\n", i, pid);          // message de confirmation
        }
    }


    printf("\n[DEBUG] === Attente des connexions ===\n");                       // message d'attente

    for(i = 0; i < num_procs ; i++) {                                           // pour le nombre de processus
        struct sockaddr_in client_addr;                                         // structure client
        socklen_t client_len = sizeof(client_addr);                             // taille structure
       
        printf("[DEBUG] Attente connexion processus %d...\n", i);               // message d'attente
       
        int client_sock = accept(listen_socket,                                 // accepter connexion
                            (struct sockaddr *)&client_addr, 
                            &client_len);
        if (client_sock < 0) {                                                  // si erreur
            if (errno == EINTR) {                                               // si interruption
                printf("[DEBUG] Accept interrompu, nouvelle tentative\n");      // message
                i--;                                                            // réessayer
                continue;                                                       // continuer boucle
            }
            perror("accept");                                                   // si autre erreur
            exit(EXIT_FAILURE);                                                 // s'arrêter
        }

        printf("[DEBUG] Connexion acceptée pour processus %d\n", i);            // message de succès

        
        dsm_proc_conn_t conn_info;                                              // structure connexion
        if (recv(client_sock, &conn_info, sizeof(dsm_proc_conn_t), 0) < 0) {    // recevoir infos
            perror("recv");                                                     // si erreur
            exit(EXIT_FAILURE);                                                 // s'arrêter
        }

        printf("[DEBUG] Infos reçues du processus %d (rank=%d)\n",              // message de réception
                i, conn_info.rank);


        if (send(client_sock, &num_procs, sizeof(int), 0) < 0) {                // envoyer nombre processus
            perror("send num_procs");                                           // si erreur
            exit(EXIT_FAILURE);                                                 // s'arrêter
        }

        if (send(client_sock, &i, sizeof(int), 0) < 0) {                        // envoyer rang
            perror("send rank");                                                // si erreur
            exit(EXIT_FAILURE);                                                 // s'arrêter
        }

        for (int j = 0; j < num_procs; j++) {                                   // pour chaque processus
            if (send(client_sock, &proc_array[j].connect_info,                  // envoyer infos connexion
                    sizeof(dsm_proc_conn_t), 0) < 0) {
                perror("send conn_info");                                       // si erreur
                exit(EXIT_FAILURE);                                             // s'arrêter
            }
        }

        proc_array[conn_info.rank].connect_info = conn_info;                    // stocker infos
        close(client_sock);                                                     // fermer socket
       
        printf("[DEBUG] Configuration terminée pour processus %d\n", i);        // message de fin
    }

    printf("[DEBUG] === Fin des initialisations, début surveillance E/S ===\n");// message début E/S

    fd_set readfds;                                                             // ensemble descripteurs
    int max_fd = 0;                                                             // fd maximum
    int active_fds = num_procs * 2;                                             // nombre fd actifs
 
    while (active_fds > 0) {                                                    // tant qu'il y a des fd actifs
        FD_ZERO(&readfds);                                                      // vider ensemble
        max_fd = 0;                                                             // réinitialiser max

        for (i = 0; i < num_procs; i++) {                                       // pour chaque processus
            if (proc_array[i].stdout_fd >= 0) {                                 // si stdout actif
                FD_SET(proc_array[i].stdout_fd, &readfds);                      // ajouter à l'ensemble
                max_fd = (proc_array[i].stdout_fd > max_fd) ?  
                    proc_array[i].stdout_fd : max_fd;                           // mettre à jour max
            }
            if (proc_array[i].stderr_fd >= 0) {                                 // si stderr actif
                FD_SET(proc_array[i].stderr_fd, &readfds);                      // ajouter à l'ensemble
                max_fd = (proc_array[i].stderr_fd > max_fd) ? 
                        proc_array[i].stderr_fd : max_fd;                       // mettre à jour max
            }
        }

        if (max_fd == 0) break;                                                 // si plus de fd, sortir

        int ret = select(max_fd + 1, &readfds, NULL, NULL, NULL);               // attendre données
        if (ret < 0) {                                                          // si erreur
            if (errno == EINTR) continue;                                       // si interruption, continuer
            perror("select");                                                   // si autre erreur
            break;                                                              // sortir
        }

        for (i = 0; i < num_procs; i++) {                                       // pour chaque processus
            char buffer[1024];                                                  // buffer lecture
            int n;                                                              // nombre octets lus

            if (proc_array[i].stdout_fd >= 0 &&                                 // si stdout actif
                FD_ISSET(proc_array[i].stdout_fd, &readfds)) {                  // et données disponibles
                n = read(proc_array[i].stdout_fd, buffer, sizeof(buffer) - 1);  // lire données
                if (n > 0) {                                                    // si données lues
                    buffer[n] = '\0';                                           // terminer chaîne
                    printf("[Proc %d : %s : stdout] %s",                        // afficher données
                            i, proc_array[i].machine_name, buffer);
                } else if (n == 0) {                                            // si fin fichier
                    close(proc_array[i].stdout_fd);                             // fermer fd
                    proc_array[i].stdout_fd = -1;                               // marquer inactif
                    active_fds--;                                               // décrémenter actifs
                }
            }

            if (proc_array[i].stderr_fd >= 0 &&                                 // si stderr actif
                FD_ISSET(proc_array[i].stderr_fd, &readfds)) {                  // et données disponibles
                n = read(proc_array[i].stderr_fd, buffer, sizeof(buffer) - 1);  // lire données
                if (n > 0) {                                                    // si données lues
                    buffer[n] = '\0';                                           // terminer chaîne
                    printf("[Proc %d : %s : stderr] %s",                        // afficher données
                        i, proc_array[i].machine_name, buffer);
                } else if (n == 0) {                                            // si fin fichier
                    close(proc_array[i].stderr_fd);                             // fermer fd
                    proc_array[i].stderr_fd = -1;                               // marquer inactif
                    active_fds--;                                               // décrémenter actifs
                }
            }
        }
    }

    printf("[DEBUG] === Nettoyage final ===\n");                                // message nettoyage

    close(listen_socket);                                                       // fermer socket écoute
    for (i = 0; i < num_procs; i++) {                                           // pour chaque processus
        if (proc_array[i].stdout_fd >= 0) close(proc_array[i].stdout_fd);       // fermer stdout si actif
        if (proc_array[i].stderr_fd >= 0) close(proc_array[i].stderr_fd);       // fermer stderr si actif
        free(proc_array[i].machine_name);                                       // libérer nom machine
    }

    for (i = 0; i <= num_procs; i++) {                                          // pour chaque machine
        free(machines[i]);                                                      // libérer nom
    }
    free(machines);                                                             // libérer tableau machines
    free(proc_array);                                                           // libérer tableau processus

    return EXIT_SUCCESS;                                                        // terminer normalement
}