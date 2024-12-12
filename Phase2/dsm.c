#include "dsm_impl.h"

int DSM_NODE_NUM; /* nombre de processus dsm */
int DSM_NODE_ID;  /* rang (= numero) du processus */ 

static dsm_proc_conn_t *procs = NULL;
static dsm_page_info_t table_page[PAGE_NUMBER];
static pthread_t comm_daemon;


/* indique l'adresse de debut de la page de numero numpage */
static char *num2address( int numpage )
{ 
   char *pointer = (char *)(BASE_ADDR+(numpage*(PAGE_SIZE)));
   
   if( pointer >= (char *)TOP_ADDR ){
      fprintf(stderr,"[%i] Invalid address !\n", DSM_NODE_ID);
      return NULL;
   }
   else return pointer;
}

/* cette fonction permet de recuperer un numero de page */
/* a partir  d'une adresse  quelconque */
static int address2num( char *addr )
{
  return (((intptr_t)(addr - BASE_ADDR))/(PAGE_SIZE));
}

/* cette fonction permet de recuperer l'adresse d'une page */
/* a partir d'une adresse quelconque (dans la page)        */
static char *address2pgaddr( char *addr )
{
  return  (char *)(((intptr_t) addr) & ~(PAGE_SIZE-1)); 
}

/* fonctions pouvant etre utiles */
static void dsm_change_info( int numpage, dsm_page_state_t state, dsm_page_owner_t owner)
{
   if ((numpage >= 0) && (numpage < PAGE_NUMBER)) {	
	if (state != NO_CHANGE )
	table_page[numpage].status = state;
      if (owner >= 0 )
	table_page[numpage].owner = owner;
      return;
   }
   else {
	fprintf(stderr,"[%i] Invalid page number !\n", DSM_NODE_ID);
      return;
   }
}

static dsm_page_owner_t get_owner( int numpage)
{
   return table_page[numpage].owner;
}

static dsm_page_state_t get_status( int numpage)
{
   return table_page[numpage].status;
}

