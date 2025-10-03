#ifndef HANDLER_H
#define HANDLER_H


#include <dos/filehandler.h>
#include <dos/exall.h>
#include <devices/trackdisk.h>

#define ACTION_TOFS_BASE            0x10000
#define ACTION_TOFS_LOCKSECTOR      (ACTION_TOFS_BASE+1)
#define ACTION_TOFS_UNLOCKSECTOR    (ACTION_TOFS_BASE+2)


#define DS_NONE             0
#define DS_WRITE_PROTECTED  1
#define DS_READY            2

#define TMPSIZEOF           32


struct HandlerData
{
    struct DeviceNode *DevNode;
    char DeviceName[256];
    ULONG DeviceUnit;
    ULONG DeviceFlags;
    ULONG Side;
    struct DeviceList *DeviceList;
    struct Process *Process;

    struct timerequest *TimerIO;
    struct MsgPort *TimerPort;

    struct DiskLayer *DiskLayerPtr;

    struct FileSystem *FS;
    struct FileLockTO *FirstLock;
    LONG FileSystemStatus;
    BOOL IsSensitive;

    LONG InhibitCounter;
    ULONG DeviceState;
    struct DateStamp LatestMod;
    char TmpName[256];
    char TmpName2[256];
};


struct FileLockTO
{
    struct FileLock fl;
    struct FileLockTO *PrevLock;
    struct FSHandle *Handle;
    LONG Pos;
};


/***** VARIABLES ET FONCTIONS    *****/
/***** PUBLIQUES UTILISABLES PAR *****/
/***** D'AUTRES BLOCS DU PROJET  *****/

extern BOOL Hdl_Inhibit(struct HandlerData *, BOOL, LONG *);

extern struct FileLockTO *Hdl_LockObjectFromName(struct HandlerData *, struct FileLockTO *, const char *, LONG,  LONG *);
extern struct FileLockTO *Hdl_LockObjectFromLock(struct HandlerData *, struct FileLockTO *, LONG *);
extern BOOL Hdl_UnLockObject(struct HandlerData *, struct FileLockTO *, LONG *);

extern BOOL Hdl_Flush(struct HandlerData *, LONG *);

extern struct FileLockTO *Hdl_OpenFile(struct HandlerData *, struct FileLockTO *, const char *, LONG, LONG *);
extern BOOL Hdl_CloseFile(struct HandlerData *, struct FileLockTO *, LONG *);
extern LONG Hdl_Read(struct HandlerData *, struct FileLockTO *, UBYTE *, LONG, LONG *);
extern LONG Hdl_Write(struct HandlerData *, struct FileLockTO *, UBYTE *, LONG, LONG *);
extern LONG Hdl_Seek(struct HandlerData *, struct FileLockTO *, LONG, LONG, LONG *);

extern BOOL Hdl_ExamineObject(struct HandlerData *, struct FileLockTO *, struct FileInfoBlock *, LONG *);
extern BOOL Hdl_ExamineNext(struct HandlerData *, struct FileLockTO *, struct FileInfoBlock *, LONG *);
extern BOOL Hdl_ExamineAll(struct HandlerData *, struct FileLockTO *, UBYTE *, LONG, LONG, struct ExAllControl *, LONG *);

extern BOOL Hdl_Rename(struct HandlerData *, struct FileLockTO *, const char *, struct FileLockTO *, const char *, LONG *);
extern BOOL Hdl_Delete(struct HandlerData *, struct FileLockTO *, const char *, LONG *);
extern BOOL Hdl_SetComment(struct HandlerData *, struct FileLockTO *, const char *, const char *, LONG *);
extern BOOL Hdl_SetDate(struct HandlerData *, struct FileLockTO *, const char *, struct DateStamp *, LONG *);

extern BOOL Hdl_SetFileSize(struct HandlerData *, struct FileLockTO *, LONG, LONG, LONG *);
extern BOOL Hdl_DiskInfo(struct HandlerData *, struct InfoData *);
extern BOOL Hdl_Relabel(struct HandlerData *, const char *, LONG *);
extern BOOL Hdl_Format(struct HandlerData *, const char *, LONG *);

extern void Hdl_CheckChange(struct HandlerData *);
extern void Hdl_Change(struct DiskLayer *, void *);
extern void Hdl_SendTimeout(struct HandlerData *);

extern void Hdl_UnsetVolumeEntry(struct HandlerData *);

extern LONG Hdl_ConvertFSCode(struct HandlerData *, LONG);
extern LONG Hdl_BSTRToString(BSTR, char *, LONG);
extern LONG Hdl_StringToBSTR(const char *, char *, LONG);

#endif  /* HANDLER_H */