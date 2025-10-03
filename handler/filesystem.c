#include "system.h"
#include "filesystem.h"
#include "util.h"
#include "convert.h"
#include "disklayer.h"


/*
    23-04-2021 (Seg)    Gestion du mode étendu via un flag
    23-09-2020 (Seg)    Modifs suite amélioration de la gestion du cache
    10-09-2020 (Seg)    Refonte totale
    28-08-2018 (Seg)    Fix sur FS_FindFile()
    27-08-2018 (Seg)    Remaniement suite gestion des noms avec accents
    13-08-2018 (Seg)    Gestion des paramètres géométriques du disque et gestion des infos .CHG
    01-10-2016 (Seg)    Ajout de FS_GetVolumeDate()
    13-09-2016 (Seg)    Ajout de FS_FlushFileInfo() et modification de FS_CloseFile(),
                        FS_SetComment(), FS_SetDate(), FS_RenameFile(), FS_DeleteFile()
                        et FS_DeleteFileFromIdx()
    12-09-2016 (Seg)    Ajout de FS_DeleteFileFromIdx()
    31-08-2016 (Seg)    Amélioration de FS_InitFileSystem()
    23-08-2016 (Seg)    Ajout option IsSetDate lors de la création d'un nouveau fichier
    22-08-2016 (Seg)    Ajout de la fonction FS_RenameFile()
    23-09-2013 (Seg)    Adaptation du code suite à la migration sous Unix
    06-10-2012 (Seg)    Fix problème de gestion de date (sur les années)
    23-03-2010 (Seg)    Gestion du case sensitive dans le scrutage de fichier
    22-03-2010 (Seg)    Gestion des fonctions de scrutage de fichiers
    24-02-2010 (Seg)    Ajout de la fonction FS_GetSize()
    31-08-2006 (Seg)    Gestion du file system
*/


/***** Prototypes */
struct FileSystem *FS_AllocFileSystem(LONG, LONG, LONG, BOOL);
void FS_FreeFileSystem(struct FileSystem *);
LONG FS_InitFileSystem(struct FileSystem *, struct DiskLayer *);
LONG FS_FlushFileInfo(struct FileSystem *);

LONG FS_Format(struct FileSystem *, const char *);

void FS_ExamineFileObject(struct FileSystem *, struct FileObject *);
BOOL FS_ExamineNextFileObject(struct FileObject *);

struct FSHandle *FS_OpenFileFromIdx(struct FileSystem *, LONG, LONG, BOOL, LONG *);
struct FSHandle *FS_OpenFile(struct FileSystem *, LONG, const char *, const LONG *, BOOL, BOOL, LONG *);
LONG FS_CloseFile(struct FSHandle *);
LONG FS_ReadFile(struct FSHandle *, UBYTE *, LONG);
LONG FS_WriteFile(struct FSHandle *, UBYTE *, LONG);
LONG FS_Seek(struct FSHandle *, LONG);
LONG FS_GetSize(struct FSHandle *);
LONG FS_SetSize(struct FSHandle *, LONG);

LONG FS_RenameFile(struct FileSystem *, const char *, const LONG *, BOOL, const char *);
LONG FS_DeleteFile(struct FileSystem *, const char *, const LONG *, BOOL);
LONG FS_DeleteFileFromIdx(struct FileSystem *, LONG);
LONG FS_SetComment(struct FileSystem *, const char *, const LONG *, BOOL, const char *);
LONG FS_SetDate(struct FileSystem *, const char *, const LONG *, BOOL, LONG, LONG, LONG, LONG, LONG, LONG);

LONG FS_FindFile(struct FileSystem *, const char *, const LONG *, BOOL);
LONG FS_GetTypeFromHandle(struct FSHandle *);
void FS_GetNameTypeFromIndex(struct FileSystem *, LONG, char *, LONG *);
void FS_GetVolumeName(struct FileSystem *, char *);
LONG FS_SetVolumeName(struct FileSystem *, const char *);
BOOL FS_GetVolumeDate(struct FileSystem *, LONG *, LONG *, LONG *, LONG *, LONG *, LONG *);
LONG FS_GetFreeSpace(struct FileSystem *);
LONG FS_GetBlockSpace(struct FileSystem *, LONG *);
const char *FS_GetTextErr(ULONG);

struct FSHandle *P_FS_SubOpenFile(struct FileSystem *, LONG, LONG, const char *, const LONG *, BOOL, LONG *);
LONG P_FS_CreateNewFile(struct FSHandle *, const char *, const LONG *, BOOL);
void P_FS_SetMetaData(struct FileSystem *, LONG, LONG, LONG, LONG, LONG, LONG, LONG, LONG, LONG);
LONG P_FS_GetTypeFromFileInfo(UBYTE *);
LONG P_FS_ObtainFileChunk(struct FSHandle *, LONG, BOOL, LONG *, LONG *, struct SectorCacheNode **);
void P_FS_ReleaseFileChunk(struct FSHandle *, struct SectorCacheNode *, LONG);
void P_FS_Terminate(struct FileSystem *, LONG, LONG, LONG, LONG);
void P_FS_SetFileInfoFlags(struct FileSystem *, LONG);
LONG P_FS_AllocNewCluster(struct FileSystem *, LONG, LONG);
LONG P_FS_GetGeoDetailFromOffset(struct FSHandle *, LONG, BOOL, LONG *, LONG *, LONG *, LONG *);
void P_FS_FreeClusters(struct FileSystem *, LONG);
LONG P_FS_CalcFileSize(struct FileSystem *, LONG, LONG, LONG *);
UBYTE *P_FS_GetFileInfo(struct FileSystem *, LONG);
LONG P_FS_GetFirstCluster(struct FileSystem *, LONG);
LONG P_FS_GetLastSectorLen(struct FileSystem *, LONG);

struct FSHandle *P_FS_AddNewHandle(struct FileSystem *);
void P_FS_RemoveHandle(struct FileSystem *, struct FSHandle *);


/*****
    Allocation d'une structure FileSystem pour la gestion des fichiers
*****/

struct FileSystem *FS_AllocFileSystem(LONG MaxTracks, LONG SectorSize, LONG SectorsPerTrack, BOOL IsExtended)
{
    struct FileSystem *FS=(struct FileSystem *)Sys_AllocMem(sizeof(struct FileSystem)+SectorSize*SectorsPerTrack);

