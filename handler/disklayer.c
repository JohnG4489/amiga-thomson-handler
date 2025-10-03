#include "system.h"
#include "disklayer.h"
#include "datalayerfloppy.h"


/*
    24-09-2020 (Seg)    Fix
    23-09-2020 (Seg)    Amélioration de la gestion du cache
    10-09-2020 (Seg)    Refonte de la couche disque de la commande todisk pour le handler
    27-08-2018 (Seg)    Gestion des paramètres géométriques du file system
    23-09-2013 (Seg)    Adaptation du code suite à la migration sous Unix
    16-08-2012 (Seg)    Indique maintenant le nombre d'unités et de face par unité géré par le disque
    22-05-2010 (Seg)    Retouche de la fontion DL_ParseOption() et ajout de DL_FreeOption()
    24-03-2010 (Seg)    Gestion de la detection automatique de la source
    30-12-2008 (Seg)    Gestion du floppy
    30-08-2006 (Seg)    Gestion transparente des differents formats
*/


/***** Prototypes */
struct DiskLayer *DL_Open(const char *, ULONG, ULONG, ULONG, ULONG, ULONG, LONG, void (*)(struct DiskLayer *, void *), void *, ULONG *);
void DL_Close(struct DiskLayer *);
LONG DL_SetBufferMax(struct DiskLayer *, LONG, LONG);
BOOL DL_IsDiskIn(struct DiskLayer *);
BOOL DL_IsProtected(struct DiskLayer *);
void DL_Clean(struct DiskLayer *);
BOOL DL_IsChanged(struct DiskLayer *);
void DL_SetChanged(struct DiskLayer *, BOOL);
BOOL DL_Finalize(struct DiskLayer *, BOOL);
BOOL DL_FormatTrack(struct DiskLayer *, ULONG, ULONG, const UBYTE *);
BOOL DL_ReadSector(struct DiskLayer *, ULONG, ULONG, UBYTE *);
BOOL DL_WriteSector(struct DiskLayer *, ULONG, ULONG, const UBYTE *);
BOOL DL_GetSector(struct DiskLayer *, ULONG, ULONG, BOOL, struct SectorCacheNode **);
BOOL DL_WriteBufferCache(struct DiskLayer *);
BOOL DL_Obtain(struct DiskLayer *, LONG, LONG, struct SectorCacheNode **);
ULONG DL_GetError(struct DiskLayer *);
const char *DL_GetDLTextErr(ULONG);
BOOL DL_IsDLFatalError(ULONG);


/*****
    Ouverture de la couche disque correspondant à la source ou la
    destination visée.
    * Paramètres:
      Name: nom du device à utiliser
      Flags: flags à passer au device lors de son ouverture
      Unit: unité du device à ouvrir
      Side: facultatif dans le cas de l'utilisation du device
      CountOfSectorPerTrack: pour indiquer le nombre de secteurs par piste
      SectorSize: pour indiquer la taille d'un secteyur
      CountOfBufferMax: nombre de buffers maximum à utiliser pour la gestion du cache interne à cette lib
      IntFuncPtr: pointeur vers une fonction callback pour savoir si un disque vient d'être inséré ou retiré
      IntData: pointeur vers une structure utilisateur à passer lors de l'utilisation du callback
      ErrorCode: pointeur vers un ULONG pour retourner un code d'erreur ou DL_SUCCESS
    * Retourne:
      - NULL si échec (vérifier ErrorCode)
      - pointeur vers une structure DataLayerFloppy si succès
*****/

struct DiskLayer *DL_Open(const char *Name, ULONG Flags, ULONG Unit, ULONG Side, ULONG CountOfSectorPerTrack, ULONG SectorSize, LONG CountOfBufferMax, void (*IntFuncPtr)(struct DiskLayer *, void *), void *IntData, ULONG *ErrorCode)
{
    struct DiskLayer *DLayer=(struct DiskLayer *)Sys_AllocMem(sizeof(struct DiskLayer));

    *ErrorCode=DL_NOT_ENOUGH_MEMORY;
    if(DLayer!=NULL)
    {
        Sch_Init(&DLayer->SectorCache,SectorSize);
        DL_SetBufferMax(DLayer,CountOfBufferMax,0);

        DLayer->Unit=Unit;
        DLayer->Side=Side;
        DLayer->DataLayerPtr=(void *)DFlp_Open(Name,Flags,Unit,Side,CountOfSectorPerTrack,SectorSize,(void (*)(struct DataLayerFloppy *, void *))IntFuncPtr,IntData,ErrorCode);

        if(DLayer->DataLayerPtr==NULL)
        {
            DL_Close(DLayer);
            DLayer=NULL;
        }
    }

