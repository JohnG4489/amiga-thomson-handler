#ifndef DEBUG_H
#define DEBUG_H


#define Debug(f) f

/***** VARIABLES ET FONCTIONS    *****/
/***** PUBLIQUES UTILISABLES PAR *****/
/***** D'AUTRES BLOCS DU PROJET  *****/

extern LONG T(char *, ...);
extern LONG Dbg_ShowMessage(char *);

#endif  /* DEBUG_H */