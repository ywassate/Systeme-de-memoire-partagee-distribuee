
#include <sys/wait.h>
#include <sys/ioctl.h>
#include <fcntl.h>
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
#include <netdb.h>

/* autres includes (eventuellement) */

#define ERROR_EXIT(str) {perror(str);exit(EXIT_FAILURE);}

/**************************************************************/
/****************** DEBUT DE PARTIE NON MODIFIABLE ************/
/**************************************************************/

#define MAX_STR  (1024)
typedef char maxstr_t[MAX_STR];

/* definition du type des infos */
/* de connexion des processus dsm */
struct dsm_proc_conn  {
   int      rank;
   maxstr_t machine;
   int      port_num;
   int      fd; 
   int      fd_for_exit; /* special */  
};

typedef struct dsm_proc_conn dsm_proc_conn_t; 

/**************************************************************/
/******************* FIN DE PARTIE NON MODIFIABLE *************/
/**************************************************************/

/* definition du type des infos */
/* d'identification des processus dsm */

struct dsm_proc {   
    pid_t pid;                  // PID du processus local (enfant)
    dsm_proc_conn_t connect_info; // Informations de connexion (socket et port)
    char *machine_name;         // Nom de la machine distante
    int rank;                   // Rang du processus DSM
    int stdout_fd;              // Descripteur de fichier pour rediriger stdout
    int stderr_fd;              // Descripteur de fichier pour rediriger stderr
    int sock_fd;               // Socket de communication avec le processus distant
};
typedef struct dsm_proc dsm_proc_t;



//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////  Partie Modifiée  /////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////



void sigchld_handler(int sig);  // gérer les processus zombies
   

int creer_socket(int type, const char *ip, int port);  // fonction pour créer une socket
    

char **read_machine_file(char *argv);  // fonction pour lire les lignes de machine_file              
char* get_local_ip();


//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////  Fin de Partie Modifiée  //////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
