#include "common_impl.h"

#include <sys/types.h>       // Définitions de types de base
#include <sys/socket.h>      // Définitions pour les sockets (inclut SOCK_STREAM)
#include <netinet/in.h>      // Définitions pour les adresses Internet (inclut sockaddr_in)
#include <arpa/inet.h>       // Fonctions pour les conversions d'adresses (ex., htons, ntohs)
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* variables globales */
#define NUM_PROCS (3)       // nombre de processus enfants créés par dsmexec
#define PAGE_SIZE (4096)    // taille d'une page mémoire
#define MAX_NAME_SIZE (20)  // taille maximum du nom d'une machine repertoriée dans machine_file

/* un tableau gerant les infos d'identification */
/* des processus dsm */
dsm_proc_t *proc_array = NULL; 

/* le nombre de processus effectivement crees */
volatile int num_procs_creat = 0;

void usage(void)
{
  fprintf(stdout,"Usage : dsmexec machine_file executable arg1 arg2 ...\n");
  fflush(stdout);
  exit(EXIT_FAILURE);
}


void sigchld_handler(int sig) {              // gérer les processus zombies
    while (waitpid(-1, NULL, WNOHANG) > 0);  // attendre la fin de tous les processus
}

int creer_socket(int type, const char *ip, int port) {
      int sock;
      struct sockaddr_in addr;

      // Création de la socket
      sock = socket(AF_INET, type, 0);
      if (sock == -1) {
         perror("socket");
         exit(EXIT_FAILURE);
      }

      // Initialisation de l'adresse de la socket
      memset(&addr, 0, sizeof(addr));
      addr.sin_family = AF_INET;
      addr.sin_port = htons(port);

      // Gestion de l'adresse IP
      if (ip == NULL) {
         addr.sin_addr.s_addr = htonl(INADDR_ANY);  // Accepter les connexions de n'importe quelle adresse
      } else {
         if (inet_pton(AF_INET, ip, &addr.sin_addr) <= 0) {  // Convertit l'IP de texte à binaire
               perror("inet_pton");
               close(sock);
               exit(EXIT_FAILURE);
         }
      }

      // Liaison de la socket à l'adresse 
      if (bind(sock, (struct sockaddr *)&addr, sizeof(addr)) == -1) {
         perror("bind");
         close(sock);
         exit(EXIT_FAILURE);
      }

      // Mettre la socket en écoute si c'est une socket TCP
      if (type == SOCK_STREAM) {
         if (listen(sock, 5) == -1) {
               perror("listen");
               close(sock);
               exit(EXIT_FAILURE);
         }
      }

      return sock;
   }
/* Création de la socket d'écoute */
   int setup_listen_socket(void) {
    int listen_socket = creer_socket(SOCK_STREAM, NULL, 0);
    if (listen_socket < 0) {
        perror("creer_socket");
        exit(EXIT_FAILURE);
    }
    listen(listen_socket, NUM_PROCS);
    return listen_socket;
}



//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////  Partie Modifiée  /////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////



char **read_machine_file(char *argv) {                   // fonction pour lire les lignes de machine_file              

   int fd = open(argv, O_RDONLY);                        // ouverture lecture seule
   int count_line = 0;                                   // compteur de lignes
   int char_in_line = 0;                                 // compteur de caractères dans la ligne
   char explorer;                                        // caractère qui va 'explorer' le fichier avec read
   ssize_t read_result;                                  // résultat de l'appel à read

   while ((read_result = read(fd, &explorer, 1)) > 0) {  // tant que read fonctionne (pas de EOF)
      if (explorer != '\n') {                            // si pas de saut de ligne
         char_in_line++;                                 // incrémenter nombre de caractères dans la ligne
      } else {
         if (char_in_line > 0) {                         // ligne non vide ET saut de ligne trouvé
            count_line++;                                // compter une ligne de plus
         }
         char_in_line = 0;                               // réinitialiser pour la ligne suivante
      }
   }

   close(fd);                                            // fermer le fichier après la lecture

   fd = open(argv, O_RDONLY);                            // réouvrir le fichier pour le lire à nouveau et stocker les mots

   char **tab = malloc((count_line+1) * sizeof(char *)); // allouer un tableau pour stocker les mots (chaque mot a une taille MAX_SIZE)
   for (int i = 0; i < count_line+1; i++) {
      tab[i] = malloc(MAX_NAME_SIZE * sizeof(char));     // allouer un espace pour chaque mot
   }

   int index_tab = 1;                                    // position dans le tableau
   int char_index = 0;                                   // position dans la ligne
                                                         // lire les mots ligne par ligne et les stocker dans tab
   char_in_line = 0;                                     // réinitialiser le compteur de caractères dans la ligne

   while ((read_result = read(fd, &explorer, 1)) > 0) {  // tant que read fonctionne (pas de EOF)
      if (explorer != '\n') {                            // s'il n'y a pas de saut de ligne
         tab[index_tab][char_index] = explorer;          // Stocker le caractère
         char_in_line++;                                 // incrémenter le nombre de caractère dans la ligne
         char_index++;                                   // incrémenter la position dans la ligne
      } else {
         if (char_in_line > 0) {                         // si la ligne n'est pas vide
            tab[index_tab][char_index] = '\0';           // terminer la chaîne
            index_tab++;                                 // passer au mot suivant
            char_in_line = 0;                            // réinitialiser le compteur de caractère
            char_index = 0;                              // réanitialiser la position dans la ligne
         }
      }
   }
   sprintf(tab[0], "%d\n", index_tab-1);                 // rajouter le nombre de lignes lues au début du tableau

   for (int i = 1; i < index_tab; i++) {                 // afficher le contenu du tableau
      printf("ligne %i lue dans machine_file %s\n", i, tab[i]); 
   }

   close(fd);                                            // fermer le processus

   return tab;                                           // renvoyer le tableau
}





