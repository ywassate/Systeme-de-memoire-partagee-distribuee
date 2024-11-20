#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/ioctl.h>
#include <errno.h>
#include <string.h>
#include <fcntl.h>

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