    if(FS!=NULL)
    {
        FS->IsExtended=IsExtended;
        FS->TrackSys=20;
        FS->SectorFAT=2;
        FS->BlocksPerTrack=2; /* INFO: pas d'autres valeurs possibles! */

        FS->MaxTracks=MaxTracks;
        FS->SectorSize=SectorSize;
        FS->FSSectorSize=SectorSize-sizeof(UBYTE);
        FS->SectorsPerTrack=SectorsPerTrack;
        FS->SectorsPerBlock=SectorsPerTrack/FS->BlocksPerTrack;
        FS->FilesPerSector=SectorSize/SIZEOF_FILEINFO;
        FS->MaxFiles=FS->FilesPerSector*(SectorsPerTrack-2);
        FS->MaxBlocks=FS->MaxTracks*FS->BlocksPerTrack;
        FS->ClusterSys=FS->TrackSys*FS->BlocksPerTrack;

        FS->Sys=&((UBYTE *)FS)[sizeof(struct FileSystem)];
        FS->Label=FS->Sys;
        FS->FAT=&FS->Sys[(FS->SectorFAT-1)*SectorSize];
        FS->Dir=&FS->FAT[SectorSize];
    }

    return FS;
}


/*****
    Libère les ressources allouées par FS_AllocFileSystem()
*****/

void FS_FreeFileSystem(struct FileSystem *FS)
{
    if(FS!=NULL) Sys_FreeMem((void *)FS);
}


/*****
    Initialisation de la structure FileSystem allouée par FS_AllocFileSystem()
    Retour:
    - FS_SUCCESS si tout s'est bien passé et que la disquette est DOS
    - FS_NOT_DOS_DISK si la disquette n'est pas DOS
    - FS_DISKLAYER_ERROR en cas d'erreur I/O
*****/

LONG FS_InitFileSystem(struct FileSystem *FS, struct DiskLayer *DiskLayerPtr)
{
    LONG Result=FS_SUCCESS,i;
    UBYTE *Ptr=FS->Sys;

    FS->DiskLayerPtr=DiskLayerPtr;

    /* Lecture de la piste système */
    for(i=1; i<=FS->SectorsPerTrack && Result>=0; i++)
    {
        if(!DL_ReadSector(FS->DiskLayerPtr,FS->TrackSys,i,&Ptr[(i-1)*FS->SectorSize]))
        {
            Result=FS_DISKLAYER_ERROR;
        }
    }

    /* On contrôle s'il s'agit bien d'un disque DOS */
    if(Result>=0)
    {
        Ptr=FS->FAT;
        if(Ptr[FS->ClusterSys+1]!=CLST_RESERVED || Ptr[FS->ClusterSys+2]!=CLST_RESERVED) Result=FS_NOT_DOS_DISK;
        for(i=1; i<=FS->MaxBlocks && i<=FS->SectorSize && Result>=0; i++)
        {
            if(Ptr[i]>CLST_TERM+FS->SectorsPerBlock && Ptr[i]!=CLST_RESERVED && Ptr[i]!=CLST_FREE) Result=FS_NOT_DOS_DISK;
        }
    }

    return Result;
}


/*****
    Met à jour la FAT et les FileInfo des fichiers modifiés
*****/

LONG FS_FlushFileInfo(struct FileSystem *FS)
{
    LONG Result=FS_SUCCESS;
    LONG Sector;

    /* Etape 1: Mise à jour de la FAT */
    if(FS->IsFATUpdated)
    {
        if(!DL_WriteSector(FS->DiskLayerPtr,FS->TrackSys,FS->SectorFAT,FS->FAT)) Result=FS_DISKLAYER_ERROR;
        else FS->IsFATUpdated=FALSE;
    }

    /* Etape 2: Mise à jour des infos des fichiers */
    for(Sector=0; Sector<32 && Result>=0; Sector++)
    {
        if(FS->FileInfoFlags&(1<<Sector))
        {
            if(!DL_WriteSector(FS->DiskLayerPtr,FS->TrackSys,Sector+1,&FS->Label[Sector*FS->SectorSize])) Result=FS_DISKLAYER_ERROR;
            else FS->FileInfoFlags&=~(1<<Sector);
        }
    }

    return Result;
}


/*****
    Initialisation du file system (équivalent d'un format quick)
    Note: La disquette doit avoir été formatée en low level
    Retour:
    - FS_SUCCESS si tout s'est bien passé
    - FS_DISKLAYER_ERROR en cas d'erreur I/O
*****/

LONG FS_Format(struct FileSystem *FS, const char *VolumeName)
{
    LONG Result=FS_SUCCESS;
    LONG i,Year,Month,Day,Hour,Min,Sec;
    UBYTE *Ptr=FS->Label;

    /* Initialisation de la piste */
    for(i=0; i<FS->SectorsPerTrack*FS->SectorSize; i++) Ptr[i]=0xff;
    Cnv_ConvertHostLabelToThomsonLabel(VolumeName,&Ptr[FV_NAME]);

    if(FS->IsExtended)
    {
        /* Nouveau: on installe la date et l'heure courante, dans le même format que pour les fichiers */
        Sys_GetTime(&Year,&Month,&Day,&Hour,&Min,&Sec);
        Ptr[FV_DAY]=(UBYTE)Day;
        Ptr[FV_MONTH]=(UBYTE)Month;
        Ptr[FV_YEAR]=(UBYTE)(Year%100);
        Ptr[FV_HOUR]=(UBYTE)Hour;
        Ptr[FV_MIN]=(UBYTE)Min;
        Ptr[FV_SEC]=(UBYTE)Sec;
    }

    /* Initialisation de la FAT */
    Ptr=FS->FAT;
    for(i=FS->MaxBlocks+1; i<FS->SectorSize; i++) Ptr[i]=0;
    Ptr[FS->ClusterSys+1]=CLST_RESERVED;
    Ptr[FS->ClusterSys+2]=CLST_RESERVED;

    for(i=1; i<=FS->SectorsPerTrack && Result>=0; i++)
    {
        if(!DL_WriteSector(FS->DiskLayerPtr,FS->TrackSys,i,&FS->Label[(i-1)*FS->SectorSize])) Result=FS_DISKLAYER_ERROR;
    }

    return Result;
}


/*****
    Initialisation d'une structure FileObject en vue de scanner un répertoire
*****/

void FS_ExamineFileObject(struct FileSystem *FS, struct FileObject *FO)
{
    FO->FS=FS;
    FO->FileInfoIdx=-1;
}


/*****
    Scanne le répertoire
    Retour:
    - TRUE s'il y a une nouvelle entrée
    - FALSE si on est en fin de répertoire
*****/

