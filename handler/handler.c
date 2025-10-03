#include "system.h"
#include "handler.h"
#include "filesystem.h"
#include "disklayer.h"
#include "util.h"
#include "convert.h"
#include "debug.h"
#include <proto/intuition.h>
#include <libraries/iffparse.h>
#include <devices/input.h>

/*
    01-10-2020 (Seg)    Externalisation des routines de debug dans debuc.c/.h
    23-09-2020 (Seg)    Gestion des buffers
    10-09-2020 (Seg)    Quelques adaptations suite à la refonte globale de la couche filesystem
    28-08-2018 (Seg)    Synchro et remaniement de code pour gérer les caractères accentués des noms et commentaires de fichiers
    11-08-2018 (Seg)    Gestion des paramètres géométriques du disque
    05-06-2017 (Seg)    Modifications pour gérer les "extradata" des fichiers .CHG
    01-10-2016 (Seg)    Réorganisation du code + ajout version
    14-09-2016 (Seg)    Refonte totale
    09-04-2010 (Seg)    Gestion du handler
*/


/***** Prototypes */
BOOL Hdl_Inhibit(struct HandlerData *, BOOL, LONG *);

struct FileLockTO *Hdl_LockObjectFromName(struct HandlerData *, struct FileLockTO *, const char *, LONG, LONG *);
struct FileLockTO *Hdl_LockObjectFromLock(struct HandlerData *, struct FileLockTO *, LONG *);
BOOL Hdl_UnLockObject(struct HandlerData *, struct FileLockTO *, LONG *);

BOOL Hdl_Flush(struct HandlerData *, LONG *);

struct FileLockTO *Hdl_OpenFile(struct HandlerData *, struct FileLockTO *, const char *, LONG, LONG *);
BOOL Hdl_CloseFile(struct HandlerData *, struct FileLockTO *, LONG *);
LONG Hdl_Read(struct HandlerData *, struct FileLockTO *, UBYTE *, LONG, LONG *);
LONG Hdl_Write(struct HandlerData *, struct FileLockTO *, UBYTE *, LONG, LONG *);
LONG Hdl_Seek(struct HandlerData *, struct FileLockTO *, LONG, LONG, LONG *);

BOOL Hdl_ExamineObject(struct HandlerData *, struct FileLockTO *, struct FileInfoBlock *, LONG *);
BOOL Hdl_ExamineNext(struct HandlerData *, struct FileLockTO *, struct FileInfoBlock *, LONG *);
BOOL Hdl_ExamineAll(struct HandlerData *, struct FileLockTO *, UBYTE *, LONG, LONG, struct ExAllControl *, LONG *);

BOOL Hdl_Rename(struct HandlerData *, struct FileLockTO *, const char *, struct FileLockTO *, const char *, LONG *);
BOOL Hdl_Delete(struct HandlerData *, struct FileLockTO *, const char *, LONG *);
BOOL Hdl_SetComment(struct HandlerData *, struct FileLockTO *, const char *, const char *, LONG *);
BOOL Hdl_SetDate(struct HandlerData *, struct FileLockTO *, const char *, struct DateStamp *, LONG *);

BOOL Hdl_SetFileSize(struct HandlerData *, struct FileLockTO *, LONG, LONG, LONG *);
BOOL Hdl_DiskInfo(struct HandlerData *, struct InfoData *);
BOOL Hdl_Relabel(struct HandlerData *, const char *, LONG *);
BOOL Hdl_Format(struct HandlerData *, const char *, LONG *);

void Hdl_CheckChange(struct HandlerData *);
void Hdl_Change(struct DiskLayer *, void *);
void Hdl_SendTimeout(struct HandlerData *);

void Hdl_UnsetVolumeEntry(struct HandlerData *);

LONG Hdl_ConvertFSCode(struct HandlerData *, LONG);
LONG Hdl_BSTRToString(BSTR, char *, LONG);
LONG Hdl_StringToBSTR(const char *, char *, LONG);

void P_Hdl_FillFib(struct HandlerData *, struct FileObject *, struct FileInfoBlock *);
LONG P_Hdl_SubExamineAll(struct HandlerData *, struct FileObject *, LONG, UBYTE *, LONG);

BOOL P_Hdl_SetVolumeEntry(struct HandlerData *, const char *, ULONG);
void P_Hdl_RefreshDiskIcon(struct HandlerData *, LONG);

struct FileLockTO *P_Hdl_LockObjectFromKey(struct HandlerData *, LONG, LONG, LONG *);
BOOL P_Hdl_IsObjectLockable(struct HandlerData *, LONG, LONG);
struct FileLockTO *P_Hdl_AddNewLock(struct HandlerData *, LONG, LONG);
void P_Hdl_RemoveLock(struct HandlerData *, struct FileLockTO *);

BOOL P_Hdl_IsDeviceValids(struct HandlerData *);
ULONG P_Hdl_GetProtectionFlags(struct HandlerData *);
void P_Hdl_GetVolumeName(struct HandlerData *, char *);
ULONG P_Hdl_GetDiskType(struct HandlerData *);
void P_Hdl_DateToDateStamp(struct DateStamp *, LONG, LONG, LONG, LONG, LONG, LONG);

const char *P_Hdl_SkipVolume(const char *);
const char *P_Hdl_NamePart(const char *);
ULONG P_Hdl_ParsePath(struct FileLockTO *, const char *, char *, LONG *);


/*****
    Gestion de la désactivation/réactivation du handler
*****/

BOOL Hdl_Inhibit(struct HandlerData *HData, BOOL Flag, LONG *Result2)
{
    BOOL Result=TRUE;

    if(Flag)
    {
        /* Inhibit */
        if(HData->FirstLock!=NULL)
        {
            /* Si on a un lock, il est impossible de faire un inhibit!
               Note: Le cas où le disque est absent ou protégé en écriture est géré
               en amont, dans le main.
            */
            *Result2=ERROR_OBJECT_IN_USE;
            Result=FALSE;
        }
        else if(HData->InhibitCounter++==0)
        {
            P_Hdl_SetVolumeEntry(HData,"Busy",MAKE_ID('B','U','S','Y'));
            P_Hdl_RefreshDiskIcon(HData,IECLASS_DISKINSERTED);
        }
    }
    else
    {
        /* Unhinbit */
        if(HData->InhibitCounter>0)
        {
            if(--HData->InhibitCounter==0)
            {
                /* Permet de relire le file system, réallouer le Dos Entry
                   pour avoir le TA0: et l'icone sur le workbench, ainsi
                   que de lancer un time out pour l'extinction du moteur.
                */
                Hdl_CheckChange(HData);
            }
        }
    }

    return Result;
}


/*****
    Permet d'obtenir un lock sur un fichier, à partir de son nom
*****/

struct FileLockTO *Hdl_LockObjectFromName(struct HandlerData *HData, struct FileLockTO *FLBase, const char *Name, LONG LockMode, LONG *Result2)
{
    struct FileLockTO *NewFL=NULL;
    char ObjectName[SIZEOF_HOSTNAME+sizeof(char)];

