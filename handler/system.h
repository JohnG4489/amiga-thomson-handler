#ifndef SYSTEM_H
#define SYSTEM_H

#define PLATFORM_AMIGA 1
#define SYSTEM_AMIGA 1

#ifndef PLATFORM_PC
#include <exec/exec.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/utility.h>


/* quelques defines definis pour un portage sur une autre plateforme */
#ifdef PLATFORM_PC
typedef long            LONG;       /* signed 32-bit quantity */
typedef unsigned long   ULONG;      /* unsigned 32-bit quantity */
typedef short           WORD;       /* signed 16-bit quantity */
typedef unsigned short  UWORD;      /* unsigned 16-bit quantity */
typedef char            BYTE;       /* signed 8-bit quantity */
typedef unsigned char   UBYTE;      /* unsigned 8-bit quantity */
typedef char           *STRPTR;     /* string pointer (NULL terminated) */
typedef void            VOID;
typedef float           FLOAT;
typedef double          DOUBLE;
typedef long            BOOL;
#endif
#ifndef TRUE
#define TRUE            1
#endif
#ifndef FALSE
#define FALSE           0
#endif

typedef void REGEX;

#define MSIZEOF(s,m) sizeof(((s*)0)->m)


/***** VARIABLES ET FONCTIONS    *****/
/***** PUBLIQUES UTILISABLES PAR *****/
/***** D'AUTRES BLOCS DU PROJET  *****/

/* declarations necessaires pour OS3.1 et MorphOS */
#if !defined(__amigaos4__) || defined(__MORPHOS__)
extern struct ExecBase *SysBase;
extern struct DosLibrary *DOSBase;          /* ouvert par SAS/C */
extern struct Library *UtilityBase;         /* ouvert par le projet */
extern struct IntuitionBase *IntuitionBase;
#endif
#endif


extern BOOL Sys_OpenAllLibs(void);
extern void Sys_CloseAllLibs(void);
extern void *Sys_AllocMem(ULONG);
extern void Sys_FreeMem(void *);

extern LONG Sys_Printf(const char *String, ...);

extern REGEX *Sys_AllocPatternNoCase(const char *, BOOL *);
extern void Sys_FreePattern(REGEX *);
extern BOOL Sys_MatchPattern(REGEX *, const char *);

#if !defined(__amigaos4__) && !defined(__MORPHOS__)
extern void Sys_SPrintf(char *,char *,...);
#endif

extern void Sys_FlushOutput(void);
extern void Sys_MemCopy(void *, void *, LONG);
extern void Sys_StrCopy(char *, const char *, LONG);
extern void Sys_StrCopyLen(char *, const char *, LONG, LONG);
extern LONG Sys_StrLen(const char *);
extern LONG Sys_StrCmp(const char *, const char *);
extern LONG Sys_StrCmpNoCase(const char *, const char *);

extern LONG Sys_GetTime(LONG *, LONG *, LONG *, LONG *, LONG *, LONG *);

#define Sys_CharToLower(Char) ToLower(Char)

#endif  /* SYSTEM_H */
