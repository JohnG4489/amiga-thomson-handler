#ifndef UTIL_H
#define UTIL_H

/***** VARIABLES ET FONCTIONS    *****/
/***** PUBLIQUES UTILISABLES PAR *****/
/***** D'AUTRES BLOCS DU PROJET  *****/

extern void Utl_NormalizeName(char *, LONG);
extern void Utl_NormalizeString(char *, LONG);
extern void Utl_SplitHostName(const char *, char *);

extern LONG Utl_CompareHostName(const char *, const LONG *, const char *, const LONG *, BOOL);

extern void Utl_ConvertThomsonNameToHostName(const UBYTE *, char *);
extern void Utl_ConvertHostNameToThomsonName(const char *, UBYTE *);

extern void Utl_ConvertHostCommentToThomsonComment(const char *, UBYTE *, LONG *, LONG *);
extern void Utl_ConvertThomsonCommentToHostComment(const UBYTE *, char *);
extern LONG Utl_SetCommentMetaData(const char *, char *, LONG, LONG);

extern void Utl_ConvertHostLabelToThomsonLabel(const char *, UBYTE *);
extern void Utl_ConvertThomsonLabelToHostLabel(const UBYTE *, char *);

extern const char *Utl_ExtractInfoFromHostComment(const char *, LONG *, LONG *);
extern LONG Utl_GetTypeFromHostName(const char *);
extern LONG Utl_GetTypeFromThomsonName(const UBYTE *);
extern void Utl_FixedStringToCString(const UBYTE *, LONG, char *);

extern BOOL Utl_CheckName(const char *, const char *);

#endif  /* UTIL_H */