BOOL FS_ExamineNextFileObject(struct FileObject *FO)
{
    struct FileSystem *FS=FO->FS;

    while(FO->FileInfoIdx<FS->MaxFiles-1)
    {
        UBYTE *FileInfo=P_FS_GetFileInfo(FS,++FO->FileInfoIdx);

        if(FileInfo[FIO_NAME]!=FST_ERASED && FileInfo[FIO_NAME]!=FST_NONE)
        {
            LONG Cluster=(LONG)FileInfo[FIO_FIRST_CLUSTER];
            LONG EndSize=(((LONG)FileInfo[FIO_LAST_SEC_LEN])<<8)+(LONG)FileInfo[FIO_LAST_SEC_LEN+1];
            LONG Year=(LONG)FileInfo[FIO_YEAR];
            char Suffix[SIZEOF_TOSUFFIX+sizeof(char)];

            Utl_FixedStringToCString(&FileInfo[FIO_SUFFIX],SIZEOF_TOSUFFIX,Suffix);

            Cnv_ConvertThomsonNameToHostName(FileInfo,FO->Name,TRUE);
            Cnv_ConvertThomsonCommentToHostComment(&FileInfo[FIO_COMMENT],FO->Comment,TRUE);
            FO->Day=(LONG)FileInfo[FIO_DAY];
            FO->Month=(LONG)FileInfo[FIO_MONTH];
            FO->Year=Year+(Year<80?2000:1900);
            FO->Hour=FS->IsExtended?(LONG)FileInfo[FIO_HOUR]:0;
            FO->Min=FS->IsExtended?(LONG)FileInfo[FIO_MIN]:0;
            FO->Sec=FS->IsExtended?(LONG)FileInfo[FIO_SEC]:0;
            FO->Type=P_FS_GetTypeFromFileInfo(FileInfo);
            FO->ExtraData=Sys_StrCmp(Suffix,"CHG")==0?((LONG)FileInfo[FIO_CHG1]<<8)+(LONG)FileInfo[FIO_CHG2]:-1;
            FO->Size=P_FS_CalcFileSize(FS,Cluster,EndSize,&FO->CountOfBlocks);

            FO->IsDateOk=FO->Day>=1 && FO->Day<=31 && FO->Month>=1 && FO->Month<=12?TRUE:FALSE;
            FO->IsTimeOk=FO->IsDateOk && (FO->Hour!=0 || FO->Min!=0 || FO->Sec!=0)?TRUE:FALSE;

            return TRUE;
        }
    }

    return FALSE;
}


/*****
    Ouverture d'un fichier sur le file system à partir de son index dans le répertoire
*****/

struct FSHandle *FS_OpenFileFromIdx(struct FileSystem *FS, LONG Mode, LONG Idx, BOOL IsSetDate, LONG *ErrorCode)
{
    return P_FS_SubOpenFile(FS,Mode,Idx,NULL,NULL,IsSetDate,ErrorCode);
}


/*****
    Ouverture d'un fichier sur le file system
*****/

struct FSHandle *FS_OpenFile(struct FileSystem *FS, LONG Mode, const char *Name, const LONG *Type, BOOL IsSensitive, BOOL IsSetDate, LONG *ErrorCode)
{
    LONG Idx=FS_FindFile(FS,Name,Type,IsSensitive);

    return P_FS_SubOpenFile(FS,Mode,Idx,Name,Type,IsSetDate,ErrorCode);
}


/*****
    Fermeture des ressources allouées par FS_OpenFile()

    Note: Cette fonction ne met pas à jour la FAT et le descripteur du fichier.
    Il faut utiliser FS_FlushFileInfo() pour mettre à jour ces derniers, une fois
    que toutes les opérations d'écriture sont terminées.
*****/

LONG FS_CloseFile(struct FSHandle *h)
{
    LONG Result=FS_SUCCESS;

    if(h!=NULL)
    {
        struct FileSystem *FS=h->FS;
        if(!DL_WriteBufferCache(FS->DiskLayerPtr)) Result=FS_DISKLAYER_ERROR;
        P_FS_RemoveHandle(FS,h);
    }

    return Result;
}


/*****
    Routine de lecture du fichier.
    Retourne:
    - Le nombre d'octets lus
    - 0 si End Of File
    - FS_DISKLAYER_ERROR en cas d'erreur I/O
*****/

LONG FS_ReadFile(struct FSHandle *h, UBYTE *Buffer, LONG Len)
{
    LONG Result=0;
    LONG RestLen=Len;

    while(RestLen>0)
    {
        LONG Pos=0,End=0;
        struct SectorCacheNode *SectorCacheNodePtr=NULL;

        /* Récupération du secteur correspondant à l'offset en cours */
        Result=P_FS_ObtainFileChunk(h,h->Offset,FALSE,&Pos,&End,&SectorCacheNodePtr);
        if(Pos>=End) break;
        /* Note: si Pos<End alors Result est forcément success.
           Si Pos>=End, alors soit on est en fin de fichier, soit Result est en échec.
        */

        /* Lecture du contenu du secteur */
        while(Pos<End && RestLen>0)
        {
            *(Buffer++)=SectorCacheNodePtr->BufferPtr[Pos++];
            h->Offset++;
            RestLen--;
        }

        P_FS_ReleaseFileChunk(h,SectorCacheNodePtr,-1);
    }

    if(Result>=0) Result=Len-RestLen;

    return Result;
}


/*****
    Routine d'écriture du fichier
    Retourne:
    - Le nombre d'octets écrits
    - 0 si la disquette est pleine
    - FS_DISKLAYER_ERROR en cas d'erreur I/O
*****/

LONG FS_WriteFile(struct FSHandle *h, UBYTE *Buffer, LONG Len)
{
    LONG Result=0;
    LONG RestLen=Len;

    while(RestLen>0)
    {
        LONG Pos,End;
        struct SectorCacheNode *SectorCacheNodePtr;
        LONG PreviousSize=FS_GetSize(h);

        /* Récupération du secteur correspondant à l'offset en cours */
        Result=P_FS_ObtainFileChunk(h,h->Offset,TRUE,&Pos,&End,&SectorCacheNodePtr);
        if(Result<0) break;

        /* Ecrasement du secteur */
        while(Pos<End && RestLen>0)
        {
            SectorCacheNodePtr->BufferPtr[Pos++]=*(Buffer++);
            h->Offset++;
            RestLen--;
        }

        P_FS_ReleaseFileChunk(h,SectorCacheNodePtr,PreviousSize);
    }

    if(Result>=0) Result=Len-RestLen;

    return Result;
}


/*****
    Fonction de positionnement dans le fichier
*****/

LONG FS_Seek(struct FSHandle *h, LONG Pos)
{
    LONG Result=h->Offset;
    LONG Size=FS_GetSize(h);

    if(Pos<0) Pos=0;
    if(Pos>Size) Pos=Size;
    h->Offset=Pos;

    return Result;
}


/*****
    Retourne la taille du fichier en fonction de son Handle
*****/

LONG FS_GetSize(struct FSHandle *h)
{
    LONG Cluster=P_FS_GetFirstCluster(h->FS,h->FileInfoIdx);
    LONG EndSize=P_FS_GetLastSectorLen(h->FS,h->FileInfoIdx);
    LONG Count;

    return P_FS_CalcFileSize(h->FS,Cluster,EndSize,&Count);
}



