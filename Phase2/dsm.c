#include "dsm_impl.h"
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>

/* Variables globales */
int DSM_NODE_NUM;  /* nombre de processus dsm */
int DSM_NODE_ID;   /* rang du processus */ 
size_t page_size;
dsm_proc_conn_t *procs; 
static int *sockets = NULL;  // tableau global des sockets
static dsm_page_info_t table_page[PAGE_NUMBER];

// structure de message de test
struct test_msg {
    int type;
    int sender_id;
    int data;
} __attribute__((packed));  // Important pour la compatibilité réseau

/* Fonctions utilitaires pour la mémoire */
static char *num2address(int numpage) { 
   char *pointer = (char *)(BASE_ADDR+(numpage * page_size));
   if (pointer >= (char *)TOP_ADDR) {
      printf("[%i] Invalid address !\n", DSM_NODE_ID);
      fflush(stdout);
      return NULL;
   }
   return pointer;
}

static int address2num(char *addr) {
  return ((int)((intptr_t)(addr - BASE_ADDR) / page_size));
}

static char *address2pgaddr(char *addr) {
  return (char *)(((intptr_t) addr) & ~(page_size-1)); 
}

static void dsm_change_info(int numpage, dsm_page_state_t state, dsm_page_owner_t owner) {
   if ((numpage >= 0) && (numpage < PAGE_NUMBER)) {
       if (state != NO_CHANGE) table_page[numpage].status = state;
       if (owner >= 0) table_page[numpage].owner = owner;
   } else {
       printf("[%i] Invalid page number !\n", DSM_NODE_ID);
       fflush(stdout);
   }
}

static dsm_page_owner_t get_owner(int numpage) {
   return table_page[numpage].owner;
}

static dsm_page_state_t get_status(int numpage) {
   return table_page[numpage].status;
}

static void dsm_alloc_page(int numpage) {
   char *page_addr = num2address(numpage);
   void *res = mmap(page_addr, page_size, PROT_READ | PROT_WRITE,
                    MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED, -1, 0);
   if (res == MAP_FAILED) {
       perror("[dsm_alloc_page] mmap");
       exit(EXIT_FAILURE);
   }
}

static void dsm_protect_page(int numpage, int prot) {
   char *page_addr = num2address(numpage);
   if (mprotect(page_addr, page_size, prot) == -1) {
       perror("[dsm_protect_page] mprotect");
       exit(EXIT_FAILURE);
   }
}

static void dsm_free_page(int numpage) {
   char *page_addr = num2address(numpage);
   if (munmap(page_addr, page_size) == -1) {
       perror("[dsm_free_page] munmap");
       exit(EXIT_FAILURE);
   }
}

static int dsm_send(int dest, void *buf, size_t size) {
    ssize_t sent_bytes = 0;
    while (sent_bytes < size) {
        ssize_t n = send(dest, (char *)buf + sent_bytes, size - sent_bytes, 0);
        if (n == -1) {
            printf("[dsm_send] échec d'envoi\n");
            fflush(stdout);
            return -1;
        }
        sent_bytes += n;
    }
    return sent_bytes;  // Renvoyer le nombre d'octets envoyés
}

static int dsm_recv(int from, void *buf, size_t size) {
    ssize_t received_bytes = 0;
    while (received_bytes < size) {
        ssize_t n = recv(from, (char *)buf + received_bytes, size - received_bytes, 0);
        if (n == -1) {
            printf("[dsm_recv] échec de récupération\n");
            fflush(stdout);
            return -1;
        } else if (n == 0) {
            printf("[dsm_recv] connexion terminée\n");
            fflush(stdout);
            return 0;
        }
        received_bytes += n;
    }
    return received_bytes;  // Renvoyer le nombre d'octets reçus
}