/* Allocation d'une nouvelle page */
static void dsm_alloc_page( int numpage )
{
   char *page_addr = num2address( numpage );
   mmap(page_addr, PAGE_SIZE, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
   return ;
}

/* Changement de la protection d'une page */
static void dsm_protect_page( int numpage , int prot)
{
   char *page_addr = num2address( numpage );
   mprotect(page_addr, PAGE_SIZE, prot);
   return;
}

static void dsm_free_page( int numpage )
{
   char *page_addr = num2address( numpage );
   munmap(page_addr, PAGE_SIZE);
   return;
}

static void *dsm_comm_daemon( void *arg)
{  
   while(1)
     {
	/* a modifier */
	printf("[%i] Waiting for incoming reqs \n", DSM_NODE_ID);
	sleep(2);
     }
   return NULL;
}

static int dsm_send(int dest,void *buf,size_t size)
{
   /* a completer */
  return 0;
}

static int dsm_recv(int from,void *buf,size_t size)
{
   /* a completer */
  return 0;
}

static void dsm_handler( void )
{  
   /* A modifier */
   printf("[%i] FAULTY  ACCESS !!! \n",DSM_NODE_ID);
   abort();
}

/* traitant de signal adequat */
static void segv_handler(int sig, siginfo_t *info, void *context)
{
   /* A completer */
   /* adresse qui a provoque une erreur */
   void  *addr = info->si_addr;   
  /* Si ceci ne fonctionne pas, utiliser a la place :*/
  /*
   #ifdef __x86_64__
   void *addr = (void *)(context->uc_mcontext.gregs[REG_CR2]);
   #elif __i386__
   void *addr = (void *)(context->uc_mcontext.cr2);
   #else
   void  addr = info->si_addr;
   #endif
   */
   /*
   pour plus tard (question ++):
   dsm_access_t access  = (((ucontext_t *)context)->uc_mcontext.gregs[REG_ERR] & 2) ? WRITE_ACCESS : READ_ACCESS;   
  */   
   /* adresse de la page dont fait partie l'adresse qui a provoque la faute */
   void  *page_addr  = (void *)(((unsigned long) addr) & ~(PAGE_SIZE-1));

   if ((addr >= (void *)BASE_ADDR) && (addr < (void *)TOP_ADDR))
     {
	dsm_handler();
     }
   else
     {
	/* SIGSEGV normal : ne rien faire*/
     }
}

/* Seules ces deux dernieres fonctions sont visibles et utilisables */
/* dans les programmes utilisateurs de la DSM                       */
char *dsm_init(int argc, char *argv[])
{   
   struct sigaction act;
   int index;   


   /* Récupération de la valeur des variables d'environnement */
   /* DSMEXEC_FD et MASTER_FD                                 */

   char *DSMEXEC_FD_ptr = getenv("DSMEXEC_FD");                                                 // récupérer la valeur de DSMEXEC_FD
   char *MASTER_FD_ptr = getenv("MASTER_FD");                                                   // récupérer la valeur de MASTER_FD
    
   if (DSMEXEC_FD_ptr== NULL || MASTER_FD_ptr == NULL) {                                        // si l'un des pointeurs est vide
      perror("[dsminit] erreur de récupération de DSMEXEC_FD ou MASTER_FD");                    // envoyer erreur
      exit(EXIT_FAILURE);                                                                       // renvoyer échec
   }

   int DSMEXEC_FD = atoi(DSMEXEC_FD_ptr);                                                       // convertir la chaîne de caractère en entier
   int MASTER_FD = atoi(MASTER_FD_ptr);                                                         // convertir la chaîne de caractère en entier

   
   /* reception du nombre de processus dsm envoye */
   /* par le lanceur de programmes (DSM_NODE_NUM) */

   ssize_t bytes_received = read(DSMEXEC_FD, &DSM_NODE_NUM, sizeof(int));                       // récupérer la variable d'environnement
   if (bytes_received == -1) {                                                                  // si impossibilité de récupérer le nombre
      perror("[dsminit] read DSM_NODE_NUM");                                                    // afficher message
      exit(EXIT_FAILURE);                                                                       // envoyer échec
   } else if (bytes_received != sizeof(int)) {
      fprintf(stderr, "[dsminit] DSM_NODE_NUM taille incorrecte\n");
      exit(EXIT_FAILURE);
   }
   

   /* reception de mon numero de processus dsm envoye */
   /* par le lanceur de programmes (DSM_NODE_ID)      */

   bytes_received = read(DSMEXEC_FD, &DSM_NODE_ID, sizeof(int));                                // récupérer la variable d'environnement
   if(bytes_received == -1) {                                                                   // si impossibilité de récupérer le rang
      perror("[dsminit] read DSM_NODE_ID");                                                     // afficher message
      exit(EXIT_FAILURE);                                                                       // envoyer échec                                                                        
   } else if (bytes_received != sizeof(int)) {
      fprintf(stderr, "[dsminit] DSM_NODE_NUM taille incorrecte\n");
      exit(EXIT_FAILURE);
   }


   /* reception des informations de connexion des autres */
   /* processus envoyees par le lanceur :                */
   /* nom de machine, numero de port, etc.               */
   
   dsm_proc_conn_t *procs_conn = malloc(DSM_NODE_NUM * sizeof(dsm_proc_conn_t));                // allouer structure de connexion
   for (int i = 0; i < DSM_NODE_NUM; i++) {                                                     // pour le nombre de processus distants
      if (recv(DSMEXEC_FD, &procs_conn[i], sizeof(dsm_proc_conn_t), 0) == -1) {                 // si échec de réception des informations de connexion
         perror("[dsminit] recv proc_conn");                                                    // afficher message
         exit(EXIT_FAILURE);                                                                    // envoyer échec
      }
      printf("========= [dsminit] Structure procs_conn %d remplie \n", i);                      // afficher message de confirmation

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////          FIN DE PARTIE FONCTIONNELLE     //////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

      if (procs_conn[DSM_NODE_ID].port_num == 0) {
         procs_conn[DSM_NODE_ID].port_num = 50000 + DSM_NODE_ID; // Port arbitraire unique par processus
         printf("[dsminit] Port assigné pour le processus %d : %d\n", DSM_NODE_ID, procs_conn[DSM_NODE_ID].port_num);
      }
   }

   close(DSMEXEC_FD);

   printf("========= [dsminit] DSMEXEC_FD fermée \n");



   /* initialisation des connexions              */ 
   /* avec les autres processus : connect/accept */

   /*
   int opt = 1;
   if (setsockopt(MASTER_FD, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) == -1) {
      perror("[dsminit] Erreur lors du setsockopt");
      exit(EXIT_FAILURE);
   }

   printf("========= [dsminit] setsockopt effectué\n");    // afficher message de confirmation
   */

   
   
   struct sockaddr_in addr;
   addr.sin_family = AF_INET;
   addr.sin_port = htons(procs_conn[DSM_NODE_ID].port_num); // Utiliser le port spécifique au processus local
   addr.sin_addr.s_addr = INADDR_ANY; // Accepter toutes les connexions
   
   /*
   printf("========= [dsminit] Port pour bind : %d\n", procs_conn[DSM_NODE_ID].port_num);
   if (procs_conn[DSM_NODE_ID].port_num <= 0 || procs_conn[DSM_NODE_ID].port_num > 65535) {
      fprintf(stderr, "[dsminit] Numéro de port invalide : %d\n", procs_conn[DSM_NODE_ID].port_num);
      exit(EXIT_FAILURE);
   }


   printf("========= [dsminit] structure de connexion remplie\n");    // afficher message de confirmation

   if (bind(MASTER_FD, (struct sockaddr *)&addr, sizeof(addr)) == -1) {
      perror("[dsminit] Erreur lors du bind");
      exit(EXIT_FAILURE);
   }

   printf("========= [dsminit] bind effectué\n");    // afficher message de confirmation

   if (listen(MASTER_FD, SOMAXCONN) == -1) {
      perror("[dsminit] Erreur lors du listen");
      exit(EXIT_FAILURE);
   }
   */

   printf("========= [dsminit] port %d\n", ntohs(addr.sin_port));    // afficher message de confirmation

   for (int index = 0; index < DSM_NODE_NUM; index++) {
      if (procs_conn[DSM_NODE_ID].port_num == 0) { // Si le port est égal à 0, attendre les connexions
         if (index < DSM_NODE_ID) {
            struct sockaddr_in client_addr;
            socklen_t client_addr_len = sizeof(client_addr);

            // Accepter la connexion
            int client_sock = accept(MASTER_FD, (struct sockaddr *)&client_addr, &client_addr_len);
            if (client_sock == -1) {
               perror("[dsminit] Erreur lors de l'acceptation de la connexion");
               exit(EXIT_FAILURE);
            }

            // Stocker le descripteur dans la structure
            procs_conn[index].fd = client_sock;

            // Afficher les informations de connexion
            char client_ip[INET_ADDRSTRLEN];
            inet_ntop(AF_INET, &client_addr.sin_addr, client_ip, INET_ADDRSTRLEN);
            printf("Connexion acceptée depuis le processus de rang %d (IP : %s, Port : %d)\n",
                   index, client_ip, ntohs(client_addr.sin_port));
         }
      } else { // Sinon, essayer de connecter aux autres processus
         if (index > DSM_NODE_ID) {
            struct sockaddr_in server_addr;
            memset(&server_addr, 0, sizeof(server_addr));
            server_addr.sin_family = AF_INET;
            server_addr.sin_port = htons(procs_conn[index].port_num); // Port du processus cible
            printf("[dsminit] Résolution de l'adresse pour le processus %d : %s\n", index, procs_conn[index].machine);

            struct addrinfo hints, *res;
            memset(&hints, 0, sizeof(hints));
            hints.ai_family = AF_INET; // IPv4 uniquement

            if (getaddrinfo(procs_conn[index].machine, NULL, &hints, &res) != 0) {
               fprintf(stderr, "[dsminit] Impossible de résoudre l'adresse pour %s\n", procs_conn[index].machine);
               exit(EXIT_FAILURE);
            }

            struct sockaddr_in *ipv4 = (struct sockaddr_in *)res->ai_addr;
            server_addr.sin_addr = ipv4->sin_addr;
            freeaddrinfo(res);

            // Création de la socket
            int sock = socket(AF_INET, SOCK_STREAM, 0);
            if (sock == -1) {
               perror("Erreur lors de la création du socket");
               exit(EXIT_FAILURE);
            }

            // Connexion au processus cible
            if (connect(sock, (struct sockaddr *)&server_addr, sizeof(server_addr)) == -1) {
               perror("Erreur lors de la connexion");
               exit(EXIT_FAILURE);
            }

            // Stocker le descripteur dans la structure
            procs_conn[index].fd = sock;
            printf("Connexion établie avec le processus de rang %d\n", index);
         }
      } 
   }


//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////    PARTIE DE MONSIEUR MERCIER    /////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////



   /* Allocation des pages en tourniquet */
   for(index = 0; index < PAGE_NUMBER; index ++){	
      if ((index % DSM_NODE_NUM) == DSM_NODE_ID)
         dsm_alloc_page(index);	     
      dsm_change_info( index, WRITE, index % DSM_NODE_NUM);
   }
   

   /* mise en place du traitant de SIGSEGV */
   act.sa_flags = SA_SIGINFO; 
   act.sa_sigaction = segv_handler;
   sigaction(SIGSEGV, &act, NULL);
   
   /* creation du thread de communication           */
   /* ce thread va attendre et traiter les requetes */
   /* des autres processus                          */
   pthread_create(&comm_daemon, NULL, dsm_comm_daemon, NULL);
   
   /* Adresse de début de la zone de mémoire partagée */
   return ((char *)BASE_ADDR);
}


void dsm_finalize( void )
{
   /* fermer proprement les connexions avec les autres processus */

   /* terminer correctement le thread de communication */
   /* pour le moment, on peut faire :                  */
   pthread_cancel(comm_daemon);
   
  return;
}