    /* Contrôle du path et extraction du nom du fichier */
    if(P_Hdl_ParsePath(FLBase,Name,ObjectName,Result2)<=1)
    {
        LONG Idx=FS_FindFile(HData->FS,ObjectName,NULL,HData->IsSensitive);

        *Result2=ERROR_OBJECT_NOT_FOUND;
        if(Idx>=0 || *ObjectName==0)
        {
            *Result2=ERROR_OBJECT_IN_USE;
            if(P_Hdl_IsObjectLockable(HData,Idx,LockMode))
            {
                *Result2=ERROR_NO_FREE_STORE;
                NewFL=P_Hdl_AddNewLock(HData,Idx,LockMode);
                if(NewFL!=NULL) *Result2=RETURN_OK;
            }
        }
    }

    return NewFL;
}


/*****
    Permet d'obtenir un lock sur un fichier à partir d'un handle de fichier
*****/

struct FileLockTO *Hdl_LockObjectFromLock(struct HandlerData *HData, struct FileLockTO *FL, LONG *Result2)
{
    struct FileLockTO *NewFL=NULL;

    *Result2=ERROR_OBJECT_IN_USE;
    if(P_Hdl_IsObjectLockable(HData,FL->fl.fl_Key,FL->fl.fl_Access))
    {
        *Result2=ERROR_NO_FREE_STORE;
        NewFL=P_Hdl_AddNewLock(HData,FL->fl.fl_Key,FL->fl.fl_Access);
        if(NewFL!=NULL) *Result2=RETURN_OK;
    }

    return NewFL;
}


/*****
    Déverrouille l'objet
*****/

BOOL Hdl_UnLockObject(struct HandlerData *HData, struct FileLockTO *FL, LONG *Result2)
{
    BOOL Result=TRUE;

    if(FL->Handle!=NULL)
    {
        LONG ErrorCode=FS_CloseFile(FL->Handle);
        if(ErrorCode<0) Result=FALSE;
        *Result2=Hdl_ConvertFSCode(HData,ErrorCode);

        Hdl_SendTimeout(HData);
    }

    P_Hdl_RemoveLock(HData,FL);

    return Result;
}


/*****
    Met à jour les données des handlers et le cache du device
    Note: ne pas utiliser de Hdl_SetTimeout()!!!
*****/

BOOL Hdl_Flush(struct HandlerData *HData, LONG *Result2)
{
    if(P_Hdl_IsDeviceValids(HData))
    {
        struct FileSystem *FS=HData->FS;
        LONG ErrorCode=FS_FlushFileInfo(FS);

        if(ErrorCode>=0)
        {
            if(!DL_Finalize(FS->DiskLayerPtr,FALSE)) ErrorCode=FS_DISKLAYER_ERROR;
            Debug(T("FLUSH: Trackdisk. ErrorCode=%ld",ErrorCode));
        }

        *Result2=Hdl_ConvertFSCode(HData,ErrorCode);
        if(ErrorCode>=0) return TRUE;
    }

    return FALSE;
}


/*****
    Permet d'ouvrir un fichier, et de le locker
*****/

struct FileLockTO *Hdl_OpenFile(struct HandlerData *HData, struct FileLockTO *FLBase, const char *FileName, LONG AccessMode, LONG *Result2)
{
    LONG FSMode=AccessMode==MODE_NEWFILE?FS_MODE_NEWFILE:(AccessMode==MODE_READWRITE?FS_MODE_READWRITE:FS_MODE_OLDFILE);
    LONG LockMode=AccessMode==MODE_NEWFILE?EXCLUSIVE_LOCK:SHARED_LOCK;
    LONG ErrorCode=FS_SUCCESS;

    struct FileLockTO *NewFL=Hdl_LockObjectFromName(HData,FLBase,FileName,LockMode,Result2);
    if(NewFL!=NULL)
    {
        if(NewFL->fl.fl_Key>=0)
        {
            /* L'objet existe! */
            *Result2=RETURN_OK;
            NewFL->Handle=FS_OpenFileFromIdx(HData->FS,FSMode,NewFL->fl.fl_Key,TRUE,&ErrorCode);
            NewFL->Pos=0;
            if(NewFL->Handle==NULL)
            {
                Hdl_UnLockObject(HData,NewFL,Result2);
                *Result2=Hdl_ConvertFSCode(HData,ErrorCode);
                NewFL=NULL;
            }
        }
        else
        {
            Hdl_UnLockObject(HData,NewFL,Result2);
            *Result2=ERROR_OBJECT_WRONG_TYPE;
            NewFL=NULL;
        }
    }
    else if(*Result2==ERROR_OBJECT_NOT_FOUND && FSMode!=FS_MODE_OLDFILE)
    {
        /* L'object n'existe pas et on est en mode NEWFILE ou READWRITE */
        *Result2=ERROR_DISK_WRITE_PROTECTED;
        if(HData->DeviceState!=DS_WRITE_PROTECTED)
        {
            char ObjectName[SIZEOF_HOSTNAME+sizeof(char)];
            LONG Count;

            /* Contrôle du path et extraction du nom du fichier */
            if((Count=P_Hdl_ParsePath(FLBase,FileName,ObjectName,Result2))<=1)
            {
                *Result2=ERROR_OBJECT_WRONG_TYPE;
                if(Count==1)
                {
                    char FinalName[SIZEOF_CONV_HOSTNAME+sizeof(char)];
                    LONG Type;
                    struct FSHandle *Handle;

                    /* On réduit le nom passé en paramètre, au format Thomson, c'est-à-dire
                       à 12 caractères (avec le point de séparation nom/suffixe), pour obtenir
                       son suffixe sur 3 caractères, sachant qu'une réduction de nom se fait
                       déjà dans FS_OpenFile pour éviter la création de doublons de fichiers.
                    */
                    Cnv_SplitHostName(ObjectName,FinalName);
                    Type=Utl_GetTypeFromHostName(FinalName);

                    Handle=FS_OpenFile(HData->FS,FSMode,FinalName,&Type,HData->IsSensitive,TRUE,&ErrorCode);

                    *Result2=Hdl_ConvertFSCode(HData,ErrorCode);
                    if(Handle!=NULL)
                    {
                        NewFL=P_Hdl_LockObjectFromKey(HData,LockMode,Handle->FileInfoIdx,Result2);
                        if(NewFL!=NULL)
                        {
                            DateStamp(&HData->LatestMod);
                            NewFL->Handle=Handle;
                            NewFL->Pos=0;
                        }
                        else
                        {
                            FS_CloseFile(Handle);
                        }
                    }

                    Hdl_SendTimeout(HData);
                }
            }
        }
    }

    return NewFL;
}


/*****
    Ferme correctement un fichier ouvert par Hdl_OpenFile()
*****/

BOOL Hdl_CloseFile(struct HandlerData *HData, struct FileLockTO *FL, LONG *Result2)
{
    return Hdl_UnLockObject(HData,FL,Result2); /* ... s'occupe du SendTimeout() */
}


/*****
    Lecture sur le handle
*****/

LONG Hdl_Read(struct HandlerData *HData, struct FileLockTO *FL, UBYTE *BufferPtr, LONG Size, LONG *Result2)
{
    LONG Result;
    struct FSHandle *h=FL->Handle;

    FS_Seek(h,FL->Pos),
    Result=FS_ReadFile(h,BufferPtr,Size);
    FL->Pos=FS_Seek(h,0);

    if(Result<0) *Result2=Hdl_ConvertFSCode(HData,Result);

    Hdl_SendTimeout(HData);

    return Result;
}