/*****
    Permet de retailler un fichier
    Retour:
    - >=0 si tout s'est bien passé
    - FS_DISK_FULL s'il n'y a pas assez de place sur le disque
*****/

LONG FS_SetSize(struct FSHandle *h, LONG NewSize)
{
    LONG Cluster,IdxSector,Pos,End;
    LONG Result=P_FS_GetGeoDetailFromOffset(h,NewSize,TRUE,&Cluster,&IdxSector,&Pos,&End);

    if(Result>=0)
    {
        struct FileSystem *FS=h->FS;

        /* On supprime les clusters inutiles, s'ils existent */
        P_FS_FreeClusters(FS,Cluster);

        /* On redéfinit les infos de fin de fichier */
        P_FS_Terminate(FS,h->FileInfoIdx,Cluster,IdxSector,Pos);

        if(NewSize<h->Offset) h->Offset=NewSize;
    }

    return Result;
}


/*****
    Renomme un fichier
    Retour:
    - >=0 si tout s'est bien passé
    - FS_FILE_NOT_FOUND si le fichier source n'existe pas
    - FS_RENAME_CONFLICT si le nouveau nom existe déjà

    Note: Cette fonction ne met pas à jour le descripteur du fichier.
    Il faut utiliser FS_FlushFileInfo() pour mettre à jour ce dernier, une fois
    que toutes les opérations d'écriture sont terminées.
*****/

LONG FS_RenameFile(struct FileSystem *FS, const char *NameOld, const LONG *Type, BOOL IsSensitive, const char *NameNew)
{
    LONG Result=FS_FindFile(FS,NameOld,Type,IsSensitive);

    if(Result>=0)
    {
        /* On regarde si le nouveau nom existe déjà */
        if(FS_FindFile(FS,NameNew,Type,IsSensitive)<0)
        {
            UBYTE *FileInfo=P_FS_GetFileInfo(FS,Result);

            /* Remplacement du nom original */
            Cnv_ConvertHostNameToThomsonName(NameNew,&FileInfo[FIO_NAME]);

            /* On flag pour sauvegarder plus tard les modifications */
            P_FS_SetFileInfoFlags(FS,Result);
        } else Result=FS_RENAME_CONFLICT;
    }

    return Result;
}


/*****
    Efface un fichier dans le répertoire du file system
    Retour:
    - >=0 si tout s'est bien passé,
    - FS_FILE_NOT_FOUND si le fichier n'existe pas

    Note: Cette fonction ne met pas à jour la FAT et le descripteur du fichier.
    Il faut utiliser FS_FlushFileInfo() pour mettre à jour ces derniers, une fois
    que toutes les opérations d'écriture sont terminées.
*****/

LONG FS_DeleteFile(struct FileSystem *FS, const char *Name, const LONG *Type, BOOL IsSensitive)
{
    LONG Result=FS_FindFile(FS,Name,Type,IsSensitive);

    if(Result>=0)
    {
        Result=FS_DeleteFileFromIdx(FS,Result);
    }

    return Result;
}


/*****
    Efface un fichier dans le répertoire du file system, à partir de son index dans le répertoire
    Retour:
    - FS_SUCCESS si tout s'est bien passé,

    Note: Cette fonction ne met pas à jour la FAT et le descripteur du fichier.
    Il faut utiliser FS_FlushFileInfo() pour mettre à jour ces derniers, une fois
    que toutes les opérations d'écriture sont terminées.
*****/

LONG FS_DeleteFileFromIdx(struct FileSystem *FS, LONG Idx)
{
    LONG Result=FS_SUCCESS;
    UBYTE *FileInfo=P_FS_GetFileInfo(FS,Idx);
    LONG Cluster=P_FS_GetFirstCluster(FS,Idx);

    /* Effacement du fichier dans le répertoire */
    FileInfo[FIO_NAME]=FST_ERASED;

    /* Effacement du fichier dans la FAT */
    P_FS_FreeClusters(FS,Cluster);

    /* On flag pour sauvegarder plus tard les modifications */
    P_FS_SetFileInfoFlags(FS,Idx);

    return Result;
}


/*****
    Ecriture du commentaire d'un fichier
    Retour:
    - >=0 si tout s'est bien passé,
    - FS_FILE_NOT_FOUND si le fichier n'existe pas

    Note: Cette fonction ne met pas à jour le descripteur du fichier.
    Il faut utiliser FS_FlushFileInfo() pour mettre à jour ce dernier, une fois
    que toutes les opérations d'écriture sont terminées.
*****/

LONG FS_SetComment(struct FileSystem *FS, const char *Name, const LONG *Type, BOOL IsSensitive, const char *Comment)
{
    LONG Result=FS_FindFile(FS,Name,Type,IsSensitive);

    if(Result>=0)
    {
        UBYTE *FileInfo=P_FS_GetFileInfo(FS,Result);
        LONG NewType=P_FS_GetTypeFromFileInfo(FileInfo);
        LONG ExtraData=-1;

        Cnv_ConvertHostCommentToThomsonComment(Comment,&FileInfo[FIO_COMMENT],&NewType,&ExtraData);
        P_FS_SetMetaData(FS,Result,-1,-1,-1,-1,-1,-1,NewType,ExtraData);

        /* On flag pour sauvegarder plus tard les modifications */
        P_FS_SetFileInfoFlags(FS,Result);
    }

    return Result;
}


/*****
    Ecriture de la date d'un fichier
    Retour:
    - >=0 si tout s'est bien passé,
    - FS_FILE_NOT_FOUND si le fichier n'existe pas

    Note: Cette fonction ne met pas à jour le descripteur du fichier.
    Il faut utiliser FS_FlushFileInfo() pour mettre à jour ce dernier, une fois
    que toutes les opérations d'écriture sont terminées.
*****/

LONG FS_SetDate(struct FileSystem *FS, const char *Name, const LONG *Type, BOOL IsSensitive, LONG Year, LONG Month, LONG Day, LONG Hour, LONG Min, LONG Sec)
{
    LONG Result=FS_FindFile(FS,Name,Type,IsSensitive);

    if(Result>=0)
    {
        P_FS_SetMetaData(FS,Result,Year,Month,Day,Hour,Min,Sec,-1,-1);
    }

    return Result;
}


/*****
    Recherche un nom de fichier dans la table des noms de fichier
    Retour:
    - l'offset sur les informations du fichier dans le buffer "Dir",
      ainsi que le numéro de la piste et le numéro du secteur qui contient
      les informations sur le fichier.
    - FS_FILE_NOT_FOUND si le fichier n'existe pas
*****/

