#include "dsm.h"


int main(int argc, char **argv) {
  
  char *pointer; 
  char *current;
  int value;

  pointer = dsm_init(argc,argv);
  current = pointer;

  printf("[%i] Coucou, mon adresse de base est : %p\n", DSM_NODE_ID, pointer);
   
  if (DSM_NODE_ID == 0) {
    current += 4*sizeof(int);
    value = *((int *)current);
    printf("[%i] valeur de l'entier : %i\n", DSM_NODE_ID, value);
  
  } else if (DSM_NODE_ID == 1) {
    //current += PAGE_SIZE;
    current += 16*sizeof(int);
    value = *((int *)current);
    printf("[%i] valeur de l'entier : %i\n", DSM_NODE_ID, value);
  }


  if (setenv("FINALIZE_COUNTER", "0", 1) == -1) {  
    perror("setenv");
    exit(EXIT_FAILURE);
    }

  
  char *FINALIZE_COUNTER_ptr = getenv("FINALIZE_COUNTER");
  if (FINALIZE_COUNTER_ptr == NULL) {
    fprintf(stderr, "Erreur : FINALIZE_COUNTER non défini\n");
    exit(EXIT_FAILURE);
  }

  sleep(5);

  int FINALIZE_COUNTER = atoi(FINALIZE_COUNTER_ptr);

  FINALIZE_COUNTER_ptr = getenv("FINALIZE_COUNTER");
    if (FINALIZE_COUNTER_ptr == NULL) {
        fprintf(stderr, "Erreur : FINALIZE_COUNTER non défini\n");
        exit(EXIT_FAILURE);
    }

  FINALIZE_COUNTER = atoi(FINALIZE_COUNTER_ptr);

  if(FINALIZE_COUNTER == DSM_NODE_NUM-1) {
    return 1;
  }
  else {
    dsm_finalize();
    return 1;
  }

  
  
  return 1;
}
