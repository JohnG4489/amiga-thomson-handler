#ifndef SECTORCACHE_H
#define SECTORCACHE_H

#define SCN_NEW 0
#define SCN_INITIALIZED 1
#define SCN_UPDATED 2

struct SectorCacheNode
{
    UBYTE *BufferPtr;
    LONG Track;
    LONG Sector;
    LONG Status;
    ULONG UID;
    struct SectorCacheNode *PrevPtr;
    struct SectorCacheNode *NextPtr;
};


struct SectorCache
{
    ULONG SectorSize;
    ULONG UID;
    struct SectorCacheNode *FirstNodePtr;
};


/***** VARIABLES ET FONCTIONS    *****/
/***** PUBLIQUES UTILISABLES PAR *****/
/***** D'AUTRES BLOCS DU PROJET  *****/

extern struct SectorCache *Sch_Alloc(ULONG);
extern void Sch_Free(struct SectorCache *);
extern void Sch_Init(struct SectorCache *, ULONG);
extern void Sch_Flush(struct SectorCache *);
extern ULONG Sch_GetCount(struct SectorCache *);
extern struct SectorCacheNode *Sch_Find(struct SectorCache *, LONG, LONG);
extern struct SectorCacheNode *Sch_ObtainOlder(struct SectorCache *, LONG, LONG);
extern struct SectorCacheNode *Sch_Obtain(struct SectorCache *, LONG, LONG, BOOL);
extern void Sch_Release(struct SectorCacheNode *, BOOL);
extern void Sch_FreeNode(struct SectorCache *, struct SectorCacheNode *);
extern struct SectorCacheNode *Sch_GetMinSectorCacheNode(struct SectorCache *, BOOL);


#endif  /* SECTORCACHE_H */