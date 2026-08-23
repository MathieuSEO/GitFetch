#ifndef DOS_DOS_H
#define DOS_DOS_H
#include <exec/types.h>
typedef void *BPTR;
#define MODE_NEWFILE  1006
#define MODE_OLDFILE  1005
#define RETURN_OK     0
#define RETURN_WARN   5
#define RETURN_ERROR  10
#define RETURN_FAIL   20
#define SIGBREAKF_CTRL_C  (1L<<12)
#endif
