#include "system.h"
#include "util.h"


/*
    16-09-2020 (Seg)    Fix Utl_ExtractInfoFromHostComment()
    29-08-2018 (Seg)    Ajout de Utl_SplitHostName()
    28-08-2018 (Seg)    Utl_CompareThomsonName() devient Utl_CompareHostName()
    27-08-2018 (Seg)    Remaniement suite gestion des noms avec accents
    11-08-2018 (Seg)    Ajout de Utl_FixedStringToCString()
    05-06-2017 (Seg)    Modifications pour gérer les "extradata" des fichiers .CHG
    14-09-2016 (Seg)    Ajout de Utl_CompareThomsonName()
    31-08-2016 (Seg)    Modification des commentaires (ajout des accents)
    23-09-2013 (Seg)    Adaptation du code suite à la migration sous Unix
    13-01-2013 (Seg)    Normalisation du nom des fonctions
    26-02-2010 (Seg)    Ajout d'une fonction
    30-08-2006 (Seg)    Gestion des noms et label du filesystem
*/


/***** Prototypes */
void Utl_NormalizeName(char *, LONG);
void Utl_NormalizeString(char *, LONG);
void Utl_SplitHostName(const char *, char *);

LONG Utl_CompareHostName(const char *, const LONG *, const char *, const LONG *, BOOL);

void Utl_ConvertThomsonNameToHostName(const UBYTE *, char *);
void Utl_ConvertHostNameToThomsonName(const char *, UBYTE *);

void Utl_ConvertHostCommentToThomsonComment(const char *, UBYTE *, LONG *, LONG *);
void Utl_ConvertThomsonCommentToHostComment(const UBYTE *, char *);
LONG Utl_SetCommentMetaData(const char *, char *, LONG, LONG);

void Utl_ConvertHostLabelToThomsonLabel(const char *, UBYTE *);
void Utl_ConvertThomsonLabelToHostLabel(const UBYTE *, char *);

const char *Utl_ExtractInfoFromHostComment(const char *, LONG *, LONG *);
LONG Utl_GetTypeFromHostName(const char *);
LONG Utl_GetTypeFromThomsonName(const UBYTE *);
void Utl_FixedStringToCString(const UBYTE *, LONG, char *);

BOOL Utl_CheckName(const char *, const char *);


/*****
    Permet de convertir un nom en évitant les caractères gênants
*****/

void Utl_NormalizeName(char *Name, LONG Len)
{
    LONG i;

    for(i=0; i<Len; i++)
    {
        UBYTE Char=(UBYTE)Name[i];
        Name[i]=(char)(Char<32 || (Char>127 && Char<192) || Char=='/'?'~':Char);
    }
}


/*****
    Permet de convertir une chaîne en évitant les caractères gênants
*****/

void Utl_NormalizeString(char *Name, LONG Len)
{
    LONG i;

    for(i=0; i<Len; i++)
    {
        UBYTE Char=(UBYTE)Name[i];
        Name[i]=(char)(Char<32 || (Char>127 && Char<192)?'~':Char);
    }
}


/*****
    Réduction d'un nom hôte en un autre nom hôte respectant le nombre de caractères
    possibles pour un nom de fichier du file system.
*****/

void Utl_SplitHostName(const char *HostName, char *SplittedHostName)
{
    UBYTE Tmp[11];

    Utl_ConvertHostNameToThomsonName(HostName,Tmp);
    Utl_ConvertThomsonNameToHostName(Tmp,SplittedHostName);
}


/*****
    Compare 2 noms de fichier Host ainsi que leur type si besoin.
    Name1: nom 1
    Name2: nom 2
    Type1: Type du premier fichier. Possibilité de passer NULL.
    Type2: Type du deuxième fichier. Possibilité de passer NULL.
*****/

