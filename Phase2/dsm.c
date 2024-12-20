#include "dsm_impl.h"

/* Variables globales */
int DSM_NODE_NUM; /* nombre de processus dsm */
int DSM_NODE_ID;  /* rang (= numero) du processus */ 
int MASTER_FD;    /* descripteur de fichier de la socket de communication dsm */

dsm_proc_conn_t *procs; 

static int *sockets = NULL;  // tableau global des sockets
static dsm_page_info_t table_page[PAGE_NUMBER];
static volatile int running = 1;  
static pthread_t listener_thread;

static pthread_mutex_t dsm_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t dsm_cond = PTHREAD_COND_INITIALIZER;
static volatile int page_ready = 0;



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
         return 0;                                                             // renvoyer échec
      }
      sent_bytes += n;                                                         // incrémenter compteur de quantité envoyée
   }
   return 1;                                                                   // renvoyer succès
}


static int dsm_recv(int from,void *buf,size_t size)  {                                 // fonction pour recevoir un message dsm

   ssize_t received_bytes = 0;                                                         // initialiser le compteur de bits reçus
   while (received_bytes < size) {                                                     // tant que tout n'a pas été reçu
      ssize_t n = recv(from, (char *)buf + received_bytes, size - received_bytes, 0);  // recevoir ce qui reste
      if (n == -1) {                                                                   // si échec
         perror("[dsm_recv] échec de récupération");                                   // envoyer message d'erreur 
         return 0;                                                                     // renvoyer échec
      } else if (n == 0) {                                                             // si rien n'est reçu
         fprintf(stderr, "[dsm_recv] connexion terminée\n");                           // annoncer connexion finie
         return 0;                                                                     // renvoyer échec
      }
      received_bytes += n;                                                             // incrémenter compteur de bits reçus
   }
   return 1;                                                                           // renvoyer succès
}


