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

int main(int argc, char *argv[])
{   
   /* processus intermediaire pour "nettoyer" */
   /* la liste des arguments qu'on va passer */
   /* a la commande a executer finalement  */
   
   /* creation d'une socket pour se connecter au */
   /* au lanceur et envoyer/recevoir les infos */
   /* necessaires pour la phase dsm_init */   

   /*

   int sock;                                    // Déclaration du descripteur de socket
   struct sockaddr_in addr;                     // Structure pour configurer l'adresse
   int yes = 1;                                 // Option pour réutiliser l'adresse
   int *ip = NULL;
    
   sock = socket(AF_INET, SOCK_STREAM, 0);      // Création socket: famille IPv4, type spécifié, protocole par défaut
   if (sock == -1) {                            // Vérification échec création socket
      perror("socket");                         // Affichage erreur
      exit(EXIT_FAILURE);                       // Arrêt du programme
   }
    
   if (setsockopt(sock, SOL_SOCKET,            // Configuration option socket pour
                  SO_REUSEADDR, &yes,          // permettre réutilisation immédiate
                  sizeof(int)) == -1) {        // de l'adresse après fermeture
      perror("setsockopt");                    // Affichage si erreur
      exit(EXIT_FAILURE);                      // Arrêt du programme
    }

   memset(&addr, 0, sizeof(addr));                      // Initialisation structure adresse à 0
   addr.sin_family = AF_INET;                           // Configuration famille IPv4
   addr.sin_port = htons(atoi(argv[2]));                // Configuration port (conversion format réseau)
   addr.sin_addr.s_addr = (ip == NULL) ?                // Si IP null, toutes interfaces,
                        htonl(INADDR_ANY) :            // sinon IP spécifique
                        inet_addr(ip);                 // (conversion format réseau)
   if (connect(sock, addr) == -1) {
      perror("connect to dsmexec");
      exit(EXIT_FAILURE);
   } 

   */

   /* Envoi du nom de machine au lanceur */
   /* Envoi du pid au lanceur (optionnel) */

   /*

   char hostname[MAX_STR];
   gethostname(hostname, MAX_STR);
   if (send(sock, hostname, strlen(hostname) + 1, 0) == -1) {
      perror("send hostname");
      exit(EXIT_FAILURE);
   }

   pid_t pid = getpid();
   if (send(sock, &pid, sizeof(pid), 0) == -1) {
      perror("send pid");
      exit(EXIT_FAILURE);
   }

   */

   /* Creation de la socket d'ecoute pour les */
   /* connexions avec les autres processus dsm */

   /*

   int listen_sock = setup_listen_socket();
   struct sockaddr_in sin;
   socklen_t len = sizeof(sin);
   getsockname(listen_sock, (struct sockaddr *)&sin, &len);
   int listen_port = ntohs(sin.sin_port);

   */

   /* Envoi du numero de port au lanceur */
   /* pour qu'il le propage à tous les autres */
   /* processus dsm */
 
   /*
   
   if (send(sock, &listen_port, sizeof(listen_port), 0) == -1) {
      perror("send listen_port");
      exit(EXIT_FAILURE);
   }

   */
   
   
   /* on execute la bonne commande */
   /* attention au chemin à utiliser ! */

   /************** ATTENTION **************/
   /* vous remarquerez que ce n'est pas   */
   /* ce processus qui récupère son rang, */
   /* ni le nombre de processus           */
   /* ni les informations de connexion    */
   /* (cf protocole dans dsmexec)         */
   /***************************************/
  
   return 0;
}
