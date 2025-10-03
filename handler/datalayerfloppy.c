#include "system.h"
#include "datalayerfloppy.h"
#include "disklayer.h"

#ifdef SYSTEM_AMIGA
#include <devices/trackdisk.h>
#include <devices/todisk.h>
#endif

/*
    03-10-2020 (Seg)    On change la gestion des secteurs. La localisation des faces
                        est maintenant géréé par les flags du device.
    10-09-2020 (Seg)    Refonte de la couche de la commande todisk pour le handler
    27-08-2018 (Seg)    Gestion des paramètres géométriques du file system
    23-09-2013 (Seg)    Adaptation du code suite à la migration sous Unix
    25-12-2008 (Seg)    Ajout du formatage de piste
    22-12-2008 (Seg)    Gestion du floppy disk
*/


/***** Prototypes */
struct DataLayerFloppy *DFlp_Open(const char *, ULONG, ULONG, ULONG, LONG, LONG, void (*)(struct DataLayerFloppy *, void *), void *, ULONG *);
void DFlp_Close(struct DataLayerFloppy *);
BOOL DFlp_IsDiskIn(struct DataLayerFloppy *);
BOOL DFlp_IsProtected(struct DataLayerFloppy *);
void DFlp_Clean(struct DataLayerFloppy *);
ULONG DFlp_Finalize(struct DataLayerFloppy *);
ULONG DFlp_FormatTrack(struct DataLayerFloppy *, ULONG, ULONG, const UBYTE *);
ULONG DFlp_ReadSector(struct DataLayerFloppy *, ULONG, ULONG, UBYTE *);
ULONG DFlp_WriteSector(struct DataLayerFloppy *, ULONG, ULONG, const UBYTE *);

#ifdef SYSTEM_AMIGA
ULONG P_DFlp_GetError(struct DataLayerFloppy *);
BYTE P_DFlp_SetMotorState(struct DataLayerFloppy *, ULONG);
BOOL P_DFlp_IsFatalError(ULONG);
BOOL P_DFlp_CheckDiskChanged(struct DataLayerFloppy *);
BYTE P_DFlp_AddInterrupt(struct DataLayerFloppy *);
BYTE P_DFlp_RemInterrupt(struct DataLayerFloppy *);
void __asm P_DFlp_InterruptFunc(register __a1 struct DataLayerFloppy *);
#endif


/*****
    Allocation des ressources necessaires pour la lecture/ecriture
    du lecteur de disquette.
    * Paramètres:
      DeviceName: nom du device à utiliser
      Flags: flags à passer au device lors de son ouverture
      Unit: unité du device à ouvrir
      Side: facultatif dans le cas de l'utilisation du device
      SectorPerTrack: pour indiquer le nombre de secteurs par piste
      SectorSize: pour indiquer la taille d'un secteyur
      IntFuncPtr: pointeur vers une fonction callback pour savoir si un disque vient d'être inséré ou retiré
      IntData: pointeur vers une structure utilisateur à passer lors de l'utilisation du callback
      ErrorCode: pointeur vers un ULONG pour retourner un code d'erreur ou DL_SUCCESS
    * Retourne:
      - NULL si échec
      - pointeur vers une structure DataLayerFloppy si succès
*****/

struct DataLayerFloppy *DFlp_Open(const char *DeviceName, ULONG Flags, ULONG Unit, ULONG Side, LONG SectorPerTrack, LONG SectorSize, void (*IntFuncPtr)(struct DataLayerFloppy *, void *), void *IntData, ULONG *ErrorCode)
{
    struct DataLayerFloppy *DLayer=(struct DataLayerFloppy *)Sys_AllocMem(sizeof(struct DataLayerFloppy));

    *ErrorCode=DL_NOT_ENOUGH_MEMORY;
    if(DLayer!=NULL)
    {
        DLayer->Unit=Unit;
        DLayer->Side=Side;
        DLayer->TrackSize=SectorPerTrack*SectorSize;
        DLayer->SectorSize=SectorSize;
        DLayer->IsChanged=FALSE;
#ifdef SYSTEM_AMIGA
        if((DLayer->DiskPort=CreatePort(NULL,NULL))!=NULL)
        {
            if((DLayer->DiskExtIO=(struct IOExtTD *)CreateExtIO(DLayer->DiskPort,sizeof(struct IOExtTD)))!=NULL)
            {
                if((DLayer->IntExtIO=(struct IOExtTD *)CreateExtIO(DLayer->DiskPort,sizeof(struct IOExtTD)))!=NULL)
                {
                    *ErrorCode=DL_OPEN_DEVICE;
                    if(!OpenDevice((STRPTR)DeviceName,Unit,(struct IORequest *)DLayer->DiskExtIO,Flags))
                    {
                        DLayer->IntFuncPtr=IntFuncPtr;
                        DLayer->IntData=IntData;
                        P_DFlp_AddInterrupt(DLayer);
                        DLayer->Device=TRUE;
                        *ErrorCode=DL_SUCCESS;
                    }
                }
            }
        }
#endif
        if(*ErrorCode!=DL_SUCCESS)
        {
            DFlp_Close(DLayer);
            DLayer=NULL;
        }
    }

