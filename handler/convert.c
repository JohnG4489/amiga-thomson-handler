#include "system.h"
#include "convert.h"
#include "util.h"

/*
    10-09-2020 (Seg)    Ajout de Cnv_SplitHostName()
    27-08-2018 (Seg)    Ajout des fonctions pour gérér les accents du file system
    04-05-2017 (Seg)    Copie du système de conversion de la commande ToDisk
*/


/***** Prototypes */
void Cnv_InitConvertContext(struct ConvertContext *, const UBYTE *, LONG, UBYTE *, LONG, LONG);

void Cnv_SplitHostName(const char *, char *);
void Cnv_ConvertHostNameToThomsonName(const char *, UBYTE *);
void Cnv_ConvertThomsonNameToHostName(const UBYTE *, char *, BOOL);
void Cnv_ConvertHostLabelToThomsonLabel(const char *, UBYTE *);
void Cnv_ConvertThomsonLabelToHostLabel(const UBYTE *, char *, BOOL);
void Cnv_ConvertHostCommentToThomsonComment(const char *, UBYTE *, LONG *, LONG *);
void Cnv_ConvertThomsonCommentToHostComment(const UBYTE *, char *, BOOL);

LONG Cnv_AnsiToAsciiG2(struct ConvertContext *);
LONG Cnv_AsciiG2ToAnsi(struct ConvertContext *);
LONG P_Cnv_AnsiToAsciiG2(UBYTE *, char);


/*****
    Initialisation de la structure de conversion
    Note:
    - SrcLen>=1 et DstLen>=8
    - State doit commencer a 0
*****/

void Cnv_InitConvertContext(struct ConvertContext *Ctx, const UBYTE *Src, LONG SrcLen, UBYTE *Dst, LONG DstLen, LONG State)
{
    Ctx->Src=Src;
    Ctx->SrcLen=SrcLen;
    Ctx->Dst=Dst;
    Ctx->DstLen=DstLen;
    Ctx->SrcPos=0;
    Ctx->State=State;
}


/*****
    Réduction d'un nom hôte en un autre nom hôte respectant le nombre de caractères
    possibles pour un nom de fichier du file system.
*****/

void Cnv_SplitHostName(const char *HostName, char *SplittedHostName)
{
    UBYTE Tmp[11];

    Cnv_ConvertHostNameToThomsonName(HostName,Tmp);
    Cnv_ConvertThomsonNameToHostName(Tmp,SplittedHostName,TRUE);
}


/*****
    Conversion d'un nom de fichier Hôte dans le format de nom Thomson,
    avec conversion du jeu de caractères accentés.
    - Name: chaîne ANSI
    - ToName: buffer de 11 octets minimum (8 pour le nom + 3 pour le suffixe)
*****/

void Cnv_ConvertHostNameToThomsonName(const char *Name, UBYTE *ToName)
{
    struct ConvertContext CtxConv;
    char Tmp[33];
    LONG Len;

    /* Conversion du jeu de caractères */
    Cnv_InitConvertContext(&CtxConv,
        (UBYTE *)Name,Sys_StrLen(Name),
        (UBYTE *)Tmp,sizeof(Tmp)-sizeof(char),
        0);
    Len=Cnv_AnsiToAsciiG2(&CtxConv);
    Tmp[Len]=0;

    /* Réduction du nom */
    Utl_ConvertHostNameToThomsonName(Tmp,ToName);
}


/*****
    Conversion d'un nom au format Thomson en nom de fichier Hôte,
    avec conversion du jeu de caractères accentés.
    - ToName: buffer de 11 octets contenant le nom et le suffixe
    - Name: buffer de 13 caractères minimum (12 pour le nom + 1 zéro de terminaison)
*****/

void Cnv_ConvertThomsonNameToHostName(const UBYTE *ToName, char *Name, BOOL IsNormalize)
{
    struct ConvertContext CtxConv;
    UBYTE Tmp[33];
    LONG i;

    for(i=0; i<sizeof(Tmp); i++) Tmp[i]=' ';

    /* Conversion du jeu de caractères du nom */
    Cnv_InitConvertContext(&CtxConv,
        ToName,8,
        Tmp,sizeof(Tmp),
        0);
    Cnv_AsciiG2ToAnsi(&CtxConv);

    /* Conversion du jeu de caractères du suffixe */
    Cnv_InitConvertContext(&CtxConv,
        &ToName[8],3,
        &Tmp[8],sizeof(Tmp)-8,
        0);
    Cnv_AsciiG2ToAnsi(&CtxConv);

    /* Génération du nom sur le host */
    Utl_ConvertThomsonNameToHostName(Tmp,Name);

    /* Normalisation */
    if(IsNormalize) Utl_NormalizeName(Name,Sys_StrLen(Name));

}


