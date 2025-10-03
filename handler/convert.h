#ifndef CONVERT_H
#define CONVERT_H


struct ConvertContext
{
    const UBYTE *Src;
    LONG SrcLen;
    UBYTE *Dst;
    LONG DstLen;
    LONG State;
    LONG SrcPos;
    LONG Data;
    void *UserData;
};

struct AnsiToAssContext
{
    UBYTE *TmpBufferPtr;
    LONG TmpBufferSize;
    LONG TmpBufferLen;
    LONG TmpBufferPos;
    LONG LineNumber;
    LONG LineNumberStep;
    LONG TabIdx;
    LONG CurPos;
    LONG DstPos;
};


/***** VARIABLES ET FONCTIONS    *****/
/***** PUBLIQUES UTILISABLES PAR *****/
/***** D'AUTRES BLOCS DU PROJET  *****/

extern void Cnv_InitConvertContext(struct ConvertContext *, const UBYTE *, LONG, UBYTE *, LONG, LONG);

extern void Cnv_SplitHostName(const char *, char *);
extern void Cnv_ConvertHostNameToThomsonName(const char *, UBYTE *);
extern void Cnv_ConvertThomsonNameToHostName(const UBYTE *, char *, BOOL);
extern void Cnv_ConvertHostLabelToThomsonLabel(const char *, UBYTE *);
extern void Cnv_ConvertThomsonLabelToHostLabel(const UBYTE *, char *, BOOL);
extern void Cnv_ConvertHostCommentToThomsonComment(const char *, UBYTE *, LONG *, LONG *);
extern void Cnv_ConvertThomsonCommentToHostComment(const UBYTE *, char *, BOOL);

extern LONG Cnv_AnsiToAsciiG2(struct ConvertContext *);
extern LONG Cnv_AsciiG2ToAnsi(struct ConvertContext *);

#endif  /* CONVERT_H */