    return DLayer;
}


/*****
    Liberation des ressources allouees par DFlp_Close()
*****/

void DFlp_Close(struct DataLayerFloppy *DLayer)
{
    if(DLayer!=NULL)
    {
#ifdef SYSTEM_AMIGA
        if(DLayer->Device)
        {
            P_DFlp_SetMotorState(DLayer,MOTOR_OFF);
            P_DFlp_RemInterrupt(DLayer);
            CloseDevice((struct IORequest *)DLayer->DiskExtIO);
        }
        if(DLayer->IntExtIO!=NULL) DeleteExtIO((struct IORequest *)DLayer->IntExtIO);
        if(DLayer->DiskExtIO!=NULL) DeleteExtIO((struct IORequest *)DLayer->DiskExtIO);
        if(DLayer->DiskPort!=NULL) DeletePort(DLayer->DiskPort);
#endif
        Sys_FreeMem((void *)DLayer);
    }
}


/*****
    Pour vérifier si un disque est présent
*****/

BOOL DFlp_IsDiskIn(struct DataLayerFloppy *DLayer)
{
#ifdef SYSTEM_AMIGA
    struct IOExtTD *IoReq=DLayer->DiskExtIO;

    IoReq->iotd_Req.io_Flags=0;
    IoReq->iotd_Req.io_Command=TD_CHANGESTATE;
    DoIO((struct IORequest *)IoReq);

    if(!IoReq->iotd_Req.io_Actual) return TRUE;
#endif
    return FALSE;
}


/*****
    Pour vérifier si le disque est protégé
*****/

BOOL DFlp_IsProtected(struct DataLayerFloppy *DLayer)
{
#ifdef SYSTEM_AMIGA
    struct IOExtTD *IoReq=DLayer->DiskExtIO;

    IoReq->iotd_Req.io_Flags=0;
    IoReq->iotd_Req.io_Command=TD_PROTSTATUS;
    DoIO((struct IORequest *)IoReq);

    if(IoReq->iotd_Req.io_Actual) return TRUE;
#endif
    return FALSE;
}


/*****
    Nettoyage des caches (suite à une insertion de disquette par exemple)
*****/

void DFlp_Clean(struct DataLayerFloppy *DLayer)
{
#ifdef SYSTEM_AMIGA
    struct IOExtTD *IoReq=DLayer->DiskExtIO;

    IoReq->iotd_Req.io_Command=ETD_CLEAR;
    IoReq->iotd_Req.io_Flags=0;
    DoIO((struct IORequest *)IoReq); /* ETD_CLEAR */
#endif
}


/*****
    Permet de terminer les operations en cache, avant de fermer
    le DataLayer.
*****/

ULONG DFlp_Finalize(struct DataLayerFloppy *DLayer)
{
    ULONG ErrorCode=DL_SUCCESS;
#ifdef SYSTEM_AMIGA
    struct IOExtTD *IoReq=DLayer->DiskExtIO;

    IoReq->iotd_Req.io_Flags=0;
    IoReq->iotd_Req.io_Command=CMD_UPDATE;
    DoIO((struct IORequest *)IoReq); /* on nettoie le cache */

    ErrorCode=P_DFlp_GetError(DLayer);

    P_DFlp_SetMotorState(DLayer,MOTOR_OFF);
#endif
    return ErrorCode;
}


/*****
    Formatage d'une piste
    * Paramètres:
      DLayer: structure allouée par DFlp_Open()
      Track: piste à formater
      Interleave: numéro d'entrelacement des secteurs de la piste (passer 7 comme valeur par défaut)
      BufferPtr: pointeur vers les données qui vont servir initialiser la piste.
        Ce buffer doit avoir pour taille le nombre d'octets par secteur (soit 256 en principe),
        multiplié par le nombre de secteurs par piste (soit 16 en principe).
    * Retourne:
      Code d'erreur ou DL_SUCCESS
*****/

