# 🧠 Projet PR204 — Implémentation d’une Mémoire Partagée Distribuée (DSM)

**Encadrants** : Joachim Bruneau-Queyreix, Guillaume Mercier, Philippe Swartvagher  
📧 joachim.bruneau-queyreix@labri.fr | guillaume.mercier@enseirb-matmeca.fr | philippe.swartvagher@enseirb-matmeca.fr  
📅 Année : 2024

---

## 🎯 Objectif et contexte du projet

Le but de ce projet est de mettre en place un logiciel permettant de partager de la mémoire virtuelle (i.e. des plages d’adresses) entre plusieurs processus répartis sur différentes machines physiques.

Cette plage d’adresses (identique pour tous les processus) est divisée en pages mémoire (blocs de 4 Ko).  
Tous les processus peuvent lire et écrire sur ces pages, **mais un seul est propriétaire d’une page à un instant donné**.

Lorsqu’un autre processus tente d’accéder à une page non possédée, une erreur de segmentation `SIGSEGV` est déclenchée. Le signal est intercepté, et les étapes suivantes sont déclenchées :

1. Identifier l’adresse ayant causé le signal
2. En déduire la page mémoire concernée
3. Identifier le processus actuellement propriétaire
4. Lui demander d’envoyer la page au processus demandeur

Le processus demandeur devient alors propriétaire, alloue la page, et reprend son exécution. Les autres processus doivent également mettre à jour leurs informations pour conserver un état global cohérent.

---

## 📐 Hypothèses sur le système

Le système DSM repose sur les hypothèses suivantes :

- Tous les processus ont accès au **même système de fichiers** (ex: NFS)
- Mémoire limitée à `PAGE_NUMBER` pages
- Taille d’une page : `PAGE_SIZE` octets (ex. `sysconf(_SC_PAGE_SIZE)`)
- Les processus connaissent :
  - Leur rang via `DSM_NODE_ID`
  - Le nombre total via `DSM_NODE_NUM`
- Les rangs sont consécutifs, de 0 à `DSM_NODE_NUM - 1`
- Chaque processus peut dialoguer directement avec les autres
- Allocation cyclique des pages (round-robin)
- Adresses utilisées : de `BASE_ADDR` à `TOP_ADDR`
- Chaque processus possède une `table_pages` contenant :
  - Propriétaire de la page
  - État de la page

### 🚀 Phase 1 : Lancement des processus (dsmexec)

La première phase du projet consiste à développer un programme appelé `dsmexec`, qui permet de **lancer les différents processus DSM à distance** sur un ensemble de machines spécifiées.

Ce programme a plusieurs responsabilités :
- Lire un fichier de configuration listant les machines cibles ;
- Lancer un processus sur chaque machine via SSH ;
- Attribuer un rang (ID) à chaque processus ;
- Transmettre les informations nécessaires via des sockets ;
- Centraliser les sorties standard (`stdout`) et erreur (`stderr`) pour les afficher de façon lisible.

Un programme intermédiaire, `dsmwrap`, est utilisé pour faciliter l’exécution à distance et nettoyer la ligne de commande avant de lancer le programme DSM final.

---

### ⚙️ Phase 2 : Mise en place de la DSM

La deuxième phase vise à implémenter la **bibliothèque logicielle de DSM** qui gère :

- L’allocation des pages mémoire selon une stratégie **cyclique** ;
- La détection des accès mémoire invalides via un gestionnaire de signal (`SIGSEGV`) ;
- L’envoi et la réception de pages entre processus ;
- La **synchronisation et la cohérence** des informations de page (propriétaire, état) entre les processus.

La communication entre processus est assurée par des **sockets UNIX** (TCP ou UDP). Pour permettre aux processus de continuer leur exécution tout en répondant aux demandes des autres, **le système est multithreadé**.

---

Ce projet est une introduction aux concepts de mémoire distribuée, de synchronisation, de signalisation bas-niveau, et de communication réseau entre processus répartis sur plusieurs hôtes.