//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////  Fin de Partie Modifiée  //////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////



/*******************************************************/
/*********** ATTENTION : BIEN LIRE LA STRUCTURE DU *****/
/*********** MAIN AFIN DE NE PAS AVOIR A REFAIRE *******/
/*********** PLUS TARD LE MEME TRAVAIL DEUX FOIS *******/
/*******************************************************/

int main(int argc, char *argv[])
{
   if (argc < 3){                                              // si pas le bon nombre d'arguments
     usage();                                                  // signifier usage de la fonction
   } else {                                                    // si usage correct
   pid_t pid;                                                  // variable de stockage des pid des processus enfants
   int num_procs = 0;                                          // nombre de processus à créer
   int i;                                                      // variable pour les boucle for

   /* Mise en place d'un traitant pour recuperer les fils zombies*/      
   /* XXX.sa_handler = sigchld_handler; */
   signal(SIGCHLD, sigchld_handler);                           // gérer les processus zombies

   /* lecture du fichier de machines */
   /* 1- on recupere le nombre de processus a lancer */
   /* 2- on recupere les noms des machines : le nom de */
   /* la machine est un des elements d'identification */

   char **tab = read_machine_file(argv[1]);                    // tableau qui contient le nombre de processus puis les noms des machines où exécuter le programme
   num_procs = atoi(tab[0]);                                   // récupérer le nombre de processus
   printf("nombre de processus à créer : %d\n", num_procs);    // printf de vérification

   // Allouez la mémoire pour proc_array
   proc_array = malloc(num_procs * sizeof(dsm_proc_t));
   if (proc_array == NULL) {
      perror("malloc");
      exit(EXIT_FAILURE);
   }
   /* creation de la socket d'ecoute */
   /* + ecoute effective */ 
   
   int listen_socket = setup_listen_socket();
   struct sockaddr_in sin;
   socklen_t len = sizeof(sin);

   if (getsockname(listen_socket, (struct sockaddr *)&sin, &len) == -1) {
      perror("getsockname");
      exit(EXIT_FAILURE);
   }
   
   int listen_port = ntohs(sin.sin_port);
   printf("Port d'écoute : %d\n", listen_port);

   for(i = 0; i < num_procs ; i++) {
      int stdout_pipe[2], stderr_pipe[2];
      if (pipe(stdout_pipe) == -1 || pipe(stderr_pipe) == -1) {
            perror("pipe");
            exit(EXIT_FAILURE);
      }

      pid = fork();
      if (pid == -1) {
            perror("fork");
            exit(EXIT_FAILURE);
      }
      
      if (pid == 0) {
            close(stdout_pipe[0]);
            if (dup2(stdout_pipe[1], STDOUT_FILENO) == -1) {
               perror("dup2 stdout");
               exit(EXIT_FAILURE);
            }

            close(stderr_pipe[0]);
            if (dup2(stderr_pipe[1], STDERR_FILENO) == -1) {
               perror("dup2 stderr");
               exit(EXIT_FAILURE);
            }

            char port_str[10];
            sprintf(port_str, "%d", listen_port);

            char *executable = argv[2];
            char *newargv[] = {
               "ssh",
               tab[i + 1],
               "dsmwrap",
               executable,
               port_str,
               NULL
            };

            execvp("ssh", newargv);
            perror("execvp");
            exit(EXIT_FAILURE);

      } else if (pid > 0) {
            printf("PID du processus: %i\n", pid);
            num_procs_creat++;
      }
   }
   fd_set readfds;
   FD_ZERO(&readfds);
   int max_fd = 0;

   for (i = 0; i < num_procs; i++) {
      FD_SET(proc_array[i].stdout_fd, &readfds);
      FD_SET(proc_array[i].stderr_fd, &readfds);
      if (proc_array[i].stdout_fd > max_fd) max_fd = proc_array[i].stdout_fd;
      if (proc_array[i].stderr_fd > max_fd) max_fd = proc_array[i].stderr_fd;
   }

   if (select(max_fd + 1, &readfds, NULL, NULL, NULL) > 0) {
    printf("Data available on pipes.\n");
    for (i = 0; i < num_procs; i++) {
        if (FD_ISSET(proc_array[i].stdout_fd, &readfds)) {
            char buffer[1024];
            int n = read(proc_array[i].stdout_fd, buffer, sizeof(buffer) - 1);
            if (n > 0) {
                buffer[n] = '\0';
                printf("[Proc %d : localhost : stdout] %s\n", i, buffer);
            }
        }
        if (FD_ISSET(proc_array[i].stderr_fd, &readfds)) {
            char buffer[1024];
            int n = read(proc_array[i].stderr_fd, buffer, sizeof(buffer) - 1);
            if (n > 0) {
                buffer[n] = '\0';
                printf("[Proc %d : localhost : stderr] %s\n", i, buffer);
            }
        }
    }
}
for (i = 0; i < num_procs; i++) {
    waitpid(proc_array[i].pid, NULL, 0);
}


   // Libération de la mémoire pour le tableau des machines
   for (int i = 0; i <= num_procs; i++) {
      free(tab[i]);
   }
   free(tab);

   
   for(i = 0; i < num_procs ; i++){                          // pour le nombre de processus à créer
	
	/* on accepte les connexions des processus dsm */
	
	/*  On recupere le nom de la machine distante */
	/* les chaines ont une taille de MAX_STR */

	/* On recupere le pid du processus distant  (optionnel)*/
	
	/* On recupere le numero de port de la socket */
	/* d'ecoute des processus distants */
        /* cf code de dsmwrap.c */  
   }

     /***********************************************************/ 
     /********** ATTENTION : LE PROTOCOLE D'ECHANGE *************/
     /********** DECRIT CI-DESSOUS NE DOIT PAS ETRE *************/
     /********** MODIFIE, NI DEPLACE DANS LE CODE   *************/
     /***********************************************************/
     
     /* 1- envoi du nombre de processus aux processus dsm*/
     /* On envoie cette information sous la forme d'un ENTIER */
     /* (IE PAS UNE CHAINE DE CARACTERES */
     
     /* 2- envoi des rangs aux processus dsm */
     /* chaque processus distant ne reçoit QUE SON numéro de rang */
     /* On envoie cette information sous la forme d'un ENTIER */
     /* (IE PAS UNE CHAINE DE CARACTERES */
     
     /* 3- envoi des infos de connexion aux processus */
     /* Chaque processus distant doit recevoir un nombre de */
     /* structures de type dsm_proc_conn_t égal au nombre TOTAL de */
     /* processus distants, ce qui signifie qu'un processus */
     /* distant recevra ses propres infos de connexion */
     /* (qu'il n'utilisera pas, nous sommes bien d'accords). */

     /***********************************************************/
     /********** FIN DU PROTOCOLE D'ECHANGE DES DONNEES *********/
     /********** ENTRE DSMEXEC ET LES PROCESSUS DISTANTS ********/
     /***********************************************************/
     
     /* gestion des E/S : on recupere les caracteres */
     /* sur les tubes de redirection de stdout/stderr */     
     /* while(1)
         {
            je recupere les infos sur les tubes de redirection
            jusqu'à ce qu'ils soient inactifs (ie fermes par les
            processus dsm ecrivains de l'autre cote ...)
       
         };
      */

     /* on attend les processus fils */
     
     /* on ferme les descripteurs proprement */
     
     /* on ferme la socket d'ecoute */
  }   
   exit(EXIT_SUCCESS);  
}

