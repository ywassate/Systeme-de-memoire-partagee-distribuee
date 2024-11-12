#include "common_impl.h"

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

void sigchld_handler(int sig)
{
   /* on traite les fils qui se terminent */
   /* pour eviter les zombies */
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


void sigchld_handler(int sig) {              // gérer les processus zombies
    while (waitpid(-1, NULL, WNOHANG) > 0);  // attendre la fin de tous les processus
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

   /* creation de la socket d'ecoute */
   /* + ecoute effective */ 
     
   /* creation des fils */
   for(i = 0; i < num_procs ; i++) {                           // pour le nombre de processus à créer

      
	
	/* creation du tube pour rediriger stdout */
	
	/* creation du tube pour rediriger stderr */
	
	pid = fork();                                               // créer un processus enfant

	if(pid == -1) ERROR_EXIT("fork");                           // si erreur, envoyer un message

	if (pid == 0) { /* fils */	                                 // si dans le processus fils
	   
	   /* redirection stdout */	      
	   
	   /* redirection stderr */	      	      
	   
	   /* Creation du tableau d'arguments pour le ssh */ 
	   
	   /* jump to new prog : */
	   /* execvp("ssh",newargv); */
      break;                                                   // s'arrêter

	} else  if(pid > 0) { /* pere */		                        // si dans le processus père
      printf(stdout, "PID du processus: %i\n", pid);           // printf le PID de l'enfant créé
	   /* fermeture des extremites des tubes non utiles */
	   num_procs_creat++;	                                    // incrémenter le nombre de processus
	}
     }
     
   
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

