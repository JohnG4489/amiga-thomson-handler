#include "system.h"
#include "debug.h"
#include <proto/intuition.h>
#include <stdarg.h>

/*
    01-10-2020 (Seg)    Gestion du debug dans une lib à part
*/


/***** Prototypes */
LONG T(char *, ...);
LONG Dbg_ShowMessage(char *);


/*****
    Ecriture du log
*****/

LONG T(char *String, ...)
{
#if 1
    LONG Err=0;
    struct MsgPort *mp;

    Forbid();
    mp=FindPort("TFS Debug");
    if(mp!=NULL)
    {
        LONG Len=sizeof(struct Message)+512;
        struct Message *msg=(struct Message *)Sys_AllocMem(Len);

        if(msg!=NULL)
        {
            char *buf=&((UBYTE *)msg)[sizeof(struct Message)];

            va_list arglist;
            va_start(arglist,String);
            RawDoFmt(String,arglist,(void (*))"\x16\xc0\x4e\x75",buf);
            va_end(arglist);

            msg->mn_Length=(UWORD)Len;
            PutMsg(mp,msg);
        }
    }
    Permit();

    return Err;
#else
    LONG Err=0;
    va_list arglist;
    struct EasyStruct Easy=
    {
        sizeof(struct EasyStruct),
        0,
        "TFS",
        NULL,
        NULL,
    };

    va_start(arglist,String);
    Easy.es_GadgetFormat="Ok";
    Easy.es_TextFormat=String;
    Err=EasyRequestArgs(NULL,&Easy,NULL,arglist);
    va_end(arglist);

    return Err;
#endif
}


/*****
    Affichage d'une requête
*****/

LONG Dbg_ShowMessage(char *String)
{
    LONG Err=0;
    va_list arglist;
    struct EasyStruct Easy=
    {
        sizeof(struct EasyStruct),
        0,
        "TFS",
        NULL,
        NULL,
    };

    va_start(arglist,String);
    Easy.es_GadgetFormat="Ok";
    Easy.es_TextFormat=String;
    Err=EasyRequestArgs(NULL,&Easy,NULL,arglist);
    va_end(arglist);

    return Err;
}