/*****
    Ecriture sur le handle
*****/

LONG Hdl_Write(struct HandlerData *HData, struct FileLockTO *FL, UBYTE *BufferPtr, LONG Size, LONG *Result2)
{
    LONG Result=-1;

    *Result2=ERROR_DISK_WRITE_PROTECTED;
    if(HData->DeviceState!=DS_WRITE_PROTECTED)
    {
        struct FSHandle *h=FL->Handle;

        FS_Seek(h,FL->Pos),
        Result=FS_WriteFile(h,BufferPtr,Size);
        FL->Pos=FS_Seek(h,0);

        if(Result<0) *Result2=Hdl_ConvertFSCode(HData,Result);

        DateStamp(&HData->LatestMod);
        Hdl_SendTimeout(HData);
    }

    return Result;
}


/*****
    Repositionnement
*****/

LONG Hdl_Seek(struct HandlerData *HData, struct FileLockTO *FL, LONG Pos, LONG Mode, LONG *Result2)
{
    LONG Result=FL->Pos;
    struct FSHandle *h=FL->Handle;

    switch(Mode)
    {
        case OFFSET_BEGINNING:
        default:
            FL->Pos=Pos;
            break;

        case OFFSET_CURRENT:
            FL->Pos=Result+Pos;
            break;

        case OFFSET_END:
            FL->Pos=FS_GetSize(h)-Pos;
            break;
    }

    return Result;
}


/*****
    Retourne les informations sur le lock donné
    struct FileInfoBlock {
        LONG   fib_DiskKey;
        LONG   fib_DirEntryType;    Type of Directory. If < 0, then a plain file.
                                        If > 0 a directory
        char   fib_FileName[108];   Null terminated. Max 30 chars used for now
        LONG   fib_Protection;      bit mask of protection, rwxd are 3-0.
        LONG   fib_EntryType;
        LONG   fib_Size;            Number of bytes in file
        LONG   fib_NumBlocks;       Number of blocks in file
        struct DateStamp fib_Date;  Date file last changed
        char   fib_Comment[80];     Null terminated comment associated with file

        * Note: the following fields are not supported by all filesystems.
        * They should be initialized to 0 sending an ACTION_EXAMINE packet.
        * When Examine() is called, these are set to 0 for you.
        * AllocDosObject() also initializes them to 0.
        UWORD  fib_OwnerUID;        owner's UID
        UWORD  fib_OwnerGID;        owner's GID

        char   fib_Reserved[32];
    }
*****/

BOOL Hdl_ExamineObject(struct HandlerData *HData, struct FileLockTO *FL, struct FileInfoBlock *fib, LONG *Result2)
{
    BOOL IsSuccess=FALSE;

    if(FL==NULL || FL->fl.fl_Key<0)
    {
        char Tmp[TMPSIZEOF]; /* Taille arbitraire assez grande pour contenir un nom potentiellement retravaillé */

        /* L'objet est le répertoire root */
        fib->fib_DiskKey=-1; /* Comme on va commencer à scanner le root, une positionne l'équivalent du FileInfoIdx de la dos lib à -1 pour que le prochain Hdl_ExamineNext() le positionne à 0 */
        fib->fib_DirEntryType=ST_ROOT;
        P_Hdl_GetVolumeName(HData,Tmp);
        Hdl_StringToBSTR(Tmp,fib->fib_FileName,sizeof(fib->fib_FileName));
        fib->fib_Protection=P_Hdl_GetProtectionFlags(HData);
        fib->fib_EntryType=fib->fib_DirEntryType;
        fib->fib_Size=HData->FS->SectorSize*HData->FS->SectorsPerTrack;
        fib->fib_NumBlocks=2;
        fib->fib_Date=HData->LatestMod;
        Hdl_StringToBSTR("",fib->fib_Comment,sizeof(fib->fib_Comment));
        fib->fib_OwnerUID=0;
        fib->fib_OwnerGID=0;
        IsSuccess=TRUE;
    }
    else
    {
        /* L'objet est un fichier */
        struct FileObject FO;
        FO.FS=HData->FS;
        FO.FileInfoIdx=FL->fl.fl_Key-1;
        IsSuccess=FS_ExamineNextFileObject(&FO);
        if(IsSuccess) P_Hdl_FillFib(HData,&FO,fib); //else *Result2=ERROR_NO_MORE_ENTRIES;
        fib->fib_DiskKey=-2; /* Un fichier n'est pas scannable. On positionne l'équivalent du FileInfoIdx de la dos lib à -2 pour qu'il soit rejeté par Hdl_ExamineNext() */
    }

    return IsSuccess;
}


/*****
    Retourne les informations du lock courant
*****/

BOOL Hdl_ExamineNext(struct HandlerData *HData, struct FileLockTO *FL, struct FileInfoBlock *fib, LONG *Result2)
{
    if(fib->fib_DiskKey>=-1)
    {
        struct FileObject FO;
        FO.FS=HData->FS;
        FO.FileInfoIdx=fib->fib_DiskKey;
        if(FS_ExamineNextFileObject(&FO))
        {
            fib->fib_DiskKey=FO.FileInfoIdx;
            P_Hdl_FillFib(HData,&FO,fib);
            return TRUE;
        }
        *Result2=ERROR_NO_MORE_ENTRIES;
    } else *Result2=ERROR_OBJECT_WRONG_TYPE;

    return FALSE;
}


/*****
    Retourne toutes les informations du répertoire
*****/

BOOL Hdl_ExamineAll(struct HandlerData *HData, struct FileLockTO *FL, UBYTE *BufferPtr, LONG Len, LONG Type, struct ExAllControl *eac, LONG *Result2)
{
    BOOL IsSuccess=FALSE;

    *Result2=ERROR_OBJECT_WRONG_TYPE;
    if(FL->fl.fl_Key<0)
    {
        *Result2=ERROR_BAD_NUMBER;
        if(Type>=ED_NAME && Type<=ED_OWNER)
        {
            struct FileObject FO;
            struct ExAllData *PrevEAC=NULL;
            LONG Size=Len;
            BOOL IsNewEntry=TRUE;

            eac->eac_Entries=0;
            FO.FS=HData->FS;
            FO.FileInfoIdx=(LONG)eac->eac_LastKey-1;
            while(Len>0 && Size>0 && (IsNewEntry=FS_ExamineNextFileObject(&FO))!=FALSE)
            {
                if((Size=P_Hdl_SubExamineAll(HData,&FO,Type,BufferPtr,Len))>0)
                {
                    BOOL IsWanted=TRUE;
                    if(eac->eac_MatchFunc!=NULL) IsWanted=CallHookPkt(eac->eac_MatchFunc,&Type,BufferPtr);
                    else if(eac->eac_MatchString!=NULL) IsWanted=MatchPatternNoCase(eac->eac_MatchString,((struct ExAllData *)BufferPtr)->ed_Name);

                    if(IsWanted)
                    {
                        if(PrevEAC!=NULL) PrevEAC->ed_Next=(struct ExAllData *)BufferPtr;
                        PrevEAC=(struct ExAllData *)BufferPtr;

                        Size=(Size+3)&0xfffffffc; /* On aligne la prochaine structure */
                        BufferPtr=&BufferPtr[Size];
                        Len-=Size;

                        eac->eac_LastKey=FO.FileInfoIdx+1;
                        eac->eac_Entries++;
                    }
                }
            }

            *Result2=ERROR_NO_MORE_ENTRIES;
            if(IsNewEntry)
            {
                *Result2=RETURN_OK;
                IsSuccess=TRUE;
            }
        }
    }

    return IsSuccess;
}


