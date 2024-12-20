#include "dsm_impl.h"

/* Variables globales */
int DSM_NODE_NUM; /* nombre de processus dsm */
int DSM_NODE_ID;  /* rang (= numero) du processus */ 
int MASTER_FD;    /* descripteur de fichier de la socket de communication dsm */
dsm_proc_conn_t *procs; 
static int *sockets = NULL;  // tableau global des sockets
static dsm_page_info_t table_page[PAGE_NUMBER];

// structure de message de test
struct test_msg {
    int type;
    int sender_id;
    int data;
} __attribute__((packed));  // Important pour la compatibilité réseau


//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////  DEBUT DE PARTIE DE MONSIEUR MERCIER    ////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


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


//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////  FIN DE PARTIE DE MONSIEUR MERCIER    /////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static int dsm_send(int dest,void *buf,size_t size) {                          // fonction pour envoyer un message de dsm

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


static int dsm_recv(int from,void *buf,size_t size)  {                                 // fonction pour recevoir un message dsm

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

/* Function to check if a socket is already bound */
int is_socket_bound(int sockfd) {
    struct sockaddr_in addr;
    socklen_t len = sizeof(addr);
    
    // Try to get the socket name
    int ret = getsockname(sockfd, (struct sockaddr*)&addr, &len);
    
    if (ret == 0) {
        // Socket is bound, let's print some info
        printf("Socket is bound to port %d\n", ntohs(addr.sin_port));
        char ip[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &(addr.sin_addr), ip, INET_ADDRSTRLEN);
        printf("Bound to address: %s\n", ip);
        return 1;
    } else {
        if (errno == EINVAL) {
            // Socket is not bound
            printf("Socket is not bound\n");
            return 0;
        } else {
            // Some other error occurred
            printf("Error checking socket: %s\n", strerror(errno));
            return -1;
        }
    }
}

static int dsm_comm_daemon(void) {
    printf("[%d] Starting communication setup\n", DSM_NODE_ID);
    fflush(stdout);

    // Allocation du tableau de sockets
    sockets = malloc(DSM_NODE_NUM * sizeof(int));
    if (!sockets) {
        perror("malloc sockets");
        return -1;
    }
    memset(sockets, -1, DSM_NODE_NUM * sizeof(int));

    if (DSM_NODE_ID == 0) {
        // Pour le processus 0, on vérifie si le socket est déjà en écoute
        int opt = 1;
        if (setsockopt(MASTER_FD, SOL_SOCKET, SO_REUSEADDR | SO_REUSEPORT, &opt, sizeof(opt))) {
            perror("setsockopt");
            return -1;
        }

        // Obtenir les informations actuelles du socket
        struct sockaddr_in addr;
        socklen_t len = sizeof(addr);
        if (getsockname(MASTER_FD, (struct sockaddr*)&addr, &len) == 0) {
            printf("[%d] MASTER_FD is bound to port %d\n", DSM_NODE_ID, ntohs(addr.sin_port));
        } else {
            perror("getsockname");
            return -1;
        }

        // Mettre le socket en mode écoute
        if (listen(MASTER_FD, DSM_NODE_NUM - 1) < 0) {
            perror("listen");
            return -1;
        }

        printf("[%d] Listening for incoming connections\n", DSM_NODE_ID);
        fflush(stdout);

        // Le processus 0 attend les connexions des autres processus
        for (int i = 1; i < DSM_NODE_NUM; i++) {
            printf("[%d] Waiting for connection from process %d\n", DSM_NODE_ID, i);
            fflush(stdout);

            struct sockaddr_in client_addr;
            socklen_t client_len = sizeof(client_addr);
            int client_fd = accept(MASTER_FD, (struct sockaddr*)&client_addr, &client_len);
            
            if (client_fd < 0) {
                perror("accept");
                return -1;
            }

            // Recevoir l'ID du processus qui se connecte
            int remote_id;
            if (recv(client_fd, &remote_id, sizeof(remote_id), MSG_WAITALL) <= 0) {
                perror("recv remote_id");
                close(client_fd);
                return -1;
            }

            if (remote_id <= 0 || remote_id >= DSM_NODE_NUM) {
                fprintf(stderr, "[%d] Invalid remote_id received: %d\n", DSM_NODE_ID, remote_id);
                close(client_fd);
                return -1;
            }

            sockets[remote_id] = client_fd;
            printf("[%d] Accepted connection from process %d\n", DSM_NODE_ID, remote_id);
            fflush(stdout);
        }
    } else {
        // Les autres processus se connectent au processus 0
        printf("[%d] Connecting to process 0\n", DSM_NODE_ID);
        fflush(stdout);

        int sock = socket(AF_INET, SOCK_STREAM, 0);
        if (sock < 0) {
            perror("socket");
            return -1;
        }

        struct sockaddr_in server_addr;
        memset(&server_addr, 0, sizeof(server_addr));
        server_addr.sin_family = AF_INET;
        server_addr.sin_port = htons(procs[0].port_num);

        // Résolution du nom d'hôte
        struct hostent *he = gethostbyname(procs[0].machine);
        if (!he) {
            perror("gethostbyname");
            close(sock);
            return -1;
        }
        memcpy(&server_addr.sin_addr, he->h_addr_list[0], he->h_length);

        // Tentative de connexion avec retry
        int connected = 0;
        for (int retry = 0; retry < 5 && !connected; retry++) {
            if (connect(sock, (struct sockaddr*)&server_addr, sizeof(server_addr)) == 0) {
                connected = 1;
                break;
            }
            printf("[%d] Connection attempt %d failed, retrying...\n", DSM_NODE_ID, retry + 1);
            sleep(1);
        }

        if (!connected) {
            perror("connect failed after retries");
            close(sock);
            return -1;
        }

        // Envoyer notre ID au processus 0
        if (send(sock, &DSM_NODE_ID, sizeof(DSM_NODE_ID), 0) < 0) {
            perror("send DSM_NODE_ID");
            close(sock);
            return -1;
        }

        sockets[0] = sock;
        printf("[%d] Successfully connected to process 0\n", DSM_NODE_ID);
        fflush(stdout);
    }

    // Stocker notre propre socket
    sockets[DSM_NODE_ID] = MASTER_FD;

    printf("[%d] Communication setup completed\n", DSM_NODE_ID);
    fflush(stdout);

    return 0;
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

    fflush(stdout);

    /* Récupération de la valeur des variables d'environnement */
    /* DSMEXEC_FD et MASTER_FD                                 */

    char *DSMEXEC_FD_ptr = getenv("DSMEXEC_FD");                                                  // récupérer la valeur de DSMEXEC_FD
    char *MASTER_FD_ptr = getenv("MASTER_FD");                                                    // récupérer la valeur de MASTER_FD
    
    if (DSMEXEC_FD_ptr== NULL || MASTER_FD_ptr == NULL) {                                         // si l'un des pointeurs est vide
        perror("[dsminit] erreur de récupération de DSMEXEC_FD ou MASTER_FD");                    // envoyer erreur
        exit(EXIT_FAILURE);                                                                       // renvoyer échec
    }

    int DSMEXEC_FD = atoi(DSMEXEC_FD_ptr);                                                        // convertir la chaîne de caractère en entier
    MASTER_FD = atoi(MASTER_FD_ptr);                                                          // convertir la chaîne de caractère en entier

    

    // Vérifier que MASTER_FD est valide
    struct sockaddr_in addr;
    socklen_t len = sizeof(addr);
    if (getsockname(MASTER_FD, (struct sockaddr*)&addr, &len) == -1) {
        fprintf(stderr, "[%d] MASTER_FD %d is not a valid socket\n", DSM_NODE_ID, MASTER_FD);
    } else {
        printf("[%d] MASTER_FD %d is bound to port %d\n", 
               DSM_NODE_ID, MASTER_FD, ntohs(addr.sin_port));
    }
    /* reception du nombre de processus dsm envoye */
    /* par le lanceur de programmes (DSM_NODE_NUM) */

    ssize_t bytes_received = read(DSMEXEC_FD, &DSM_NODE_NUM, sizeof(int));                        // récupérer la variable d'environnement
    if (bytes_received == -1) {                                                                   // si impossibilité de récupérer le nombre
        perror("[dsminit] read DSM_NODE_NUM");                                                    // afficher message
        exit(EXIT_FAILURE);                                                                       // envoyer échec
    } 
   
    /* reception de mon numero de processus dsm envoye */
    /* par le lanceur de programmes (DSM_NODE_ID)      */

    bytes_received = read(DSMEXEC_FD, &DSM_NODE_ID, sizeof(int));                                 // récupérer la variable d'environnement
    if(bytes_received == -1) {                                                                    // si impossibilité de récupérer le rang
        perror("[dsminit] read DSM_NODE_ID");                                                     // afficher message
        exit(EXIT_FAILURE);                                                                       // envoyer échec                                                                        
    } 

    printf("[%d] DSMEXEC_FD %d / MASTER_FD %d / DSM_NODE_NUM %d / DSM_NODE_ID %d\n", DSM_NODE_ID, DSMEXEC_FD, MASTER_FD, DSM_NODE_NUM, DSM_NODE_ID);

    /* reception des informations de connexion des autres */
    /* processus envoyees par le lanceur :                */
    /* nom de machine, numero de port, etc.               */

    /* Allocation de la structure des processus */
    procs = malloc(DSM_NODE_NUM * sizeof(dsm_proc_conn_t));
    if (!procs) {
        perror("malloc procs_conn");
        exit(1);
    }
    memset(procs, 0, DSM_NODE_NUM * sizeof(dsm_proc_conn_t));

    /* Lecture des informations de connexion pour chaque processus */
    for (int i = 0; i < DSM_NODE_NUM; i++) {
        dsm_proc_conn_t temp;
        size_t total_read = 0;
        
        while (total_read < sizeof(dsm_proc_conn_t)) {
            ssize_t ret = read(DSMEXEC_FD, 
                      ((char*)&temp) + total_read, 
                      sizeof(dsm_proc_conn_t) - total_read);
            if (ret <= 0) {
                printf("[dsminit] erreur lecture info processus %d\n", i);
                fflush(stdout);
                exit(1);
            }
            total_read += ret;
        }
        
        memcpy(&procs[i], &temp, sizeof(dsm_proc_conn_t));
        printf("[%d] Reçu info processus %d: rank=%d, machine=%s, port=%d \n", DSM_NODE_ID, i, procs[i].rank, procs[i].machine, procs[i].port_num);
    }

    
    /* Mise en place des communications */
    printf("[%d] Début dsm_com_daemon \n", DSM_NODE_ID);
    fflush(stdout);

    printf("[%d] Initialisation des communications \n", DSM_NODE_ID);
    fflush(stdout);
    

    


    if (dsm_comm_daemon() != 0) {
        printf("[%d] Erreur initialisation communications\n", DSM_NODE_ID);
        fflush(stdout);
        exit(1);
    }

    sleep(2); // Attendre que tous les processus soient prêts

    /* Test des communications */
    struct test_msg msg = {
        .type = 1,
        .sender_id = DSM_NODE_ID,
        .data = 42
    };

    printf("[%d] Début des tests de communication\n", DSM_NODE_ID);
    fflush(stdout);

    // Envoi des messages de test à tous les autres processus
    for(int i = 0; i < DSM_NODE_NUM; i++) {
        if (i != DSM_NODE_ID) {
            printf("[%d] Envoi message test à %d\n", DSM_NODE_ID, i);
            fflush(stdout);
            if (dsm_send(sockets[i], &msg, sizeof(msg)) <= 0) {
                printf("[%d] Erreur envoi message à %d\n", DSM_NODE_ID, i);
                fflush(stdout);
                exit(1);
            }
        }
    }

    // Attente des messages de test
    int messages_received = 0;
    int expected_messages = DSM_NODE_NUM - 1;

    printf("[%d] Attente de %d messages de test\n", DSM_NODE_ID, expected_messages);
    fflush(stdout);

    while (messages_received < expected_messages) {
        fd_set readfds;
        FD_ZERO(&readfds);
        int max_fd = -1;

        for(int i = 0; i < DSM_NODE_NUM; i++) {
            if (i != DSM_NODE_ID && sockets[i] >= 0) {
                FD_SET(sockets[i], &readfds);
                if (sockets[i] > max_fd) max_fd = sockets[i];
            }
        }

        struct timeval tv = {.tv_sec = 5, .tv_usec = 0};
        int ready = select(max_fd + 1, &readfds, NULL, NULL, &tv);

        if (ready < 0) {
            perror("select");
            exit(1);
        } else if (ready == 0) {
            printf("[%d] Timeout en attente des messages\n", DSM_NODE_ID);
            fflush(stdout);
            exit(1);
        }

        for(int i = 0; i < DSM_NODE_NUM; i++) {
            if (i != DSM_NODE_ID && sockets[i] >= 0 && FD_ISSET(sockets[i], &readfds)) {
                struct test_msg received_msg;
                if (dsm_recv(sockets[i], &received_msg, sizeof(received_msg)) > 0) {
                    printf("[%d] Reçu message de %d: type=%d, data=%d\n", 
                           DSM_NODE_ID, received_msg.sender_id, received_msg.type, 
                           received_msg.data);
                    fflush(stdout);
                    messages_received++;
                }
            }
        }
    }
    
    printf("[DSM] Début allocation des pages\n");
    fflush(stdout);

    /* Allocation des pages en tourniquet */
    for (int index = 0; index < PAGE_NUMBER; index++) {
        if ((index % DSM_NODE_NUM) == DSM_NODE_ID) {
            dsm_alloc_page(index);
            dsm_change_info(index, WRITE, DSM_NODE_ID);
        } else {
            dsm_change_info(index, INVALID, index % DSM_NODE_NUM);
        }
    }

    printf("[DSM] Pages allouées\n");
    fflush(stdout);

    /* Configuration du handler SIGSEGV */
    printf("[DSM] Configuration du handler SIGSEGV\n");
    fflush(stdout);

    struct sigaction act;
    memset(&act, 0, sizeof(act));
    act.sa_flags = SA_SIGINFO; 
    act.sa_sigaction = segv_handler;
    if (sigaction(SIGSEGV, &act, NULL) == -1) {
        printf("[DSM] Erreur configuration handler SIGSEGV\n");
        fflush(stdout);
        perror("sigaction");
        exit(1);
    }

    printf("[DSM] Handler SIGSEGV configuré\n");
    fflush(stdout);

    printf("[%d] Initialisation DSM terminée\n", DSM_NODE_ID);
    fflush(stdout);
    
    return (char *)BASE_ADDR;
}


void dsm_finalize(void) {
    printf("[DSM-%d] Début de dsm_finalize\n", DSM_NODE_ID);
    fflush(stdout);
    
    // Envoyer un message de finalisation à tous les autres processus
    struct test_msg msg = {
        .type = DSM_FINALIZE,
        .sender_id = DSM_NODE_ID,
        .data = 0
    };

    // Envoi du message de finalisation à tous les processus
    for(int i = 0; i < DSM_NODE_NUM; i++) {
        if (i != DSM_NODE_ID && sockets[i] >= 0) {
            printf("[DSM-%d] Envoi message de finalisation à %d\n", DSM_NODE_ID, i);
            fflush(stdout);
            dsm_send(sockets[i], &msg, sizeof(msg));
        }
    }

    // Fermeture propre des sockets
    if (sockets) {
        for(int i = 0; i < DSM_NODE_NUM; i++) {
            if (sockets[i] >= 0) {
                printf("[DSM-%d] Fermeture socket %d\n", DSM_NODE_ID, i);
                fflush(stdout);
                shutdown(sockets[i], SHUT_RDWR);  // Arrêt propre de la socket
                close(sockets[i]);
            }
        }
        free(sockets);
        sockets = NULL;
    }

    // Libération des autres ressources
    if (procs) {
        free(procs);
        procs = NULL;
    }

    printf("[DSM-%d] Fin de dsm_finalize\n", DSM_NODE_ID);
    fflush(stdout);
}