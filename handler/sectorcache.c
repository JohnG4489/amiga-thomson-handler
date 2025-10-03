#include "system.h"
#include "sectorcache.h"

/*
    23-09-2020 (Seg)    Amélioration de la gestion du cache
    16-09-2020 (Seg)    Renommage de l'api
    16-08-2020 (Seg)    Gestion d'un cache de secteurs
*/


/***** Prototypes */
struct SectorCache *Sch_Alloc(ULONG);
void Sch_Free(struct SectorCache *);
void Sch_Init(struct SectorCache *, ULONG);
void Sch_Flush(struct SectorCache *);
ULONG Sch_GetCount(struct SectorCache *);
struct SectorCacheNode *Sch_Find(struct SectorCache *, LONG, LONG);
struct SectorCacheNode *Sch_ObtainOlder(struct SectorCache *, LONG, LONG);
struct SectorCacheNode *Sch_Obtain(struct SectorCache *, LONG, LONG, BOOL);
void Sch_Release(struct SectorCacheNode *, BOOL);
void Sch_FreeNode(struct SectorCache *, struct SectorCacheNode *);
struct SectorCacheNode *Sch_GetMinSectorCacheNode(struct SectorCache *, BOOL);

struct SectorCacheNode *P_Sch_New(struct SectorCache *, LONG, LONG);


/*****
    Allocation d'un nouveau cache pour la gestion des secteurs
*****/

struct SectorCache *Sch_Alloc(ULONG SectorSize)
{
    struct SectorCache *Ptr=(struct SectorCache *)Sys_AllocMem(sizeof(struct SectorCache));

    if(Ptr!=NULL) Sch_Init(Ptr,SectorSize);

    return Ptr;
}


/*****
    Libération du cache alloué par Sch_Alloc(), et libération des ressources associées
*****/

void Sch_Free(struct SectorCache *SectorCachePtr)
{
    if(SectorCachePtr!=NULL)
    {
        Sch_Flush(SectorCachePtr);
        Sys_FreeMem((void *)SectorCachePtr);
    }
}


/*****
    Initialisation de la structure
*****/

void Sch_Init(struct SectorCache *SectorCachePtr, ULONG SectorSize)
{
    if(SectorCachePtr!=NULL)
    {
        SectorCachePtr->UID=0;
        SectorCachePtr->SectorSize=SectorSize;
        SectorCachePtr->FirstNodePtr=NULL;
    }
}


/*****
    Nettoyage des ressources allouées
*****/

void Sch_Flush(struct SectorCache *SectorCachePtr)
{
    if(SectorCachePtr!=NULL)
    {
        while(SectorCachePtr->FirstNodePtr!=NULL) Sch_FreeNode(SectorCachePtr,SectorCachePtr->FirstNodePtr);
    }
}


/*****
    Retourne le nombre de secteurs actuellement en cache
*****/

ULONG Sch_GetCount(struct SectorCache *SectorCachePtr)
{
    ULONG Result=0;
    struct SectorCacheNode *NodePtr=SectorCachePtr->FirstNodePtr;

    while(NodePtr!=NULL)
    {
        Result++;
        NodePtr=NodePtr->NextPtr;
    }

    return Result;
}


/*****
    Recherche un secteur dans le cache.
    Retourne:
    - NULL si le secteur n'est pas en cache
    - sinon le pointeur sur le SectorCacheNode qui correspond
*****/

struct SectorCacheNode *Sch_Find(struct SectorCache *SectorCachePtr, LONG Track, LONG Sector)
{
    struct SectorCacheNode *Ptr=SectorCachePtr->FirstNodePtr;

    while(Ptr!=NULL && (Ptr->Track!=Track || Ptr->Sector!=Sector)) Ptr=Ptr->NextPtr;

    return Ptr;
}


/*****
    Cherche un vieux cache pour le libérer et l'utiliser comme nouveau cache pour un nouveau couple Track/Sector
*****/

struct SectorCacheNode *Sch_ObtainOlder(struct SectorCache *SectorCachePtr, LONG Track, LONG Sector)
{
    struct SectorCacheNode *ResultPtr=NULL;
    struct SectorCacheNode *NodePtr=SectorCachePtr->FirstNodePtr;
    ULONG UIDMin=~0;

    while(NodePtr!=NULL)
    {
        if(NodePtr->Status!=SCN_UPDATED && NodePtr->UID<UIDMin)
        {
            UIDMin=NodePtr->UID;
            ResultPtr=NodePtr;
        }

        NodePtr=NodePtr->NextPtr;
    }

    if(ResultPtr!=NULL)
    {
        LONG i;
        for(i=0; i<SectorCachePtr->SectorSize; i++) ResultPtr->BufferPtr[i]=0;
        ResultPtr->Track=Track;
        ResultPtr->Sector=Sector;
        ResultPtr->Status=SCN_NEW;
        ResultPtr->UID=SectorCachePtr->UID++;
    }

