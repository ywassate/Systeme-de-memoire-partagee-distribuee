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

#### Exemple de répartition (Figure 1)

