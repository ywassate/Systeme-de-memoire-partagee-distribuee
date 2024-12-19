#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>

int main(int argc, char *argv[]) {
    // Flush stdout après chaque écriture
    setvbuf(stdout, NULL, _IONBF, 0);
    setvbuf(stderr, NULL, _IONBF, 0);

    // Afficher des informations sur l'environnement
    printf("=== Programme de test démarré ===\n");
    printf("PID: %d\n", getpid());
    printf("Hostname: ");
    fflush(stdout);
    system("hostname");

    // Afficher les variables d'environnement
    printf("DSM_NODE_ID=%s\n", getenv("DSM_NODE_ID") ? getenv("DSM_NODE_ID") : "non défini");
    printf("DSM_NODE_NUM=%s\n", getenv("DSM_NODE_NUM") ? getenv("DSM_NODE_NUM") : "non défini");
    printf("DSM_BIN=%s\n", getenv("DSM_BIN") ? getenv("DSM_BIN") : "non défini");

    // Afficher les arguments
    printf("Arguments:\n");
    for (int i = 0; i < argc; i++) {
        printf("argv[%d] = %s\n", i, argv[i]);
    }

    // Écrire sur stderr
    fprintf(stderr, "Message de test sur stderr\n");

    // Boucle pour maintenir le programme en vie
    for (int i = 0; i < 5; i++) {
        printf("Test en cours, itération %d\n", i);
        sleep(1);
    }

    printf("=== Programme de test terminé ===\n");
    return 0;
}