LONG FS_FindFile(struct FileSystem *FS, const char *Name, const LONG *Type, BOOL IsSensitive)
{
    char CurrentName[SIZEOF_CONV_HOSTNAME+sizeof(char)],FinalName[SIZEOF_CONV_HOSTNAME+sizeof(char)];
    LONG FileInfoIdx=FS_FILE_NOT_FOUND,Idx;
    LONG FIIdxS=FS_FILE_NOT_FOUND;
    LONG FIIdxI=FS_FILE_NOT_FOUND;
    BOOL FoundS=FALSE,FoundI=FALSE;

    /* On réduit le nom passé en paramètre, au format Thomson, c'est-à-dire
       à 12 caractères (avec le point de séparation nom/suffixe).
       De cette manière, en mode création, on évite la création de doublons de
       fichiers après réduction du nom.
    */
    Cnv_SplitHostName(Name,FinalName);
    for(Idx=0; Idx<FS->MaxFiles; Idx++)
    {
        UBYTE *FileInfo=P_FS_GetFileInfo(FS,Idx);
        UBYTE FileStatus=FileInfo[FIO_NAME];

        if(FileStatus!=FST_NONE && FileStatus!=FST_ERASED)
        {
            LONG Type2=P_FS_GetTypeFromFileInfo(FileInfo);

            Cnv_ConvertThomsonNameToHostName(FileInfo,CurrentName,TRUE);
            if(Utl_CompareHostName(CurrentName,&Type2,FinalName,Type,FALSE)==0 && !FoundS)
            {
                if(Type!=NULL)
                {
                    ULONG CurrentType=P_FS_GetTypeFromFileInfo(FileInfo);
                    if(CurrentType==*Type) {FIIdxS=Idx; FoundS=TRUE;}
                } else if(FIIdxS<0) FIIdxS=Idx;
            }

            if(Utl_CompareHostName(CurrentName,NULL,FinalName,NULL,TRUE)==0 && !FoundI)
            {
                if(Type!=NULL)
                {
                    ULONG CurrentType=P_FS_GetTypeFromFileInfo(FileInfo);
                    if(CurrentType==*Type) {FIIdxI=Idx; FoundI=TRUE;}
                } else if(FIIdxI<0) FIIdxI=Idx;
            }
        }
    }

    if(FIIdxS>=0)
    {
        if(Type==NULL || (Type!=NULL && FoundS)) FileInfoIdx=FIIdxS;
    }

    if(FIIdxI>=0 && FileInfoIdx<0 && !IsSensitive)
    {
        if(Type==NULL || (Type!=NULL && FoundI)) FileInfoIdx=FIIdxI;
    }

    return FileInfoIdx;
}


/*****
    Retourne le type du fichier en cours
*****/

LONG FS_GetTypeFromHandle(struct FSHandle *h)
{
    UBYTE *FileInfo=P_FS_GetFileInfo(h->FS,h->FileInfoIdx);
    return P_FS_GetTypeFromFileInfo(FileInfo);
}


/*****
    Permet d'extraire les informations de nom et de type à partir de
    l'index du fichier dans le répertoire.
*****/

void FS_GetNameTypeFromIndex(struct FileSystem *FS, LONG Index, char *Name, LONG *Type)
{
    *Name=0;
    *Type=0;
    if(Index>=0)
    {
        UBYTE *FileInfo=P_FS_GetFileInfo(FS,Index);

        Cnv_ConvertThomsonNameToHostName(FileInfo,Name,TRUE);
        *Type=P_FS_GetTypeFromFileInfo(FileInfo);
    }
}


/*****
    Retourne le nom du volume
*****/

void FS_GetVolumeName(struct FileSystem *FS, char *VolumeName)
{
    Cnv_ConvertThomsonLabelToHostLabel(FS->Label,VolumeName,TRUE);
}


/*****
    Change le nom du volume
*****/

LONG FS_SetVolumeName(struct FileSystem *FS, const char *VolumeName)
{
    LONG Result=FS_SUCCESS;

    Cnv_ConvertHostLabelToThomsonLabel(VolumeName,&FS->Label[FV_NAME]);
    if(!DL_WriteSector(FS->DiskLayerPtr,FS->TrackSys,1,FS->Label)) Result=FS_DISKLAYER_ERROR;

    return Result;
}


/*****
    Pour obtenir la date du volume (si disponible)
*****/

BOOL FS_GetVolumeDate(struct FileSystem *FS, LONG *Year, LONG *Month, LONG *Day, LONG *Hour, LONG *Min, LONG *Sec)
{
    BOOL Result=FALSE;

    if(FS->IsExtended)
    {
        UBYTE *Ptr=FS->Label;

        *Day=(LONG)Ptr[FV_DAY];
        *Month=(LONG)Ptr[FV_MONTH];
        *Year=(LONG)Ptr[FV_YEAR];
        *Year=*Year+(*Year<80?2000:1900);
        *Hour=(LONG)Ptr[FV_HOUR];
        *Min=(LONG)Ptr[FV_MIN];
        *Sec=(LONG)Ptr[FV_SEC];
        if(*Day>=1 && *Day<=31 && *Month>=1 && *Month<=12)
        {
            if(*Hour<24 && *Min<60 && *Sec<60) Result=TRUE;
        }
    }

    return Result;
}


/*****
    Calcule le nombre de kilo octets encore disponible sur la disquette
    Note: Pas d'erreur I/O possible
*****/

LONG FS_GetFreeSpace(struct FileSystem *FS)
{
    LONG i,Count=0;

    for(i=1;i<=FS->MaxBlocks;i++) if(FS->FAT[i]==CLST_FREE) Count++;

    return FS->BlocksPerTrack*Count;
}


/*****
    Calcule le nombre de blocs disponibles et le nombre de blocs
    utilisés sur la disquette
    Note: Pas d'erreur I/O possible
*****/

LONG FS_GetBlockSpace(struct FileSystem *FS, LONG *CountUsedSpace)
{
    LONG i,Count=0;

    for(i=1;i<=FS->MaxBlocks;i++) if(FS->FAT[i]==CLST_FREE) Count++;
    if(CountUsedSpace!=NULL) *CountUsedSpace=FS->MaxBlocks-Count;

    return Count;
}


/*****
    Retourne le texte de l'erreur FS
*****/

const char *FS_GetTextErr(ULONG ErrorCode)
{
    char *Result="";

    switch(ErrorCode)
    {
        case FS_SUCCESS:
        case FS_NO_MORE_ENTRY:
            break;

        case FS_FILE_NOT_FOUND:
            Result="File not found";
            break;

        case FS_FILE_ALREADY_EXISTS:
            Result="File already exists";
            break;

        case FS_DIRECTORY_FULL:
            Result="Directory full";
            break;

        case FS_DISK_FULL:
            Result="Disk full";
            break;

        case FS_NOT_ENOUGH_MEMORY:
            Result="Not enough memory";
            break;

        case FS_DISKLAYER_ERROR:
            Result="Disk layer error";
            break;

        case FS_OTHER:
            Result="Unknown FS error";
            break;
    }

    return Result;
}