/*****
    Renommage d'un fichier
*****/

BOOL Hdl_Rename(struct HandlerData *HData, struct FileLockTO *FLBase1, const char *PathName1, struct FileLockTO *FLBase2, const char *PathName2, LONG *Result2)
{
    BOOL IsSuccess=FALSE;

    *Result2=ERROR_DISK_WRITE_PROTECTED;
    if(HData->DeviceState!=DS_WRITE_PROTECTED)
    {
        char ObjectNameOld[SIZEOF_HOSTNAME+sizeof(char)];

        /* Contrôle du path et extraction du nom du fichier */
        if(P_Hdl_ParsePath(FLBase1,PathName1,ObjectNameOld,Result2)<=1)
        {
            char ObjectNameNew[SIZEOF_HOSTNAME+sizeof(char)];

            /* Contrôle du path et extraction du nom du fichier */
            if(P_Hdl_ParsePath(FLBase2,PathName2,ObjectNameNew,Result2)<=1)
            {
                LONG ErrorCode=FS_RenameFile(HData->FS,ObjectNameOld,NULL,HData->IsSensitive,ObjectNameNew);
                *Result2=Hdl_ConvertFSCode(HData,ErrorCode);
                if(ErrorCode>=0)
                {
                    DateStamp(&HData->LatestMod);
                    IsSuccess=TRUE;
                }

                Hdl_SendTimeout(HData);
            }
        }
    }

    return IsSuccess;
}


/*****
    Effacement d'un fichier
*****/

BOOL Hdl_Delete(struct HandlerData *HData, struct FileLockTO *FLBase, const char *PathName, LONG *Result2)
{
    BOOL IsSuccess=FALSE;

    *Result2=ERROR_DISK_WRITE_PROTECTED;
    if(HData->DeviceState!=DS_WRITE_PROTECTED)
    {
        char ObjectName[SIZEOF_HOSTNAME+sizeof(char)];

        /* Contrôle du path et extraction du nom du fichier */
        if(P_Hdl_ParsePath(FLBase,PathName,ObjectName,Result2)<=1)
        {
            LONG Idx=FS_FindFile(HData->FS,ObjectName,NULL,HData->IsSensitive);

            *Result2=Hdl_ConvertFSCode(HData,Idx);
            if(Idx>=0)
            {
                *Result2=ERROR_OBJECT_IN_USE;
                if(P_Hdl_IsObjectLockable(HData,Idx,EXCLUSIVE_LOCK))
                {
                    LONG ErrorCode=FS_DeleteFileFromIdx(HData->FS,Idx);

                    *Result2=Hdl_ConvertFSCode(HData,ErrorCode);
                    if(ErrorCode>=0)
                    {
                        DateStamp(&HData->LatestMod);
                        IsSuccess=TRUE;
                    }

                    Hdl_SendTimeout(HData);
                }
            }
        }
    }

    return IsSuccess;
}


/*****
    Applique un commentaire au fichier
*****/

BOOL Hdl_SetComment(struct HandlerData *HData, struct FileLockTO *FLBase, const char *PathName, const char *Comment, LONG *Result2)
{
    BOOL IsSuccess=FALSE;

    *Result2=ERROR_DISK_WRITE_PROTECTED;
    if(HData->DeviceState!=DS_WRITE_PROTECTED)
    {
        char ObjectName[SIZEOF_HOSTNAME+sizeof(char)];

        /* Contrôle du path et extraction du nom du fichier */
        if(P_Hdl_ParsePath(FLBase,PathName,ObjectName,Result2)<=1)
        {
            LONG ErrorCode=FS_SetComment(HData->FS,ObjectName,NULL,HData->IsSensitive,Comment);

            *Result2=Hdl_ConvertFSCode(HData,ErrorCode);
            if(ErrorCode>=0)
            {
                DateStamp(&HData->LatestMod);
                IsSuccess=TRUE;
            }

            Hdl_SendTimeout(HData);
        }
    }

    return IsSuccess;
}


/*****
    Applique une date au fichier
*****/

BOOL Hdl_SetDate(struct HandlerData *HData, struct FileLockTO *FLBase, const char *PathName, struct DateStamp *ds, LONG *Result2)
{
    BOOL IsSuccess=FALSE;

    *Result2=ERROR_DISK_WRITE_PROTECTED;
    if(HData->DeviceState!=DS_WRITE_PROTECTED)
    {
        char ObjectName[SIZEOF_HOSTNAME+sizeof(char)];

        /* Contrôle du path et extraction du nom du fichier */
        if(P_Hdl_ParsePath(FLBase,PathName,ObjectName,Result2)<=1)
        {
            struct ClockData cd;
            LONG Second=ds->ds_Days*24*60*60+ds->ds_Minute*60+ds->ds_Tick/TICKS_PER_SECOND,ErrorCode;

            Amiga2Date(Second,&cd);

            ErrorCode=FS_SetDate(HData->FS,ObjectName,NULL,HData->IsSensitive,
                (LONG)cd.year,
                (LONG)cd.month,
                (LONG)cd.mday,
                (LONG)cd.hour,
                (LONG)cd.min,
                (LONG)cd.sec);
            *Result2=Hdl_ConvertFSCode(HData,ErrorCode);
            if(ErrorCode>=0)
            {
                DateStamp(&HData->LatestMod);
                IsSuccess=TRUE;
            }

            Hdl_SendTimeout(HData);
        }
    }

    return IsSuccess;
}


/*****
    Réserve un espace sur le disque pour un fichier
*****/

BOOL Hdl_SetFileSize(struct HandlerData *HData, struct FileLockTO *FL, LONG Mode, LONG Len, LONG *Result2)
{
    BOOL IsSuccess=FALSE;

    *Result2=ERROR_DISK_WRITE_PROTECTED;
    if(HData->DeviceState!=DS_WRITE_PROTECTED)
    {
        LONG ErrorCode=FS_SUCCESS;
        struct FSHandle *h=FL->Handle;
        LONG Size=FS_GetSize(h);

        switch(Mode)
        {
            case OFFSET_BEGINNING:
            default:
                break;

            case OFFSET_CURRENT:
                Len=Size+Len;
                break;

            case OFFSET_END:
                Len=Size-Len;
                break;
        }

        if(Len<0) Len=0;
        ErrorCode=FS_SetSize(h,Len);

        *Result2=Hdl_ConvertFSCode(HData,ErrorCode);
        if(ErrorCode>=0)
        {
            DateStamp(&HData->LatestMod);
            IsSuccess=TRUE;
        }

        Hdl_SendTimeout(HData);
    }

    return IsSuccess;
}


