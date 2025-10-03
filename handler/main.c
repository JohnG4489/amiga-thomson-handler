#include "system.h"
#include "debug.h"
#include "main.h"
#include "handler.h"
#include "filesystem.h"
#include "disklayer.h"


/*
    23-04-2021 (Seg)    Gestion du mode étendu via le flag
    10-09-2020 (Seg)    Quelques adaptations suite à la refonte globale de la couche filesystem
    14-08-2018 (Seg)    Gestion des paramètres géométriques du disque
    30-08-2016 (Seg)    Finalisation du handler
    03-01-2009 (Seg)    Début du codage du Handler Thomson

    TODO:
    - gérer les fichiers lower/upper case.
    - gérer les doublons de fichiers liés au type différent.
*/

#define DEFAULT_BUFFERS 16

char Version[]="\0$VER:tofilesystem v0.84b by Seg (c) Dimension "__AMIGADATE__;


/***** Prototypes */
BOOL WaitStart(struct HandlerData *);
void MainLoop(struct HandlerData *);
BOOL CheckStatus(struct HandlerData *, LONG, LONG *);


/*****
    Routine principale
*****/

LONG __saveds main(VOID)
{
    if(Sys_OpenAllLibs())
    {
        struct HandlerData HData;
        LONG Idx;

        /* Initialisation de la structure HandlerData */
        for(Idx=0; Idx<sizeof(HData); Idx++) ((UBYTE *)&HData)[Idx]=0;
        HData.Process=(struct Process *)FindTask(NULL);
        HData.FileSystemStatus=FS_OTHER;
        Debug(T("START HANDLER"));

        /* Initialisation du timer */
        if((HData.TimerPort=CreateMsgPort())!=NULL)
        {
            if((HData.TimerIO=(struct timerequest *)CreateExtIO(HData.TimerPort,sizeof(struct timerequest)))!=NULL)
            {
                if(!OpenDevice(TIMERNAME,UNIT_MICROHZ,(struct IORequest *)HData.TimerIO,0))
                {
                    /* Envoi de la première requête time out */
                    HData.TimerIO->tr_time.tv_secs=0;
                    HData.TimerIO->tr_time.tv_micro=0;
                    HData.TimerIO->tr_node.io_Command=TR_ADDREQUEST;
                    SendIO((struct IORequest *)HData.TimerIO);

                    /* Début du traitement des messages */
                    if(WaitStart(&HData))
                    {
                        LONG Result2=RETURN_OK;

                        MainLoop(&HData);

                        /* On nettoie tout. Note: les messages résiduels sont détruits
                           lors de la libération du msgport.
                        */
                        WaitIO((struct IORequest *)HData.TimerIO);
                        Hdl_Flush(&HData,&Result2); /* + MOTOR OFF */

                        DL_Close(HData.DiskLayerPtr);
                    }

                    FS_FreeFileSystem(HData.FS);

                    AbortIO((struct IORequest *)HData.TimerIO);
                    CloseDevice((struct IORequest *)HData.TimerIO);
                }

                DeleteExtIO((struct IORequest *)HData.TimerIO);
            }

            DeleteMsgPort(HData.TimerPort);
        }

        Debug(T("THE END"));

        Hdl_UnsetVolumeEntry(&HData);

        /* Sortie du handler */
        HData.DevNode->dn_Task=NULL;

        Sys_CloseAllLibs();
    }

    return 0;
}


/*****
    Attente du message de startup
*****/

BOOL WaitStart(struct HandlerData *HData)
{
    BOOL IsSuccess=DOSFALSE,IsExit=FALSE;

    while(!IsExit)
    {
        struct DosPacket *DosPacket=WaitPkt();
        HData->DevNode=BADDR(DosPacket->dp_Arg3);

        /* Attente du message de startup */
        if(DosPacket->dp_Type==ACTION_STARTUP)
        {
            /* Le premier message contient:
             *  - dp_Arg1: BSTR sur le nom du Device
             *  - dp_Arg2: BPTR sur la struture FileSysStartupMsg
             *  - dp_Arg3: BPTR sur la structure DeviceNode
             *  - dp_Arg4: Reserve pour un Message Port alternatif
             *
             * Format du flag: eoofllsssss
             * - e=format étendu (=1) ou original (=0).   Mask=10000000000 ($400)
             * - o=Side operation (01=side 0, 10=side 1). Mask=01100000000 ($300)
             * - f=flag Thomson (=1).                     Mask=00010000000 ($080)
             * - l=sector length (10=256 bytes).          Mask=00001100000 ($060)
             * - s=count of sectors (10000=16 sectors).   Mask=00000011111 ($01f)
            */
            struct FileSysStartupMsg *FSStartupMsg=(struct FileSysStartupMsg *)BADDR(HData->DevNode->dn_Startup);
            struct DosEnvec *EnvTab=(struct DosEnvec *)BADDR(FSStartupMsg->fssm_Environ);
            BOOL IsExtended=(FSStartupMsg->fssm_Flags>>10)&1;
            LONG BitsSectorSize=(FSStartupMsg->fssm_Flags&0x60)>>5;
            LONG BitsSectorCount=FSStartupMsg->fssm_Flags&0x1f;
            LONG SectorSize=BitsSectorSize!=0?128<<BitsSectorSize:EnvTab->de_SizeBlock;
            LONG SectorsPerTrack=BitsSectorCount!=0?BitsSectorCount:EnvTab->de_BlocksPerTrack;
            LONG CountOfBufferMax=EnvTab->de_NumBuffers!=0?EnvTab->de_NumBuffers:DEFAULT_BUFFERS;

            if((HData->FS=FS_AllocFileSystem((LONG)EnvTab->de_HighCyl+1,SectorSize,SectorsPerTrack,IsExtended))!=NULL)
            {
                ULONG ErrorCode=0;

                HData->Side=((FSStartupMsg->fssm_Flags>>8)&3)==2?1:0;

                HData->DevNode->dn_Task=(struct MsgPort *)&HData->Process->pr_MsgPort;
                HData->DeviceUnit=FSStartupMsg->fssm_Unit;
                HData->DeviceFlags=FSStartupMsg->fssm_Flags;
                Hdl_BSTRToString(FSStartupMsg->fssm_Device,HData->DeviceName,sizeof(HData->DeviceName));
                Debug(T("Startup: Side=%ld",HData->Side));

                HData->DiskLayerPtr=DL_Open(HData->DeviceName,HData->DeviceFlags,HData->DeviceUnit,HData->Side,SectorsPerTrack,SectorSize,CountOfBufferMax,Hdl_Change,(void *)HData,&ErrorCode);
                if(HData->DiskLayerPtr!=NULL)
                {
                    Debug(T("ACTION_STARTUP:\nName='%s'\nFlags=%lx\nUnit=%ld\nInterleave=%ld\nSectorSize=%ld\nSectorsPerTrack=%ld",
                        HData->DeviceName,
                        HData->DeviceFlags,
                        HData->DeviceUnit,
                        EnvTab->de_Interleave,
                        SectorSize,
                        SectorsPerTrack));

                    Hdl_CheckChange(HData);
                    IsSuccess=DOSTRUE;
                }
            }

            IsExit=TRUE;
        }

        ReplyPkt(DosPacket,IsSuccess,IsSuccess?RETURN_OK:ERROR_DEVICE_NOT_MOUNTED);
    }

    return IsSuccess;
}