    return DLayer;
}


/*****
    Fermeture de la couche disque ouverte par OpenDiskLayer()
*****/

void DL_Close(struct DiskLayer *DLayer)
{
    if(DLayer!=NULL)
    {
        DFlp_Close((struct DataLayerFloppy *)DLayer->DataLayerPtr);
        Sch_Flush(&DLayer->SectorCache);
        Sys_FreeMem((void *)DLayer);
    }
}


/*****
    Pour définir la taille du SectorCache, soit en absolu, soit en incrémental
    * Paramètres:
      DLayer: structure allouée par DFlp_Open()
      CountOfBufferMax: nombre de buffers maximum à utiliser pour la gestion du cache interne
        à cette lib, ou -1
      AddBuffer: nombre de buffers à ajouter ou retirer, ou 0 pour ne rien changer
    * Retourne:
      Nombre de buffers actuellement autorisés
*****/

LONG DL_SetBufferMax(struct DiskLayer *DLayer, LONG CountOfBufferMax, LONG AddBuffer)
{
    if(CountOfBufferMax>=0) DLayer->CountOfBufferMax=CountOfBufferMax;
    DLayer->CountOfBufferMax+=AddBuffer;
    if(DLayer->CountOfBufferMax<1) DLayer->CountOfBufferMax=1;
    return DLayer->CountOfBufferMax;
}


/*****
    Pour vérifier si un disque est présent
*****/

BOOL DL_IsDiskIn(struct DiskLayer *DLayer)
{
    return DFlp_IsDiskIn((struct DataLayerFloppy *)DLayer->DataLayerPtr);
}


/*****
    Pour vérifier si le disque est protégé
*****/

BOOL DL_IsProtected(struct DiskLayer *DLayer)
{
    return DFlp_IsProtected((struct DataLayerFloppy *)DLayer->DataLayerPtr);
}


/*****
    Nettoyage des caches (suite à une insertion de disquette par exemple)
*****/

void DL_Clean(struct DiskLayer *DLayer)
{
    DFlp_Clean((struct DataLayerFloppy *)DLayer->DataLayerPtr);
    Sch_Flush(&DLayer->SectorCache);
}


/*****
    Test si le disque a changé
*****/

BOOL DL_IsChanged(struct DiskLayer *DLayer)
{
    return ((struct DataLayerFloppy *)DLayer->DataLayerPtr)->IsChanged;
}


/*****
    Pour changer le flag de changement de disque
*****/

void DL_SetChanged(struct DiskLayer *DLayer, BOOL IsChanged)
{
    ((struct DataLayerFloppy *)DLayer->DataLayerPtr)->IsChanged=IsChanged;
}


/*****
    Termine les opérations en cache
    * Paramètres:
      DLayer: structure allouée par DFlp_Open()
      IsFreeCache: TRUE si on veut libérer le cache interne à cette lib
    * Retourne:
      TRUE si succès
      FALSE si échec (vérifier DLayer->Error pour avoir le détail)
*****/

