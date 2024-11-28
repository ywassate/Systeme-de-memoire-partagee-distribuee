#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <errno.h>
#include <unistd.h>
#include <assert.h>
#include <string.h>
#include <sys/wait.h>
#include <sys/mman.h>
#include <signal.h>
#include <pthread.h>


#define MAX_SIZE (30)     // taille maximum des mots exo 29
#define NPROCS (3)        // nombre de processus enfant exo 51-3
#define NTHREADS85 (2)    // nombre de threads exo 85
#define NTHREADS86 (10)   // nombre de threads exo 86
#define CHILD_NUMBER (10) // nombre de processus enfants exo 99
#define MAP_SIZE (4*1024) // taille de la zone de mémoire partagée exo 99


typedef struct {
    int a;
    char b;
    int c;
    char d;
} my_args_t;


void *T1(void *arg) {     // fonction exo 85 1
    fprintf(stdout, "Thread 1 \n");
    return NULL;
}

void *T2(void *arg) {     // fonction exo 85 1
    fprintf(stdout, "Thread 2 \n");
    return NULL;
}

void *T3(void *arg) {     // fonction exo 85 2

    fprintf(stdout, "Thread numéro %i\n", arg);
    return NULL;
}


void *T86(void *arg) {     // fonction exo 86 1

    int myarg = *((int *)arg);

    fprintf(stdout, "Thread %i\n", myarg);
    return NULL;
}

void *T86_2(int arg1, char arg2, int arg3, char arg4) {

    int *val = malloc(sizeof(int));

    fprintf(stdout, "==> %i, %c, %i, %c\n", arg1, arg2, arg3, arg4);

    *val = arg1 + arg2 + arg3 + arg4;

    return (void *)val;
}

void *wrapper(void *arg) {
    my_args_t *ptr = (my_args_t *)arg;
    return T86_2(ptr->a, ptr->b, ptr->c, ptr->d);
}




