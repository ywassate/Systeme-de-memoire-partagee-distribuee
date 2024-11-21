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
#define PAGE_SIZE (4096)
#define MAX_NAME_SIZE (20)

dsm_proc_t *proc_array = NULL; 
volatile int num_procs_creat = 0;

void usage(void)
{          
  fprintf(stdout,"Usage : dsmexec machine_file executable arg1 arg2 ...\n");
  fflush(stdout);
  exit(EXIT_FAILURE);
}

int main(int argc, char *argv[])
{
   if (argc < 3){
     usage();
   }

   pid_t pid;
   int num_procs = 0;
   int i;

   signal(SIGCHLD, sigchld_handler);

   char **machines = read_machine_file(argv[1]);
   num_procs = atoi(machines[0]);
   printf("[DEBUG] Nombre de processus à créer : %d\n", num_procs);

   proc_array = malloc(num_procs * sizeof(dsm_proc_t));
   if (!proc_array) {
       perror("malloc proc_array");
       exit(EXIT_FAILURE);
   }

   for(i = 0; i < num_procs; i++) {
       proc_array[i].pid = -1;
       proc_array[i].rank = i;
       proc_array[i].machine_name = strdup(machines[i + 1]);
   }

   int listen_socket = creer_socket(SOCK_STREAM, NULL, 0);
   struct sockaddr_in sin;
   socklen_t len = sizeof(sin);

   if (getsockname(listen_socket, (struct sockaddr *)&sin, &len) == -1) {
       perror("getsockname");
       exit(EXIT_FAILURE);
   }
   
   // Récupérer le port et l'IP
int listen_port = ntohs(sin.sin_port);
char listen_ip[INET_ADDRSTRLEN];
inet_ntop(AF_INET, &(sin.sin_addr), listen_ip, INET_ADDRSTRLEN);

printf("[DEBUG] Socket d'écoute créée sur %s:%d\n", listen_ip, listen_port);

   for(i = 0; i < num_procs ; i++) {
      int stdout_pipe[2], stderr_pipe[2];
      
      printf("[DEBUG] Création des pipes pour processus %d\n", i);
      if (pipe(stdout_pipe) == -1 || pipe(stderr_pipe) == -1) {
          perror("pipe");
          exit(EXIT_FAILURE);
      }
      printf("[DEBUG] Pipes créés - stdout[%d, %d], stderr[%d, %d]\n", 
             stdout_pipe[0], stdout_pipe[1], stderr_pipe[0], stderr_pipe[1]);

      pid = fork();
      if (pid == -1) {
          perror("fork");
          exit(EXIT_FAILURE);
      }
      
      if (pid == 0) {
           printf("[DEBUG][Enfant %d] Fermeture des extrémités de lecture des pipes\n", i);
           close(stdout_pipe[0]);
           close(stderr_pipe[0]);
           printf("[DEBUG][Enfant %d] Descripteurs avant dup2: stdout=%d, stderr=%d\n", 
                  i, fileno(stdout), fileno(stderr));

           printf("[DEBUG][Enfant %d] Duplication stdout_pipe[1](%d) vers STDOUT_FILENO(%d)\n", 
                  i, stdout_pipe[1], STDOUT_FILENO);
           if (dup2(stdout_pipe[1], STDOUT_FILENO) == -1) {
               perror("dup2 stdout");
               exit(EXIT_FAILURE);
           }

           printf("[DEBUG][Enfant %d] Duplication stderr_pipe[1](%d) vers STDERR_FILENO(%d)\n", 
                  i, stderr_pipe[1], STDERR_FILENO);
           if (dup2(stderr_pipe[1], STDERR_FILENO) == -1) {
               perror("dup2 stderr");
               exit(EXIT_FAILURE);
           }

           printf("[DEBUG][Enfant %d] Descripteurs après dup2: stdout=%d, stderr=%d\n", 
                  i, fileno(stdout), fileno(stderr));

           char *dsm_bin = getenv("DSM_BIN");
           if (!dsm_bin) {
               fprintf(stderr, "[ERROR] DSM_BIN non défini\n");
               exit(EXIT_FAILURE);
           }
           printf("[DEBUG][Enfant %d] DSM_BIN = %s\n", i, dsm_bin);

           char port_str[10];
           sprintf(port_str, "%d", listen_port);

           char rank_str[10];
           sprintf(rank_str, "%d", i);

           // Dans la partie où vous créez la commande SSH dans dsmexec.c
char remote_cmd[4096];
snprintf(remote_cmd, sizeof(remote_cmd),
         "export DSM_BIN=%s; "
         "export PATH=$DSM_BIN:$PATH; "
         "%s/dsmwrap %s %d %d %s",
         dsm_bin,
         dsm_bin, 
         "0.0.0.0",  // On utilise 0.0.0.0 pour le moment
         listen_port,
         i,          // rang
         argv[2]);   // programme à exécuter

// Ajout de messages de debug
printf("[DEBUG][Enfant %d] DSM_BIN = %s\n", i, dsm_bin);
printf("[DEBUG][Enfant %d] Commande complète: %s\n", i, remote_cmd);

char *ssh_args[] = {
    "ssh",
    "-v",           // Ajout du mode verbose pour SSH
    proc_array[i].machine_name,
    remote_cmd,
    NULL
};

// Afficher les arguments SSH
printf("[DEBUG][Enfant %d] Exécution SSH avec arguments:\n", i);
for(int j = 0; ssh_args[j] != NULL; j++) {
    printf("  arg[%d] = %s\n", j, ssh_args[j]);
}

           execvp("ssh", ssh_args);
           perror("execvp");
           exit(EXIT_FAILURE);
      }

      proc_array[i].pid = pid;
      proc_array[i].stdout_fd = stdout_pipe[0];
      proc_array[i].stderr_fd = stderr_pipe[0];
      printf("[DEBUG][Parent] Fermeture des extrémités d'écriture pour processus %d\n", i);
      close(stdout_pipe[1]);
      close(stderr_pipe[1]);
      
      printf("[DEBUG][Parent] Processus %d créé avec pid %d, stdout_fd=%d, stderr_fd=%d\n", 
             i, pid, stdout_pipe[0], stderr_pipe[0]);
   }


   printf("\n[DEBUG] === Attente des connexions ===\n");                       // message d'attente

   // Acceptation des connexions des processus distants
   for(i = 0; i < num_procs ; i++) {
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

       // Réception des informations
       dsm_proc_conn_t conn_info;                                              // structure connexion
       if (recv(client_sock, &conn_info, sizeof(dsm_proc_conn_t), 0) < 0) {    // recevoir infos
           perror("recv");                                                     // si erreur
           exit(EXIT_FAILURE);                                                 // s'arrêter
       }

       printf("[DEBUG] Infos reçues du processus %d (rank=%d)\n",              // message de réception
              i, conn_info.rank);

       // Envoi du nombre de processus
       if (send(client_sock, &num_procs, sizeof(int), 0) < 0) {                // envoyer nombre processus
           perror("send num_procs");                                           // si erreur
           exit(EXIT_FAILURE);                                                 // s'arrêter
       }

       // Envoi du rang
       if (send(client_sock, &i, sizeof(int), 0) < 0) {                        // envoyer rang
           perror("send rank");                                                // si erreur
           exit(EXIT_FAILURE);                                                 // s'arrêter
       }

       // Envoi des infos de connexion de tous les processus
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

   printf("[DEBUG] === Fin des initialisations, début surveillance E/S ===\n"); // message début E/S

   // Boucle de surveillance des E/S
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
                        proc_array[i].stdout_fd : max_fd;                      // mettre à jour max
           }
           if (proc_array[i].stderr_fd >= 0) {                                 // si stderr actif
               FD_SET(proc_array[i].stderr_fd, &readfds);                      // ajouter à l'ensemble
               max_fd = (proc_array[i].stderr_fd > max_fd) ? 
                        proc_array[i].stderr_fd : max_fd;                      // mettre à jour max
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

   // Nettoyage
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