/*****
    Retourne les informations sur le disque
    struct InfoData {
       LONG   id_NumSoftErrors;     number of soft errors on disk
       LONG   id_UnitNumber;        Which unit disk is (was) mounted on
       LONG   id_DiskState;         See defines below
       LONG   id_NumBlocks;         Number of blocks on disk
       LONG   id_NumBlocksUsed;     Number of block in use
       LONG   id_BytesPerBlock;
       LONG   id_DiskType;          Disk Type code
       BPTR   id_VolumeNode;        BCPL pointer to volume node
       LONG   id_InUse;             Flag, zero if not in use
    }
*****/

BOOL Hdl_DiskInfo(struct HandlerData *HData, struct InfoData *Info)
{
    struct FileSystem *FS=HData->FS;
    LONG Used=0,Free=FS->MaxTracks*FS->SectorsPerTrack*FS->SectorSize;

    if(P_Hdl_IsDeviceValids(HData)) Free=FS_GetBlockSpace(FS,&Used);

    Info->id_NumSoftErrors=0;
    Info->id_UnitNumber=HData->DeviceUnit;
    Info->id_DiskState=ID_VALIDATED;
    Info->id_NumBlocks=Free+Used;
    Info->id_NumBlocksUsed=Used;
    Info->id_BytesPerBlock=FS->SectorsPerBlock*FS->SectorSize; //CHECK:SectorSize ou FSSectorSize???
    Info->id_DiskType=HData->DeviceList!=NULL?HData->DeviceList->dl_DiskType:P_Hdl_GetDiskType(HData);
    Info->id_VolumeNode=MKBADDR(HData->DeviceList);
    Info->id_InUse=HData->FirstLock!=NULL?DOSTRUE:DOSFALSE;
    return TRUE;
}


/*****
    Renomme le disque
*****/

BOOL Hdl_Relabel(struct HandlerData *HData, const char *VolumeName, LONG *Result2)
{
    BOOL IsSuccess=FALSE;

    *Result2=ERROR_DISK_WRITE_PROTECTED;
    if(HData->DeviceState!=DS_WRITE_PROTECTED)
    {
        LONG ErrorCode=FS_SetVolumeName(HData->FS,VolumeName);

        if(ErrorCode>=0)
        {
            char Tmp[TMPSIZEOF]; /* Taille arbitraire assez grande pour contenir un nom potentiellement retravaillé */
            FS_GetVolumeName(HData->FS,Tmp);
            IsSuccess=P_Hdl_SetVolumeEntry(HData,Tmp,P_Hdl_GetDiskType(HData));
            DateStamp(&HData->LatestMod);
            P_Hdl_RefreshDiskIcon(HData,IECLASS_DISKINSERTED);
        }

        *Result2=Hdl_ConvertFSCode(HData,ErrorCode);

        Hdl_SendTimeout(HData);
    }

    return IsSuccess;
}


/*****
    Formatage rapide
*****/

BOOL Hdl_Format(struct HandlerData *HData, const char *VolumeName, LONG *Result2)
{
    BOOL Result=FALSE;

    *Result2=ERROR_DISK_WRITE_PROTECTED;
    if(HData->DeviceState!=DS_WRITE_PROTECTED)
    {
        if(FS_Format(HData->FS,VolumeName)>=0)
        {
            /* ATTENTION: On nettoie le cache du device ici! */
            Result=DL_Finalize(HData->DiskLayerPtr,TRUE);
        }

        if(!Result)
        {
            *Result2=Hdl_ConvertFSCode(HData,FS_DISKLAYER_ERROR);
        }

        DateStamp(&HData->LatestMod);

        /* Note: La relecture du file system, ainsi que l'extinction du moteur, sont
           lancés par le dé-hinibit.
        */
    }

    return Result;
}


/*****
    Fonction appelée automatiquement quand un disque a été inséré ou enlevé
*****/

void Hdl_CheckChange(struct HandlerData *HData)
{
    HData->DeviceState=DS_NONE;

    /* Pour savoir si on a un disque ou pas */
    if(DL_IsDiskIn(HData->DiskLayerPtr))
    {
        /* Disk In */
        char VolumeName[TMPSIZEOF]="TFS"; /* Taille arbitraire assez grande pour contenir un nom potentiellement retravaillé */

        /* Récupération du statut du lecteur ou du disque */
        HData->DeviceState=DL_IsProtected(HData->DiskLayerPtr)?DS_WRITE_PROTECTED:DS_READY;

        /* On nettoie le cache du device et le cache de la couche DiskLayer */
        DL_Clean(HData->DiskLayerPtr);

        /* Lecture du file system */
        HData->FileSystemStatus=FS_InitFileSystem(HData->FS,HData->DiskLayerPtr);
        Debug(T("Hdl_CheckChange: %ld",HData->FileSystemStatus));
        if(HData->FileSystemStatus>=0)
        {
            LONG Year,Month,Day,Hour,Min,Sec;
            if(FS_GetVolumeDate(HData->FS,&Year,&Month,&Day,&Hour,&Min,&Sec))
            {
                /* On fixe la date du disque comme dernière date de modification */
                P_Hdl_DateToDateStamp(&HData->LatestMod,Year,Month,Day,Hour,Min,Sec);
            }
            else
            {
                /* Note: comme il n'y a pas de date pour les répertoires, on met la
                   date courante. Le système se sert de la date du répertoire comme UID
                   pour savoir s'il y a eu des modifications sur le disque.
                   Par exemple, si la date est la même entre 2 disques, diskmaster
                   ne rafraîchit pas sa liste de fichiers.
                   Donc, de cette manière, la liste est rafraîchie.
                */
                DateStamp(&HData->LatestMod);
            }

            P_Hdl_GetVolumeName(HData,VolumeName);
        }

        /* On rafraîchit le nom et l'icone du volume dans l'amiga OS */
        P_Hdl_SetVolumeEntry(HData,VolumeName,P_Hdl_GetDiskType(HData));
        P_Hdl_RefreshDiskIcon(HData,IECLASS_DISKINSERTED);
        Debug(T("Disk inserted"));

        /* On lance un timeout pour exécuter un Motor Off */
        Hdl_SendTimeout(HData);
    }
    else
    {
        /* Disk Out */
        HData->FileSystemStatus=FS_OTHER;
        Hdl_UnsetVolumeEntry(HData);
        P_Hdl_RefreshDiskIcon(HData,IECLASS_DISKREMOVED);
        Debug(T("Disk removed"));
    }
}


/*****
    Hook pour gérer le changement de disque
*****/

void Hdl_Change(struct DiskLayer *DLayer, void *UserData)
{
    struct HandlerData *HData=(struct HandlerData *)UserData;
    Signal(HData->Process->pr_MsgPort.mp_SigTask,1<<HData->Process->pr_MsgPort.mp_SigBit);
}


/*****
    Envoie ou prolonge une requête Time Out pour l'écriture
*****/

void Hdl_SendTimeout(struct HandlerData *HData)
{
    if(!CheckIO((struct IORequest *)HData->TimerIO))
    {
        /* On annule la requête en cours */
        AbortIO((struct IORequest *)HData->TimerIO);
        Debug(T("Annulation Timeout"));
    }

    /* On attend le droit d'envoyer une nouvelle requête */
    WaitIO((struct IORequest *)HData->TimerIO);

    HData->TimerIO->tr_time.tv_secs=1; /* 1 seconde */
    HData->TimerIO->tr_time.tv_micro=0;
    HData->TimerIO->tr_node.io_Command=TR_ADDREQUEST;
    SetSignal(0L,1UL<<HData->TimerPort->mp_SigBit);
    SendIO((struct IORequest *)HData->TimerIO);
    Debug(T("Envoi Timeout"));
}