/*****
    Conversopn d'un nom de volume dans le format thomson, avec conversion
    du jeu de caractères accentués.
    - Label: nom au format ANSI
    - ToLabel: buffer de 8 caractères minimum
*****/

void Cnv_ConvertHostLabelToThomsonLabel(const char *Label, UBYTE *ToLabel)
{
    struct ConvertContext CtxConv;
    char Tmp[33];
    LONG Len;

    /* Conversion du jeu de caractères */
    Cnv_InitConvertContext(&CtxConv,
        (UBYTE *)Label,Sys_StrLen(Label),
        (UBYTE *)Tmp,sizeof(Tmp)-sizeof(char),
        0);
    Len=Cnv_AnsiToAsciiG2(&CtxConv);
    Tmp[Len]=0;

    /* Réduction du nom */
    Utl_ConvertHostLabelToThomsonLabel(Tmp,ToLabel);
}


/*****
    Conversion d'un nom de volume Thomson au format ANSI, avec conversion
    du jeu de caractères accentués.
    - ToLabel: buffer 8 octets contenant le nom
    - Label: buffers de 9 caractères contenant le nom converti
*****/

void Cnv_ConvertThomsonLabelToHostLabel(const UBYTE *ToLabel, char *Label, BOOL IsNormalize)
{
    struct ConvertContext CtxConv;
    char Tmp[33];
    LONG Len;

    /* Génération du nom sur le host */
    Utl_ConvertThomsonLabelToHostLabel(ToLabel,Label);

    /* Conversion du jeu de caractères du nom */
    Cnv_InitConvertContext(&CtxConv,
        (UBYTE *)Label,Sys_StrLen(Label),
        (UBYTE *)Tmp,sizeof(Tmp),
        0);
    Len=Cnv_AsciiG2ToAnsi(&CtxConv);
    Tmp[Len]=0;
    Sys_StrCopy(Label,Tmp,9);

    /* Normalisation */
    if(IsNormalize) Utl_NormalizeString(Label,Sys_StrLen(Label));
}


/*****
    Conversion d'un commentaire dans le format de commentaire Thomson,
    avec conversion du jeu de caractères accentués et extraction des metas
    informations.
    - Comment: chaîne ANSI contenant le commentaire
    - ToComment: buffer de 8 octets pour la destination
    - Type: pointeur sur un long qui va contenir le type extrait du commentaire Hôte (NULL possible)
    - ExtraData: pointeur sur un long qui va contenir les extradata du commentaire Hôte (NULL possible)
*****/

void Cnv_ConvertHostCommentToThomsonComment(const char *Comment, UBYTE *ToComment, LONG *Type, LONG *ExtraData)
{
    struct ConvertContext CtxConv;
    char Tmp[33];
    LONG Len;

    /* Conversion du jeu de caractères */
    Cnv_InitConvertContext(&CtxConv,
        Comment,Sys_StrLen(Comment),
        (UBYTE *)Tmp,sizeof(Tmp)-sizeof(char),
        0);
    Len=Cnv_AnsiToAsciiG2(&CtxConv);
    Tmp[Len]=0;

    Utl_ConvertHostCommentToThomsonComment(Tmp,ToComment,Type,ExtraData);
}


/*****
    Conversion d'un commentaire thomson au format de commentaire Hôte,
    avec conversion du jeu de caractères accentués.
    - ToComment: buffer de 8 octets contenant le commentaire
    - Comment: buffer de 9 caractères min pour la destination
*****/

void Cnv_ConvertThomsonCommentToHostComment(const UBYTE *ToComment, char *Comment, BOOL IsNormalize)
{
    struct ConvertContext CtxConv;
    char Tmp[33];
    LONG Len;

    Utl_ConvertThomsonCommentToHostComment(ToComment,Comment);

    /* Conversion du jeu de caractères du nom */
    Cnv_InitConvertContext(&CtxConv,
        (UBYTE *)Comment,Sys_StrLen(Comment),
        (UBYTE *)Tmp,sizeof(Tmp)-sizeof(char),
        0);
    Len=Cnv_AsciiG2ToAnsi(&CtxConv);
    Tmp[Len]=0;
    Sys_StrCopy(Comment,Tmp,9);

    /* Normalisation */
    if(IsNormalize) Utl_NormalizeString(Comment,Sys_StrLen(Comment));
}