/* Fonction pour mettre la communication entre les processus en place */
static int setup_dsm_comm(void) {
    printf("[%d] Configuration des communications\n", DSM_NODE_ID);
    fflush(stdout);

    // Création de la socket d'écoute
    int listen_sock = socket(AF_INET, SOCK_STREAM, 0);
    if (listen_sock < 0) {
        perror("socket");
        return -1;
    }

    // Obtenir l'adresse IP de la machine locale
    struct hostent *host_info;
    if ((host_info = gethostbyname(procs[DSM_NODE_ID].machine)) == NULL) {
        perror("gethostbyname");
        close(listen_sock);
        return -1;
    }

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    memcpy(&addr.sin_addr.s_addr, host_info->h_addr, host_info->h_length);

    // Configuration de SO_REUSEADDR
    int opt = 1;
    if (setsockopt(listen_sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        perror("setsockopt");
        close(listen_sock);
        return -1;
    }

    // Bind sur port dynamique
    addr.sin_port = 0;
    if (bind(listen_sock, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("bind");
        close(listen_sock);
        return -1;
    }

    // Récupérer le port attribué
    socklen_t len = sizeof(addr);
    if (getsockname(listen_sock, (struct sockaddr *)&addr, &len) < 0) {
        perror("getsockname");
        close(listen_sock);
        return -1;
    }

    int my_port = ntohs(addr.sin_port);
    printf("[%d] Bind réussi sur port %d\n", DSM_NODE_ID, my_port);
    fflush(stdout);

    // Mise en écoute
    if (listen(listen_sock, 10) < 0) {
        perror("listen");
        close(listen_sock);
        return -1;
    }

    printf("[%d] Socket en écoute\n", DSM_NODE_ID);
    fflush(stdout);

    // Allocation du tableau des sockets
    sockets = malloc(DSM_NODE_NUM * sizeof(int));
    if (!sockets) {
        perror("malloc sockets");
        close(listen_sock);
        return -1;
    }
    memset(sockets, -1, DSM_NODE_NUM * sizeof(int));

    // Phase de synchronisation - attendre que tous les processus soient prêts
    sleep(2);

    // Établissement des connexions dans un ordre déterministe
    if (DSM_NODE_ID == 0) {
        // Le processus 0 attend d'abord tous les autres
        for (int i = 1; i < DSM_NODE_NUM; i++) {
            struct sockaddr_in client_addr;
            socklen_t client_len = sizeof(client_addr);
            printf("[%d] Attente connexion de %d\n", DSM_NODE_ID, i);
            fflush(stdout);

            sockets[i] = accept(listen_sock, (struct sockaddr*)&client_addr, &client_len);
            if (sockets[i] < 0) {
                perror("accept");
                close(listen_sock);
                return -1;
            }
            printf("[%d] Accepté connexion de %d\n", DSM_NODE_ID, i);
            fflush(stdout);
        }
    } else {
        // Les autres processus se connectent au processus 0
        printf("[%d] Tentative de connexion à 0\n", DSM_NODE_ID);
        fflush(stdout);

        sockets[0] = socket(AF_INET, SOCK_STREAM, 0);
        if (sockets[0] < 0) {
            perror("socket");
            close(listen_sock);
            return -1;
        }

        struct sockaddr_in server_addr;
        memset(&server_addr, 0, sizeof(server_addr));
        server_addr.sin_family = AF_INET;
        server_addr.sin_port = htons(procs[0].port_num);

        struct hostent *server_host;
        if ((server_host = gethostbyname(procs[0].machine)) == NULL) {
            perror("gethostbyname");
            close(listen_sock);
            return -1;
        }
        memcpy(&server_addr.sin_addr.s_addr, server_host->h_addr, server_host->h_length);

        // Tentatives de connexion avec retry
        int connected = 0;
        for (int retry = 0; retry < 5 && !connected; retry++) {
            if (connect(sockets[0], (struct sockaddr*)&server_addr, sizeof(server_addr)) == 0) {
                connected = 1;
                printf("[%d] Connecté à 0\n", DSM_NODE_ID);
                fflush(stdout);
            } else {
                sleep(1);
            }
        }

        if (!connected) {
            perror("connect failed after retries");
            close(listen_sock);
            return -1;
        }

        // Puis ils établissent les connexions entre eux dans l'ordre
        for (int i = 1; i < DSM_NODE_NUM; i++) {
            if (i == DSM_NODE_ID) {
                // Attendre les connexions des processus suivants
                for (int j = i + 1; j < DSM_NODE_NUM; j++) {
                    struct sockaddr_in client_addr;
                    socklen_t client_len = sizeof(client_addr);
                    printf("[%d] Attente connexion de %d\n", DSM_NODE_ID, j);
                    fflush(stdout);

                    sockets[j] = accept(listen_sock, (struct sockaddr*)&client_addr, &client_len);
                    if (sockets[j] < 0) {
                        perror("accept");
                        close(listen_sock);
                        return -1;
                    }
                    printf("[%d] Accepté connexion de %d\n", DSM_NODE_ID, j);
                    fflush(stdout);
                }
            } else if (i < DSM_NODE_ID) {
                // Se connecter aux processus précédents
                printf("[%d] Tentative de connexion à %d\n", DSM_NODE_ID, i);
                fflush(stdout);

                sockets[i] = socket(AF_INET, SOCK_STREAM, 0);
                if (sockets[i] < 0) {
                    perror("socket");
                    close(listen_sock);
                    return -1;
                }

                struct sockaddr_in peer_addr;
                memset(&peer_addr, 0, sizeof(peer_addr));
                peer_addr.sin_family = AF_INET;
                peer_addr.sin_port = htons(procs[i].port_num);

                struct hostent *peer_host;
                if ((peer_host = gethostbyname(procs[i].machine)) == NULL) {
                    perror("gethostbyname");
                    close(listen_sock);
                    return -1;
                }
                memcpy(&peer_addr.sin_addr.s_addr, peer_host->h_addr, peer_host->h_length);

                // Tentatives de connexion avec retry
                connected = 0;
                for (int retry = 0; retry < 5 && !connected; retry++) {
                    if (connect(sockets[i], (struct sockaddr*)&peer_addr, sizeof(peer_addr)) == 0) {
                        connected = 1;
                        printf("[%d] Connecté à %d\n", DSM_NODE_ID, i);
                        fflush(stdout);
                    } else {
                        sleep(1);
                    }
                }

                if (!connected) {
                    perror("connect failed after retries");
                    close(listen_sock);
                    return -1;
                }
            }
        }
    }

    close(listen_sock);
    printf("[%d] Toutes les connexions établies\n", DSM_NODE_ID);
    fflush(stdout);
    return 0;
}
static void dsm_handler(void) {
   printf("[%i] FAULTY ACCESS !!! \n", DSM_NODE_ID);
   fflush(stdout);
   abort();
}

static void segv_handler(int sig, siginfo_t *info, void *context) {
    void *addr = info->si_addr;
    if ((addr >= (void*)BASE_ADDR) && (addr < (void*)TOP_ADDR)) {
        dsm_handler();
    } else {
        signal(SIGSEGV, SIG_DFL);
        raise(SIGSEGV);
    }
}

char *dsm_init(int argc, char *argv[]) {
    printf("[DSM] Démarrage dsm_init sur le processus %d\n", getpid());
    fflush(stdout);
    
    page_size = (size_t)sysconf(_SC_PAGE_SIZE);

    // Récupération des descripteurs de fichiers depuis les variables d'environnement
    char *DSMEXEC_FD_ptr = getenv("DSMEXEC_FD");
    char *MASTER_FD_ptr = getenv("MASTER_FD");
    
    if (DSMEXEC_FD_ptr == NULL || MASTER_FD_ptr == NULL) {
        printf("[dsminit] erreur de récupération de DSMEXEC_FD ou MASTER_FD\n");
        fflush(stdout);
        exit(EXIT_FAILURE);
    }

    int DSMEXEC_FD = atoi(DSMEXEC_FD_ptr);
    int MASTER_FD = atoi(MASTER_FD_ptr); 

    printf("[DSM] Variables d'environnement lues: DSMEXEC_FD=%d, MASTER_FD=%d\n", 
           DSMEXEC_FD, MASTER_FD);
    fflush(stdout);

    /* Lecture du nombre de processus */
    if (read(DSMEXEC_FD, &DSM_NODE_NUM, sizeof(int)) != sizeof(int)) {
        printf("[dsminit] erreur lecture DSM_NODE_NUM\n");
        fflush(stdout);
        exit(1);
    }

    /* Lecture de l'ID du noeud */
    if (read(DSMEXEC_FD, &DSM_NODE_ID, sizeof(int)) != sizeof(int)) {
        printf("[dsminit] erreur lecture DSM_NODE_ID\n");
        fflush(stdout);
        exit(1);
    }

    printf("[DSM] Nombre de processus: %d, ID local: %d\n", DSM_NODE_NUM, DSM_NODE_ID);
    fflush(stdout);

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
        printf("[%d] Reçu info processus %d: rank=%d, machine=%s, port=%d\n",
               DSM_NODE_ID, i, procs[i].rank, procs[i].machine, procs[i].port_num);
    }

    printf("[DSM] Début allocation des pages\n");
    fflush(stdout);

    
    /* Mise en place des communications */
    printf("[%d] Début setup_dsm_comm\n", DSM_NODE_ID);
    fflush(stdout);

    printf("[%d] Initialisation des communications\n", DSM_NODE_ID);
    fflush(stdout);

    // Création et bind de la socket d'écoute
    int listen_sock = socket(AF_INET, SOCK_STREAM, 0);
    if (listen_sock < 0) {
        perror("socket");
        exit(1);
    }

    struct hostent *host_info = gethostbyname(procs[DSM_NODE_ID].machine);
    if (!host_info) {
        perror("gethostbyname");
        exit(1);
    }

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    memcpy(&addr.sin_addr.s_addr, host_info->h_addr, host_info->h_length);
    addr.sin_port = 0;  // Port dynamique

    int opt = 1;
    if (setsockopt(listen_sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        perror("setsockopt");
        exit(1);
    }

    if (bind(listen_sock, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("bind");
        exit(1);
    }

    // Récupérer le port attribué
    socklen_t len = sizeof(addr);
    if (getsockname(listen_sock, (struct sockaddr *)&addr, &len) < 0) {
        perror("getsockname");
        exit(1);
    }

    int my_port = ntohs(addr.sin_port);
    printf("[%d] Bind réussi sur port %d\n", DSM_NODE_ID, my_port);
    fflush(stdout);

    // Envoi du nouveau port à dsmexec
    if (write(MASTER_FD, &my_port, sizeof(my_port)) != sizeof(my_port)) {
        perror("write port");
        exit(1);
    }

    // Attendre que tous les processus aient envoyé leur port
    for (int i = 0; i < DSM_NODE_NUM; i++) {
        int new_port;
        if (i != DSM_NODE_ID) {
            if (read(DSMEXEC_FD, &new_port, sizeof(new_port)) != sizeof(new_port)) {
                perror("read port");
                exit(1);
            }
            procs[i].port_num = new_port;
            printf("[%d] Reçu nouveau port %d pour processus %d\n", 
                   DSM_NODE_ID, new_port, i);
            fflush(stdout);
        } else {
            procs[i].port_num = my_port;
        }
    }

    close(DSMEXEC_FD);  // On peut maintenant fermer DSMEXEC_FD

    if (listen(listen_sock, 10) < 0) {
        perror("listen");
        exit(1);
    }

    printf("[%d] Socket en écoute\n", DSM_NODE_ID);
    fflush(stdout);

    if (setup_dsm_comm() != 0) {
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