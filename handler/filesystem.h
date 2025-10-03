#ifndef FILESYSTEM_H
#define FILESYSTEM_H

#define SIZEOF_HOSTNAME         32
#define SIZEOF_TONAME           8  /* sizeof(Nom) */
#define SIZEOF_TOSUFFIX         3  /* sizoef(Suffix) */
#define SIZEOF_TOFULLNAME       (SIZEOF_TONAME+SIZEOF_TOSUFFIX)
#define SIZEOF_CONV_HOSTNAME    12 /* sizeof(Nom) + sizeof(Séparateur) + sizeof(Suffixe) */

/* Offsets dans le nom de volume */
#define FV_NAME                 0
#define FV_DAY                  8
#define FV_MONTH                9
#define FV_YEAR                 10
#define FV_HOUR                 11
#define FV_MIN                  12
#define FV_SEC                  13

/* Taille du descripteur de fichier */
#define SIZEOF_FILEINFO         32

/* File status d'un fichier */
#define FST_NONE                0xff
#define FST_ERASED              0x00

/* Offsets dans le descripteur de fichier */
#define FIO_NAME                0
#define FIO_SUFFIX              8
#define FIO_TYPE                11
#define FIO_FIRST_CLUSTER       13
#define FIO_LAST_SEC_LEN        14
#define FIO_COMMENT             16
#define FIO_DAY                 24
#define FIO_MONTH               25
#define FIO_YEAR                26
#define FIO_HOUR                27
#define FIO_MIN                 28
#define FIO_SEC                 29
#define FIO_CHG1                30
#define FIO_CHG2                31

/* Retours d'erreurs du FS */
#define FS_SUCCESS              0
#define FS_FILE_NOT_FOUND       -1
#define FS_FILE_ALREADY_EXISTS  -2
#define FS_DIRECTORY_FULL       -3
#define FS_DISK_FULL            -4
#define FS_NOT_ENOUGH_MEMORY    -5
#define FS_DISKLAYER_ERROR      -6
#define FS_OTHER                -7
#define FS_NO_MORE_ENTRY        -8
#define FS_RENAME_CONFLICT      -9
#define FS_NOT_DOS_DISK         -10

/* Modes d'ouverture des fichiers */
#define FS_MODE_OLDFILE         0
#define FS_MODE_NEWFILE         1
#define FS_MODE_READWRITE       2

/* Type de cluster */
#define CLST_FREE               0xff
#define CLST_RESERVED           0xfe
#define CLST_TERM               0xc0


struct FileSystem
{
    struct DiskLayer *DiskLayerPtr;
    struct FSHandle *FirstHandlePtr;
    BOOL IsExtended;
    BOOL IsFATUpdated;
    ULONG FileInfoFlags;
    LONG BlocksPerTrack;
    LONG MaxTracks;
    LONG SectorSize;
    LONG FSSectorSize;
    LONG SectorsPerTrack;
    LONG SectorsPerBlock;
    LONG FilesPerSector;
    LONG TrackSys;
    LONG SectorFAT;
    LONG MaxFiles;
    LONG MaxBlocks;
    LONG ClusterSys;
    UBYTE *Sys;
    UBYTE *Label;
    UBYTE *FAT;
    UBYTE *Dir;
};


struct FSHandle
{
    struct FileSystem *FS;
    struct FSHandle *PrevHandlePtr;
    struct FSHandle *NextHandlePtr;
    LONG Mode;
    LONG FileInfoIdx;
    LONG Offset;
};


struct FileObject
{
    struct FileSystem *FS;
    char Name[13];
    char Comment[9];
    LONG Day;
    LONG Month;
    LONG Year;
    LONG Hour;
    LONG Min;
    LONG Sec;
    LONG Type;
    LONG Size;
    BOOL IsDateOk;
    BOOL IsTimeOk;
    LONG ExtraData;
    LONG CountOfBlocks;
    LONG FileInfoIdx;
};


/***** VARIABLES ET FONCTIONS    *****/
/***** PUBLIQUES UTILISABLES PAR *****/
/***** D'AUTRES BLOCS DU PROJET  *****/

extern struct FileSystem *FS_AllocFileSystem(LONG, LONG, LONG, BOOL);
extern void FS_FreeFileSystem(struct FileSystem *);
extern LONG FS_InitFileSystem(struct FileSystem *, struct DiskLayer *);
extern LONG FS_FlushFileInfo(struct FileSystem *);

extern LONG FS_Format(struct FileSystem *, const char *);

extern void FS_ExamineFileObject(struct FileSystem *, struct FileObject *);
extern BOOL FS_ExamineNextFileObject(struct FileObject *);

extern struct FSHandle *FS_OpenFileFromIdx(struct FileSystem *, LONG, LONG, BOOL, LONG *);
extern struct FSHandle *FS_OpenFile(struct FileSystem *, LONG, const char *, const LONG *, BOOL, BOOL, LONG *);
extern LONG FS_CloseFile(struct FSHandle *);
extern LONG FS_ReadFile(struct FSHandle *, UBYTE *, LONG);
extern LONG FS_WriteFile(struct FSHandle *, UBYTE *, LONG);
extern LONG FS_Seek(struct FSHandle *, LONG);
extern LONG FS_GetSize(struct FSHandle *);
extern LONG FS_SetSize(struct FSHandle *, LONG);

extern LONG FS_RenameFile(struct FileSystem *, const char *, const LONG *, BOOL, const char *);
extern LONG FS_DeleteFile(struct FileSystem *, const char *, const LONG *, BOOL);
extern LONG FS_DeleteFileFromIdx(struct FileSystem *, LONG);
extern LONG FS_SetComment(struct FileSystem *, const char *, const LONG *, BOOL, const char *);
extern LONG FS_SetDate(struct FileSystem *, const char *, const LONG *, BOOL, LONG, LONG, LONG, LONG, LONG, LONG);

extern LONG FS_FindFile(struct FileSystem *, const char *, const LONG *, BOOL);
extern LONG FS_GetTypeFromHandle(struct FSHandle *);
extern void FS_GetNameTypeFromIndex(struct FileSystem *, LONG, char *, LONG *);
extern void FS_GetVolumeName(struct FileSystem *, char *);
extern LONG FS_SetVolumeName(struct FileSystem *, const char *);
extern BOOL FS_GetVolumeDate(struct FileSystem *, LONG *, LONG *, LONG *, LONG *, LONG *, LONG *);
extern LONG FS_GetFreeSpace(struct FileSystem *);
extern LONG FS_GetBlockSpace(struct FileSystem *, LONG *);
extern const char *FS_GetTextErr(ULONG);

#endif  /* FILESYSTEM_H */