    return ResultPtr;
}


/*****
    Récupère le cache associé à la Track et au Sector.
    Si le cache n'existe pas, la fonction alloue un nouveau cache.
    Retourne:
    - NULL si erreur mémoire
    - sinon NULL sinon. Attention à vérifier le champ Status pour savoir s'il s'agit d'un nouveau cache alloué, ou d'un
      ancien déjà initialisé.
*****/

struct SectorCacheNode *Sch_Obtain(struct SectorCache *SectorCachePtr, LONG Track, LONG Sector, BOOL IsCreateIfNotExists)
{
    struct SectorCacheNode *Ptr=Sch_Find(SectorCachePtr,Track,Sector);

    if(Ptr==NULL && IsCreateIfNotExists)
    {
        Ptr=P_Sch_New(SectorCachePtr,Track,Sector);
    }

    return Ptr;
}


/*****
    Libération d'un cache précédemment obtenu par Sch_Obtain().
    Cette méthode ne désalloue pas les ressources, mais elle libère un vérou dessus.
    Paramètres:
    - NodePtr: le pointeur sur le cache à libérer
    - IsUpdated: pour indiquer s'il y a eu une modification sur le cache
*****/

void Sch_Release(struct SectorCacheNode *NodePtr, BOOL IsUpdated)
{
    if(NodePtr!=NULL)
    {
        if(IsUpdated) NodePtr->Status=SCN_UPDATED;
    }
}



/*****
    Fonction pour libérer les resources d'un cache
*****/

void Sch_FreeNode(struct SectorCache *SectorCachePtr, struct SectorCacheNode *NodePtr)
{
    if(NodePtr!=NULL)
    {
        /* On refait le chainage */
        if(NodePtr->PrevPtr!=NULL) NodePtr->PrevPtr->NextPtr=NodePtr->NextPtr; else SectorCachePtr->FirstNodePtr=NodePtr->NextPtr;
        if(NodePtr->NextPtr!=NULL) NodePtr->NextPtr->PrevPtr=NodePtr->PrevPtr;

        /* On libère les ressources */
        Sys_FreeMem((void *)NodePtr);
    }
}


/*****
    Permet d'obtenir le secteur dans le cache qui est le plus proche de la piste 0, secteur 0.
    Paramètres:
    - SectorCachePtr: pointeur sur le cache
    - IsUpdatedOnly: pour ne rechercher que les secteurs flagués "SCN_UPDATED"
    Retourne: un pointeur sur un cache, ou NULL
*****/

struct SectorCacheNode *Sch_GetMinSectorCacheNode(struct SectorCache *SectorCachePtr, BOOL IsUpdatedOnly)
{
    struct SectorCacheNode *NodePtr=NULL;
    struct SectorCacheNode *CurNodePtr=SectorCachePtr->FirstNodePtr;
    ULONG Track=~0,Sector=~0;

    while(CurNodePtr!=NULL)
    {
        if(!IsUpdatedOnly || (IsUpdatedOnly && CurNodePtr->Status==SCN_UPDATED))
        {
            if((ULONG)CurNodePtr->Track<Track || ((ULONG)CurNodePtr->Track==Track && (ULONG)CurNodePtr->Sector<Sector))
            {
                NodePtr=CurNodePtr;
                Track=NodePtr->Track;
                Sector=NodePtr->Sector;
            }
        }

        CurNodePtr=CurNodePtr->NextPtr;
    }

    return NodePtr;
}


/*****
    Fonction privée pour allouer un nouveau cache
*****/

struct SectorCacheNode *P_Sch_New(struct SectorCache *SectorCachePtr, LONG Track, LONG Sector)
{
    /* Allocation de la structure et du buffer en même temps */
    struct SectorCacheNode *Ptr=(struct SectorCacheNode *)Sys_AllocMem(sizeof(struct SectorCacheNode)+sizeof(UBYTE)*SectorCachePtr->SectorSize);

    if(Ptr!=NULL)
    {
        Ptr->BufferPtr=&((UBYTE *)Ptr)[sizeof(struct SectorCacheNode)];
        Ptr->Track=Track;
        Ptr->Sector=Sector;
        Ptr->Status=SCN_NEW;
        Ptr->UID=SectorCachePtr->UID++;
        if(SectorCachePtr->FirstNodePtr!=NULL)
        {
            SectorCachePtr->FirstNodePtr->PrevPtr=Ptr;
            Ptr->NextPtr=SectorCachePtr->FirstNodePtr;
        }
        SectorCachePtr->FirstNodePtr=Ptr;
    }

    return Ptr;
}