static int dsm_comm_daemon(void) {                                                               // fonction pour mettre en place les communications entre processus
 
    printf("[%d] Début de dsm_comm_daemon\n", DSM_NODE_ID);                                      // message d'entrée     

    sockets = malloc(DSM_NODE_NUM * sizeof(int));                                                // allocation du tableau des sockets
    if (!sockets) {                                                                              // si échec
        perror("malloc sockets");                                                                // envoyer message
        exit(EXIT_FAILURE);                                                                      // renvoyer échec
    }
    memset(sockets, -1, DSM_NODE_NUM * sizeof(int));                                             // mettre tous les bits de mémoire à -1
    
    sockets[DSM_NODE_ID] = MASTER_FD;                                                            // assigner le MASTER_FD du processus

    if (listen(MASTER_FD, DSM_NODE_NUM - 1) < 0) {                                               // mettre MASTER_FD en écoute
        perror("listen");                                                                        // si échec, envoyer erreur
        exit(EXIT_FAILURE);                                                                      // renvoyer échec
    }


    for (int i = DSM_NODE_ID + 1; i < DSM_NODE_NUM; i++) {                                       // pour l'ensemble des processus supérieur au DSM_NODE_ID

        struct sockaddr_in client_addr;                                                          // initialiser stucture de connexion
        socklen_t client_len = sizeof(client_addr);                                              // longueur de la structure
        int client_fd = accept(MASTER_FD, (struct sockaddr*)&client_addr, &client_len);          // accepter la connexion sur la socket client_fd
        
        if (client_fd < 0) {                                                                     // si échec
            perror("accept");                                                                    // envoyer message
            exit(EXIT_FAILURE);                                                                  // renvoyer échec
        }

        int remote_id;                                                                           // initialiser un DSM_NODE_ID pour le processus distant
        if (recv(client_fd, &remote_id, sizeof(remote_id), MSG_WAITALL) <= 0) {                  // recevoir son DSM_NODE_ID
            perror("recv remote_id");                                                            // si échec, envoyer message
            close(client_fd);                                                                    // fermer la socket
            exit(EXIT_FAILURE);                                                                  // renvoyer échec
        }

        sockets[remote_id] = client_fd;                                                          // conserver la socket assignée
        printf("[%d] connexion acceptée du processus %d\n", DSM_NODE_ID, remote_id);             // message de confirmation
    }


    for (int i = 0; i < DSM_NODE_ID; i++) {                                                      // pour l'ensemble des processus inférieurs au DSM_NODE_ID
    

        int sock = socket(AF_INET, SOCK_STREAM, 0);                                              // création d'une socket
        if (sock < 0) {                                                                          // si échec
            perror("socket");                                                                    // envoyer message
            exit(EXIT_FAILURE);                                                                  // renvoyer échec
        }

        struct sockaddr_in server_addr;                                                          // initialiser structure de connexion 
        memset(&server_addr, 0, sizeof(server_addr));                                            // mettre les bits de mémoire à 0
        server_addr.sin_family = AF_INET;                                                        // paramétrer la structure
        server_addr.sin_port = htons(procs[i].port_num);                                         // assigner le port correspondant au processus

        struct hostent *he = gethostbyname(procs[i].machine);                                    // récupérer l'adresse IP de la machine distante
        if (!he) {                                                                               // si échec
            perror("gethostbyname");                                                             // envoyer message
            close(sock);                                                                         // fermer la socket
            exit(EXIT_FAILURE);                                                                  // renvoyer échec         
        }
        memcpy(&server_addr.sin_addr, he->h_addr_list[0], he->h_length);                         // copier l'IP dans la structure de connexion

        int connected = 0;                                                                       // indicateur de connexion
        for (int retry = 0; retry < 5 && !connected; retry++) {                                  // retenter 5 fois tant qu'on est pas connecté
            if (connect(sock, (struct sockaddr*)&server_addr, sizeof(server_addr)) == 0) {       // tenter de se connecter
                connected = 1;                                                                   // si réussite, mettre indicateur à 1
                break;                                                                           // arrêt
            }
            sleep(1);                                                                            // attendre un peu
        }

        if (!connected) {                                                                        // si échec
            perror("échec de connexion après 5 tentatives");                                     // envoyer message
            close(sock);                                                                         // fermer la socket
            exit(EXIT_FAILURE);                                                                  // renvoyer échec 
        }

        if (send(sock, &DSM_NODE_ID, sizeof(DSM_NODE_ID), 0) < 0) {                              // envoyer le DSM_NODE_ID du processus local
            perror("send DSM_NODE_ID");                                                          // si échec, envoyer message
            close(sock);                                                                         // fermer la socket
            exit(EXIT_FAILURE);                                                                  // renvoyer échec 
        }

        sockets[i] = sock;                                                                       // stocker le descripteur de fichier de la socket correspondante
    }

    printf("[%d] Fin de dsm_comm_daemon\n", DSM_NODE_ID);                                        // message de fin

    return 0;
}


