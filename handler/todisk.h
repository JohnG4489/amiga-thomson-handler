#ifndef DEVICES_TODISK_H
#define DEVICES_TODISK_H

/*
**
**      $VER: todisk.h 1.0 (25.2.99)
**      Includes Release 1.0
**
**      todisk device structure and value definitions
**
**      (C) Copyright 1999 Régis GREAU.
**          All Rights Reserved
**
*/

#ifndef EXEC_IO_H
#include <exec/io.h>
#endif

#ifndef EXEC_DEVICES_H
#include <exec/devices.h>
#endif

#ifndef DEVICES_TRACKDISK_H
#include <devices/trackdisk.h>
#endif


#define TOFLAG_NORMAL 0
#define TOFLAG_THOMSON 1
#define TO_SECLEN128 0
#define TO_SECLEN256 1
#define TO_SECLEN512 2
#define TO_SECLEN1024 3
#define TO_MAKEFLAG(ToFlg,SecLen,NbSec) ((ToFlg)<<7|(SecLen)<<5|NbSec)

#define TO_DFLT_INTERLEAVE 7
#define TO_SECTORFLG    1       /* 0=128, 1=256, 2=512 */
#define TO_SECTORSIZE   (1L<<(TO_SECTORFLG+7)) /* Bytes per sector    */
#define TO_NBSECTORS    16      /* Sectors per Track   */
#define TO_TRACKSIZE    (TO_NBSECTORS*TO_SECTORSIZE)
#define TO_LABELSIZE    128

#define TO_NAME "todisk.device"

#define TO_GETTRKINTERLEAVE (CMD_NONSTD+16)
#define TO_GETCURINTERLEAVE TO_GETTRKINTERLEAVE
#define TO_GETFMTINTERLEAVE (CMD_NONSTD+17)
#define TO_SETFMTINTERLEAVE (CMD_NONSTD+18)
#define TO_MAKEFMTINTERLEAVE (CMD_NONSTD+19)
#define TO_ADDCHANGEINT TD_ADDCHANGEINT
#define TO_CHANGENUM TD_CHANGENUM
#define TO_CHANGESTATE TD_CHANGESTATE
#define TO_EJECT TD_EJECT
#define TO_FORMAT TD_FORMAT
#define TO_GETDRIVETYPE TD_GETDRIVETYPE
#define TO_GETGEOMETRY TD_GETGEOMETRY
#define TO_GETNUMTRACKS TD_GETNUMTRACKS
#define TO_MOTOR TD_MOTOR
#define TO_PROTSTATUS TD_PROTSTATUS
#define TO_RAWREAD TD_RAWREAD
#define TO_RAWWRITE TD_RAWWRITE
#define TO_REMCHANGEINT TD_REMCHANGEINT
#define TO_SEEK TD_SEEK

#endif  /* DEVICES_TODISK_H */