BOOL DL_Finalize(struct DiskLayer *DLayer, BOOL IsFreeCache)
{
    /* On vide tout le cache et on écrit les secteurs qui sont modifiés */
    if(DL_WriteBufferCache(DLayer))
    {
        if(IsFreeCache) Sch_Flush(&DLayer->SectorCache);

        /* On demande à la couche disque de vider aussi son cache */
        DLayer->Error=DFlp_Finalize((struct DataLayerFloppy *)DLayer->DataLayerPtr);

        if(!DLayer->Error) return TRUE;
    }

    return FALSE;
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
      TRUE si succès
      FALSE si échec (vérifier DLayer->Error pour avoir le détail)
*****/

BOOL DL_FormatTrack(struct DiskLayer *DLayer, ULONG Track, ULONG Interleave, const UBYTE *BufferPtr)
{
    DLayer->Error=DFlp_FormatTrack((struct DataLayerFloppy *)DLayer->DataLayerPtr,Track,Interleave,BufferPtr);
    if(DLayer->Error) return FALSE;
    return TRUE;
}


/*****
    Lecture d'un secteur de donnees
    * Paramètres:
      DLayer: structure allouée par DFlp_Open()
      Track: numéro de piste à lire
      Sector: numéro du secteur de la piste à lire
      IsPreload: TRUE pour demander à charger le secteur si jamais le cache vient d'être créé
      SectorCacheNodePtr: pointeur de pointeur pour obtenir le noeud du cache correspondant
    * Retourne:
      TRUE si succès
      FALSE si échec (vérifier DLayer->Error pour avoir le détail)
*****/

BOOL DL_GetSector(struct DiskLayer *DLayer, ULONG Track, ULONG Sector, BOOL IsPreload, struct SectorCacheNode **SectorCacheNodePtr)
{
    BOOL Result=DL_Obtain(DLayer,Track,Sector,SectorCacheNodePtr);

    if(Result && IsPreload && (*SectorCacheNodePtr)->Status==SCN_NEW)
    {
        /* Si le cache vient d'être alloué, on l'initialise en lisant le secteur demandé */
        Result=DL_ReadSector(DLayer,Track,Sector,(*SectorCacheNodePtr)->BufferPtr);
        if(Result) (*SectorCacheNodePtr)->Status=SCN_INITIALIZED;
    }

    return Result;
}


/*****
    Lecture directe d'un secteur
    * Paramètres:
      DLayer: structure allouée par DFlp_Open()
      Track: numéro de piste à lire
      Sector: numéro du secteur de la piste à lire
      BufferPtr: récipiant pour recevoir le secteur lu
    * Retourne:
      TRUE si succès
      FALSE si échec (vérifier DLayer->Error pour avoir le détail)
*****/

BOOL DL_ReadSector(struct DiskLayer *DLayer, ULONG Track, ULONG Sector, UBYTE *BufferPtr)
{
    DLayer->Error=DFlp_ReadSector((struct DataLayerFloppy *)DLayer->DataLayerPtr,Track,Sector,BufferPtr);
    if(DLayer->Error) return FALSE;
    return TRUE;
}


/*****
    Ecriture directe d'un secteur
    * Paramètres:
      DLayer: structure allouée par DFlp_Open()
      Track: numéro de piste à écrire
      Sector: numéro du secteur de la piste à écrire
      BufferPtr: pointeur vers les données du secteur à écrire
    * Retourne:
      TRUE si succès
      FALSE si échec (vérifier DLayer->Error pour avoir le détail)
*****/

BOOL DL_WriteSector(struct DiskLayer *DLayer, ULONG Track, ULONG Sector, const UBYTE *BufferPtr)
{
    DLayer->Error=DFlp_WriteSector((struct DataLayerFloppy *)DLayer->DataLayerPtr,Track,Sector,BufferPtr);
    if(DLayer->Error) return FALSE;
    return TRUE;
}


/*****
    Ecriture des données contenues dans le cache
*****/

BOOL DL_WriteBufferCache(struct DiskLayer *DLayer)
{
    BOOL Result=TRUE;
    struct SectorCacheNode *NodePtr;

    while(Result && (NodePtr=Sch_GetMinSectorCacheNode(&DLayer->SectorCache,TRUE))!=NULL)
    {
        BOOL Result2=DL_WriteSector(DLayer,NodePtr->Track,NodePtr->Sector,NodePtr->BufferPtr);
        if(Result2) NodePtr->Status=SCN_INITIALIZED; else Result=Result2;
    }

    return Result;
}


/*****
    Permet de trouver un secteur dans le cache, ou, à défaut, de libérer une nouvelle
    place dans le cache.
    Note: Cette fonction tente de récupérer un secteur du cache, ou d'en allouer un nouveau,
    soit en libérant un vieux cache, soit en allouant de la mémoire, seulement s'il est
    possible de le faire.
    Cette fonction ne lit pas de secteur sur le disque. Ceci doit être fait par les fonctions
    DL_GetSector() ou DL_ReadSector().
    Cette fonction est susceptible de lancer des opérations d'écriture pour libérer de
    l'espace dans le cache.
    * Paramètres:
      DLayer: structure allouée par DFlp_Open()
      Track: numéro de piste à lire
      Sector: numéro du secteur de la piste à lire
      SectorCacheNodePtr: pointeur de pointeur pour obtenir le noeud du cache correspondant
    * Retourne:
      TRUE si succès
      FALSE si échec (vérifier DLayer->Error pour avoir le détail)
*****/

BOOL DL_Obtain(struct DiskLayer *DLayer, LONG Track, LONG Sector, struct SectorCacheNode **SectorCacheNodePtr)
{
    BOOL Result=TRUE;
    BOOL IsCreateIfNotExists=Sch_GetCount(&DLayer->SectorCache)<DLayer->CountOfBufferMax;

    DLayer->Error=DL_SUCCESS;

    /* On cherche si le secteur existe dans le cache, sinon on le crée si le cache n'est pas plein */
    *SectorCacheNodePtr=Sch_Obtain(&DLayer->SectorCache,Track,Sector,IsCreateIfNotExists);
    if(*SectorCacheNodePtr==NULL)
    {
        /* Si on était en mode auto-création et que le résultat est nul, alors on retourne une erreur */
        if(IsCreateIfNotExists)
        {
            DLayer->Error=DL_NOT_ENOUGH_MEMORY;
            Result=FALSE;
        }
        else
        {
            /* Tente de libérer un ancien cache de secteur non mis à jour */
            *SectorCacheNodePtr=Sch_ObtainOlder(&DLayer->SectorCache,Track,Sector);
            if(*SectorCacheNodePtr==NULL)
            {
                /* Comme il n'y a plus de place, on libère les secteurs qui sont en mode update */
                Result=DL_WriteBufferCache(DLayer);
                if(Result)
                {
                    /* On retente de libérer un ancien cache de secteur non mis à jour */
                    *SectorCacheNodePtr=Sch_ObtainOlder(&DLayer->SectorCache,Track,Sector);
                }
            }
        }
    }

    return Result;
}


/*****
    Pour libérer un secteur qui serait modifié dans le cache.
    Le cache est alors invalidé, et le secteur est écrit sur le disque, si jamais
    il s'agit d'un secteur modifié seulement.
    * Paramètres:
      DLayer: structure allouée par DFlp_Open()
      Track: numéro de piste à libérer
      Sector: numéro du secteur de la piste à libérer
    * Retourne:
      TRUE si succès
      FALSE si échec (vérifier DLayer->Error pour avoir le détail)
*****/

BOOL DL_Release(struct DiskLayer *DLayer, LONG Track, LONG Sector)
{
    BOOL Result=TRUE;
    struct SectorCacheNode *NodePtr=Sch_Find(&DLayer->SectorCache,Track,Sector);

    if(NodePtr!=NULL)
    {
        if(NodePtr->Status==SCN_UPDATED) Result=DL_WriteSector(DLayer,Track,Sector,NodePtr->BufferPtr);
        NodePtr->Status=SCN_NEW;
    }

    return Result;
}


/*****
    Retourne le code d'erreur converti
*****/

ULONG DL_GetError(struct DiskLayer *DLayer)
{
    return DLayer->Error;
}


/*****
    Retourne le texte de l'erreur
*****/

const char *DL_GetDLTextErr(ULONG ErrorCode)
{
    char *Result="";

    switch(ErrorCode)
    {
        case DL_SUCCESS:
            break;

        case DL_NOT_ENOUGH_MEMORY:
            Result="Not enough memory";
            break;

        case DL_OPEN_FILE:
            Result="Error opening image disk file";
            break;

        case DL_READ_FILE:
            Result="Error reading image disk file";
            break;

        case DL_WRITE_FILE:
            Result="Error writing image disk file";
            break;

        case DL_OPEN_DEVICE:
            Result="Error opening todisk.device";
            break;

        case DL_DEVICE_IO:
            Result="Device I/O error";
            break;

        case DL_SECTOR_GEO:
            Result="Bad track structure";
            break;

        case DL_SECTOR_CRC:
            Result="Bad sector CRC";
            break;

        case DL_PROTECTED:
            Result="Disk write protected";
            break;

        case DL_NO_DISK:
            Result="No disk";
            break;

        case DL_UNIT_ACCESS:
            Result="Device unit not accessible";
            break;

        case DL_UNKNOWN_TYPE:
            Result="Unknown source disk";
            break;
    }

    return Result;
}


/*****
    Test si l'erreur est fatale
*****/

BOOL DL_IsDLFatalError(ULONG ErrorCode)
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