LONG Utl_CompareHostName(const char *Name1, const LONG *Type1, const char *Name2, const LONG *Type2, BOOL IsSensitive)
{
    LONG res=0;

    if(IsSensitive) res=Sys_StrCmp(Name1,Name2);
    else res=Sys_StrCmpNoCase(Name1,Name2);

    if(res==0 && Type1!=NULL && Type2!=NULL)
    {
        if(*Type1<*Type2) res=-1;
        else if(*Type1>*Type2) res=1;
    }

    return res;
}


/*****
    Conversion d'un nom de fichier Thomson.
    SrcName: 11 octets sans 0 de terminaison
    DstName: 13 caractères (dont un prévu pour la terminaison et un pour le "." séparateur)
*****/

void Utl_ConvertThomsonNameToHostName(const UBYTE *SrcName, char *DstName)
{
    /*
        "TOTO    BIN" -> "TOTO.BIN"
        "        BIN" -> ".BIN"
        "TOTO       " -> "TOTO"
        "           " -> " "
        " TOTO    BI" -> " TOTO. BI"
    */
    LONG ln,ls,p=0,i;

    for(ln=7;ln>=0 && SrcName[ln]==32;ln--);
    for(ls=2;ls>=0 && SrcName[8+ls]==32;ls--);

    if(ln<0 && ls<0) DstName[p++]=' ';
    else
    {
        for(i=0;i<=ln;i++) DstName[p++]=SrcName[i];
        if(ls>=0) DstName[p++]='.';
        for(i=0;i<=ls;i++) DstName[p++]=SrcName[8+i];
    }
    DstName[p]=0;
}


/*****
    Conversion d'un nom de fichier pour le Thomson.
    SrcName: chaîne variable terminée par un 0
    DstName: 11 octets sans 0 de terminaison
*****/

void Utl_ConvertHostNameToThomsonName(const char *SrcName, UBYTE *DstName)
{
    /*
        "TOTO.BIN"      -> "TOTO    BIN"
        ".BIN"          -> "        BIN"
        "TOTO"          -> "TOTO       "
        "  "            -> "           "
        "TOTO.T.A"      -> "TOTO    T  "
        "TOTO...A"      -> "TOTO    A  "
        "TOTO12345.BIN" -> "TOTO1234BIN"
        "TOTO.BIN123"   -> "TOTO    BIN"
    */
    LONG l,p=0;

    /* on scanne jusqu'à 8 caractères ou jusqu'au '.' ou jusqu'à la fin de chaîne */
    for(l=0;p<8 && SrcName[l]!=0 && SrcName[l]!='.';l++) DstName[p++]=SrcName[l];

    /* on complète le nom avec des espaces si besoin */
    for(;p<8;p++) DstName[p]=' ';

    /* on cherche le '.' ou la fin de chaîne */
    for(;SrcName[l]!=0 && SrcName[l]!='.';l++);

    /* on cherche le premier caractère après le '.' ou la fin de chaîne */
    for(;SrcName[l]!=0 && SrcName[l]=='.';l++);

    /* on scanne jusqu'à 3 caractères ou jusqu'à la fin de chaîne ou
       jusqu'au prochain point */
    for(;p<11 && SrcName[l]!=0 && SrcName[l]!='.';l++) DstName[p++]=SrcName[l];

    /* on complète le suffixe avec des espaces si besoin */
    for(;p<11;p++) DstName[p]=' ';
}


/*****
    Retourne un pointeur sur le début du commentaire et extrait le Type du commentaire
    ainsi que l'éventuel "extradata" des fichiers .CHG
*****/