/*****
    Ouverture d'un fichier sur le file system
*****/

struct FSHandle *P_FS_SubOpenFile(struct FileSystem *FS, LONG Mode, LONG Idx, const char *Name, const LONG *Type, BOOL IsSetDate, LONG *ErrorCode)
{
    struct FSHandle *h=P_FS_AddNewHandle(FS);

    *ErrorCode=FS_NOT_ENOUGH_MEMORY;

    if(h!=NULL)
    {
        *ErrorCode=FS_SUCCESS;
        h->FS=FS;
        h->Mode=Mode;
        h->FileInfoIdx=Idx;
        h->Offset=0;

        switch(Mode)
        {
            case FS_MODE_OLDFILE:
            default:
                if(Idx<0) *ErrorCode=FS_FILE_NOT_FOUND;
                break;

            case FS_MODE_NEWFILE:
                if(Idx>=0)
                {
                    LONG Cluster=P_FS_GetFirstCluster(FS,Idx);
                    P_FS_FreeClusters(FS,Cluster);
                    P_FS_Terminate(FS,Idx,Cluster,0,0);
                } else *ErrorCode=P_FS_CreateNewFile(h,Name,Type,IsSetDate);
                break;

            case FS_MODE_READWRITE:
                if(Idx<0) *ErrorCode=P_FS_CreateNewFile(h,Name,Type,IsSetDate);
                break;
        }

        if(*ErrorCode<0)
        {
            FS_CloseFile(h);
            h=NULL;
        }
    }

    return h;
}


/*****
    Création d'un nouveau fichier
*****/

LONG P_FS_CreateNewFile(struct FSHandle *h, const char *Name, const LONG *Type, BOOL IsSetDate)
{
    LONG Cluster=FS_DIRECTORY_FULL;
    struct FileSystem *FS=h->FS;
    LONG IdxOfErasedZone=-1,IdxOfFreeZone=-1;
    LONG FileInfoIdx;

    /* Recherche une place libre dans le directory */
    for(FileInfoIdx=0; FileInfoIdx<FS->MaxFiles; FileInfoIdx++)
    {
        UBYTE *FileInfo=P_FS_GetFileInfo(FS,FileInfoIdx);
        if(FileInfo[FIO_NAME]==FST_ERASED && IdxOfErasedZone<0) IdxOfErasedZone=FileInfoIdx;
        if(FileInfo[FIO_NAME]==FST_NONE && IdxOfFreeZone<0) IdxOfFreeZone=FileInfoIdx;
    }

    if(IdxOfFreeZone>=0) FileInfoIdx=IdxOfFreeZone;
    else if(IdxOfErasedZone>=0) FileInfoIdx=IdxOfErasedZone;

    if(FileInfoIdx<FS->MaxFiles)
    {
        /* Recherche d'un bloc libre */
        Cluster=P_FS_AllocNewCluster(FS,FileInfoIdx,-1);
        if(Cluster>=0)
        {
            /* Maintenant, on peut intialiser le FileInfo */
            LONG Year=-1,Month=0,Day=0,Hour=0,Min=0,Sec=0;
            UBYTE *FileInfo=P_FS_GetFileInfo(FS,FileInfoIdx);

            h->FileInfoIdx=FileInfoIdx;

            /* Nom, Comment et First Cluster */
            Cnv_ConvertHostNameToThomsonName(Name,&FileInfo[FIO_NAME]);
            Cnv_ConvertHostCommentToThomsonComment("",&FileInfo[FIO_COMMENT],NULL,NULL);
            FileInfo[FIO_FIRST_CLUSTER]=Cluster;

            /* Date/Heure et Type */
            if(IsSetDate) Sys_GetTime(&Year,&Month,&Day,&Hour,&Min,&Sec);
            P_FS_SetMetaData(FS,FileInfoIdx,Year,Month,Day,Hour,Min,Sec,*Type,-1);

            /* On flag pour les mises à jour faites sur le nom, sachant que AllocNewCluster et SetMetaData l'ont déjà flagué... */
            P_FS_SetFileInfoFlags(FS,FileInfoIdx);
        }
    }

    return Cluster;
}


/*****
    Permet de modifier les données annexes du fichier (type, date et extradata)
*****/

void P_FS_SetMetaData(struct FileSystem *FS, LONG FileInfoIdx, LONG Year, LONG Month, LONG Day, LONG Hour, LONG Min, LONG Sec, LONG Type, LONG ExtraData)
{
    UBYTE *FileInfo=P_FS_GetFileInfo(FS,FileInfoIdx);
    BOOL IsChanged=FALSE;

    /* Gestion de la date du fichier */
    if(Year>=0)
    {
        FileInfo[FIO_DAY]=(UBYTE)Day;
        FileInfo[FIO_MONTH]=(UBYTE)Month;
        FileInfo[FIO_YEAR]=(UBYTE)(Year%100);
        IsChanged=TRUE;
    }

    /* Gestion de l'horaire du fichier */
    if(FS->IsExtended &&  Hour>=0)
    {
        FileInfo[FIO_HOUR]=(UBYTE)Hour;
        FileInfo[FIO_MIN]=(UBYTE)Min;
        FileInfo[FIO_SEC]=(UBYTE)Sec;
        IsChanged=TRUE;
    }

    /* Gestion du type du fichier */
    if(Type>=0)
    {
        FileInfo[FIO_TYPE]=(UBYTE)(Type>>8);
        FileInfo[FIO_TYPE+1]=(UBYTE)Type;
        IsChanged=TRUE;
    }

    /* Gestion des données annexes */
    if(ExtraData>=0)
    {
        FileInfo[FIO_CHG1]=(UBYTE)(ExtraData>>8);
        FileInfo[FIO_CHG2]=(UBYTE)ExtraData;
        IsChanged=TRUE;
    }

    /* On flag pour sauvegarder plus tard les modifications */
    if(IsChanged) P_FS_SetFileInfoFlags(FS,FileInfoIdx);
}


/*****
    Extraction du type d'un fichier à partir de sa structure FileInfo
*****/

LONG P_FS_GetTypeFromFileInfo(UBYTE *FileInfo)
{
    return ((LONG)FileInfo[FIO_TYPE]<<8)+(LONG)FileInfo[FIO_TYPE+1];
}