/*****
    Retire le volume ajouté par P_Hdl_SetVolumeEntry()
*****/

void Hdl_UnsetVolumeEntry(struct HandlerData *HData)
{
    if(HData->DeviceList!=NULL)
    {
        /* On tente de verrouiller la DosList des volumes, en mode écriture, pour pouvoir
           Retirer notre volume de cette DosList.
        */
        while(!AttemptLockDosList(LDF_VOLUMES|LDF_WRITE)) Delay(5);
        //LockDosList(LDF_VOLUMES|LDF_READ);
        RemDosEntry((struct DosList *)HData->DeviceList);
        UnLockDosList(LDF_VOLUMES|LDF_WRITE);

        /* On libère le noeud alloué dans P_Hdl_SetVolumeEntry() */
        FreeDosEntry((struct DosList *)HData->DeviceList);
        HData->DeviceList=NULL;
    }
}


/*****
    Conversion d'un code d'erreur de la lib FileSystem.c, en code Amiga OS
*****/

LONG Hdl_ConvertFSCode(struct HandlerData *HData, LONG ErrorCode)
{
    LONG Result=RETURN_OK;

    switch(ErrorCode)
    {
        case FS_SUCCESS:
        default:
            break;

        case FS_FILE_NOT_FOUND:
            Result=ERROR_OBJECT_NOT_FOUND;
            break;

        case FS_FILE_ALREADY_EXISTS:
            Result=ERROR_OBJECT_EXISTS;
            break;

        case FS_DIRECTORY_FULL:
        case FS_DISK_FULL:
            Result=ERROR_DISK_FULL;
            break;

        case FS_NOT_ENOUGH_MEMORY:
            Result=ERROR_NO_FREE_STORE;
            break;

        case FS_DISKLAYER_ERROR:
            switch(DL_GetError(HData->DiskLayerPtr))
            {
                case DL_NOT_ENOUGH_MEMORY:
                    Result=ERROR_NO_FREE_STORE;
                    break;

                case DL_OPEN_DEVICE:
                    Result=ERROR_DEVICE_NOT_MOUNTED;
                    break;

                case DL_PROTECTED:
                    Result=ERROR_WRITE_PROTECTED;
                    break;

                case DL_NO_DISK:
                    Result=ERROR_NO_DISK;
                    break;

                case DL_OPEN_FILE:
                case DL_READ_FILE :
                case DL_WRITE_FILE:
                case DL_DEVICE_IO:
                case DL_SECTOR_GEO:
                case DL_SECTOR_CRC:
                case DL_UNIT_ACCESS:
                case DL_UNKNOWN_TYPE:
                default:
                    Result=RETURN_ERROR;
                    break;
            }
            break;

        case FS_NO_MORE_ENTRY:
            Result=ERROR_NO_MORE_ENTRIES;
            break;

        case FS_RENAME_CONFLICT:
            Result=ERROR_OBJECT_EXISTS;
            break;
    }

    return Result;
}


/*****
    Conversion de chaîne BSTR
    Entrée:
    - Src: pointeur BCPL sur une chaîne BSTR
    - Dst: pointeur normal sur un buffer de conversion destination
    - SizeOfDst: taille du buffer destination (max 255 octets)
*****/

LONG Hdl_BSTRToString(BSTR Src, char *Dst, LONG SizeOfDst)
{
    const char *Ptr=(char *)BADDR(Src);
    LONG Len=0,Size=(LONG)(UBYTE)*(Ptr++);

    for(;Len<Size && Len+1<SizeOfDst; Len++) *(Dst++)=*(Ptr++);
    *Dst=0;

    return Len;
}


/*****
    Conversion d'une châine normale au format BSTR
    Note: Le pointeur Dst ne doit pas être BSTR
    Entrée:
    - Src: pointeur normal sur la chaîne source
    - Dst: pointeur normal sur le buffer destination qui doit contenir la chaîne BSTR finale
    - SizeOfDst: taille du buffer destination (min 256 octets)
*****/

LONG Hdl_StringToBSTR(const char *Src, char *Dst, LONG SizeOfDst)
{
    LONG Len=0;

    if(SizeOfDst>0)
    {
        char *Ptr=Dst++;

        for(Len=0; Len+1<SizeOfDst && *Src!=0 && Len<256; Len++) *(Dst++)=*(Src++);
        *Ptr=Len;
    }

    return Len;

}


/******************************************/
/* SOUS-ROUTINES DES FONCTIONS DU HANDLER */
/******************************************/

/*****
    Sous-routine pour Examine()
*****/

void P_Hdl_FillFib(struct HandlerData *HData, struct FileObject *FO, struct FileInfoBlock *fib)
{
    char Tmp[TMPSIZEOF]; /* Taille arbitraire assez grande pour contenir un nom potentiellement retravaillé */

    fib->fib_DirEntryType=ST_FILE;
    Hdl_StringToBSTR(FO->Name,fib->fib_FileName,sizeof(fib->fib_FileName));
    fib->fib_Protection=P_Hdl_GetProtectionFlags(HData);
    fib->fib_EntryType=fib->fib_DirEntryType;
    fib->fib_Size=FO->Size;
    fib->fib_NumBlocks=FO->CountOfBlocks;
    Utl_SetCommentMetaData(FO->Comment,Tmp,FO->Type,FO->ExtraData);
    Hdl_StringToBSTR(Tmp,fib->fib_Comment,sizeof(fib->fib_Comment));
    fib->fib_OwnerUID=0;
    fib->fib_OwnerGID=0;
    if(FO->IsTimeOk) P_Hdl_DateToDateStamp(&fib->fib_Date,FO->Year,FO->Month,FO->Day,FO->Hour,FO->Min,FO->Sec);
    else DateStamp(&fib->fib_Date);
}


/*****
    Sous routine examine all
*****/