/*****
    Gestion des messages reçus par le handler
*****/

void MainLoop(struct HandlerData *HData)
{
    BOOL IsExit=FALSE;

    /* Démarrage du handler */
    while(!IsExit)
    {
        ULONG WaitSig=Wait(
            (1UL<<HData->Process->pr_MsgPort.mp_SigBit)|
            (1UL<<HData->TimerPort->mp_SigBit));
        struct DiskLayer *DLayer=HData->DiskLayerPtr;

        /* On vérifie si un disque a été inséré ou retiré */
        if(DL_IsChanged(DLayer))
        {
            Hdl_CheckChange(HData);
            DL_SetChanged(DLayer,FALSE);
        }

        /* On vérifie si on a une requête Time out pour vider les buffers */
        if((WaitSig & (1UL<<HData->TimerPort->mp_SigBit))!=0)
        {
            LONG Result2=RETURN_OK;

            WaitIO((struct IORequest *)HData->TimerIO);

            /* On enregistre ce qui est en cache + Motor OFF */
            Hdl_Flush(HData,&Result2);

            Debug(T("Time Out: FLUSH\nState=%ld\nResult2=%ld",(long)HData->DeviceState,Result2));
        }

        /* Traitement des messages du handler */
        if((WaitSig & (1UL<<HData->Process->pr_MsgPort.mp_SigBit))!=0)
        {
            struct Message *Msg;

            while((Msg=GetMsg(&HData->Process->pr_MsgPort))!=NULL)
            {
                struct DosPacket *DosPacket=(struct DosPacket *)Msg->mn_Node.ln_Name;
                LONG Result1=DOSFALSE;
                LONG Result2=RETURN_OK;

                if(CheckStatus(HData,DosPacket->dp_Type,&Result2))
                {
                    switch(DosPacket->dp_Type)
                    {
                        case ACTION_DIE:
                            /* Note: le DIE ne peut fonctionner que s'il n'y a pas de lock ouvert,
                               sinon, l'action retourne ERROR_OBJECT_IN_USE.
                            */
                            if(HData->FirstLock!=NULL) Result2=ERROR_OBJECT_IN_USE;
                            else {Result1=DOSTRUE; IsExit=TRUE;}
                            break;

                        case ACTION_INHIBIT: /* Inhibit(...) */
                            /* ARG1:   BOOL    DOSTRUE = inhibit
                                               DOSFALSE = uninhibit
                               RES1:   BOOL    Success/failure (DOSTRUE/DOSFALSE)

                               Note: l'action ne fonctionne que s'il n'y a pas de lock déjà ouvert,
                               sinon, elle retourne ERROR_OBJECT_IN_USE.
                            */
                            {
                                BOOL Flag=(BOOL)DosPacket->dp_Arg1;
                                if(Hdl_Inhibit(HData,Flag,&Result2)) Result1=DOSTRUE;
                                Debug(T("ACTION_INHIBIT\nMode=%ld\nResult1=%ld\nResult2=%ld",Flag,Result1,Result2));
                            }
                            break;

                        case ACTION_LOCATE_OBJECT:  /* Lock(Name,AccessMode) */
                            /* ARG1:   LOCK    Lock on directory to which ARG2 is relative
                               ARG2:   BSTR    Name (possibly with a path) of object to lock
                               ARG3:   LONG    Mode:   ACCESS_READ/SHARED_LOCK, ACCESS_WRITE/EXCLUSIVE_LOCK
                               RES1:   LOCK    Lock on requested object or 0 to indicate failure
                               RES2:   CODE    Failure code if RES1 = 0
                            */
                            {
                                struct FileLockTO *FLBase=(struct FileLockTO *)BADDR(DosPacket->dp_Arg1);
                                LONG AccessMode=(LONG)DosPacket->dp_Arg3;

                                Hdl_BSTRToString((BPTR)DosPacket->dp_Arg2,HData->TmpName,sizeof(HData->TmpName));
                                Result1=(LONG)MKBADDR(Hdl_LockObjectFromName(HData,FLBase,HData->TmpName,AccessMode,&Result2));
                                Debug(T("ACTION_LOCATE_OBJECT\nParent:%08lx\nName='%s'\nAccess=%ld\nResult1=%08lx\nResult2=%ld",FLBase,HData->TmpName,(long)AccessMode,(long)BADDR(Result1),Result2));
                            }
                            break;

                        case ACTION_FH_FROM_LOCK: /* OpenFromLock(lock) */
                            /* ARG1:   BPTR    BPTR to file handle to fill in
                               ARG2:   LOCK    Lock of file to open
                               RES1:   BOOL    Success/failure (DOSTRUE/DOSFALSE)
                               RES2:   CODE    Failure code if RES1 = NULL

                               Note: Cette action ouvre un handle en fonction d'un lock. Si un handle est
                               déjà attaché au lock, celui-ci est écrasé par le nouveau. C'est à l'utilisateur
                               de cette action d'être vigilant.
                            */
                            {
                                LONG ErrorCode=FS_SUCCESS;
                                struct FileHandle *NewFH=(struct FileHandle *)BADDR(DosPacket->dp_Arg1);
                                struct FileLockTO *FL=(struct FileLockTO *)BADDR(DosPacket->dp_Arg2);
                                LONG FSMode=FL->fl.fl_Access==EXCLUSIVE_LOCK?FS_MODE_NEWFILE:FS_MODE_OLDFILE;
                                struct FSHandle *h=FS_OpenFileFromIdx(HData->FS,FSMode,FL->fl.fl_Key,TRUE,&ErrorCode);

                                if(h!=NULL)
                                {
                                    FL->Handle=h;
                                    NewFH->fh_Arg1=(LONG)FL;
                                    Result1=DOSTRUE;
                                }

                                Result2=Hdl_ConvertFSCode(HData,ErrorCode);
                                Debug(T("ACTION_FH_FROM_LOCK:\nFH=%08lx\nFL=%08lx\nResult1=%ld\nResult2=%ld",NewFH,FL,Result1,Result2));
                            }
                            break;

                        case ACTION_FREE_LOCK:  /* UnLock(...) */
                            /* ARG1:   LOCK    Lock to free
                               RES1:   BOOL    TRUE

                               Note: Cette action libère les ressources allouées par le lock.
                               Si un handle est attaché au lock, celui-ci est fermé par cette action.
                            */
                            {
                                struct FileLockTO *FL=(struct FileLockTO *)BADDR(DosPacket->dp_Arg1);

                                if(Hdl_UnLockObject(HData,FL,&Result2)) Result1=DOSTRUE;
                                Debug(T("ACTION_FREE_LOCK:\nFL=%08lx\nResult1=%ld",FL,Result1));
                            }
                            break;

                        case ACTION_SAME_LOCK:  /* SameLock(lock1,lock2) */
                            /* ARG1:   BPTR    Lock 1 to compare
                               ARG2:   BPTR    Lock 2 to compare
                               RES1:   LONG    Result of comparison, one of
                                               DOSTRUE  if locks are for the same object
                                               DOSFALSE if locks are on different objects
                               RES2:   CODE    Failure code if RES1 is LOCK_DIFFERENT
                            */
                            {
                                struct FileLockTO *FL1=(struct FileLockTO *)BADDR(DosPacket->dp_Arg1);
                                struct FileLockTO *FL2=(struct FileLockTO *)BADDR(DosPacket->dp_Arg2);
                                if(FL1->fl.fl_Key==FL2->fl.fl_Key && FL1->fl.fl_Task==FL2->fl.fl_Task) Result1=DOSTRUE;
                                else Result2=ERROR_INVALID_LOCK;
                                Debug(T("ACTION_SAME_LOCK"));
                            }
                            break;

                        case ACTION_COPY_DIR:   /* DupLock(...) */
                            /* ARG1:   LOCK    Lock to duplicate
                               RES1:   LOCK    Duplicated Lock or 0 to indicate failure
                               RES2:   CODE    Failure code if RES1 = 0
                            */
                            {
                                struct FileLockTO *FL=(struct FileLockTO *)BADDR(DosPacket->dp_Arg1);
                                Result1=MKBADDR(Hdl_LockObjectFromLock(HData,FL,&Result2));
                                Debug(T("ACTION_COPY_DIR:\nFL=%08lx\nResult1=%08lx\nResult2=%ld",(long)FL,BADDR(Result1),Result2));
                            }
                            break;

                        case ACTION_COPY_DIR_FH: /* DupLockFromFH(fh) */
                            /* ARG1:   LONG    fh_Arg1 of file handle
                               RES1:   BPTR    Lock associated with file handle or NULL
                               RES2:   CODE    Failure code if RES1 = NULL
                            */
                            {
                                struct FileLockTO *FL=(struct FileLockTO *)DosPacket->dp_Arg1;
                                Result1=MKBADDR(Hdl_LockObjectFromLock(HData,FL,&Result2));
                                Debug(T("ACTION_COPY_DIR_FH:\nFL=%08lx\nResult1=%08lx\Result2=%ld",FL,BADDR(Result1),Result2));
                            }
                            break;

                        case ACTION_PARENT:     /* Parent(...) */
                            /* ARG1:   LOCK    Lock on object to get the parent of
                               RES1:   LOCK    Parent Lock
                               RES2:   CODE    Failure code if RES1 = 0
                            */
                            {
                                struct FileLockTO *FL=(struct FileLockTO *)BADDR(DosPacket->dp_Arg1);
                                if(FL->fl.fl_Key<0) Result1=NULL;
                                else Result1=(LONG)MKBADDR(Hdl_LockObjectFromName(HData,FL,":",SHARED_LOCK,&Result2));
                                Debug(T("ACTION_PARENT:\nFL=%08lx\nKey=%ld\nResult1=%08lx\nResult2=%ld",FL,FL->fl.fl_Key,BADDR(Result1),Result2));
                            }
                            break;

                        case ACTION_PARENT_FH:  /* ParentOfFH(fh) */
                            /* ARG1:   LONG    fh_Arg1 of File handle to get parent of
                               RES1:   BPTR    Lock on parent of a file handle
                               RES2:   CODE    Failure code if RES1 = NULL
                            */
                            {
                                struct FileLockTO *FL=(struct FileLockTO *)DosPacket->dp_Arg1;
                                if(FL->fl.fl_Key<0) Result1=NULL;
                                else Result1=(LONG)MKBADDR(Hdl_LockObjectFromName(HData,FL,":",SHARED_LOCK,&Result2));
                                Debug(T("ACTION_PARENT_FH:\nFL=%08lx\nKey=%ld\nResult1=%08lx\nResult2=%ld",FL,FL->fl.fl_Key,BADDR(Result1),Result2));
                            }
                            break;

                        case ACTION_EXAMINE_OBJECT: /* Examine(lock,fib) */
                            /* ARG1:   LOCK    Lock of object to examine
                               ARG2:   BPTR    FileInfoBlock to fill in
                               RES1:   BOOL    Success/failure (DOSTRUE/DOSFALSE)
                               RES2:   CODE    Failure code if RES1 = DOSFALSE
                            */
                            {
                                struct FileLockTO *FL=(struct FileLockTO *)BADDR(DosPacket->dp_Arg1);
                                struct FileInfoBlock *fib=(struct FileInfoBlock *)BADDR(DosPacket->dp_Arg2);
                                if(Hdl_ExamineObject(HData,FL,fib,&Result2)) Result1=DOSTRUE;
                                Hdl_BSTRToString(MKBADDR(fib->fib_FileName),HData->TmpName,sizeof(HData->TmpName)); //Pour le log
                                Debug(T("ACTION_EXAMINE_OBJECT:\nFL=%08lx\nName='%s'\nResult1=%ld\nResult2=%ld",FL,HData->TmpName,Result1,Result2));
                            }
                            break;

                        case ACTION_EXAMINE_NEXT: /* ExNext(lock,fib) */
                            /* ARG1:   LOCK    Lock on directory being examined
                               ARG2:   BPTR    BPTR FileInfoBlock
                               RES1:   BOOL    Success/failure (DOSTRUE/DOSFALSE)
                               RES2:   CODE    Failure code if RES1 = DOSFALSE
                            */
                            {
                                struct FileLockTO *FL=(struct FileLockTO *)BADDR(DosPacket->dp_Arg1);
                                struct FileInfoBlock *fib=(struct FileInfoBlock *)BADDR(DosPacket->dp_Arg2);
                                if(Hdl_ExamineNext(HData,FL,fib,&Result2)) Result1=DOSTRUE;
                                Debug(T("ACTION_EXAMINE_NEXT:\nFL=%08lx\nfib_Protection=%08lx\nResult1=%ld\nResult2=%ld",FL,fib->fib_Protection,Result1,Result2));
                            }
                            break;

                        case ACTION_EXAMINE_FH: /* ExamineFH(fh,fib) */
                             /* ARG1:   BPTR    File handle on open file
                                ARG2:   BPTR    FileInfoBlock to fill in
                                RES1:   BOOL    Success/Failure (DOSTRUE/DOSFALSE)
                                RES2:   CODE    Failure code if RES1 is DOSFALSE
                            */
                            {
                                struct FileLockTO *FL=(struct FileLockTO *)DosPacket->dp_Arg1;
                                struct FileInfoBlock *fib=(struct FileInfoBlock *)BADDR(DosPacket->dp_Arg2);
                                if(Hdl_ExamineObject(HData,FL,fib,&Result2)) Result1=DOSTRUE;
                                Debug(T("ACTION_EXAMINE_FH:\nFL=%08lx\nResult1=%ld\nResult2=%ld",FL,Result1,Result2));
                            }
                            break;

                        case ACTION_EXAMINE_ALL: /* ExAll(lock,buff,size,type,ctl) */
                             /* ARG1:   BPTR    Lock on directory to examine
                                ARG2:   APTR    Buffer to store results
                                ARG3:   LONG    Length (in bytes) of buffer (ARG2)
                                ARG4:   LONG    Type of request - one of the following:
                                                ED_NAME Return only file names
                                                ED_TYPE Return above plus file type
                                                ED_SIZE Return above plus file size
                                                ED_PROTECTION Return above plus file protection
                                                ED_DATE Return above plus 3 longwords of date
                                                ED_COMMENT Return above plus comment or NULL
                                ARG5:   BPTR    Control structure to store state information.  The control
                                                  structure must be allocated with AllocDosObject()!
                                RES1:   LONG    Continuation flag - DOSFALSE indicates termination
                                RES2:   CODE    Failure code if RES1 is DOSFALSE
                            */
                            {
                                struct FileLockTO *FL=(struct FileLockTO *)BADDR(DosPacket->dp_Arg1);
                                UBYTE *BufferPtr=(UBYTE *)DosPacket->dp_Arg2;
                                LONG Len=DosPacket->dp_Arg3;
                                LONG Type=DosPacket->dp_Arg4;
                                struct ExAllControl *eac=(struct ExAllControl *)DosPacket->dp_Arg5;

                                if(Hdl_ExamineAll(HData,FL,BufferPtr,Len,Type,eac,&Result2)) Result1=DOSTRUE;
                                Debug(T("ACTION_EXAMINE_ALL:\nFL=%08lx\nPtr=%08lx\nLen=%ld\nType=%ld\neac=%08lx\nKey=%ld\nEntry=%ld\nResult1=%ld\nResult2=%ld",
                                    FL,
                                    BufferPtr,
                                    Len,
                                    Type,
                                    eac,
                                    eac->eac_LastKey,
                                    eac->eac_Entries,
                                    Result1,
                                    Result2));
                            }
                            break;

                        case ACTION_FINDINPUT:  /* Open(..., MODE_OLDFILE) */
                        case ACTION_FINDOUTPUT: /* Open(..., MODE_NEWFILE) */
                        case ACTION_FINDUPDATE: /* Open(..., MODE_READWRITE) */
                            /* ARG1:   BPTR    FileHandle to fill in
                               ARG2:   LOCK    Lock on directory that ARG3 is relative to
                               ARG3:   BSTR    Name of file to be opened (relative to ARG1)
                               RES1:   BOOL    Success/Failure (DOSTRUE/DOSFALSE)
                               RES2:   CODE    Failure code if RES1 is DOSFALSE

                               Note: ces actions sont évitées en amont si aucun disque n'est présent dans
                               le lecteur. En revanche, ces actions gèrent si le disque est protégé.
                            */
                            {
                                struct FileLockTO *NewFL;
                                struct FileHandle *NewFH=(struct FileHandle *)BADDR(DosPacket->dp_Arg1);
                                struct FileLockTO *FLBase=(struct FileLockTO *)BADDR(DosPacket->dp_Arg2);

                                Hdl_BSTRToString(DosPacket->dp_Arg3,HData->TmpName,sizeof(HData->TmpName));
                                if((NewFL=Hdl_OpenFile(HData,FLBase,HData->TmpName,DosPacket->dp_Type,&Result2))!=NULL)
                                {
                                    NewFH->fh_Arg1=(LONG)NewFL;
                                    Result1=DOSTRUE;
                                }
                                Debug(T("ACTION_FIND %ld...\nName='%s'\nFL=%08lx\nResult1=%ld\nResult2=%ld",(long)DosPacket->dp_Type,HData->TmpName,NewFL,Result1,Result2));
                            }
                            break;

                        case ACTION_END:        /* Close(...) */
                            /* ARG1:   ARG1    fh_Arg1 field of the opened FileHandle
                               RES1:   LONG    DOSTRUE

                               Note: cette action est évitée en amont si aucun disque n'est présent dans
                               le lecteur. En revanche, cette action gère si le disque est protégé.
                            */
                            {
                                struct FileLockTO *FL=(struct FileLockTO *)DosPacket->dp_Arg1;

                                if(Hdl_CloseFile(HData,FL,&Result2)) Result1=DOSTRUE;
                                Debug(T("ACTION_END:\nFL=%08lx\nResult1=%ld\nResult2=%ld",FL,Result1,Result2));
                            }
                            break;

                        case ACTION_READ:       /* Read(...) */
                            /* ARG1:   ARG1    fh_Arg1 field of the opened FileHandle
                               ARG2:   APTR    Buffer to put data into
                               ARG3:   LONG    Number of bytes to read
                               RES1:   LONG    Number of bytes read.
                                                0 indicates EOF.
                                               -1 indicates ERROR
                               RES2:   CODE    Failure code if RES1 is -1

                               Note: cette action est évitée en amont si aucun disque n'est présent dans
                               le lecteur.
                            */
                            {
                                struct FileLockTO *FL=(struct FileLockTO *)DosPacket->dp_Arg1;
                                UBYTE *BufferPtr=(UBYTE *)DosPacket->dp_Arg2;
                                LONG Size=DosPacket->dp_Arg3;
                                Result1=Hdl_Read(HData,FL,BufferPtr,Size,&Result2);
                                Debug(T("ACTION_READ:\nFL=%08lx\nLen=%ld\nResult1=%ld\nResult2=%ld",FL,(long)Size,Result1,Result2));
                            }
                            break;

                        case ACTION_WRITE:      /* Write(...) */
                            /* ARG1:   ARG1    fh_Arg1 field of the opened file handle
                               ARG2:   APTR    Buffer to write to the file handle
                               ARG3:   LONG    Number of bytes to write
                               RES1:   LONG    Number of bytes written.
                               RES2:   CODE    Failure code if RES1 not the same as ARG3

                               Note: cette action est évitée en amont si aucun disque n'est présent dans
                               le lecteur. En revanche, cette action gère si le disque est protégé.
                            */
                            {
                                struct FileLockTO *FL=(struct FileLockTO *)DosPacket->dp_Arg1;
                                UBYTE *BufferPtr=(UBYTE *)DosPacket->dp_Arg2;
                                LONG Size=DosPacket->dp_Arg3;

                                Result1=Hdl_Write(HData,FL,BufferPtr,Size,&Result2);
                                Debug(T("ACTION_WRITE:\nFL=%08lx\nLen=%ld\nResult1=%ld\nResult2=%ld",FL,Size,Result1,Result2));
                            }
                            break;

                        case ACTION_SEEK:       /* Seek(...) */
                            /* ARG1:   ARG1    fh_Arg1 field of the opened FileHandle
                               ARG2:   LONG    New Position
                               ARG3:   LONG    Mode: OFFSET_BEGINNING,OFFSET_END, or  OFFSET_CURRENT
                               RES1:   LONG    Old Position.   -1 indicates an error
                               RES2:   CODE    Failure code if RES1 = -1
                            */
                            {
                                struct FileLockTO *FL=(struct FileLockTO *)DosPacket->dp_Arg1;
                                LONG Pos=DosPacket->dp_Arg2;
                                LONG Mode=DosPacket->dp_Arg3;

                                Result1=Hdl_Seek(HData,FL,Pos,Mode,&Result2);
                                Debug(T("ACTION_SEEK\nLock=%08lx\nPos=%ld\nMode=%ld\nResult1=%ld\nResult2=%ld",FL,Pos,Mode,Result1,Result2));
                            }
                            break;

                        case ACTION_SET_FILE_SIZE: /* SetFileSize(file,off,mode) */
                            /* ARG1:   BPTR    FileHandle of opened file to modify
                               ARG2:   LONG    New end of file location based on mode
                               ARG3:   LONG    Mode.  One of OFFSET_CURRENT, OFFSET_BEGIN, or OFFSET_END
                               RES1:   BOOL    Success/Failure (DOSTRUE/DOSFALSE)
                               RES2:   CODE    Failure code if RES1 is DOSFALSE

                               Note: cette action est évitée en amont si aucun disque n'est présent dans
                               le lecteur. En revanche, cette action gère si le disque est protégé.
                            */
                            {
                                struct FileLockTO *FL=(struct FileLockTO *)DosPacket->dp_Arg1;
                                LONG Len=DosPacket->dp_Arg2;
                                LONG Mode=DosPacket->dp_Arg3;

                                if(Hdl_SetFileSize(HData,FL,Mode,Len,&Result2)) Result1=DOSTRUE;
                                Debug(T("ACTION_SET_FILE_SIZE:\nFL=%08lx\nLen=%ld\nMode=%ld\nResult1=%ld\nResult2=%ld",FL,Len,Mode,Result1,Result2));
                            }
                            break;

                        case ACTION_FORMAT:     /* Format(fs,vol,type) */
                            /* ARG1:   BSTR    Name for volume (if supported)
                               ARG2:   LONG    Type of format (file system specific)
                               RES1:   BOOL    Success/Failure (DOSTRUE/DOSFALSE)
                               RES2:   CODE    Failure code if RES1 is DOSFALSE

                               Note: cette action est évitée en amont si aucun disque n'est présent dans
                               le lecteur. En revanche, cette action gère si le disque est protégé.
                            */
                            {
                                LONG Type=DosPacket->dp_Arg2;
                                Hdl_BSTRToString(DosPacket->dp_Arg1,HData->TmpName,sizeof(HData->TmpName));
                                if(Hdl_Format(HData,HData->TmpName,&Result2)) Result1=DOSTRUE;
                                Debug(T("ACTION_FORMAT:\nName='%s'\nType=%08lx\nResult1=%ld\nResult2=%ld",HData->TmpName,Type,Result1,Result2));
                            }
                            break;

                        case ACTION_DELETE_OBJECT: /* DeleteFile(...) */
                            /* ARG1:   LOCK    Lock to which ARG2 is relative
                               ARG2:   BSTR    Name of object to delete (relative to ARG1)
                               RES1:   BOOL    Success/failure (DOSTRUE/DOSFALSE)
                               RES2:   CODE    Failure code if RES1 = DOSFALSE

                               Note: cette action est évitée en amont si aucun disque n'est présent dans
                               le lecteur. En revanche, cette action gère si le disque est protégé.
                            */
                            {
                                struct FileLockTO *FLBase=(struct FileLockTO *)BADDR(DosPacket->dp_Arg1);
                                Hdl_BSTRToString(DosPacket->dp_Arg2,HData->TmpName,sizeof(HData->TmpName));
                                if(Hdl_Delete(HData,FLBase,HData->TmpName,&Result2)) Result1=DOSTRUE;
                                Debug(T("ACTION_DELETE_OBJECT:\nFLBase=%08lx\nName='%s'\nResult1=%ld\nResult2=%ld",FLBase,HData->TmpName,Result1,Result2));
                            }
                            break;

                        case ACTION_SET_COMMENT: /* SetComment(...) */
                            /* ARG1:   Unused
                               ARG2:   LOCK    Lock to which ARG3 is relative
                               ARG3:   BSTR    Name of object (relative to ARG2)
                               ARG4:   BSTR    New Comment string
                               RES1:   BOOL    Success/failure (DOSTRUE/DOSFALSE)
                               RES2:   CODE    Failure code if RES1 = DOSFALSE

                               Note: cette action est évitée en amont si aucun disque n'est présent dans
                               le lecteur. En revanche, cette action gère si le disque est protégé.
                            */
                            {
                                struct FileLockTO *FLBase=(struct FileLockTO *)BADDR(DosPacket->dp_Arg2);
                                Hdl_BSTRToString(DosPacket->dp_Arg3,HData->TmpName,sizeof(HData->TmpName));
                                Hdl_BSTRToString(DosPacket->dp_Arg4,HData->TmpName2,sizeof(HData->TmpName2));
                                if(Hdl_SetComment(HData,FLBase,HData->TmpName,HData->TmpName2,&Result2)) Result1=DOSTRUE;
                                Debug(T("ACTION_SET_COMMENT:\nFLBase=%08lx\nName='%s'\nComment='%s'\nResult1=%ld\nResult2=%ld",FLBase,HData->TmpName,HData->TmpName2,Result1,Result2));
                            }
                            break;

                        case ACTION_RENAME_OBJECT:  /* Rename(...) */
                            /* ARG1:   LOCK    Lock to which ARG2 is relative
                               ARG2:   BSTR    Name of object to rename (relative to ARG1)
                               ARG3:   LOCK    Lock associated with target directory
                               ARG4:   BSTR    Requested new name for the object
                               RES1:   BOOL    Success/failure (DOSTRUE/DOSFALSE)
                               RES2:   CODE    Failure code if RES1 = DOSFALSE

                               Note: cette action est évitée en amont si aucun disque n'est présent dans
                               le lecteur. En revanche, cette action gère si le disque est protégé.
                            */
                            {
                                struct FileLockTO *FLBase1=(struct FileLockTO *)BADDR(DosPacket->dp_Arg1);
                                struct FileLockTO *FLBase2=(struct FileLockTO *)BADDR(DosPacket->dp_Arg3);
                                Hdl_BSTRToString(DosPacket->dp_Arg2,HData->TmpName,sizeof(HData->TmpName));
                                Hdl_BSTRToString(DosPacket->dp_Arg4,HData->TmpName2,sizeof(HData->TmpName2));
                                if(Hdl_Rename(HData,FLBase1,HData->TmpName,FLBase2,HData->TmpName2,&Result2)) Result1=DOSTRUE;
                                Debug(T("ACTION_RENAME_OBJECT\nName='%s'\nNew Name='%s'\nResult1=%ld\nResult2=%ld",HData->TmpName,HData->TmpName2,Result1,Result2));
                            }
                            break;

                        case ACTION_RENAME_DISK: /* Relabel(...) */
                            /* ARG1:   BSTR    New disk name
                               RES1:   BOOL    Success/Failure (DOSTRUE/DOSFALSE)

                               Note: cette action est évitée en amont si aucun disque n'est présent dans
                               le lecteur. En revanche, cette action gère si le disque est protégé.
                            */
                            {
                                Hdl_BSTRToString(DosPacket->dp_Arg1,HData->TmpName,sizeof(HData->TmpName));
                                if(Hdl_Relabel(HData,HData->TmpName,&Result2)) Result1=DOSTRUE;
                                Debug(T("ACTION_RENAME_DISK\nName='%s'\nResult1=%ld\nResult2=%ld",HData->TmpName,Result1,Result2));
                            }
                            break;

                        case ACTION_DISK_INFO:  /* Info(...) */
                            /* ARG1:   BPTR    Pointer to an InfoData structure to fill in
                               RES1:   BOOL    Success/Failure (DOSTRUE/DOSFALSE)
                            */
                            {
                                struct InfoData *InfoData=(struct InfoData *)BADDR(DosPacket->dp_Arg1);
                                if(Hdl_DiskInfo(HData,InfoData)) Result1=DOSTRUE;
                                Debug(T("ACTION_DISK_INFO:\nid_NumBlocks=%ld\nid_NumBlocksUsed=%ld\nid_BytesPerBlock=%ld\nid_DiskType=%08lx\nid_UnitNumber=%ld",
                                    InfoData->id_NumBlocks,
                                    InfoData->id_NumBlocksUsed,
                                    InfoData->id_BytesPerBlock,
                                    InfoData->id_DiskType,
                                    InfoData->id_UnitNumber));
                            }
                            break;

                        case ACTION_INFO: /* Bool=Info(Lock,InfoData)  */
                            {
                                //struct FileLockEx *FL=(struct FileLockEx *)BADDR(DosPacket->dp_Arg1);
                                struct InfoData *InfoData=(struct InfoData *)BADDR(DosPacket->dp_Arg2);
                                if(Hdl_DiskInfo(HData,InfoData)) Result1=DOSTRUE;
                                Debug(T("ACTION_INFO:\nid_NumBlocks=%ld\nid_NumBlocksUsed=%ld\nid_BytesPerBlock=%ld\nid_DiskType=%08lx\nid_UnitNumber=%ld",
                                    InfoData->id_NumBlocks,
                                    InfoData->id_NumBlocksUsed,
                                    InfoData->id_BytesPerBlock,
                                    InfoData->id_DiskType,
                                    InfoData->id_UnitNumber));
                            }
                            break;

                        case ACTION_SET_PROTECT: /* SetProtection(...) */
                            /* ARG1:   Unused
                               ARG2:   LOCK    Lock to which ARG3 is relative
                               ARG3:   BSTR    Name of object (relative to ARG2)
                               ARG4:   LONG    Mask of new protection bits
                               RES1:   BOOL    Success/failure (DOSTRUE/DOSFALSE)
                               RES2:   CODE    Failure code if RES1 = DOSFALSE
                            */
                            DosPacket->dp_Res2=ERROR_ACTION_NOT_KNOWN;
                            Debug(T("ACTION_SET_PROTECT"));
                            break;

                        case ACTION_IS_FILESYSTEM: /* IsFileSystem(devname) */
                            /* RES1:   BOOL    Success/Failure (DOSTRUE/DOSFALSE)
                               RES2:   CODE    Failure code if RES1 is DOSFALSE
                            */
                            Result1=DOSTRUE;
                            Debug(T("ACTION_IS_FILESYSTEM"));
                            break;

                        case ACTION_SET_DATE:   /* SetFileDate(...) */
                            /* ARG1:   Unused
                               ARG2:   LOCK    Lock to which ARG3 is relative
                               ARG3:   BSTR    Name of Object (relative to ARG2)
                               ARG4:   CPTR    DateStamp
                               RES1:   BOOL    Success/failure (DOSTRUE/DOSFALSE)
                               RES2:   CODE    Failure code if RES1 = DOSFALSE

                               Note: cette action est évitée en amont si aucun disque n'est présent dans
                               le lecteur. En revanche, cette action gère si le disque est protégé.
                            */
                            {
                                struct FileLockTO *FLBase=(struct FileLockTO *)BADDR(DosPacket->dp_Arg2);
                                struct DateStamp *ds=(struct DateStamp *)DosPacket->dp_Arg4;
                                Hdl_BSTRToString(DosPacket->dp_Arg3,HData->TmpName,sizeof(HData->TmpName));
                                if(Hdl_SetDate(HData,FLBase,HData->TmpName,ds,&Result2)) Result1=DOSTRUE;
                                Debug(T("ACTION_SET_DATE"));
                            }
                            break;

                        case ACTION_CURRENT_VOLUME:
                            /* ARG1:   APTR    Lock (filled in by Open()) or NULL
                               RES1:   BPTR    Volume node structure
                               RES2:   LONG    Unit number
                               fharg1,Magic,Count=VolNode,UnitNr,Private
                            */
                            {
                                struct FileLockTO *FL=(struct FileLockTO *)DosPacket->dp_Arg1;
                                Result1=FL!=NULL?FL->fl.fl_Volume:MKBADDR(HData->DevNode);
                                Result2=HData->DeviceUnit;
                                Debug(T("ACTION_CURRENT_VOLUME:\nFL=%08lx\nResult1=%08lx\nResult2=%ld",FL,Result1,Result2));
                            }
                            break;

                        case ACTION_FLUSH:
                            /* RES1:   BOOL    DOSTRUE */
                            {
                                if(!Hdl_Flush(HData,&Result2)) Result1=DOSTRUE;
                                Debug(T("ACTION_FLUSH"));
                            }
                            break;

                        case ACTION_WRITE_PROTECT:
                            /* ARG1:   BOOL    DOSTRUE/DOSFALSE (write protect/un-write protect)
                               ARG2:   LONG    32 Bit pass key
                               RES1:   BOOL    DOSTRUE/DOSFALSE
                            */
                            Debug(T("ACTION_WRITE_PROTECT"));
                            break;

                        case ACTION_SET_OWNER:
                            {
                                ULONG Owner=(ULONG)DosPacket->dp_Arg4;
                                Result1=DOSTRUE;
                                Debug(T("ACTION_SET_OWNER:\nOwner=%ld",Owner));
                            }
                            break;

                        case ACTION_CREATE_DIR:
                            /* ARG1:   LOCK    Lock to which ARG2 is relative
                               ARG2:   BSTR    Name of new directory  (relative to ARG1)
                               RES1:   LOCK    Lock on new directory
                               RES2:   CODE    Failure code if RES1 = DOSFALSE
                            */
                            {
                                Result2=ERROR_ACTION_NOT_KNOWN;
                            }
                            break;

                        case ACTION_ADD_NOTIFY:
                            Debug(T("ACTION_ADD_NOTIFY"));
                            /*{
                                m=(struct fsNotifyRequest *)DosPacket->dp_Arg1;
                            }*/
                            break;

                        case ACTION_REMOVE_NOTIFY:
                            Debug(T("ACTION_REMOVE_NOTIFY"));
                            /*{
                                sruct fsNotifyRequest *nr;
                                m=(struct fsNotifyRequest *)DosPacket->dp_Arg1;
                            }*/
                            break;

                        case ACTION_MORE_CACHE:
                            /* ARG1:   LONG    Number of buffers to add
                               //RES1:   BOOL    DOSTRUE (-1L)
                               //RES2:   LONG    New total number of buffers
                               RES1:   LONG    New total number of buffers
                               RES2:   LONG    Error code
                            */
                            {
                                LONG NumberToAdd=(LONG)DosPacket->dp_Arg1;
                                Result1=DL_SetBufferMax(HData->DiskLayerPtr,-1,NumberToAdd);
                                Debug(T("ACTION_MORE_CACHE: AddBuffers(%ld), Final=%ld",NumberToAdd,Result1));
                            }
                            break;

                        case ACTION_TOFS_LOCKSECTOR:
                            {
                                LONG Unit=(LONG)DosPacket->dp_Arg1;
                                LONG Side=(LONG)DosPacket->dp_Arg2;
                                LONG Track=(LONG)DosPacket->dp_Arg3;
                                LONG Sector=(LONG)DosPacket->dp_Arg4;
                                BOOL IsReadOnly=(BOOL)DosPacket->dp_Arg5;
                                Debug(T("ACTION_TOFS_LOCKSECTOR: Arg1=%ld, Arg2=%ld, Arg3=%ld, Arg4=%ld, Arg5=%ld",Unit,Side,Track,Sector,IsReadOnly));
                            }
                            break;

                        default:
                            Result2=ERROR_ACTION_NOT_KNOWN;
                            Debug(T("Action inconnue!\n%ld",(long)DosPacket->dp_Type));
                            break;
                    }
                }

                //DosPacket->dp_Link->mn_Node.ln_Name=(char *)DosPacket; /* Connect Message and Packet */
                //DosPacket->dp_Link->mn_Node.ln_Succ=NULL;
                //DosPacket->dp_Link->mn_Node.ln_Pred=NULL;
                //DosPacket->dp_Res1=Result1;
                //DosPacket->dp_Res2=Result2;
                //DosPacket->dp_Port=&HData->Process->pr_MsgPort; /* Setting Packet-Port back */
                //PutMsg(DosPacket->dp_Port,DosPacket->dp_Link); /* Send the Message */
                ReplyPkt(DosPacket,Result1,Result2);
            }
        }
    }
}


