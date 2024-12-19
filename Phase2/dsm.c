#include "dsm_impl.h"

/* Variables globales */
int DSM_NODE_NUM;  /* nombre de processus dsm */
int DSM_NODE_ID;   /* rang du processus */ 
size_t page_size;
dsm_proc_conn_t *procs; 
static int *sockets = NULL;  // tableau global des sockets

static dsm_page_info_t table_page[PAGE_NUMBER];
static pthread_t comm_daemon;

/* Mutex et condition pour attendre la fin des connexions inter-processus */
static pthread_mutex_t connect_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t connect_cond = PTHREAD_COND_INITIALIZER;
static int connections_established = 0;


// Ajouter en haut du fichier avec les autres définitions
struct test_msg {
    int type;
    int sender_id;
    int data;
} __attribute__((packed));  // Important pour la compatibilité réseau


// Fonction utilitaire pour créer une socket
static int creer_socket_local(int type, char *ip, int port) {
    int sock = socket(AF_INET, type, 0);
    if (sock < 0) {
        perror("socket");
        return -1;
    }

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    
    if (ip) {
        inet_pton(AF_INET, ip, &addr.sin_addr);
    } else {
        addr.sin_addr.s_addr = htonl(INADDR_ANY);
    }

    int opt = 1;
    setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    if (bind(sock, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("bind");
        close(sock);
        return -1;
    }

    if (type == SOCK_STREAM) {
        if (listen(sock, 10) < 0) {
            perror("listen");
            close(sock);
            return -1;
        }
    }

    return sock;
}


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


static int dsm_send(int dest,void *buf,size_t size)                            // fonction pour envoyer un message de dsm
{
   ssize_t sent_bytes = 0;                                                     // initialiser le compteur de bits envoyés
   while (sent_bytes < size) {                                                 // tant que tout n'a pas été envoyé
      ssize_t n = send(dest, (char *)buf + sent_bytes, size - sent_bytes, 0);  // envoyer ce qui reste
      if (n == -1) {                                                           // si échec
         perror("[dsm_send] échec d'envoi");                                   // envoyer message d'erreur
         return 1;                                                             // renvoyer échec
      }
      sent_bytes += n;                                                         // incrémenter compteur de quantité envoyée
   }
   return 0;                                                                   // renvoyer succès
}


static int dsm_recv(int from,void *buf,size_t size)                                    // fonction pour recevoir un message dsm
{
   ssize_t received_bytes = 0;                                                         // initialiser le compteur de bits reçus
   while (received_bytes < size) {                                                     // tant que tout n'a pas été reçu
      ssize_t n = recv(from, (char *)buf + received_bytes, size - received_bytes, 0);  // recevoir ce qui reste
      if (n == -1) {                                                                   // si échec
         perror("[dsm_recv] échec de récupération");                                   // envoyer message d'erreur
         return 1;                                                                     // renvoyer échec
      } else if (n == 0) {                                                             // si rien n'est reçu
         fprintf(stderr, "[dsm_recv] connexion terminée\n");                           // annoncer connexion finie
         return 1;                                                                     // renvoyer échec
      }
      received_bytes += n;                                                             // incrémenter compteur de bits reçus
   }
   return 0;                                                                           // renvoyer succès
}

static void *dsm_comm_daemon(void *arg) {
    fprintf(stderr, "[%d] Démarrage du thread de communication\n", DSM_NODE_ID);
    
    // Création de la socket d'écoute
    int listen_sock = socket(AF_INET, SOCK_STREAM, 0);
    if (listen_sock < 0) {
        perror("socket");
        exit(1);
    }

    // Configuration de l'adresse
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(procs[DSM_NODE_ID].port_num);
    addr.sin_addr.s_addr = INADDR_ANY;

    // Options de socket
    int opt = 1;
    setsockopt(listen_sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    // Bind
    if (bind(listen_sock, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("bind");
        exit(1);
    }

    // Listen
    if (listen(listen_sock, 10) < 0) {
        perror("listen");
        exit(1);
    }

    // Allocation du tableau des sockets
    sockets = malloc(DSM_NODE_NUM * sizeof(int));
    memset(sockets, -1, DSM_NODE_NUM * sizeof(int));

    fprintf(stderr, "[%d] Socket d'écoute créée sur le port %d\n", 
            DSM_NODE_ID, procs[DSM_NODE_ID].port_num);

    // Connexion aux processus de rang inférieur
    for(int i = 0; i < DSM_NODE_ID; i++) {
        sockets[i] = socket(AF_INET, SOCK_STREAM, 0);
        if (sockets[i] < 0) {
            perror("socket");
            exit(1);
        }

        struct sockaddr_in peer_addr;
        memset(&peer_addr, 0, sizeof(peer_addr));
        peer_addr.sin_family = AF_INET;
        peer_addr.sin_port = htons(procs[i].port_num);
        if (inet_pton(AF_INET, procs[i].machine, &peer_addr.sin_addr) <= 0) {
            fprintf(stderr, "[%d] Erreur conversion adresse IP pour %s\n", 
                    DSM_NODE_ID, procs[i].machine);
            exit(1);
        }

        fprintf(stderr, "[%d] Tentative connexion à %d (port %d)\n", 
                DSM_NODE_ID, i, procs[i].port_num);

        if (connect(sockets[i], (struct sockaddr*)&peer_addr, sizeof(peer_addr)) < 0) {
            perror("connect");
            fprintf(stderr, "[%d] Échec connexion à %d\n", DSM_NODE_ID, i);
            exit(1);
        }

        fprintf(stderr, "[%d] Connecté à %d\n", DSM_NODE_ID, i);
    }

    // Attente des connexions des processus de rang supérieur
    for(int i = DSM_NODE_ID + 1; i < DSM_NODE_NUM; i++) {
        struct sockaddr_in client_addr;
        socklen_t client_len = sizeof(client_addr);
        
        fprintf(stderr, "[%d] Attente connexion de %d\n", DSM_NODE_ID, i);
        
        sockets[i] = accept(listen_sock, (struct sockaddr*)&client_addr, &client_len);
        if (sockets[i] < 0) {
            perror("accept");
            fprintf(stderr, "[%d] Échec accept de %d\n", DSM_NODE_ID, i);
            exit(1);
        }

        fprintf(stderr, "[%d] Accepté connexion de %d\n", DSM_NODE_ID, i);
    }

    // Notifier que toutes les connexions sont établies
    pthread_mutex_lock(&connect_mutex);
    connections_established = 1;
    pthread_cond_signal(&connect_cond);
    pthread_mutex_unlock(&connect_mutex);

    fprintf(stderr, "[%d] Toutes les connexions établies\n", DSM_NODE_ID);

    // Test des connexions
    struct test_msg {
        int type;
        int sender_id;
        int data;
    } msg;
    msg.type = 1;  // MSG_TEST
    msg.sender_id = DSM_NODE_ID;
    msg.data = 42;

    // Envoyer un message de test à tous les autres processus
    for(int i = 0; i < DSM_NODE_NUM; i++) {
        if (i != DSM_NODE_ID) {
            fprintf(stderr, "[%d] Envoi message test à %d\n", DSM_NODE_ID, i);
            if (dsm_send(i, &msg, sizeof(msg)) != sizeof(msg)) {
                fprintf(stderr, "[%d] Erreur envoi message test à %d\n", DSM_NODE_ID, i);
                exit(1);
            }
        }
    }

    // Attendre les messages de test de tous les autres processus
    int received = 0;
    int expected = DSM_NODE_NUM - 1;  // tous sauf nous-même

    while(received < expected) {
        fd_set readfds;
        FD_ZERO(&readfds);
        int max_fd = -1;

        for(int i = 0; i < DSM_NODE_NUM; i++) {
            if (i != DSM_NODE_ID && sockets[i] >= 0) {
                FD_SET(sockets[i], &readfds);
                if (sockets[i] > max_fd) max_fd = sockets[i];
            }
        }

        // Timeout de 5 secondes
        struct timeval tv;
        tv.tv_sec = 5;
        tv.tv_usec = 0;

        int ret = select(max_fd + 1, &readfds, NULL, NULL, &tv);
        if (ret < 0) {
            if (errno == EINTR) continue;
            perror("select");
            break;
        } else if (ret == 0) {
            fprintf(stderr, "[%d] Timeout en attente des messages de test\n", DSM_NODE_ID);
            exit(1);
        }

        for(int i = 0; i < DSM_NODE_NUM; i++) {
            if (i != DSM_NODE_ID && sockets[i] >= 0 && FD_ISSET(sockets[i], &readfds)) {
                struct test_msg received_msg;
                if (dsm_recv(i, &received_msg, sizeof(received_msg)) == sizeof(received_msg)) {
                    if (received_msg.type == 1) {  // MSG_TEST
                        fprintf(stderr, "[%d] Reçu message test de %d (data=%d)\n", 
                                DSM_NODE_ID, i, received_msg.data);
                        received++;
                    }
                } else {
                    fprintf(stderr, "[%d] Erreur réception message test de %d\n", 
                            DSM_NODE_ID, i);
                    exit(1);
                }
            }
        }
    }

    fprintf(stderr, "[%d] Test de connexion réussi avec tous les processus\n", DSM_NODE_ID);

    // Boucle principale de traitement des requêtes
    while(1) {
        fd_set readfds;
        FD_ZERO(&readfds);
        int max_fd = -1;

        for(int i = 0; i < DSM_NODE_NUM; i++) {
            if (i != DSM_NODE_ID && sockets[i] >= 0) {
                FD_SET(sockets[i], &readfds);
                if (sockets[i] > max_fd) max_fd = sockets[i];
            }
        }

        if (select(max_fd + 1, &readfds, NULL, NULL, NULL) < 0) {
            if (errno == EINTR) continue;
            perror("select");
            break;
        }

        for(int i = 0; i < DSM_NODE_NUM; i++) {
            if (i != DSM_NODE_ID && sockets[i] >= 0 && FD_ISSET(sockets[i], &readfds)) {
                fprintf(stderr, "[%d] Message reçu de %d\n", DSM_NODE_ID, i);
            }
        }
    }

    // Nettoyage
    for(int i = 0; i < DSM_NODE_NUM; i++) {
        if (sockets[i] >= 0) close(sockets[i]);
    }
    close(listen_sock);
    free(sockets);

    return NULL;
}


static void dsm_handler( void )
{  
   /* 
   struct dsm_req_t req;                                                             // allouer la mémoire pour recevoir la structure de communication

   if (dsm_recv(MASTER_FD, &req, sizeof(req)) != 0) {                                // recevoir la structure
      fprintf(stderr, "[dsm_handler] Erreur lors de la réception de la requête\n");  // si échec, afficher message
      exit(EXIT_FAILURE);                                                            // renvoyer échec
   }

   switch (req.type) {                                                               // examiner le type de requête reçue

      case DSM_REQ:                                                                  // si demande d'accès à une page
         printf("[dsm_handler] DSM_REQ source = %d / page_num = %d\n", req.source, req.page_num);
         // gestion de la requête DSM_REQ
         break;

      case DSM_PAGE:                                                                 // si demande 
         printf("[dsm_handler] DSM_PAGE source = %d / page_num = %d\n", req.source, req.page_num);
         // gestion de la requête DSM_PAGE
         break;
      
      case DSM_NREQ:                                                                 // si demande d'accès à plusieurs (?) pages
         printf("[dsm_handler] DSM_NREQ source = %d / page_num = %d\n", req.source, req.page_num);
         // gestion de la finalisation DSM
         break;

      case DSM_FINALIZE:                                                             // si annonce de finalisation
         printf("[dsm_handler] DSM_FINALIZE source = %d\n", req.source);
         // gestion de la finalisation DSM
         break;

      default:                                                                       // par défaut
         fprintf(stderr, "[dsm_handler] Type de requête inconnu\n");                 // signaler requête inconnue
         exit(EXIT_FAILURE);                                                         // renvoyer échec
   }
   */
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


char *dsm_init(int argc, char *argv[]) {
    page_size = (size_t)sysconf(_SC_PAGE_SIZE);

    
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
   } 
   
   /* reception de mon numero de processus dsm envoye */
   /* par le lanceur de programmes (DSM_NODE_ID)      */

   bytes_received = read(DSMEXEC_FD, &DSM_NODE_ID, sizeof(int));                                // récupérer la variable d'environnement
   if(bytes_received == -1) {                                                                   // si impossibilité de récupérer le rang
      perror("[dsminit] read DSM_NODE_ID");                                                     // afficher message
      exit(EXIT_FAILURE);                                                                       // envoyer échec                                                                        
   } 

   fprintf(stderr, "||||||||||[dsminit] DSMEXEC_FD %d / MASTER_FD %d / DSM_NODE_NUM %d / DSM_NODE_ID %d\n", DSMEXEC_FD, MASTER_FD, DSM_NODE_NUM, DSM_NODE_ID);

   /* reception des informations de connexion des autres */
   /* processus envoyees par le lanceur :                */
   /* nom de machine, numero de port, etc.               */

    procs = malloc(DSM_NODE_NUM * sizeof(dsm_proc_conn_t));
    if (!procs) {
        perror("malloc procs_conn");
        exit(1);
    }
    memset(procs, 0, DSM_NODE_NUM * sizeof(dsm_proc_conn_t));

    // Lecture synchronisée des informations de processus
    pthread_mutex_lock(&connect_mutex);
    for (int i = 0; i < DSM_NODE_NUM; i++) {
        dsm_proc_conn_t temp;
        size_t total_read = 0;
        
        int ret;
        // Lecture complète de la structure avec vérification
        while (total_read < sizeof(dsm_proc_conn_t)) {
            ret = read(DSMEXEC_FD, 
                      ((char*)&temp) + total_read, 
                      sizeof(dsm_proc_conn_t) - total_read);
            if (ret <= 0) {
                fprintf(stderr, "[dsminit] erreur lecture info processus %d\n", i);
                exit(1);
            }
            total_read += ret;
        }
        
        memcpy(&procs[i], &temp, sizeof(dsm_proc_conn_t));
        fprintf(stderr, "||||||||||[dsminit] Reçu info processus %d: rank=%d, machine=%s, port=%d\n",
                i, procs[i].rank, procs[i].machine, procs[i].port_num);
        
        connections_established++;
    }

    // Signal que toutes les connexions sont établies
    pthread_cond_signal(&connect_cond);
    pthread_mutex_unlock(&connect_mutex);

    // Envoyer un acquittement au dsmexec
    char ack = 1;
    if (write(DSMEXEC_FD, &ack, 1) < 0) {
        perror("write ack");
        exit(1);
    }

    // Attendre le signal de synchronisation finale
    char sync;
    int ret = read(DSMEXEC_FD, &sync, 1);
    if (ret != 1) {
        fprintf(stderr, "[dsminit] erreur lecture sync\n");
        exit(1);
    }

    /* Allocation des pages en tourniquet */
    for (int index = 0; index < PAGE_NUMBER; index++) {
        if ((index % DSM_NODE_NUM) == DSM_NODE_ID) {
            dsm_alloc_page(index);
            dsm_change_info(index, WRITE, DSM_NODE_ID);
        } else {
            dsm_change_info(index, INVALID, index % DSM_NODE_NUM);
        }
    }

    /* Configuration du handler SIGSEGV */
    struct sigaction act;
    memset(&act, 0, sizeof(act));
    act.sa_flags = SA_SIGINFO; 
    act.sa_sigaction = segv_handler;
    sigaction(SIGSEGV, &act, NULL);

    /* Thread de communication */
    if (pthread_create(&comm_daemon, NULL, dsm_comm_daemon, NULL) != 0) {
        perror("pthread_create");
        exit(1);
    }

    return (char *)BASE_ADDR;
}

void dsm_finalize(void) {
    pthread_cancel(comm_daemon);
    pthread_join(comm_daemon, NULL);
    free(procs);
}