int main(int argc, char const *argv[])
{
    
    /*  EXO PAGE 21

    int fd1 = open("exo1.txt",O_RDONLY | O_CREAT, S_IRWXU);  //Création mode lecture seule avec droit de lecture, écriture et exécution
    fprintf(stdout, "Valeur du descripteur: %i\n", fd1); //3 est attendu (0,1,2 déjà pris et aucun autre programme lancé)
    close(fd1);                  //fermeture du fichier
    
    */
    

    /*  EXO PAGE 23

    int entier = 33; int resultat = 0;
    int fd = open(argv[1], O_RDWR | O_CREAT, S_IRWXU);
    int fd2 = open(argv[1], O_RDONLY, S_IRWXU);        //besoin d'ouvrir un deuxième descripteur car position courant du premier à la fin du fichier
    fprintf(stdout, "Valeur du descripteur 1 : %i\n", fd);
    fprintf(stdout, "Valeur du descripteur 2 : %i\n", fd2);
    assert(3==fd); //vérification du bon index de descripteur
    write(fd, &entier, sizeof(int));    //&entier adresse de l'entier à écrire
    read(fd2, &resultat, sizeof(int));  //&resultat adresse allouée pour stocker le résultat de la lecture
    fprintf(stdout, "Valeur de l'entier : %i\n", resultat);
    fprintf(stdout, "Valeur du caractère : %c\n", resultat);
    close(fd);
    close(fd2); 
    
    */
    

    /* EXO PAGE 24 

    int fd = open(argv[1], O_RDWR | O_CREAT, S_IRWXU);
    int n = atoi(argv[2]);
    char c = 'A';  // Commencer par la lettre 'a'

    for (int i = 1; i <= n; i++) {
        // Calculer la position 2^i
        off_t pos = (off_t)(1 << i);  // 1 << i est équivalent à 2^i
        // Déplacer le curseur à la position 2^i
        lseek(fd, pos, SEEK_SET);
        // Écrire le caractère dans le fichier
        write(fd, &c, 1);       
        c++;
    }
    close(fd);

    */


    /* EXO PAGE 26 

    int fd = open("exopage26.txt", O_RDWR | O_CREAT, S_IRWXU);
    close(STDOUT_FILENO); //fermeture de la sortie standard
    dup(fd);
    close(fd);
    printf("message\n");

    */


    /* EXO PAGE 27 

    FILE *fp = fopen("exo27.txt", "r");
    int fd = open("exo27.txt", O_RDWR);
    printf("Valeur du descripteur : %i\n", fd);
    close(fd);
    fclose(fp);

    */


    /* EXO PAGE 29 

    int fd = open(argv[1], O_RDONLY);  //ouverture lecture seule
    int count_line = 0;                //compteur de lignes
    int char_in_line = 0;              //compteur de caractères dans la ligne
    char explorer;                     //caractère qui va 'explorer' le fichier avec read
    ssize_t read_result;               //résultat de l'appel à read

    while ((read_result = read(fd, &explorer, 1)) > 0) {  //tant que read fonctionne (pas de EOF)
        if (explorer != '\n') { //si pas de saut de ligne
            char_in_line++;     //incrémenter nombre de caractères dans la ligne
        } else {
            if (char_in_line > 0) {  // ligne non vide ET saut de ligne trouvé
                count_line++;       //compter une ligne de plus
            }
            char_in_line = 0;  // réinitialiser pour la ligne suivante
        }
    }

    close(fd); // Fermer le fichier après la lecture

    fd = open(argv[1], O_RDONLY); // réouvrir le fichier pour le lire à nouveau et stocker les mots

    char **tab = malloc(count_line * sizeof(char *)); // allouer un tableau pour stocker les mots (chaque mot a une taille MAX_SIZE)
    for (int i = 0; i < count_line; i++) {
        tab[i] = malloc(MAX_SIZE * sizeof(char)); // allouer un espace pour chaque mot
    }

    int index_tab = 0; //position dans le tableau
    int char_index = 0; //position dans la ligne
    // Lire les mots ligne par ligne et les stocker dans tab
    char_in_line = 0; // réinitialiser le compteur de caractères dans la ligne

    while ((read_result = read(fd, &explorer, 1)) > 0) {  //tant que read fonctionne (pas de EOF)
        if (explorer != '\n') {                        //s'il n'y a pas de saut de ligne
            tab[index_tab][char_index] = explorer;  // Stocker le caractère
            char_in_line++;                         //incrémenter le nombre de caractère dans la ligne
            char_index++;                           //incrémenter la position dans la ligne
        } else {
            if (char_in_line > 0) {  // Si la ligne n'est pas vide
                tab[index_tab][char_index] = '\0';  // Terminer la chaîne
                index_tab++;       //passer au mot suivant
                char_in_line = 0;  // réinitialiser le compteur de caractère
                char_index = 0;    // réanitialiser la position dans la ligne
            }
        }
    }

    // Afficher le contenu du tableau
    for (int i = 0; i < index_tab; i++) { //pour chaque mot du tableau
        printf("%s\n", tab[i]);           //afficher le mot
        free(tab[i]);  // libérer la mémoire du mot
    }

    free(tab);  // libérer la mémoire du tableau
    close(fd);  //fermer le processus

    */


    /* TP PAGE 30 */

    //correction avec chatgpt, comparaison entre libc et appel système en terme de performances

    
    /* EXO PAGE 33 

    pid_t currentPID = getpid();
    fprintf(stdout, "PID du processus : %i\n", currentPID);

    */


    /* EXO PAGE 36 

    #1 et 2 
    fprintf(stdout, "==== PID : %i", getpid());
    fflush(stdout);
    pid_t process_son = fork();
    fprintf(stdout, "PID : %i\n", process_son);
    

    #3
    pid_t son = -1;

    for(int i=0; i<10; i++) {                              //on souhaite créer 10 processus enfant
        son = fork();                                      //son = PID renvoyé par fork
        fprintf(stdout, "PID du processus: %i\n", son);    //fprintf le PID 
        if(0 == son) {                                     //si le processus est un enfant
            break;                                         //arrêter la boucle pour ne pas créer de nouveau processus 
        }
    }

    */


    /* EXO PAGE 37 

    #1
    pid_t son = -1;
    son = fork();
    if(0 == son) {                        //si dans le processus enfant
        pid_t parent_pid = getppid();
        fprintf(stdout, "PID du processus parent : %i\n", parent_pid); 
    }
    else {                                //si dans le processus parent
        fprintf(stdout, "PID du processus enfant : %i\n", son);
    }

    #2
    pid_t current_pid = getpid();
    fprintf(stdout, "PID du processus parent : %i\n", current_pid);
    pid_t son = fork();
    if(0 == son) {                        //si dans le processus enfant
        pid_t parent_pid = getppid();
        pid_t newpid = getpid();
        fprintf(stdout, "Mon PID : %i\n", newpid);
        fprintf(stdout, "PID de mon parent : %i\n", parent_pid); 
    }
    else {                                //si dans le processus parent
        fprintf(stdout, "PID de mon enfant : %i\n", son);
    }

    */
    

    /* EXO PAGE 40 

    pid_t son = fork();
    assert(son != -1);

    if(son > 0) {                     //si dans le processus parent
        fprintf(stdout, "PID de mon enfant : %i\n", son);
        int status = 0;

        pid_t pid = wait(&status);

        fprintf(stdout, "PID de mon enfant terminé : %i | code de retour %i\n", pid, WEXITSTATUS(status));
    }
    else if(0 == son) {
        fprintf(stdout, "Mon PID est : %i | PID Parent : %i\n", getpid(), getppid());

        int toto = 0;
        while(++toto);

        exit(33);
    }

    */

    /* EXO PAGE 46

    int fd1 = open("exo46.txt",O_RDONLY | O_CREAT, S_IRWXU);  //Création mode lecture seule avec droit de lecture, écriture et exécution
    fprintf(stdout, "Valeur du descripteur: %i\n", fd1); 

    */

    /* EXO PAGE 51 1 

    int pipefd[2];  // tableau des descripteurs associés au tube

    if (pipe(pipefd) == -1) {  // création du tube et gestion d'erreur
        perror("pipe");
        exit(EXIT_FAILURE);
    }

    pid_t pid = fork();       // création du processus enfant
    if (pid == -1) {          // erreur du fork
        perror("fork");
        exit(EXIT_FAILURE);
    }

    if (0 == pid) {           // processus enfant

        close(pipefd[0]);     // ferme le côté lecture
        char *message = (char *)argv[1];           // message à transmettre
        size_t sent = 0;
        size_t message_len = strlen(message) + 1;  // inclure '\0' dans la taille

        while (sent < message_len) {               // boucle d'envoi
            ssize_t ret = write(pipefd[1], message + sent, message_len - sent);
            if (ret == -1) {                       // gestion d'erreur d'écriture
                perror("write");
                exit(EXIT_FAILURE);
            }
            sent += ret;
        }
        close(pipefd[1]);                          // ferme le côté écriture une fois terminé
        exit(EXIT_SUCCESS);

    } else {                                       // processus parent
        close(pipefd[1]);                          // ferme le côté écriture
        char buffer[strlen(argv[1]) + 1];          // buffer de lecture
        size_t msg_read = 0;
        size_t message_len = strlen(argv[1]) + 1;  // taille du message attendu

        while (msg_read < message_len) {           // boucle de lecture
            ssize_t ret = read(pipefd[0], buffer + msg_read, message_len - msg_read);
            if (ret == -1) {                       // gestion d'erreur de lecture
                perror("read");
                exit(EXIT_FAILURE);
            }
            msg_read += ret;
        }

        printf("Le parent de PID %i a reçu : %s\n", getpid(), buffer);
        close(pipefd[0]);                         // ferme le côté lecture une fois terminé
        wait(NULL);                               // attendre la fin du processus enfant
    }  

    */
    

    /* EXO PAGE 51 2 

    const char* pipe_name = "tube1";

    if (mkfifo(pipe_name, S_IRWXU) == -1) {            // ouverture du tube
        perror("mkfifo");
        exit(EXIT_FAILURE);
        
    }

    pid_t pid = fork();                                // création du processus enfant
    if (pid == -1) {                                   // gestion d'erreur du fork
        perror("fork");
        exit(EXIT_FAILURE);
    }

    if (0 == pid) {                                    // si dans le processus enfant

        char *message = (char *)argv[1];               // message à transmettre
        size_t sent = 0;
        size_t message_len = strlen(message) + 1;      // inclure EOF dans la taille


        int pipefd_write = open(pipe_name, O_WRONLY);  // ouverture du côté écriture
        if (pipefd_write == -1) {
            perror("open write");
            exit(EXIT_FAILURE);
        }

        while (sent < message_len) {                   // boucle d'envoi
            ssize_t ret = write(pipefd_write, message + sent, message_len - sent);
            if (ret == -1) {                           // gestion d'erreur d'écriture
                perror("write");
                exit(EXIT_FAILURE);
            }
            sent += ret;
        }

        close(pipefd_write);                           // fermer le côté écriture une fois terminé
        exit(EXIT_SUCCESS);

    } else {  // Processus parent

        char buffer[strlen(argv[1]) + 1];              // buffer de lecture
        size_t msg_read = 0;
        size_t message_len = strlen(argv[1]) + 1;      // taille du message attendu


        int pipefd_read = open(pipe_name, O_RDONLY);   // ouverture du côté lecture
        if (pipefd_read == -1) {
            perror("open read");
            exit(EXIT_FAILURE);
        }

        while (msg_read < message_len) {               // boucle de lecture
            ssize_t ret = read(pipefd_read, buffer + msg_read, message_len - msg_read);
            if (ret == -1) {                           // gestion d'erreur de lecture
                perror("read");
                exit(EXIT_FAILURE);
            }
            msg_read += ret;
        }

        printf("Le parent de PID %i a reçu : %s\n", getpid(), buffer);

        close(pipefd_read);                            // fermer le côté lecture une fois terminé
        wait(NULL);                                    // attendre la fin du processus enfant
    }

    unlink(pipe_name);                                 // supprimer le tube

    */


    /* EXO PAGE 51 3 

    int fd1[2];
    int fd2[2];
    int index = 0;
  
    pipe(fd1);      // premier tube
    pipe(fd2);      // deuxième tube

    for(index = 0 ; index < NPROCS ; index++){
        pid_t child = fork();                   // création des processus enfants

    if(0 == child)
      break;
    }

    if(0 == index) {            // code du premier enfant
        close(fd2[0]);          // pas de lecture dans le deuxième tube
        close(fd2[1]);          // pas d'écriture dans le deuxième tube
        close(fd1[0]);          // pas de lecture dans le premier tube

        close(STDOUT_FILENO);   // fermeture sortie standard
        dup(fd1[1]);            // redirection en écriture de la sortie standard
        close(fd1[1]);          // fermeture du descripteur inutile
    
        execlp("cat","cat","toto.txt",NULL);
        
    } else if ( 1 == index ) {  // code du second enfant
        close(fd1[1]);          // pas d'écriture dans le premier tube
        close(fd2[0]);          // pas de lecture dans le deuxième tube

        close(STDIN_FILENO);    // fermeture entrée standard
        dup(fd1[0]);            // redirection en lecture de l'entrée standard
        close(fd1[0]);          // fermeture du descripteur de fichier inutile
 
        close(STDOUT_FILENO);   // fermeture de la sortie standard
        dup(fd2[1]);            // redirection en écriture de la sortie standard
        close(fd2[1]);          // fermeture du descripteur de fichier inutile
    
        execlp("grep","grep","truc",NULL);

    } else if ( 2 == index ) {  // code du dernier enfant
        close(fd1[0]);          // pas de lecture dans le premier tube
        close(fd1[1]);          // pas d'écriture dans le premier tube
        close(fd2[1]);          // pas d'écriture dans le deuxième tube

        close(STDIN_FILENO);    // fermeture de l'entrée standart
        dup(fd2[0]);            // redirection en lecture de l'entrée standard
        close(fd2[0]);          // fermeture du descripteur de fichier inutile
    
        execlp("wc","wc","-l",NULL);
    
    } else if ( NPROCS == index ) { // code du parent
        close(fd1[0]);              // aucune lecture/écriture dans le tube 1
        close(fd1[1]);              
        close(fd2[0]);              // aucune lecture/écriture dans le tube 2
        close(fd2[1]);
   
    for(index = 0 ; index < NPROCS ; index++){
        wait(NULL);                 // attente de la terminaison de tous ses enfants
        }
    }

    */


    /* EXO PAGE 54 1 

    // programme launcher
    // arguments : nombre du fichier de projection (exo54_1.txt) / chaîne de caractère à transférer

    int index;

    mkfifo("exo54_1.txt", S_IRWXU);

    int fd = open(argv[1], O_RDWR | O_CREAT, S_IRWXU);
    assert(fd > 0);
    ftruncate(fd, 4096);
    close(fd);

    for(index = 0; index < 2; index++) {
        pid_t child = fork();
        if(0 == child) {
            break;
        } else {
            fprintf(stdout, "processus %i créé \n", child);
        }
    }

    if (0 == index) {

        execlp("./exo54_1writer.c", "exo54_1writer.c", argv[1], argv[2], NULL);

    } else if (1 == index) {
        
        execlp("./exo54_1reader.c", "exo54_1reader.c", argv[1], argv[2], NULL);

    } else if (2 == index) {
        for (index = 0; index < 2; index++) {
            pid_t child = wait(NULL);
            fprintf(stdout, "[%i] === processus terminé %i\n", getpid(), child);
        }

        unlink(argv[1]);
    }
    
    unlink("exo54_1.txt");

    */
    

    /* EXO PAGE 67 1

    fprintf(stdout, "Mon PID est : %i\n", getpid());

    while(1);

    */

   /* EXO PAGE 67 2 

   pid_t child = fork();
   assert(child > 0);pthread_t tids[NTHREADS86];

    void *(*(tab[NTHREADS86]))(void *arg) = {T86};

    int args[NTHREADS86] = {0};

    for(int i = 0; i < NTHREADS86; i++) {
        args[i] = i;
        pthread_create(&tids[i], NULL, tab[0], (void *)&args[i]);
    }

    sleep(1);

    exit(EXIT_SUCCESS);

   if(0 == child) {      // code du processus enfant
        while(1);
   } else {              // code du processus parent
        fprintf(stdout, "Je suis : %i, mon enfant est %i\n", getpid(), child);
        kill(child, 9);  // envoyer signal de fin au processus enfant
   }

   */


   /* EXO PAGE 72
   
   VOIR mise_en_place_traitant.c

   */


    /* EXO PAGE 85 

    pthread_t tids[NTHREADS85];

    void *(*(tab[NTHREADS85]))(void *arg) = {T1, T2};

    for(int i = 0; i < NTHREADS85; i++) {
        pthread_create(&tids[i], NULL, tab[i], NULL);
    }

    sleep(1);

    exit(EXIT_SUCCESS);

    */


    /* EXO PAGE 85 2 
    
    pthread_t tids[NTHREADS85];

    void *(*(tab[NTHREADS85]))(void *arg) = {T3, T3};

    int args[NTHREADS85] = {11, 33};

    for(int i = 0; i < NTHREADS85; i++) {
        pthread_create(&tids[i], NULL, tab[i], (void *)&(args[i]));
    }

    sleep(1);

    exit(EXIT_SUCCESS);

    */


    /* EXO PAGE 86 1 
    
    pthread_t tids[NTHREADS86];

    int args[NTHREADS86];

    for(int i = 0; i < NTHREADS86; i++) {
        args[i] = i;
        pthread_create(&tids[i], NULL, T86, (void *)&args[i]);
    }

    sleep(1);

    exit(EXIT_SUCCESS);

    */


    /* EXO PAGE 86 3 

    pthread_t tid;

    my_args_t arg = {.a = 100, .b = 'b', .c = 45, .d = 'd'};
    
    pthread_create(&tid, NULL, wrapper, (void *)&arg);

    sleep(1);

    exit(EXIT_SUCCESS);
    
    */


    /* EXO PAGE 88 1 

    pthread_t tid;

    my_args_t arg = {.a = 100, .b = 'b', .c = 45, .d = 'd'};

    pthread_create(&tid, NULL, wrapper, (void *)&arg);

    void *retval = NULL;

    pthread_join(tid, &retval);

    fprintf(stdout, "valeur de retour %i\n", *(int *)retval);

    free(retval);
    
    exit(EXIT_SUCCESS);

    */


    /* EXO PAGE 99 

    pid_t pid_fils[CHILD_NUMBER];                          // tableau de pid processus enfants
    sem_t *sem_addr = NULL;                                // adresse du sémaphore
    char  *base_addr = NULL;                               // adresse de la zone de mémoire partagée
    char  *curr_addr = NULL;                               // adresse actuelle dans la zone de mémoire partagée
    int i;                                                 // variable pour créer les processus enfants

                                                           // création de la zone de mémoire partagée en anonyme avec droit lecture + écriture
    base_addr = (char *)mmap(NULL, sizeof(int), PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    curr_addr = base_addr;

    sem_addr = (sem_t *)curr_addr;                         // pointeur du sémaphore

    assert((((intptr_t)sem_addr)%(sizeof(sem_t))) == 0 );  // vérifier si l'adresse du sémaphore est alignée sur la zone de mémoire partagée

    curr_addr += = sizeof(sem_t);                          // décaler la tête de lecture de la mémoire partagée
    sem_init(sem_addr, 1, 0);                              // initialiser le sémaphore avec 0 jetons


    for(i = 0 ; i < CHILD_NUMBER; i++) {                   // on souhaite créer 10 processus enfant
        pid_fils[i] = fork();                              // son = PID renvoyé par fork
        if(0 == pid_fils[i]) {                             // si le processus est un enfant
            break;                                         // arrêter la boucle pour ne pas créer de nouveau processus 
        }
    }

    if(i < CHILD_NUMBER) {

    }

    munmap(memory, sizeof(int));                           // libérer la mémoire partagée

    */


    /* EXO PRECEDENT AVEC BARRIERE DE SYNCHRO 

    pid_t pid_fils[CHILD_NUMBER];                          // tableau de pid processus enfants
    int i;                                                 // variable pour créer les processus enfants
    pthread_barrier_t *restrict barriere = NULL;           // pointeur de barrière

    pthread_barrier_init(barriere, PTHREAD_PROCESS_SHARED, CHILD_NUMBER);

    for(i = 0 ; i < CHILD_NUMBER; i++) {                   // on souhaite créer 10 processus enfant
        pid_fils[i] = fork();                              // son = PID renvoyé par fork
        if(0 == pid_fils[i]) {                             // si le processus est un enfant
            pthread_barrier_wait(barriere);                // l'enfant signale la jonction de la barrière
            break;                                         // arrêter la boucle pour ne pas créer de nouveau processus 
        }
    }

    for(i = 0 ; i < CHILD_NUMBER; i++) {                   // pour les 10 processus enfant
        fprintf("Fils : %i\n",pid_fils[i]);                // message envoyé par l'enfant 
    }
    */


    exit(EXIT_SUCCESS);
    return 0;
}


