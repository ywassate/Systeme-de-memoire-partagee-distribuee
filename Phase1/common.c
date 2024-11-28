#include "common_impl.h"


/* variables globales */
#define PAGE_SIZE (4096)    // taille d'une page mémoire
#define MAX_NAME_SIZE (20)  // taille maximum du nom d'une machine repertoriée dans machine_file


/* Vous pouvez ecrire ici toutes les fonctions */
/* qui pourraient etre utilisees par le lanceur */
/* et le processus intermediaire. N'oubliez pas */
/* de declarer le prototype de ces nouvelles */
/* fonctions dans common_impl.h */



//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////  Partie Modifiée  /////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////



void sigchld_handler(int sig) {                                                                  // gérer les processus zombies
    int status;                                                                                  // statut de la sortie
    pid_t pid;                                                                                   // pid du processus à attendre
    while ((pid = waitpid(-1, &status, WNOHANG)) > 0) {                                          // attendre la fin des processus zombie
    }
}


int creer_socket(int type, const char *ip, int port) {  // création de socket d'écoute
    int sock;                                           // déclaration du descripteur de socket
    struct sockaddr_in addr;                            // structure pour configurer l'adresse
    int yes = 1;                                        // option pour réutiliser l'adresse
    
    sock = socket(AF_INET, type, 0);                    // création socket: famille IPv4, type spécifié, protocole par défaut
    if (sock == -1) {                                   // vérification échec création socket
        perror("socket");                               // affichage erreur
        exit(EXIT_FAILURE);                             // arrêt du programme
    }
    
    if (setsockopt(sock, SOL_SOCKET,                    // configuration option socket pour
                   SO_REUSEADDR, &yes,                  // permettre réutilisation immédiate
                   sizeof(int)) == -1) {                // de l'adresse après fermeture
        perror("setsockopt");                           // affichage si erreur
        exit(EXIT_FAILURE);                             // arrêt du programme
    }

    memset(&addr, 0, sizeof(addr));                     // initialisation structure adresse à 0
    addr.sin_family = AF_INET;                          // configuration famille IPv4
    addr.sin_port = htons(port);                        // configuration port (conversion format réseau)
    addr.sin_addr.s_addr = (ip == NULL) ?               // si IP null, toutes interfaces,
                          htonl(INADDR_ANY) :           // sinon IP spécifique
                          inet_addr(ip);                // (conversion format réseau)
    
    if (bind(sock, (struct sockaddr *)&addr,            // association socket avec adresse
             sizeof(addr)) == -1) {                     // locale configurée
        perror("bind");                                 // affichage si erreur
        close(sock);                                    // fermeture socket
        exit(EXIT_FAILURE);                             // arrêt du programme
    }
    
    if (type == SOCK_STREAM) {                          // si socket TCP (mode connecté)
        if (listen(sock, 10) == -1) {                   // mise en écoute avec queue 10 connexions
            perror("listen");                           // affichage si erreur
            close(sock);                                // fermeture socket
            exit(EXIT_FAILURE);                         // arrêt du programme
        }
    }
    
    
    return sock;                                        // retourne descripteur socket configuré
}


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

   close(fd);                                            // fermer le processus

   return tab;                                           // renvoyer le tableau
}


char* get_local_ip() {                                                                  // fonction pour obtenir l'adresse IP de la machine distante

    static char ip[INET_ADDRSTRLEN];                                                    // buffer de l'adresse IP         
    char hostname[256];                                                                 // buffer du nom d'hôte
    struct hostent *host_entry;                                                         // structure d'adressage

    if (gethostname(hostname, sizeof(hostname)) < 0) {                                  // si échec de récupération du nom d'hôte
        perror("gethostname");                                                          // envoyer erreur
        return NULL;                                                                    // rien retourner
    }

    host_entry = gethostbyname(hostname);                                               // récupérer les informations de l'hôte grâce à son nom
    if (host_entry == NULL) {                                                           // si rien n'est obtenu
        perror("gethostbyname");                                                        // envoyer erreur
        return NULL;                                                                    // rien renvoyer
    }

    if (inet_ntop(AF_INET, host_entry->h_addr_list[0], ip, INET_ADDRSTRLEN) == NULL) {  // si échec de conversion de l'adresse IP
        perror("inet_ntop");                                                            // envoyer erreur
        return NULL;                                                                    // rien renvoyer
    }
                              
    return ip;                                                                          // renvoyer l'adresse IP
}


//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////  Fin de Partie Modifiée  //////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