static void dsm_handler(int sockfd) {
    dsm_req_t req;
    char buffer[PAGE_SIZE];

    if (dsm_recv(sockfd, &req, sizeof(dsm_req_t)) <= 0) {
        fprintf(stderr, "[%d] Failed to receive request\n", DSM_NODE_ID);
        return;
    }

    switch (req.type) {
        case DSM_REQ: {
            int page_num = req.page_num;
            // Vérifier si on est le propriétaire actuel de la page
            if (get_owner(page_num) == DSM_NODE_ID) {
                // Envoyer la page demandée
                void *page_addr = num2address(page_num);
                memcpy(buffer, page_addr, PAGE_SIZE);

                // Envoyer d'abord un message DSM_PAGE pour signaler l'envoi de la page
                dsm_req_t page_msg;
                page_msg.type = DSM_PAGE;
                page_msg.source = DSM_NODE_ID;
                page_msg.page_num = page_num;
                // Pas de page_msg.access

                if (dsm_send(sockets[req.source], &page_msg, sizeof(dsm_req_t)) <= 0) {
                    fprintf(stderr, "[%d] Failed to send DSM_PAGE header\n", DSM_NODE_ID);
                    return;
                }

                // Puis envoyer le contenu de la page
                if (dsm_send(sockets[req.source], buffer, PAGE_SIZE) <= 0) {
                    fprintf(stderr, "[%d] Failed to send page data\n", DSM_NODE_ID);
                    return;
                }

                // Mettre à jour notre table interne
                dsm_protect_page(page_num, PROT_NONE);
                dsm_change_info(page_num, INVALID, req.source);

                // Notifier les autres processus (DSM_NREQ) pour leur dire que le owner a changé
                dsm_req_t notify_req;
                notify_req.type = DSM_NREQ;
                notify_req.source = DSM_NODE_ID;
                notify_req.page_num = page_num;
                

                for (int j = 0; j < DSM_NODE_NUM; j++) {
                    if (j != DSM_NODE_ID && j != req.source && sockets[j] >= 0) {
                        dsm_send(sockets[j], &notify_req, sizeof(dsm_req_t));
                    }
                }
            }
            break;
        }
        case DSM_PAGE: {
            // On va recevoir une page en réponse à un DSM_REQ qu’on a envoyé.
            // Le message DSM_PAGE est déjà reçu, maintenant on reçoit le contenu de la page.
            if (dsm_recv(sockfd, buffer, PAGE_SIZE) <= 0) {
                fprintf(stderr, "[%d] Failed to receive page data\n", DSM_NODE_ID);
                return;
            }

            int page_num = req.page_num;
            // Allouer la page localement
            dsm_alloc_page(page_num);
            void *page_addr = num2address(page_num);
            memcpy(page_addr, buffer, PAGE_SIZE);

            // Sans access, on suppose que l’accès est toujours en écriture
            dsm_protect_page(page_num, PROT_READ | PROT_WRITE);
            dsm_change_info(page_num, WRITE, DSM_NODE_ID);

            // Signaler que la page est prête
            page_ready = 1;
            pthread_cond_signal(&dsm_cond);

            break;
        }
        case DSM_NREQ: {
            // Notification de changement de propriétaire
            int page_num = req.page_num;
            // Le owner a changé, on invalide chez nous si on n’est pas le nouveau propriétaire
            if (get_owner(page_num) != req.source) {
                dsm_protect_page(page_num, PROT_NONE);
                dsm_change_info(page_num, INVALID, req.source);
            }
            break;
        }
        case DSM_FINALIZE: {
            // Un autre processus signale qu’il se termine.
            running = 0;
            pthread_cond_broadcast(&dsm_cond);
            break;
        }
        default:
            fprintf(stderr, "[%d] Unknown request type %d from %d\n", 
                    DSM_NODE_ID, req.type, req.source);
            break;
    }
}

/* traitant de signal adequat */
static void segv_handler(int sig, siginfo_t *info, void *context) {
    void *addr = info->si_addr;
    
    if ((addr >= (void *)BASE_ADDR) && (addr < (void *)TOP_ADDR)) {
        pthread_mutex_lock(&dsm_mutex);
        
        int page_num = address2num(addr);
        dsm_page_owner_t owner = get_owner(page_num);
        
        printf("[%d] SIGSEGV on page %d owned by %d\n", DSM_NODE_ID, page_num, owner);

        // Envoyer la requête
        dsm_req_t req;
        req.source = DSM_NODE_ID;
        req.page_num = page_num;
        req.type = DSM_REQ;

        if (dsm_send(sockets[owner], &req, sizeof(dsm_req_t)) < 0) {
            fprintf(stderr, "[%d] Failed to send page request\n", DSM_NODE_ID);
            pthread_mutex_unlock(&dsm_mutex);
            return;
        }

        // Attendre que la page soit prête
        while (!page_ready) {
            pthread_cond_wait(&dsm_cond, &dsm_mutex);
        }
        page_ready = 0;

        pthread_mutex_unlock(&dsm_mutex);
    }
}

