#include <stdarg.h>
#include "system.h"

/*
    28-08-2018 (Seg)    Fix sur les fonctions de comparaison de chaînes
    03-09-2000 (Seg)    Utilisation des fonctions systeme du 3dengine
    19-05-2004 (Seg)    Ajout des fonctions Sys_MemCopy() et Sys_StrCopy()
*/


struct DosLibrary *DOSBase=NULL;
struct Library *UtilityBase=NULL;
struct IntuitionBase *IntuitionBase=NULL;


/***** Prototypes */
BOOL Sys_OpenAllLibs(void);
void Sys_CloseAllLibs(void);
void *Sys_AllocMem(ULONG);
void Sys_FreeMem(void *);

LONG Sys_Printf(const char *String, ...);
void Sys_FlushOutput(void);

void Sys_MemCopy(void *, void *, LONG);
void Sys_StrCopy(char *, const char *, LONG);
void Sys_StrCopyLen(char *, const char *, LONG, LONG);
LONG Sys_StrLen(const char *);
LONG Sys_StrCmp(const char *, const char *);
LONG Sys_StrCmpNoCase(const char *, const char *);

LONG Sys_GetTime(LONG *, LONG *, LONG *, LONG *, LONG *, LONG *);


/*****
    Ouverture des librairies system.
*****/


BOOL Sys_OpenAllLibs(void)
{
    DOSBase=(struct DosLibrary *)OpenLibrary("dos.library",37L);
    UtilityBase=OpenLibrary("utility.library",37L);
    IntuitionBase=(struct IntuitionBase *)OpenLibrary("intuition.library",37L);

    if(UtilityBase!=NULL && DOSBase!=NULL && IntuitionBase!=NULL) return TRUE;

    Sys_CloseAllLibs();

    return FALSE;
}


/*****
    Fermeture des librairies system.
*****/

void Sys_CloseAllLibs(void)
{
    CloseLibrary((struct Library *)IntuitionBase);
    CloseLibrary(UtilityBase);
    CloseLibrary((struct Library *)DOSBase);
}


/*****
    Allocation mémoire
    - la mémoire allouée est initialisée à zéro.
    Retourne NULL en cas d'echec
*****/

void *Sys_AllocMem(ULONG Size)
{
    return AllocVec(Size,MEMF_ANY|MEMF_CLEAR);
}


/*****
    Désallocation de la mémoire allouée par Sys_AllocMem()
    Précondition: Ptr peut être NULL
*****/

void Sys_FreeMem(void *Ptr)
{
    FreeVec(Ptr);
}


/*****
    Ecriture d'une chaîne de caractères dans la sortie standard.
*****/
#if !defined(__amigaos4__) && !defined(__MORPHOS__)
LONG Sys_Printf(const char *String, ...)
{
    LONG Error;
    va_list argptr;

    va_start(argptr,String);
    Error=VPrintf((STRPTR)String,argptr);
    va_end(argptr);

    return Error;      /*  count/error */
}
#endif


/*****
    Alloue les ressources nécessaires pour les expressions régulières
*****/

REGEX *Sys_AllocPatternNoCase(const char *Pattern, BOOL *IsPattern)
{
    LONG BufferLen=Sys_StrLen(Pattern)*2+3;
    REGEX *BufferPtr=(REGEX *)Sys_AllocMem(BufferLen);

    if(BufferPtr!=NULL)
    {
        LONG Result=ParsePatternNoCase((STRPTR)Pattern,(STRPTR)BufferPtr,BufferLen);
        if(Result>=0)
        {
            if(IsPattern!=NULL) *IsPattern=Result>0?TRUE:FALSE;
            return BufferPtr;
        }

        Sys_FreePattern(BufferPtr);
    }

    return NULL;
}


/*****
    Désalloue les ressources allouées par Sys_AllocTegExPatternNoCase()
*****/

void Sys_FreePattern(REGEX *Ptr)
{
    if(Ptr!=NULL) Sys_FreeMem((void *)Ptr);
}


/*****
    Test une expression
*****/

BOOL Sys_MatchPattern(REGEX *Pattern, const char *Str)
{
    return MatchPatternNoCase((STRPTR)Pattern,(STRPTR)Str);
}


/*****
    Flush de la sortie courante
*****/

void Sys_FlushOutput(void)
{
    Flush(Output());
}


/*****
    Equivalent de memcpy()
*****/

void Sys_MemCopy(void *Dst, void *Src, LONG Size)
{
    UBYTE *pdst=(UBYTE *)Dst, *psrc=(UBYTE *)Src;
    while(--Size>=0) *(pdst++)=*(psrc++);
}


/*****
    Formatage d'une chaîne de caractères.
*****/
#if !defined(__amigaos4__) && !defined(__MORPHOS__)
void Sys_SPrintf(char *Buffer,char *String,...)
{
    va_list argptr;

    va_start(argptr,String);
    RawDoFmt(String,argptr,(void (*))"\x16\xc0\x4e\x75",Buffer);
    /*sprintf(Buffer,String,argptr);*/
    va_end(argptr);
}
#endif


/*****
    Equivalent de strcpy()
*****/

void Sys_StrCopy(char *Dst, const char *Src, LONG SizeOfDst)
{
    while(--SizeOfDst>0 && *Src!=0) *(Dst++)=*(Src++);
    *Dst=0;
}


/*****
    Copie un nombre de caractères spécifié par Len, dans la limite
    du buffer destination.
*****/

void Sys_StrCopyLen(char *Dst, const char *Src, LONG SizeOfDst, LONG SrcLen)
{
    while(--SizeOfDst>0 && --SrcLen>=0 && *Src!=0) *(Dst++)=*(Src++);
    *Dst=0;
}


/*****
    Permet d'obtenir le nombre de caractères du chaîne.
*****/

LONG Sys_StrLen(const char *String)
{
    LONG Count=0;

    while(String[Count]!=0) Count++;

    return Count;
}


/*****
    Comparaison de deux chaînes
*****/

LONG Sys_StrCmp(const char *Str1, const char *Str2)
{
    LONG c1,c2;

    do
    {
        c1=(LONG)(UBYTE)*(Str1++);
        c2=(LONG)(UBYTE)*(Str2++);
    } while(c1==c2 && c1!=0 && c2!=0);

    return c1-c2;
}


/*****
    Comparaison de deux chaînes
*****/

LONG Sys_StrCmpNoCase(const char *Str1, const char *Str2)
{
    LONG c1,c2;

    do
    {
        c1=(LONG)(UBYTE)*(Str1++);
        c2=(LONG)(UBYTE)*(Str2++);
        if(c1>='A' && c1<='Z') c1|=32;
        if(c2>='A' && c2<='Z') c2|=32;
    } while(c1==c2 && c1!=0 && c2!=0);

    return c1-c2;
}


/*****
    Permet d'obtenir l'heure courante
*****/

LONG Sys_GetTime(LONG *Year, LONG *Month, LONG *Day, LONG *Hour, LONG *Min, LONG *Sec)
{
    struct ClockData cd;
    struct DateStamp ds;
    LONG Second;

    DateStamp(&ds);
    Second=ds.ds_Days*24*60*60+ds.ds_Minute*60+ds.ds_Tick/TICKS_PER_SECOND;

    Amiga2Date(Second,&cd);

    *Year=(LONG)cd.year;
    *Month=(LONG)cd.month;
    *Day=(LONG)cd.mday;
    *Hour=(LONG)cd.hour;
    *Min=(LONG)cd.min;
    *Sec=(LONG)cd.sec;

    return Second;
}