/*****
    Retourne le morceau du fichier ciblé par l'offset
    * Paramètres:
      h: handle
      Offset: l'offset ciblé
      IsWrite: pour indiquer si on va écrire ou lire dans le chunk. En mode écriture, la fonction agrandit le fichier.
      Pos: pour retourner la position de l'offset dans le chunk
      End: pour indiquer la fin du chunk
      SectorCacheNodePtr: retourne le pointeur sur le chunk
    * Retourne:
      >=0 succès. Sinon, code d'erreur
*****/
LONG P_FS_ObtainFileChunk(struct FSHandle *h, LONG Offset, BOOL IsWrite, LONG *Pos, LONG *End, struct SectorCacheNode **SectorCacheNodePtr)
{
    LONG Result;
    LONG Cluster;
    LONG IdxSector;
    struct FileSystem *FS=h->FS;

    *SectorCacheNodePtr=NULL;

    /* On récupère les infos sur la position de l'offset */
    Result=P_FS_GetGeoDetailFromOffset(h,Offset,FALSE,&Cluster,&IdxSector,Pos,End);
    if(Result>=0 && IsWrite)
    {
        /* En mode écriture, on utilise tout le secteur en cours */
        *End=FS->FSSectorSize;

        /* On teste s'il ne reste plus de place sur le secteur en cours */
        if(*Pos>=*End)
        {
            *Pos=0;

            /* S'il ne reste plus de place, on teste s'il reste un secteur dispo sur le bloc en cours */
            if(IdxSector+1<FS->SectorsPerBlock)
            {
                IdxSector++;
                P_FS_Terminate(FS,h->FileInfoIdx,Cluster,IdxSector,0);
            }
            else
            {
                /* Si le bloc en cours est complètement plein, on tente d'agrandir le fichier */
                IdxSector=0;
                Cluster=P_FS_AllocNewCluster(FS,h->FileInfoIdx,Cluster);
                if(Cluster<0) Result=Cluster; /* = code d'erreur d'AllocNewCluster */
            }
        }
    }

    /* Si pas d'erreur (ex: disque plein) ou que l'on n'est pas en fin de fichier (en mode
       ReadOnly), alors on récupère les données du secteur.
    */
    if(Result>=0 && *Pos<*End)
    {
        LONG Track=Cluster>>1;
        LONG Sector=((Cluster&1)*FS->SectorsPerBlock)+IdxSector+1;
        struct DiskLayer *DLayer=FS->DiskLayerPtr;

        /* On tente de récupérer le cache du secteur */
        BOOL IsSuccess=DL_GetSector(DLayer,Track,Sector,TRUE,SectorCacheNodePtr);
        if(!IsSuccess) Result=DLayer->Error?FS_DISKLAYER_ERROR:FS_NOT_ENOUGH_MEMORY;
    }

    return Result;
}


/*****
    Permet de fermer le chunk précédemment ouvert par P_FS_ObtainFileChunk().
    Cette fonction permet de redéfinir la fin du fichier si celui-ci a grossi.
    * Paramètres:
      h: handle du fichier
      SectorCacheNodePtr: pointeur précédemment fourni par P_FS_ObtainFileChunk()
      PreviousSize: -1 si on était en lecture seule, sinon transmettre la nouvelle taille du fichier
*****/

void P_FS_ReleaseFileChunk(struct FSHandle *h, struct SectorCacheNode *SectorCacheNodePtr, LONG PreviousSize)
{
    if(PreviousSize>=0 && h->Offset>PreviousSize)
    {
        LONG Count;
        LONG Cluster=P_FS_GetFirstCluster(h->FS,h->FileInfoIdx);
        LONG EndLen=h->Offset-P_FS_CalcFileSize(h->FS,Cluster,0,&Count);
        P_FS_Terminate(h->FS,h->FileInfoIdx,-1,-1,EndLen);
    }

    Sch_Release(SectorCacheNodePtr,PreviousSize<0?FALSE:TRUE);
}


/*****
    Pour redéfinir la fin d'un fichier
*****/

void P_FS_Terminate(struct FileSystem *FS, LONG FileInfoIdx, LONG Cluster, LONG IdxSector, LONG EndLen)
{
    if(Cluster>=0 && IdxSector>=0)
    {
        FS->FAT[Cluster+1]=CLST_TERM+IdxSector+1;
        FS->IsFATUpdated=TRUE;
    }

    if(FileInfoIdx>=0 && EndLen>=0)
    {
        UBYTE *FileInfo=P_FS_GetFileInfo(FS,FileInfoIdx);
        FileInfo[FIO_LAST_SEC_LEN]=(UBYTE)(EndLen>>8);
        FileInfo[FIO_LAST_SEC_LEN+1]=(UBYTE)EndLen;
        P_FS_SetFileInfoFlags(FS,FileInfoIdx);
    }
}


/*****
    Pour flaguer comme quoi des infos de fichier ont été mises à jour
*****/

void P_FS_SetFileInfoFlags(struct FileSystem *FS, LONG FileInfoIdx)
{
    FS->FileInfoFlags|=1<<(FileInfoIdx/FS->FilesPerSector+2);
}

/*****
    Permet d'allouer un nouveau cluster à la suite de celui passé en paramètre.
    * Paramètres:
      FS: Poiteur sur la structure FileSystem
      FileInfoIdx: Index sur le File Info ou -1
      Cluster: Cluster précédent, ou -1 si on crée un nouveau fichier
    * Retourne:
      >=0: nouveau cluster
      <0: code d'erreur
*****/

LONG P_FS_AllocNewCluster(struct FileSystem *FS, LONG FileInfoIdx, LONG Cluster)
{
    LONG NewCluster=FS_DISK_FULL;
    LONG i;

    /* Si aucun cluster est passé en paramètre, on recherche après la zone directory */
    if(Cluster<0) Cluster=FS->ClusterSys;

    /* On recherche un cluster libre après le cluster passé en paramètre */
    for(i=Cluster; i<FS->MaxBlocks && NewCluster<0; i++)
    {
        LONG CurCluster=(LONG)FS->FAT[i+1];
        if(CurCluster==CLST_FREE) NewCluster=i;
    }

    /* Si on ne trouve pas de cluster libre, on recherche avant... */
    for(i=Cluster; i>=0 && NewCluster<0; i--)
    {
        LONG CurCluster=(LONG)FS->FAT[i+1];
        if(CurCluster==CLST_FREE) NewCluster=i;
    }

    if(NewCluster>=0)
    {
        /* Initialisation de la FAT pour le nouveau cluster */
        if(FS->FAT[Cluster+1]!=CLST_RESERVED)
        {
            FS->FAT[Cluster+1]=(UBYTE)NewCluster;
            FS->IsFATUpdated=TRUE;
        }

        P_FS_Terminate(FS,FileInfoIdx,NewCluster,0,0);
    }

    return NewCluster;
}


