#ifndef DISKLAYER_H
#define DISKLAYER_H

#include "sectorcache.h"

#define DISKLAYER_TYPE_NONE         0
#define DISKLAYER_TYPE_FLOPPY       1
#define DISKLAYER_TYPE_TDS          2
#define DISKLAYER_TYPE_FD           3
#define DISKLAYER_TYPE_SAP          4

#define DL_SUCCESS                  0
#define DL_NOT_ENOUGH_MEMORY        1
#define DL_OPEN_FILE                2
#define DL_READ_FILE                3
#define DL_WRITE_FILE               4
#define DL_OPEN_DEVICE              5
#define DL_DEVICE_IO                6
#define DL_SECTOR_GEO               7
#define DL_SECTOR_CRC               8
#define DL_PROTECTED                9
#define DL_NO_DISK                  10
#define DL_UNIT_ACCESS              11
#define DL_UNKNOWN_TYPE             12


struct DiskLayer
{
    struct SectorCache SectorCache;
    ULONG Unit;
    ULONG Side;
    LONG CountOfBufferMax;
    void *DataLayerPtr;
    ULONG Error;
};


/***** VARIABLES ET FONCTIONS    *****/
/***** PUBLIQUES UTILISABLES PAR *****/
/***** D'AUTRES BLOCS DU PROJET  *****/

extern struct DiskLayer *DL_Open(const char *, ULONG, ULONG, ULONG, ULONG, ULONG, LONG, void (*)(struct DiskLayer *, void *), void *, ULONG *);
extern void DL_Close(struct DiskLayer *);
extern LONG DL_SetBufferMax(struct DiskLayer *, LONG, LONG);
extern BOOL DL_IsDiskIn(struct DiskLayer *);
extern BOOL DL_IsProtected(struct DiskLayer *);
extern void DL_Clean(struct DiskLayer *);
extern BOOL DL_IsChanged(struct DiskLayer *);
extern void DL_SetChanged(struct DiskLayer *, BOOL);
extern BOOL DL_Finalize(struct DiskLayer *, BOOL);
extern BOOL DL_FormatTrack(struct DiskLayer *, ULONG, ULONG, const UBYTE *);
extern BOOL DL_ReadSector(struct DiskLayer *, ULONG, ULONG, UBYTE *);
extern BOOL DL_WriteSector(struct DiskLayer *, ULONG, ULONG, const UBYTE *);
extern BOOL DL_GetSector(struct DiskLayer *, ULONG, ULONG, BOOL, struct SectorCacheNode **);
extern BOOL DL_WriteBufferCache(struct DiskLayer *);
extern ULONG DL_GetError(struct DiskLayer *);
extern const char *DL_GetDLTextErr(ULONG);
extern BOOL DL_IsDLFatalError(ULONG);


#endif  /* DISKLAYER_H */