static void *dsm_listener_thread(void *arg) {
    printf("[%d] Thread d'écoute démarré\n", DSM_NODE_ID);
    fflush(stdout);

    while (running) {
        fd_set readfds;
        int max_fd = -1;

        FD_ZERO(&readfds);
        for (int i = 0; i < DSM_NODE_NUM; i++) {
            if (sockets[i] >= 0) {
                FD_SET(sockets[i], &readfds);
                if (sockets[i] > max_fd) max_fd = sockets[i];
            }
        }

        if (max_fd < 0) {
            // Aucune socket valide, on fait une courte pause
            usleep(10000);  // 10ms
            if (!running) break;
            continue;
        }

        // Timeout pour vérifier régulièrement 'running'
        struct timeval tv = {.tv_sec = 0, .tv_usec = 100000};  // 100ms
        int ready = select(max_fd + 1, &readfds, NULL, NULL, &tv);
        if (ready < 0) {
            if (errno == EINTR) continue;  // interruption par un signal
            if (!running) break;
            perror("select");
            continue;
        }

        // Si running est passé à 0, on sort
        if (!running) break;

        for (int i = 0; i < DSM_NODE_NUM; i++) {
            if (sockets[i] >= 0 && FD_ISSET(sockets[i], &readfds)) {
                dsm_handler(sockets[i]);

                // Si dsm_handler a provoqué une finalisation (running=0)
                // on sort immédiatement pour ne pas continuer de lire sur des sockets fermées
                if (!running) {
                    break;
                }
            }
        }

        if (!running) break;
    }

    printf("[%d] Thread d'écoute terminé\n", DSM_NODE_ID);
    fflush(stdout);
    return NULL;
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
    MASTER_FD = atoi(MASTER_FD_ptr);                                                              // convertir la chaîne de caractère en entier

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

    procs = malloc(DSM_NODE_NUM * sizeof(dsm_proc_conn_t));                                       // allouer la structure d'information de connexion
    if (!procs) {                                                                                 // si échec
        perror("malloc procs_conn");                                                              // envoyer message
        exit(EXIT_FAILURE);                                                                       // renvoyer échec
    }
    memset(procs, 0, DSM_NODE_NUM * sizeof(dsm_proc_conn_t));                                     // mettre les bits de mémoire à 0

    for (int i = 0; i < DSM_NODE_NUM; i++) {                                                      // pour l'ensemble des processus
        dsm_proc_conn_t temp;                                                                     // allouer structure de stockage temporaire
        size_t total_read = 0;                                                                    // initialiser compteur de lecture
        
        while (total_read < sizeof(dsm_proc_conn_t)) {                                            // tant qu'on a pas récupéré toute la structure
            ssize_t ret = read(DSMEXEC_FD,                                                        // lire dans la socket
                      ((char*)&temp) + total_read, 
                      sizeof(dsm_proc_conn_t) - total_read);
            if (ret <= 0) {                                                                       // si échec
                printf("[dsminit] erreur lecture info processus %d\n", i);                        // envoyer message
                exit(EXIT_FAILURE);                                                               // renvoyer échec
            }
            total_read += ret;                                                                    // incrémenter compteur de lecture
        }
        
        memcpy(&procs[i], &temp, sizeof(dsm_proc_conn_t));                                        // copier dans la structure définitive
        printf("[%d] Reçu info processus %d: rank=%d, machine=%s, port=%d \n", DSM_NODE_ID, i, procs[i].rank, procs[i].machine, procs[i].port_num);
    }


    if (dsm_comm_daemon() != 0) {                                                                 // mise en place des communications par dsm_comm_daemon
        printf("[%d] Erreur initialisation communications\n", DSM_NODE_ID);                       // si échec, envoyer message
        exit(EXIT_FAILURE);                                                                       // renvoyer échec
    }

    sleep(2);                                                                                     // attendre que tous les processus soient prêts


    struct test_msg msg = {                                                                       // structure de test pour les messages
        .type = 1,                                                                                // type arbitraire
        .sender_id = DSM_NODE_ID,                                                                 // DSM_NODE_ID du processus
        .data = 42                                                                                // donnée fixée
    };

    printf("[%d] Début des tests de communication\n", DSM_NODE_ID);                               // message de début

    for(int i = 0; i < DSM_NODE_NUM; i++) {                                                       // pour l'ensemble des processus
        if (i != DSM_NODE_ID) {                                                                   // DSM_NODE_ID excepté                   

            if (dsm_send(sockets[i], &msg, sizeof(msg)) <= 0) {                                   // si échec de l'envoi du message de test
                printf("[%d] Erreur envoi message à %d\n", DSM_NODE_ID, i);                       // envoyer message
                exit(EXIT_FAILURE);                                                               // renvoyer échec
            }
        }
    }

    int messages_received = 0;                                                                    // compteur de messages reçus
    int expected_messages = DSM_NODE_NUM - 1;                                                     // nombre de messages attendus

    printf("[%d] Attente de %d messages de test\n", DSM_NODE_ID, expected_messages);              // affichage du nombre attendu

    while (messages_received < expected_messages) {                                               // tant qu'on a pas reçu tous les messages de test
        fd_set readfds;                                                                           // tableau de stockage de descripteurs de fichier                                                       
        FD_ZERO(&readfds);                                                                        // mettre à zéro le tableau
        int max_fd = -1;                                                                          // descripteur de fichier maximum de socket

        for(int i = 0; i < DSM_NODE_NUM; i++) {                                                   // pour l'ensemble des processus
            if (i != DSM_NODE_ID && sockets[i] >= 0) {                                            // s'il ne s'agit pas du processus local et que la socket est valide 
                FD_SET(sockets[i], &readfds);                                                     // stocker le descripteur de fichier de la socket dans le tableau
                if (sockets[i] > max_fd) max_fd = sockets[i];                                     // si le descripteur est supérieur au max, mettre à jour
            }
        }

        struct timeval tv = {.tv_sec = 5, .tv_usec = 0};                                          // structure d'attente de 5s
        int ready = select(max_fd + 1, &readfds, NULL, NULL, &tv);                                // une fois l'attente effectuée, renvoyer le nombre de descripteurs prêt à communiquer 

        if (ready < 0) {                                                                          // si échec
            perror("select");                                                                     // envoyer message
            exit(EXIT_FAILURE);                                                                   // afficher échec     
        } else if (ready == 0) {                                                                  // si aucun processus n'est prêt
            printf("[%d] Timeout en attente des messages\n", DSM_NODE_ID);                        // afficher message
            exit(EXIT_FAILURE);                                                                   // renvoyer échec
        }

        for(int i = 0; i < DSM_NODE_NUM; i++) {                                                   // pour l'ensemble des processus
            if (i != DSM_NODE_ID && sockets[i] >= 0 && FD_ISSET(sockets[i], &readfds)) {          // excepté DSM_NODE_ID et si la socket est valide
                struct test_msg received_msg;                                                     // initialiser une structure de stockage
                if (dsm_recv(sockets[i], &received_msg, sizeof(received_msg)) > 0) {              // recevoir la structure de test
                    printf("[%d] Reçu message de %d: type=%d, data=%d\n",                         // afficher message de test
                           DSM_NODE_ID, received_msg.sender_id, received_msg.type, 
                           received_msg.data);
                    messages_received++;                                                          // incrémenter le compteur de messages reçus
                }
            }
        }
    }
    

    for (int index = 0; index < PAGE_NUMBER; index++) {                                           //////// début de partie M. Mercier ////////
        if ((index % DSM_NODE_NUM) == DSM_NODE_ID) {
            dsm_alloc_page(index);
            dsm_change_info(index, WRITE, DSM_NODE_ID);
        } else {
            dsm_change_info(index, INVALID, index % DSM_NODE_NUM);
        }
    }                                                                                             //////// fin de partie M. Mercier ////////


    struct sigaction act;                                                                         // structure pour traitant de signaux
    memset(&act, 0, sizeof(act));                                                                 // mettre les bits de mémoire à 0
    act.sa_flags = SA_SIGINFO;                                                                    // paramétrer la structure
    act.sa_sigaction = segv_handler;                                                              // fonction traitante
    if (sigaction(SIGSEGV, &act, NULL) == -1) {                                                   // assigner au signal le traitant
        perror("sigaction");                                                                      // si échec, envoyer message
        exit(EXIT_FAILURE);                                                                       // renvoyer échec
    }
    
    /* Création du thread d'écoute */
    printf("[%d] Creating listener thread\n", DSM_NODE_ID);
    fflush(stdout);
    if (pthread_create(&listener_thread, NULL, dsm_listener_thread, NULL) != 0) {            
        perror("pthread_create");
        exit(EXIT_FAILURE);
    }

    printf("[%d] Initialisation DSM terminée\n", DSM_NODE_ID);                                    // message de fin
    
    return (char *)BASE_ADDR;                                                                     // renvoyer adresse de base
}