/*****
    Conversion Ansi -> Ascii Thomson
*****/

LONG Cnv_AnsiToAsciiG2(struct ConvertContext *Ctx)
{
    LONG DstPos=0;

    while(Ctx->SrcPos<Ctx->SrcLen && DstPos+4<Ctx->DstLen)
    {
        LONG CurChar=(char)Ctx->Src[Ctx->SrcPos++];

        switch(Ctx->State)
        {
            case 0:
                if(CurChar=='\r') Ctx->State=1;
                else if(CurChar=='\n') Ctx->State=2;
                else DstPos+=P_Cnv_AnsiToAsciiG2(&Ctx->Dst[DstPos],CurChar);
                break;

            case 1: /* on est précédé d'un retour chariot */
                if(CurChar=='\n') {Ctx->Dst[DstPos++]='\r'; Ctx->Dst[DstPos++]='\n'; Ctx->State=0;}
                else if(CurChar!='\r') {DstPos+=P_Cnv_AnsiToAsciiG2(&Ctx->Dst[DstPos],CurChar); Ctx->State=0;}
                break;

            case 2: /* on est précédé d'un \n */
                Ctx->State=0;
                Ctx->Dst[DstPos++]='\r';
                Ctx->Dst[DstPos++]='\n';
                if(CurChar!='\r') DstPos+=P_Cnv_AnsiToAsciiG2(&Ctx->Dst[DstPos],CurChar);
                else if(CurChar=='\n') Ctx->State=2;
                break;
        }
    }

    return DstPos;
}


/*****
    Conversion Ascii Thomson -> Ansi
*****/

LONG Cnv_AsciiG2ToAnsi(struct ConvertContext *Ctx)
{
    /* Tableaux: respectivement a,e,i,o,u,y,A,E,I,O,U,Y */
    static const UBYTE TableGrave[]      = {0x60,0xe0,0xe8,0xec,0xf2,0xf9, 'y',0xc0,0xc8,0xcc,0xd2,0xd9, 'Y'};
    static const UBYTE TableAigu[]       = {0xb4,0xe1,0xe9,0xed,0xf3,0xfa,0xfd,0xc1,0xc9,0xcd,0xd3,0xda,0xdd};
    static const UBYTE TableCirconflexe[]= {0x5e,0xe2,0xea,0xee,0xf4,0xfb, 'y',0xc2,0xca,0xce,0xd4,0xdb, 'Y'};
    static const UBYTE TableTrema[]      = {0xa8,0xe4,0xeb,0xef,0xf6,0xfc,0xff,0xc4,0xcb,0xcf,0xd6,0xdc, 'Y'};
    const UBYTE **CurTable=(const UBYTE **)&Ctx->UserData;
    LONG DstPos=0;

    while(Ctx->SrcPos<Ctx->SrcLen && DstPos+3<Ctx->DstLen)
    {
        LONG CurChar=(LONG)Ctx->Src[Ctx->SrcPos++];
        LONG NewChar=0;

        switch(Ctx->State)
        {
            case 0:
                if(CurChar==0x16) Ctx->State=1;
                else Ctx->Dst[DstPos++]=CurChar;
                break;

            case 1:
                switch(CurChar)
                {
                    case 0x23: NewChar='£'; break;
                    case 0x24: NewChar='$'; break;
                    case 0x26: NewChar='#'; break;
                    case 0x27: NewChar=0xa7; break;
                    case 0x2c:
                    case 0x2e: NewChar='-'; break;
                    case 0x2d:
                    case 0x2f: NewChar='|'; break;
                    case 0x30: NewChar=' '; break;
                    case 0x31: NewChar=0xb1; break;
                    case 0x38: NewChar=0xf7; break;
                    case 0x3c: NewChar=0xbc; break;
                    case 0x3d: NewChar=0xbd; break;
                    case 0x3e: NewChar=0xbe; break;
                    case 0x6a: Ctx->Dst[DstPos++]='O'; Ctx->Dst[DstPos++]='E'; break;
                    case 0x7a: Ctx->Dst[DstPos++]='o'; Ctx->Dst[DstPos++]='e'; break;
                    case 0x7b: NewChar=0xdf; break;
                    case 0x41: Ctx->State=2; *CurTable=TableGrave; break;
                    case 0x42: Ctx->State=2; *CurTable=TableAigu; break;
                    case 0x43: Ctx->State=2; *CurTable=TableCirconflexe; break;
                    case 0x48: Ctx->State=2; *CurTable=TableTrema; break;
                    case 0x4b: Ctx->State=3; break;
                }
                if(NewChar!=0) Ctx->Dst[DstPos++]=(UBYTE)NewChar;
                if(Ctx->State==1) Ctx->State=0;
                break;

            case 2: /* Accent */
                NewChar=CurChar;
                switch(CurChar)
                {
                    case ' ': NewChar=(LONG)(*CurTable)[0]; break;
                    case 'a': NewChar=(LONG)(*CurTable)[1]; break;
                    case 'e': NewChar=(LONG)(*CurTable)[2]; break;
                    case 'i': NewChar=(LONG)(*CurTable)[3]; break;
                    case 'o': NewChar=(LONG)(*CurTable)[4]; break;
                    case 'u': NewChar=(LONG)(*CurTable)[5]; break;
                    case 'y': NewChar=(LONG)(*CurTable)[6]; break;
                    case 'A': NewChar=(LONG)(*CurTable)[7]; break;
                    case 'E': NewChar=(LONG)(*CurTable)[8]; break;
                    case 'I': NewChar=(LONG)(*CurTable)[9]; break;
                    case 'O': NewChar=(LONG)(*CurTable)[10]; break;
                    case 'U': NewChar=(LONG)(*CurTable)[11]; break;
                    case 'Y': NewChar=(LONG)(*CurTable)[12]; break;
                }
                Ctx->Dst[DstPos++]=(UBYTE)NewChar;
                Ctx->State=0;
                break;

            case 3: /* cedille */
                switch(CurChar)
                {
                    case ' ': CurChar=0xb8; break;
                    case 'c': CurChar=0xe7; break;
                    case 'C': CurChar=0xc7; break;
                }
                Ctx->Dst[DstPos++]=CurChar;
                Ctx->State=0;
                break;
        }
    }

    return DstPos;
}