const char *Utl_ExtractInfoFromHostComment(const char *Comment, LONG *Type, LONG *ExtraData)
{
    LONG ExtractedType=-1;
    LONG ExtractedExtraData=-1;
    LONG p;

    /* on recherche le type */
    for(p=0; Comment[p]==' '; p++);
    if(Comment[p++]=='(')
    {
        LONG TmpType=0;
        LONG TmpExtraData=0;
        LONG i;
        UBYTE c;

        for(i=0; (c=(UBYTE)Comment[p])!=0 && i<4; i++,p++)
        {
            LONG v=-1;

            if(c>='0' && c<='9') v=c-'0';
            else {c|=32; if(c>='a' && c<='f') v=c-'a'+10;}

            if(v>=0) TmpType=(TmpType<<4)+v; else break;
        }
        if(i==4) ExtractedType=TmpType;

        for(i=0; (c=(UBYTE)Comment[p])!=0 && i<4; i++,p++)
        {
            LONG v=-1;

            if(c>='0' && c<='9') v=c-'0';
            else {c|=32; if(c>='a' && c<='f') v=c-'a'+10;}

            if(v>=0) TmpExtraData=(TmpExtraData<<4)+v; else break;
        }
        if(i==4) ExtractedExtraData=TmpExtraData;

        /* On squizz le contenu restant de la parenthèse et les espaces après */
        while(Comment[p]!=0) if(Comment[p++]==')') break;
        while(Comment[p]==' ') p++;

        Comment=&Comment[p];
    }

    if(Type!=NULL && ExtractedType>=0) *Type=ExtractedType;
    if(ExtraData!=NULL && ExtractedExtraData>=0) *ExtraData=ExtractedExtraData;

    return Comment;
}


/*****
    Conversion d'un commentaire Host en commentaire Thomson (8 caractères)
    et extraction du type contenu dans le commentaire Host.
    SrcComment: chaîne variable contenant ou non un type
    DstComment: 8 octets (pas de 0 de terminaison)
    Type (src/dst): type extrait du commentaire SrcComment
    ExtraData (src/dst): extradata (pour les fichiers .CHG) extrait du commentaire SrcComment
*****/

void Utl_ConvertHostCommentToThomsonComment(const char *SrcComment, UBYTE *DstComment, LONG *Type, LONG *ExtraData)
{
    const char *CommentPos=Utl_ExtractInfoFromHostComment(SrcComment,Type,ExtraData);
    LONG p;

    for(p=0;p<8 && CommentPos[p]!=0;p++) DstComment[p]=CommentPos[p];

    /* on complete avec des espace */
    for(;p<8;p++) DstComment[p]=' ';
}


/*****
    Conversion d'un commentaire Thomson en commentaire Host.
    SrcComment: 8 octets pour le commentaire thomson (pas besoin de 0 en fin)
    DstComment: 9 caractères dont un pour le 0 de terminaison
    Type: Type à inclure dans le commentaire DstComment
*****/

void Utl_ConvertThomsonCommentToHostComment(const UBYTE *SrcComment, char *DstComment)
{
    LONG i;

    for(i=0;i<8 && SrcComment[i]!=0 && SrcComment[i]!=0xff;i++)
    {
        DstComment[i]=SrcComment[i];
    }
    for(--i;i>=0 && DstComment[i]==' ';i--);
    DstComment[++i]=0;
}


/*****
    Pour ajouter ou non les meta data à une zone de commentaire
    DstComment: buffer de 20 caractères minimum
    Type: type du fichier à intégrer dans le commentaire
    ExtraData: données CHG à intégrer dans le commentaire (valeur -1 pour ne pas intégrer de donnée)
*****/

LONG Utl_SetCommentMetaData(const char *Comment, char *DstComment, LONG Type, LONG ExtraData)
{
    if(ExtraData<0) Sys_SPrintf(DstComment,"(%04lX)",Type&0xffff);
    else Sys_SPrintf(DstComment,"(%04lX%04lX)",Type&0xffff,ExtraData&0xffff);

    if(*Comment!=0) Sys_SPrintf(&DstComment[Sys_StrLen(DstComment)]," %s",Comment);

    return Sys_StrLen(DstComment);
}


/*****
    Conversion d'une chaîne en nom de volume Thomson (8 caractères)
    SrcLabel: chaîne correspondant au label du disque
    DstLabel: 8 octets (pas de 0 de terminaison de chaîne)
*****/

void Utl_ConvertHostLabelToThomsonLabel(const char *SrcLabel, UBYTE *DstLabel)
{
    LONG Type,ExtraData;

    /* on utilise la routine des comments */
    Utl_ConvertHostCommentToThomsonComment(SrcLabel,DstLabel,&Type,&ExtraData);
}


