/* Shim: dos.library-functies op de host. */
#ifndef PROTO_DOS_H
#define PROTO_DOS_H
#include <exec/types.h>
#include <dos/dos.h>
BPTR Open(const char *name, LONG mode);
void Close(BPTR file);
LONG Write(BPTR file, APTR buf, LONG len);
LONG DeleteFile(const char *name);
LONG AddPart(char *dir, const char *file, ULONG size);
LONG Printf(const char *fmt, ...);
#endif