ULONG DFlp_FormatTrack(struct DataLayerFloppy *DLayer, ULONG Track, ULONG Interleave, const UBYTE *BufferPtr)
{
    ULONG ErrorCode=DL_SUCCESS;
#ifdef SYSTEM_AMIGA
    struct IOExtTD *IoReq=DLayer->DiskExtIO;

    IoReq->iotd_Req.io_Length=Interleave;
    IoReq->iotd_Req.io_Command=TO_MAKEFMTINTERLEAVE;
    DoIO((struct IORequest *)IoReq);

    IoReq->iotd_Req.io_Offset=Track*DLayer->TrackSize;
    IoReq->iotd_Req.io_Flags=0;
    IoReq->iotd_Req.io_Length=DLayer->TrackSize;
    IoReq->iotd_Req.io_Data=(UBYTE *)BufferPtr;
    IoReq->iotd_Req.io_Command=TD_FORMAT;
    DoIO((struct IORequest *)IoReq);

    ErrorCode=P_DFlp_GetError(DLayer);
#endif
    return ErrorCode;
}


/*****
    Lecture d'un secteur
    * Paramètres:
      DLayer: structure allouée par DFlp_Open()
      Track: numéro de piste à lire
      Sector: numéro du secteur de la piste à lire
      BufferPtr: récipiant pour recevoir le secteur lu
    * Retourne:
      Code d'erreur ou DL_SUCCESS
*****/

ULONG DFlp_ReadSector(struct DataLayerFloppy *DLayer, ULONG Track, ULONG Sector, UBYTE *BufferPtr)
{
    ULONG ErrorCode=DL_SUCCESS;
#ifdef SYSTEM_AMIGA
    struct IOExtTD *IoReq=DLayer->DiskExtIO;

    P_DFlp_CheckDiskChanged(DLayer);
    IoReq->iotd_Req.io_Offset=Track*DLayer->TrackSize+DLayer->SectorSize*(Sector-1);
    IoReq->iotd_Req.io_Flags=0;
    IoReq->iotd_Req.io_Length=DLayer->SectorSize;
    IoReq->iotd_Req.io_Data=BufferPtr;
    IoReq->iotd_Req.io_Command=CMD_READ;
    DoIO((struct IORequest *)IoReq);

    ErrorCode=P_DFlp_GetError(DLayer);
#endif
    return ErrorCode;
}


/*****
    Ecriture d'un secteur
    * Paramètres:
      DLayer: structure allouée par DFlp_Open()
      Track: numéro de piste à écrire
      Sector: numéro du secteur de la piste à écrire
      BufferPtr: pointeur vers les données du secteur à écrire
    * Retourne:
      Code d'erreur ou DL_SUCCESS
*****/

ULONG DFlp_WriteSector(struct DataLayerFloppy *DLayer, ULONG Track, ULONG Sector, const UBYTE *BufferPtr)
{
    ULONG ErrorCode=DL_SUCCESS;
#ifdef SYSTEM_AMIGA
    struct IOExtTD *IoReq=DLayer->DiskExtIO;

    IoReq->iotd_Req.io_Offset=Track*DLayer->TrackSize+DLayer->SectorSize*(Sector-1);
    IoReq->iotd_Req.io_Flags=0;
    IoReq->iotd_Req.io_Length=DLayer->SectorSize;
    IoReq->iotd_Req.io_Data=(UBYTE *)BufferPtr;
    IoReq->iotd_Req.io_Command=CMD_WRITE;
    DoIO((struct IORequest *)IoReq);

    ErrorCode=P_DFlp_GetError(DLayer);
#endif
    return ErrorCode;
}


#ifdef SYSTEM_AMIGA

/*****
    Conversion des erreurs todisk.device en erreur ToDisk
*****/