LONG P_Hdl_SubExamineAll(struct HandlerData *HData, struct FileObject *Obj, LONG Type, UBYTE *BufferPtr, LONG SpaceLeft)
{
    LONG Size=MSIZEOF(struct ExAllData,ed_Next);
    struct ExAllData *ead=(struct ExAllData *)BufferPtr;
    char *Name=NULL,*Comment=NULL,TmpComment[TMPSIZEOF]; /* Taille arbitraire de commentaire assez grand pour contenir un commentaire potentiellement retravaillé */
    LONG NameLen=Sys_StrLen(Obj->Name)+sizeof(char);
    LONG CommentLen=Utl_SetCommentMetaData(Obj->Comment,TmpComment,Obj->Type,Obj->ExtraData)+sizeof(char);

    /* Phase 1: On calcule l'espace nécessaire */
    if(Type>=ED_NAME) Size+=MSIZEOF(struct ExAllData,ed_Name);
    if(Type>=ED_TYPE) Size+=MSIZEOF(struct ExAllData,ed_Type);
    if(Type>=ED_SIZE) Size+=MSIZEOF(struct ExAllData,ed_Size);
    if(Type>=ED_PROTECTION) Size+=MSIZEOF(struct ExAllData,ed_Prot);
    if(Type>=ED_DATE) Size+=MSIZEOF(struct ExAllData,ed_Days)+MSIZEOF(struct ExAllData,ed_Mins)+MSIZEOF(struct ExAllData,ed_Ticks);
    if(Type>=ED_COMMENT) Size+=MSIZEOF(struct ExAllData,ed_Comment);
    if(Type>=ED_OWNER) Size+=MSIZEOF(struct ExAllData,ed_OwnerUID)+MSIZEOF(struct ExAllData,ed_OwnerGID);

    if(Type>=ED_NAME)
    {
        Name=&BufferPtr[Size];
        Size+=NameLen;
    }
    if(Type>=ED_COMMENT)
    {
        Comment=&BufferPtr[Size];
        Size+=CommentLen;
    }

    if(Size<=SpaceLeft)
    {
        /* Phase 2: Remplissage de la structure */
        ead->ed_Next=NULL;
        if(Type>=ED_NAME) {ead->ed_Name=Name; Sys_StrCopy(Name,Obj->Name,NameLen);}
        if(Type>=ED_TYPE) ead->ed_Type=ST_FILE;
        if(Type>=ED_SIZE) ead->ed_Size=Obj->Size;
        if(Type>=ED_PROTECTION) ead->ed_Prot=P_Hdl_GetProtectionFlags(HData);
        if(Type>=ED_DATE)
        {
            struct DateStamp ds;
            if(Obj->IsTimeOk) P_Hdl_DateToDateStamp(&ds,Obj->Year,Obj->Month,Obj->Day,Obj->Hour,Obj->Min,Obj->Sec);
            else DateStamp(&ds);
            ead->ed_Days=ds.ds_Days;
            ead->ed_Mins=ds.ds_Minute;
            ead->ed_Ticks=ds.ds_Tick;
        }
        if(Type>=ED_COMMENT) {ead->ed_Comment=Comment; Sys_StrCopy(Comment,TmpComment,CommentLen);}
        if(Type>=ED_OWNER) {ead->ed_OwnerUID=0; ead->ed_OwnerGID=0;}

        return Size;
    }

    return 0;
}


/*****************************************************/
/* SOUS-ROUTINE DE GESTION DU VOLUME POUR LE SYSTEME */
/*****************************************************/

/*****
    Permet d'ajouter le volume dans l'Amiga OS
*****/

BOOL P_Hdl_SetVolumeEntry(struct HandlerData *HData, const char *VolumeName, ULONG DiskType)
{
    struct DeviceList *ndl=(struct DeviceList *)MakeDosEntry((STRPTR)VolumeName,DLT_VOLUME);

    if(ndl!=NULL)
    {
        ndl->dl_Task=HData->DevNode->dn_Task;
        ndl->dl_DiskType=DiskType;
        ndl->dl_LockList=MKBADDR(HData->FirstLock);

        /* Note: On ne doit pas indiquer de date de volume quand celui-ci
           est non DOS. Sinon, la commande format du workbench condidère
           le volume comme ayant un file system.
        */
        if(ndl->dl_DiskType==ID_DOS_DISK)
        {
            LONG Year,Month,Day,Hour,Min,Sec;
            if(FS_GetVolumeDate(HData->FS,&Year,&Month,&Day,&Hour,&Min,&Sec))
            {
                P_Hdl_DateToDateStamp(&ndl->dl_VolumeDate,Year,Month,Day,Hour,Min,Sec);
            }
            else
            {
                DateStamp(&ndl->dl_VolumeDate);
            }
        }

        Debug(T("P_Hdl_SetVolumeEntry: Name='%s', Type=%08lX",VolumeName,DiskType));

        /* On retire le précédent set qu'on a fait. Cette fonction fonctionne même s'il n'y en a pas */
        Hdl_UnsetVolumeEntry(HData);

        /* On tente de verrouiller la DosList qui concerne les volumes.
           Comme on souhaite ajouter un nouveau noeud, on la verrouille en écriture.
        */
        while(!AttemptLockDosList(LDF_VOLUMES|LDF_WRITE)) Delay(5);
        //LockDosList(LDF_VOLUMES|LDF_READ);

        /* On ajoute le noeud créé en début de fonction, dans cette DosList */
        if(AddDosEntry((struct DosList *)ndl)) HData->DeviceList=ndl;
        else
        {
            /* Si échec, on libère l'espace alloué par MakeDosEntry(), et tant pis... */
            FreeDosEntry((struct DosList *)ndl);
            ndl=NULL;
        }

        /* On déverrouille la DosList qui a été verrouillée par AttemptLockDosList() */
        UnLockDosList(LDF_VOLUMES|LDF_WRITE);
    }

    return (BOOL)(ndl!=NULL?TRUE:FALSE);
}


/*****
    Lance le rafraîchissement de la Dos List par le workbench.
    Note: cette fonction accélère le rafraîchissement de l'icône
*****/

void P_Hdl_RefreshDiskIcon(struct HandlerData *HData, LONG ClassID)
{
    struct MsgPort *mp;

    if((mp=CreateMsgPort())!=NULL)
    {
        struct IOStdReq *StdIO;

        if((StdIO=CreateStdIO(mp))!=NULL)
        {
            if(!OpenDevice("input.device",0,StdIO,0))
            {
                struct InputEvent ie;
                LONG i;

                //DateStamp(&dt);
                //secs=dt.ds_Days*60*60*24;
                //secs+=dt.ds_Minute*60;
                //secs+=dt.ds_Tick/50;
                for(i=0; i<sizeof(struct InputEvent); i++) ((UBYTE *)&ie)[i]=0;
                ie.ie_Class=ClassID;
                ie.ie_Qualifier=IEQUALIFIER_MULTIBROADCAST;
                StdIO->io_Command=IND_WRITEEVENT;
                StdIO->io_Data=&ie;
                StdIO->io_Length=sizeof(struct InputEvent);
                DoIO(StdIO);

                CloseDevice(StdIO);
            }

            DeleteStdIO(StdIO);
        }

        DeleteMsgPort(mp);
    }
}


/*********************/
/* GESTION DES LOCKS */
/*********************/

/*****
    Permet d'obtenir un lock sur un fichier à partir de sa clé
*****/

struct FileLockTO *P_Hdl_LockObjectFromKey(struct HandlerData *HData, LONG LockMode, LONG Key, LONG *Result2)
{
    struct FileLockTO *NewFL=NULL;

    *Result2=ERROR_OBJECT_IN_USE;
    if(P_Hdl_IsObjectLockable(HData,Key,LockMode))
    {
        *Result2=ERROR_NO_FREE_STORE;
        NewFL=P_Hdl_AddNewLock(HData,Key,LockMode);
        if(NewFL!=NULL) *Result2=RETURN_OK;
    }

    return NewFL;
}


/*****
    Permet de savoir si un fichier est lockable à partir de son index (Key)
*****/

BOOL P_Hdl_IsObjectLockable(struct HandlerData *HData, LONG Key, LONG LockMode)
{
    struct FileLockTO *Lock=HData->FirstLock;

    while(Lock!=NULL)
    {
        if(Lock->fl.fl_Key==Key)
        {
            if(Lock->fl.fl_Access==EXCLUSIVE_LOCK || LockMode==EXCLUSIVE_LOCK) return FALSE;
            break;
        }

        Lock=BADDR(Lock->fl.fl_Link);
    }

    return TRUE;
}


