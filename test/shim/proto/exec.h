/* Shim: exec.library-functies op de host. */
#ifndef PROTO_EXEC_H
#define PROTO_EXEC_H
#include <exec/types.h>
struct Library { int dummy; };
APTR AllocVec(ULONG size, ULONG flags);
void FreeVec(APTR mem);
struct Library *OpenLibrary(const char *name, ULONG version);
void CloseLibrary(struct Library *lib);
ULONG SetSignal(ULONG newsig, ULONG mask);
#endif