ULONG P_DFlp_GetError(struct DataLayerFloppy *DLayer)
{
    ULONG Result=DL_SUCCESS;

    switch(DLayer->DiskExtIO->iotd_Req.io_Error)
    {
        case 0:
            break;

        case TDERR_NotSpecified:    /* general catchall */
        case TDERR_SeekError:       /* couldn't find track 0 */
        case TDERR_PostReset:       /* user hit reset; awaiting doom */
        default:
            Result=DL_DEVICE_IO;
            break;

        case TDERR_NoSecHdr:        /* couldn't even find a sector */
        case TDERR_BadSecPreamble:  /* sector looked wrong */
        case TDERR_BadSecID:        /* ditto */
        case TDERR_TooFewSecs:      /* couldn't find enough sectors */
        case TDERR_BadSecHdr:       /* another "sector looked wrong" */
            Result=DL_SECTOR_GEO;
            break;

        case TDERR_BadHdrSum:       /* header had incorrect checksum */
        case TDERR_BadSecSum:       /* data had incorrect checksum */
            Result=DL_SECTOR_CRC;
            break;

        case TDERR_WriteProt:       /* can't write to a protected disk */
            Result=DL_PROTECTED;
            break;

        case TDERR_DiskChanged:     /* no disk in the drive */
            Result=DL_NO_DISK;
            break;

        case TDERR_NoMem:           /* ran out of memory */
            Result=DL_NOT_ENOUGH_MEMORY;
            break;

        case TDERR_BadUnitNum:      /* asked for a unit > NUMUNITS */
        case TDERR_BadDriveType:    /* not a drive that trackdisk groks */
        case TDERR_DriveInUse:      /* someone else allocated the drive */
            Result=DL_UNIT_ACCESS;
            break;
    }

    return Result;
}


/*****
    Gestion du moteur
    * Paramètres:
      DLayer: structure allouée par DFlp_Open()
    * Retourne:
      Code d'erreur Amiga
*****/

BYTE P_DFlp_SetMotorState(struct DataLayerFloppy *DLayer, ULONG State)
{
    struct IOExtTD *IoReq=DLayer->DiskExtIO;

    IoReq->iotd_Req.io_Flags=0;
    IoReq->iotd_Req.io_Length=State;
    IoReq->iotd_Req.io_Command=TD_MOTOR;
    DoIO((struct IORequest *)IoReq);

    return IoReq->iotd_Req.io_Error;
}


/*****
    Test si l'erreur est fatale
*****/

BOOL P_DFlp_IsFatalError(ULONG ErrorCode)
{
    switch(ErrorCode)
    {
        case DL_NOT_ENOUGH_MEMORY:
        case DL_OPEN_FILE:
        case DL_OPEN_DEVICE:
        case DL_PROTECTED:
        case DL_NO_DISK:
        case DL_UNIT_ACCESS:
        case DL_UNKNOWN_TYPE:
            return TRUE;
    }

    return FALSE;
}


/*****
    Verification si un disque a ete change
*****/

BOOL P_DFlp_CheckDiskChanged(struct DataLayerFloppy *DLayer)
{
    if(DLayer->IsChanged)
    {
        struct IOExtTD *IoReq=DLayer->DiskExtIO;

        DLayer->IsChanged=FALSE;
        IoReq->iotd_Req.io_Flags=0;
        IoReq->iotd_Req.io_Command=CMD_CLEAR;
        DoIO((struct IORequest *)IoReq);

        return TRUE;
    }

    return FALSE;
}


/*****
    Ajout d'une interruption
*****/

BYTE P_DFlp_AddInterrupt(struct DataLayerFloppy *DLayer)
{
    struct IOExtTD *IoReq=DLayer->IntExtIO;

    /* Init struct Interrupt */
    DLayer->IntDisk.is_Data=DLayer;
    DLayer->IntDisk.is_Code=(void (*)())P_DFlp_InterruptFunc;

    /* Init Interrupt request */
    IoReq->iotd_Req.io_Device=DLayer->DiskExtIO->iotd_Req.io_Device;
    IoReq->iotd_Req.io_Unit=DLayer->DiskExtIO->iotd_Req.io_Unit;
    IoReq->iotd_Req.io_Flags=0;
    IoReq->iotd_Req.io_Length=sizeof(struct Interrupt);
    IoReq->iotd_Req.io_Data=(APTR)&DLayer->IntDisk;
    IoReq->iotd_Req.io_Command=TD_ADDCHANGEINT;
    SendIO((struct IORequest *)IoReq);

    return IoReq->iotd_Req.io_Error;
}


/*****
    Suppression d'une interruption
*****/

BYTE P_DFlp_RemInterrupt(struct DataLayerFloppy *DLayer)
{
    struct IOExtTD *IoReq=DLayer->IntExtIO;

    IoReq->iotd_Req.io_Command=TD_REMCHANGEINT;
    DoIO((struct IORequest *)IoReq);

    return IoReq->iotd_Req.io_Error;
}


/*****
    Sous routine d'interruption pour le disque
*****/

void __asm P_DFlp_InterruptFunc(register __a1 struct DataLayerFloppy *DLayer)
{
    DLayer->IsChanged=TRUE;
    if(DLayer->IntFuncPtr!=NULL) DLayer->IntFuncPtr(DLayer,DLayer->IntData);
}

#endif