/*****
    Permet de valider si le type de packet est permis.
    Cette fonction teste l'état du file system, la présence du disque, ainsi
    que le flag d'inhibition.
    Note: Cette fonction ne teste pas si le disque est protégé ou non. Ceci
    est fait au niveau des fonctions concernées.
*****/

BOOL CheckStatus(struct HandlerData *HData, LONG Type, LONG *Result2)
{
    BOOL IsOk=TRUE;

    switch(Type)
    {
        case ACTION_LOCATE_OBJECT:
        case ACTION_FH_FROM_LOCK:
        case ACTION_FREE_LOCK:
        case ACTION_SAME_LOCK:
        case ACTION_COPY_DIR:
        case ACTION_COPY_DIR_FH:
        case ACTION_PARENT:
        case ACTION_PARENT_FH:
        case ACTION_FINDINPUT:
        case ACTION_FINDOUTPUT:
        case ACTION_FINDUPDATE:
        case ACTION_READ:
        case ACTION_WRITE:
        case ACTION_END:
        case ACTION_SEEK:
        case ACTION_SET_FILE_SIZE:
        case ACTION_SET_COMMENT:
        case ACTION_SET_DATE:
        case ACTION_DELETE_OBJECT:
        case ACTION_RENAME_OBJECT:
        case ACTION_EXAMINE_OBJECT:
        case ACTION_EXAMINE_NEXT:
        case ACTION_EXAMINE_FH:
        case ACTION_EXAMINE_ALL:
        case ACTION_RENAME_DISK:
        case ACTION_INFO:
        case ACTION_SET_PROTECT:
        case ACTION_FLUSH:
        case ACTION_SET_OWNER:
        case ACTION_CREATE_DIR:
        case ACTION_CURRENT_VOLUME:
            /* Ces commandes sont non permises en cas d'absence de disque,
               de file system invalide, ou de mode inhibit activé.
            */
            if(HData->DeviceState==DS_NONE)
            {
                Debug(T("ERROR_NO_DISK: %ld",Type));
                *Result2=ERROR_NO_DISK;
                IsOk=FALSE;
            }
            else if(HData->FileSystemStatus!=FS_SUCCESS || HData->InhibitCounter>0)
            {
                Debug(T("ERROR_NOT_A_DOS_DISK: %ld",Type));
                *Result2=ERROR_NOT_A_DOS_DISK;
                IsOk=FALSE;
            }
            break;

        case ACTION_FORMAT:
            /* Ces commandes sont non permises en cas d'absence de disque
               uniquement.
            */
            if(HData->DeviceState==DS_NONE)
            {
                Debug(T("ERROR_NO_DISK: %ld",Type));
                *Result2=ERROR_NO_DISK;
                IsOk=FALSE;
            }
            break;

        case ACTION_DISK_INFO:
        case ACTION_DIE:
        case ACTION_INHIBIT:
        case ACTION_IS_FILESYSTEM:
        case ACTION_ADD_NOTIFY:
        case ACTION_REMOVE_NOTIFY:
        case ACTION_WRITE_PROTECT:
        case ACTION_MORE_CACHE:
        case ACTION_TOFS_LOCKSECTOR:
            /* Ces commandes sont toujours permises */
            break;
    }

    return IsOk;
}
