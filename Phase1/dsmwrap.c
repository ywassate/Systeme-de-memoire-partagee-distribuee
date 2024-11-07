#include "common_impl.h"

int main(int argc, char *argv[])
{   
   /* processus intermediaire pour "nettoyer" */
   /* la liste des arguments qu'on va passer */
   /* a la commande a executer finalement  */
   
   /* creation d'une socket pour se connecter au */
   /* au lanceur et envoyer/recevoir les infos */
   /* necessaires pour la phase dsm_init */   
   
   /* Envoi du nom de machine au lanceur */
   /* Envoi du pid au lanceur (optionnel) */

   /* Creation de la socket d'ecoute pour les */
   /* connexions avec les autres processus dsm */

   /* Envoi du numero de port au lanceur */
   /* pour qu'il le propage à tous les autres */
   /* processus dsm */
 
   /* on execute la bonne commande */
   /* attention au chemin à utiliser ! */

   /************** ATTENTION **************/
   /* vous remarquerez que ce n'est pas   */
   /* ce processus qui récupère son rang, */
   /* ni le nombre de processus           */
   /* ni les informations de connexion    */
   /* (cf protocole dans dsmexec)         */
   /***************************************/
  
   return 0;
}