/*****
    Fonction pour retourner les informations géométriques qui correspondent à l'offset demandé.
    Si IsGrowEnabled égal true, et que l'Offset est supérieur à la taille du fichier, la fonction auto agrandit le fichier.
    * Paramètres:
      h: handle du fichier
      Offset: offset demandé
      IsGrowEnabled: TRUE pour auto agrandir le fichier si besoin, sinon FALSE
      Cluster: retourne le cluster correspondant à l'offset
      IdxSector: retourne l'index du secteur du cluster
      Pos: retourne la position de l'offset dans le secteur
      End: retourne la taille du secteur pour écrire ou lire
    * Retourne:
      - si >=0, retourne l'offset final possible
      - si <0, retourne un code d'erreur
*****/

LONG P_FS_GetGeoDetailFromOffset(struct FSHandle *h, LONG Offset, BOOL IsGrowEnabled, LONG *Cluster, LONG *IdxSector, LONG *Pos, LONG *End)
{
    LONG Result=0;
    struct FileSystem *FS=h->FS;
    LONG BlockSize=FS->SectorsPerBlock*FS->FSSectorSize;
    LONG NextCluster;

    *Cluster=P_FS_GetFirstCluster(FS,h->FileInfoIdx);
    *IdxSector=0;
    *Pos=0;
    *End=0;

    /* On se positionne sur le cluster relatif à l'offset */
    NextCluster=(LONG)FS->FAT[*Cluster+1];
    while(Result>=0 && Result+BlockSize<=Offset)
    {
        if(NextCluster>CLST_TERM && IsGrowEnabled) NextCluster=P_FS_AllocNewCluster(FS,h->FileInfoIdx,*Cluster);
        if(NextCluster<0) Result=NextCluster; /* = code d'erreur d'AllocNewCluster */
        else if(NextCluster<=CLST_TERM)
        {
            Result+=BlockSize;
            *Cluster=NextCluster;
            NextCluster=(LONG)FS->FAT[*Cluster+1];
        } else break;
    }

    /* Si pas d'erreur, on recherche le secteur relatif à l'offset */
    if(Result>=0)
    {
        LONG SectorCount=NextCluster<=CLST_TERM?FS->SectorsPerBlock:NextCluster-CLST_TERM;

        while(Result+FS->FSSectorSize<=Offset && (*IdxSector+1)<SectorCount)
        {
            Result+=FS->FSSectorSize;
            (*IdxSector)++;
        }

        /* On calcule la taille du secteur */
        *End=NextCluster>CLST_TERM && (*IdxSector+1)>=SectorCount?P_FS_GetLastSectorLen(FS,h->FileInfoIdx):FS->FSSectorSize;

        /* On calcule la position sur le secteur */
        *Pos=Offset>=Result+*End?*End:Offset-Result;

        Result+=*Pos;
    }

    return Result;
}

/*****
    Libère la chaîne de clusters à partir du cluster passé en paramètre
*****/

void P_FS_FreeClusters(struct FileSystem *FS, LONG Cluster)
{
    LONG i;

    for(i=0; i<FS->MaxBlocks && Cluster<=CLST_TERM; i++)
    {
        LONG NextCluster=(LONG)FS->FAT[Cluster+1];
        FS->FAT[Cluster+1]=CLST_FREE;
        Cluster=NextCluster;
        FS->IsFATUpdated=TRUE;
    }
}


/*****
    Calcule une taille en fonction des paramètres en entrée.
    * Paramètres:
      FS: pointeur sur la structure FileSystem
      Cluster: premier cluster d'une chaine de clusters
      EndSize: taille occupée par le dernier secteur
      CountOfBlocks: retourne le nombre de clusters occupés
    * Retourne:
      retourne la taille calculée
*****/

LONG P_FS_CalcFileSize(struct FileSystem *FS, LONG Cluster, LONG EndSize, LONG *CountOfBlocks)
{
    LONG Result=0;

    *CountOfBlocks=0;
    if(Cluster>=0 && Cluster<=CLST_TERM)
    {
        LONG i;

        (*CountOfBlocks)++;
        Cluster=(LONG)FS->FAT[Cluster+1];
        for(i=0; i<FS->MaxBlocks && Cluster<=CLST_TERM; i++)
        {
            EndSize+=FS->SectorsPerBlock*FS->FSSectorSize;
            Cluster=(LONG)FS->FAT[Cluster+1];
            (*CountOfBlocks)++;
        }

        if(Cluster>CLST_TERM+FS->SectorsPerBlock) Cluster=CLST_TERM+FS->SectorsPerBlock;
        Result=EndSize+FS->FSSectorSize*(Cluster-CLST_TERM-1);
    }

    return Result;
}


/*****
    Retourne un pointeur sur le fileinfo du fichier
*****/

UBYTE *P_FS_GetFileInfo(struct FileSystem *FS, LONG FileInfoIdx)
{
    return &FS->Dir[FileInfoIdx*SIZEOF_FILEINFO];
}


/*****
    Retourne le premier cluster du fichier
*****/

LONG P_FS_GetFirstCluster(struct FileSystem *FS, LONG FileInfoIdx)
{
    UBYTE *FileInfo=P_FS_GetFileInfo(FS,FileInfoIdx);
    return (LONG)FileInfo[FIO_FIRST_CLUSTER];
}


/*****
    Retourne la taille du dernier secteur du fichier
*****/

LONG P_FS_GetLastSectorLen(struct FileSystem *FS, LONG FileInfoIdx)
{
    UBYTE *FileInfo=P_FS_GetFileInfo(FS,FileInfoIdx);
    return ((LONG)FileInfo[FIO_LAST_SEC_LEN]<<8)+(LONG)FileInfo[FIO_LAST_SEC_LEN+1];
}


/*****
    Ajoute un handle dans la liste des handles
*****/

struct FSHandle *P_FS_AddNewHandle(struct FileSystem *FS)
{
    struct FSHandle *h=(struct FSHandle *)Sys_AllocMem(sizeof(struct FSHandle)+FS->SectorSize);

    if(h!=NULL)
    {
        h->NextHandlePtr=FS->FirstHandlePtr;
        h->PrevHandlePtr=NULL;
        if(FS->FirstHandlePtr!=NULL) FS->FirstHandlePtr->PrevHandlePtr=h;
        FS->FirstHandlePtr=h;
    }

    return h;
}


/*****
    Libère un handle de la liste des handles
*****/

void P_FS_RemoveHandle(struct FileSystem *FS, struct FSHandle *h)
{
    struct FSHandle *PrevHandlePtr=h->PrevHandlePtr;
    struct FSHandle *NextHandlePtr=h->NextHandlePtr;
    if(PrevHandlePtr!=NULL) PrevHandlePtr->NextHandlePtr=h->NextHandlePtr; else FS->FirstHandlePtr=NextHandlePtr;
    if(NextHandlePtr!=NULL) NextHandlePtr->PrevHandlePtr=PrevHandlePtr;
    Sys_FreeMem((void *)h);
}
