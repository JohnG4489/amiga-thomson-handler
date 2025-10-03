Mise à disposition du handler pour lire le système de fichier thomson (to8, to9, to9+) sur Amiga.
Ce handler s'appuie sur todisk.device.

Le thomson voit les faces d'un disque comme 2 disques séparés. Il existe donc deux type TA0 et TB0 pour la même unité disque. TA0 étant la face par défaut de l'unité 0, et TB0 étant l'autre face de l'unité 0, c'est-à-dire le disque "1" sur thomson.