/*****
    Conversion d'un caractere ANSI
*****/

LONG P_Cnv_AnsiToAsciiG2(UBYTE *Buffer, char Char)
{
    struct AnsiToAsciiG2
    {
        ULONG Ansi;
        char *AsciiG2;
    };

    static const struct AnsiToAsciiG2 Table[]=
    {
        {'à',  "\101a"}, {'è',  "\101e"}, {0xec, "\101i"}, {0xf2, "\101o"}, {'ù',  "\101u"},
        {0xc0, "\101A"}, {0xc8, "\101E"}, {0xcc, "\101I"}, {0xd2, "\101O"}, {0xd9, "\101U"},
        {0xe1, "\102a"}, {'é',  "\102e"}, {0xed, "\102i"}, {0xf3, "\102o"}, {0xfa, "\102u"}, {0xfd, "\102y"},
        {0xc1, "\102A"}, {0xc9, "\102E"}, {0xcd, "\102I"}, {0xd3, "\102O"}, {0xda, "\102U"}, {0xdd, "\102Y"},
        {'â',  "\103a"}, {'ê',  "\103e"}, {'î',  "\103i"}, {'ô',  "\103o"}, {'û',  "\103u"},
        {'Â',  "\103A"}, {'Ê',  "\103E"}, {'Î',  "\103I"}, {'Ô',  "\103O"}, {'Û',  "\103U"},
        {'ä',  "\110a"}, {'ë',  "\110e"}, {'ï',  "\110i"}, {'ö',  "\110o"}, {'ü',  "\110u"}, {'ÿ',  "\110y"},
        {'Ä',  "\110A"}, {'Ë',  "\110E"}, {'Ï',  "\110I"}, {'Ö',  "\110O"}, {'Ü',  "\110U"}, {'ÿ',  "\110Y"},
        {'ç',  "\113c"}, {0xc7, "\113C"},
        {'£',  "\x23"},  {0xa7, "\x27"},  {0xb1, "\x31"},  {0xf7, "\x38"},  {0xbc, "\x3c"},
        {0xbd, "\x3d"},  {0xbe, "\x3e"},  {0xdf, "\x7b"},
        {0,    ""},
    };
    LONG Count=1;
    char *Result=NULL;

    if(Char<0 || Char>0x7f)
    {
        LONG i;
        for(i=0;Table[i].Ansi!=0;i++)
        {
            if((UBYTE)Table[i].Ansi==(UBYTE)Char) {Result=Table[i].AsciiG2; break;}
        }
    }

    if(Result==NULL) *Buffer=Char;
    else
    {
        *(Buffer++)=0x16;
        while(*Result!=0) {*(Buffer++)=*(Result++); Count++;}
    }

    return Count;
}