/*****
    Conversion d'un nom de volume Thomson en chaîne terminée par 0
    SrcLabel: 8 octets (pas de 0 de terminaison)
    DstLabel: 9 caractères minimum (dont un pour le 0 de terminaison)
*****/

void Utl_ConvertThomsonLabelToHostLabel(const UBYTE *SrcLabel, char *DstLabel)
{
    Utl_FixedStringToCString(SrcLabel,8,DstLabel);
}


/*****
    Cette fonction déduit le type d'un fichier en fonction du suffixe de son nom.
    Name: Nom de fichier classic
    Retourne: Type déduit du suffixe ($0200 si le suffixe est inconnu)
*****/

LONG Utl_GetTypeFromHostName(const char *Name)
{
    struct AutoType {
        const char *Name;
        LONG Type;
    };

    static struct AutoType ATList[]=
    {
#ifdef SYSTEM_UNIX
        {"*.BAS",0x0000},
        {"*.BAT",0x0000},
        {"*.DAT",0x01FF},
        {"*.CHG",0x01FF},
        {"*.CAR",0x01FF},
        {"*.ASC",0x01FF},
        {"*.TXT",0x01FF},
        {"*.HTM",0x01FF},
        {"*.DOC",0x01FF},
        {"*.S",0x01FF},
        {"*.C",0x01FF},
        {"*.H",0x01FF},
        {"*.ASS",0x0300},
        {"*.ASM",0x03FF},
        {"*.AND",0x0500},
        {"*.PAR",0x0A00},
#else
        {"#?.(BAS|BAT)",0x0000},
        {"#?.(DAT|CHG|CAR|ASC|TXT|HTM|DOC|C|H|S)",0x01FF},
        {"#?.ASS",0x0300},
        {"#?.ASM",0x03FF},
        {"#?.AND",0x0500},
        {"#?.PAR",0x0A00},
#endif
        {NULL   ,0x0200}    /* type par défaut *, BIN, MAP,... */
    };
    struct AutoType *ATPtr;

    for(ATPtr=ATList;ATPtr->Name!=NULL;ATPtr++)
    {
        BOOL IsMatch=FALSE;
        REGEX *PPatt=Sys_AllocPatternNoCase(ATPtr->Name,NULL);

        if(PPatt!=NULL)
        {
            IsMatch=Sys_MatchPattern(PPatt,Name);
            Sys_FreePattern(PPatt);
        }

        if(IsMatch) break;
    }

    return ATPtr->Type;
}

/*****
    Cette fonction déduit le type d'un fichier en fonction du suffixe de son nom.
    Name: 11 octets (pas de 0 de terminaison)
    Retourne: Type déduit du suffixe ($0200 si le suffixe est inconnu)
*****/

LONG Utl_GetTypeFromThomsonName(const UBYTE *Name)
{
    char DstName[13];

    Utl_ConvertThomsonNameToHostName(Name,DstName);

    return Utl_GetTypeFromHostName(DstName);
}


/*****
    Conversion du chaîne de taille fixe, terminée par des espaces, en chaîne terminée par nul
*****/

void Utl_FixedStringToCString(const UBYTE *Str, LONG Size, char *Dst)
{
    LONG l,i;

    for(l=Size-1; l>=0 && (Str[l]==' ' || Str[l]>127); l--);
    for(i=0; i<=l; i++) Dst[i]=Str[i];
    Dst[i]=0;
}


/*****
    Permet de tester un nom via une expression régulière
*****/

BOOL Utl_CheckName(const char *Pattern, const char *Name)
{
    BOOL IsMatch=FALSE;
    REGEX *PPatt=Sys_AllocPatternNoCase(Pattern,NULL);

    if(PPatt!=NULL)
    {
        IsMatch=Sys_MatchPattern(PPatt,Name);

        Sys_FreePattern(PPatt);
    }

    return IsMatch;
}