/*****
    Ajoute un lock dans la liste des locks
    struct FileLock {
        BPTR                fl_Link;        * bcpl pointer to next lock
        LONG                fl_Key;         * disk block number
        LONG                fl_Access;      * exclusive or shared
        struct MsgPort *    fl_Task;        * handler task's port
        BPTR                fl_Volume;      * bptr to DLT_VOLUME DosList entry
    }
*****/

struct FileLockTO *P_Hdl_AddNewLock(struct HandlerData *HData, LONG Key, LONG LockMode)
{
    struct FileLockTO *FL=(struct FileLockTO *)Sys_AllocMem(sizeof(struct FileLockTO));

    if(FL!=NULL)
    {
        FL->fl.fl_Link=MKBADDR(HData->FirstLock);
        FL->fl.fl_Key=Key;
        FL->fl.fl_Access=LockMode;
        FL->fl.fl_Task=&HData->Process->pr_MsgPort;
        FL->fl.fl_Volume=MKBADDR(HData->DeviceList);
        FL->PrevLock=NULL;
        if(HData->FirstLock!=NULL) HData->FirstLock->PrevLock=FL;
        HData->FirstLock=FL;

        if(HData->DeviceList->dl_LockList!=NULL) HData->DeviceList->dl_LockList=MKBADDR(HData->FirstLock);
    }

    return FL;
}


/*****
    Libère un lock de la liste des locks
*****/

void P_Hdl_RemoveLock(struct HandlerData *HData, struct FileLockTO *FL)
{
    struct FileLockTO *PrevLock=FL->PrevLock;
    struct FileLockTO *NextLock=BADDR(FL->fl.fl_Link);
    if(PrevLock!=NULL) PrevLock->fl.fl_Link=FL->fl.fl_Link; else HData->FirstLock=NextLock;
    if(NextLock!=NULL) NextLock->PrevLock=PrevLock;
    if(HData->DeviceList->dl_LockList!=NULL) HData->DeviceList->dl_LockList=MKBADDR(HData->FirstLock);
    Sys_FreeMem((void *)FL);
}


/************************/
/* SOUS-ROUTINES AUTRES */
/************************/

/*****
    Pour savoir si le périphérique est prêt à être géré
*****/

BOOL P_Hdl_IsDeviceValids(struct HandlerData *HData)
{
    if(HData->DeviceState!=DS_NONE && HData->FileSystemStatus>=0) return TRUE;
    return FALSE;
}


/*****
    Récupération des bits de protection des fichiers
*****/

ULONG P_Hdl_GetProtectionFlags(struct HandlerData *HData)
{
    ULONG Prot=FIBF_READ|FIBF_WRITE|FIBF_EXECUTE|FIBF_DELETE;
    Prot^=(FIBF_READ|FIBF_WRITE|FIBF_EXECUTE|FIBF_DELETE);
    //Prot^=HData->DeviceState==DS_WRITE_PROTECTED?(FIBF_READ|FIBF_EXECUTE):(FIBF_READ|FIBF_WRITE|FIBF_EXECUTE|FIBF_DELETE);
    return Prot;
}


/*****
    Récupération saine du nom du volume
    Note: il est interdit de donner un nom vide à l'OS.
*****/

void P_Hdl_GetVolumeName(struct HandlerData *HData, char *VolumeName)
{
    FS_GetVolumeName(HData->FS,(char *)VolumeName);
    if(*VolumeName==0)
    {
        Sys_SPrintf(VolumeName,"T%lc%ld_NoName",(long)(HData->Side!=0?'B':'A'),HData->DeviceUnit);
    }
}


/*****
    Retourne le bon disque type en fonction de l'état du disque
    Retour:
    - ID_NO_DISK_PRESENT: si pas de disque dans le lecteur
    - ID_UNREADABLE_DISK: si erreur de lecture sur la piste 20
    - ID_NOT_REALLY_DOS: si le file system est absent
    - ID_DOS_DISK: si tout est ok
*****/

ULONG P_Hdl_GetDiskType(struct HandlerData *HData)
{
    ULONG Type=ID_NO_DISK_PRESENT;

    if(HData->DeviceState!=DS_NONE)
    {
        if(HData->FileSystemStatus==FS_DISKLAYER_ERROR) Type=ID_UNREADABLE_DISK;
        else if(HData->FileSystemStatus==FS_NOT_DOS_DISK) Type=ID_NOT_REALLY_DOS;
        else Type=ID_DOS_DISK;
    }

    return Type;
}


/*****
    Conversion d'une date en datestamp
    Retour:
    - ds: pointe une structure DateStamp qui sera correctement initialisée
*****/

void P_Hdl_DateToDateStamp(struct DateStamp *ds, LONG Year, LONG Month, LONG Day, LONG Hour, LONG Min, LONG Sec)
{
    struct ClockData cd;
    ULONG Time;

    cd.sec=(UWORD)Sec;
    cd.min=(UWORD)Min;
    cd.hour=(UWORD)Hour;
    cd.mday=(UWORD)Day;
    cd.month=(UWORD)Month;
    cd.year=(UWORD)Year;
    cd.wday=0;
    Time=Date2Amiga(&cd);

    ds->ds_Days=Time/(24*60*60);
    ds->ds_Minute=(Time-ds->ds_Days*24*60*60)/60;
    ds->ds_Tick=((Time-ds->ds_Days*24*60*60)-ds->ds_Minute*60)*TICKS_PER_SECOND;
}


/*************************************************/
/* SOUS-ROUTINES DE GESTION DES NOMS DE FICHIERS */
/*************************************************/

/*****
    Sous routine pour passer le nom du volume dans un path
*****/

const char *P_Hdl_SkipVolume(const char *Path)
{
    const char *Result=Path;

    while(*Result!=0) if(*(Result++)==':') return Result;

    return Path;
}


/*****
    Permet d'obtenir un pointeur sur la fin d'un nom dans le path
*****/

const char *P_Hdl_NamePart(const char *Path)
{
    while(*Path!=0)
    {
        if(*Path=='/') break;
        Path++;
    }

    return Path;
}


/*****
    Extraction du nom du fichier d'un chemin, si le chemin est valide
    Retourne:
    - 0 si le chemin est valide mais ne contient pas de nom de fichier (par exemple, juste un nom de volume "TA0:")
    - 1 si un nom de fichier a été extrait
    - >1 si le chemin contient une arborescence de répertoire (non valide avec le file system thomson)
*****/

ULONG P_Hdl_ParsePath(struct FileLockTO *FLBase, const char *Path, char *ObjectName, LONG *Result2)
{
    ULONG Count=~0;

    *ObjectName=0;
    Count=0;

    Path=P_Hdl_SkipVolume(Path);
    if(*Path!=0)
    {
        for(;;)
        {
            const char *NameEnd=P_Hdl_NamePart(Path);

            Sys_StrCopyLen(ObjectName,Path,SIZEOF_HOSTNAME,(LONG)(NameEnd-Path));
            Count++;
            if(*NameEnd==0) break; else Path=NameEnd+1;
        }
    }

    *Result2=Count<=1?RETURN_OK:ERROR_DIR_NOT_FOUND;

    return Count;
}
