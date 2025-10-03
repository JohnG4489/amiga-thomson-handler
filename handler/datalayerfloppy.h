#ifndef DATALAYERFLOPPY_H
#define DATALAYERFLOPPY_H

#define MOTOR_ON    1
#define MOTOR_OFF   0

struct DataLayerFloppy
{
    ULONG Unit;
    ULONG Side;
    UBYTE Device;
#ifdef SYSTEM_AMIGA
    struct IOExtTD *IntExtIO;
    struct IOExtTD *DiskExtIO;
    struct MsgPort *DiskPort;
    struct Interrupt IntDisk;
#endif
    void (*IntFuncPtr)(struct DataLayerFloppy *, void *);
    void *IntData;
    LONG TrackSize;
    LONG SectorSize;
    BOOL IsChanged;
};


/***** VARIABLES ET FONCTIONS    *****/
/***** PUBLIQUES UTILISABLES PAR *****/
/***** D'AUTRES BLOCS DU PROJET  *****/

extern struct DataLayerFloppy *DFlp_Open(const char *, ULONG, ULONG, ULONG, LONG, LONG, void (*)(struct DataLayerFloppy *, void *), void *, ULONG *);
extern void DFlp_Close(struct DataLayerFloppy *);
extern BOOL DFlp_IsDiskIn(struct DataLayerFloppy *);
extern BOOL DFlp_IsProtected(struct DataLayerFloppy *);
extern void DFlp_Clean(struct DataLayerFloppy *);
extern ULONG DFlp_Finalize(struct DataLayerFloppy *);
extern ULONG DFlp_FormatTrack(struct DataLayerFloppy *, ULONG, ULONG, const UBYTE *);
extern ULONG DFlp_ReadSector(struct DataLayerFloppy *, ULONG, ULONG, UBYTE *);
extern ULONG DFlp_WriteSector(struct DataLayerFloppy *, ULONG, ULONG, const UBYTE *);

#endif  /* DATALAYERFLOPPY_H */