void dsm_finalize(void) {                                                         // fonction pour finaliser la dsm
    printf("[%d] Début de dsm_finalize\n", DSM_NODE_ID);                          // message d'entrée
    fflush(stdout);

    struct test_msg msg = {                                                       // structure de finalisation
        .type = DSM_FINALIZE,                                                     // type de fin
        .sender_id = DSM_NODE_ID,                                                 // processus local
        .data = 0                                                                 // aucune donnée
    };

    // Envoyer un message de finalisation à tous les autres processus
    // Avant de fermer les sockets, on informe les autres qu'on se finalise
    for(int i = 0; i < DSM_NODE_NUM; i++) {                                       // pour l'ensemble des processus
        if (i != DSM_NODE_ID && sockets[i] >= 0) {                                // excepté le processus local et si la socket est valide
            printf("[%d] Envoi message de finalisation à %d\n", DSM_NODE_ID, i);  // afficher message
            fflush(stdout);
            dsm_send(sockets[i], &msg, sizeof(msg));                              // envoyer message de finalisation
        }
    }

    // Avant de fermer les sockets, on doit s'assurer que le thread d'écoute n'est plus en cours d'exécution
    // pour éviter qu'il ne fasse un select ou un recv sur des sockets déjà fermées.
    pthread_mutex_lock(&dsm_mutex);                                               // verrouiller le mutex pour protéger 'running'
    running = 0;                                                                  // signaler l'arrêt du thread d'écoute
    pthread_mutex_unlock(&dsm_mutex);                                             // libérer le mutex

    // Attendre la fin du thread d'écoute
    // Le thread d'écoute va détecter que running=0 et sortir de sa boucle
    pthread_join(listener_thread, NULL);                                          // attendre la fin du thread

    // Maintenant que le thread d'écoute est terminé, on peut fermer les sockets en toute sécurité
    if (sockets) {                                                                // si le tableau des sockets est non NULL
        for(int i = 0; i < DSM_NODE_NUM; i++) {                                   // pour chaque socket
            if (sockets[i] >= 0) {                                                // si la socket est valide
                printf("[%d] Fermeture socket %d\n", DSM_NODE_ID, i);             // message indiquant la fermeture
                fflush(stdout);
                shutdown(sockets[i], SHUT_RDWR);                                  // arrêt propre de la socket
                close(sockets[i]);                                                // fermer le descripteur de fichier de la socket
                sockets[i] = -1;                                                  // marquer la socket comme invalide
            }
        }
        free(sockets);                                                            // libérer la mémoire allouée pour le tableau des sockets
        sockets = NULL;                                                           // remettre le pointeur à NULL par sécurité
    }

    // Libérer les infos de connexion des processus
    if (procs) {                                                                  // si la structure d'informations sur les processus est allouée
        free(procs);                                                              // libérer la mémoire
        procs = NULL;                                                             // remettre le pointeur à NULL
    }

    // Mettre à jour une variable d'environnement FINALIZE_COUNTER (optionnel)
    // Ce code part du principe que FINALIZE_COUNTER est défini avant
    char *FINALIZE_COUNTER_ptr = getenv("FINALIZE_COUNTER");
    if (FINALIZE_COUNTER_ptr == NULL) {
        fprintf(stderr, "[%d] Erreur : FINALIZE_COUNTER non défini\n", DSM_NODE_ID);
        fflush(stderr);
        // On peut choisir d'arrêter le programme ici ou de continuer, selon la logique du projet.
        // exit(EXIT_FAILURE);
    } else {
        int FINALIZE_COUNTER = atoi(FINALIZE_COUNTER_ptr);                        // convertir la valeur actuelle en entier
        FINALIZE_COUNTER++;                                                       // incrémenter le compteur
        char new_value[20];
        snprintf(new_value, sizeof(new_value), "%d", FINALIZE_COUNTER);           // convertir en chaîne
        if (setenv("FINALIZE_COUNTER", new_value, 1) == -1) {                     // mettre à jour la variable d'environnement
            perror("setenv");
            // exit(EXIT_FAILURE); // selon la logique du projet
        }
    }

    printf("[%d] Fin de dsm_finalize\n", DSM_NODE_ID);                            // message de sortie
    fflush(stdout);
}
