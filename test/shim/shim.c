/*
 * shim.c -- AmigaOS-aanroepen naar POSIX, zodat net.c en backend_proxy.c
 * op de host tegen een echte proxy getest kunnen worden.
 *
 * Dit is nadrukkelijk geen emulatie van AmigaOS: het dekt precies de
 * functies die deze twee modules gebruiken, met hetzelfde contract.
 */

#include <exec/types.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/socket.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdarg.h>
#include <sys/select.h>

static struct Library fake_library;

APTR AllocVec(ULONG size, ULONG flags)
{
    void *p = malloc(size);
    if (p && (flags & (1 << 16)))
        memset(p, 0, size);
    return p;
}

void FreeVec(APTR mem) { free(mem); }

struct Library *OpenLibrary(const char *name, ULONG version)
{
    (void)name; (void)version;
    return &fake_library;
}

void CloseLibrary(struct Library *lib) { (void)lib; }

/* Geen Amiga-signalen op de host; nooit een afbreekverzoek melden. */
ULONG SetSignal(ULONG newsig, ULONG mask) { (void)newsig; (void)mask; return 0; }

LONG CloseSocket(LONG sock) { return close((int)sock); }

LONG WaitSelect(LONG nfds, fd_set *r, fd_set *w, fd_set *e,
                struct timeval *tv, ULONG *sigmask)
{
    if (sigmask)
        *sigmask = 0;       /* host kent geen exec-signalen */
    return select((int)nfds, r, w, e, tv);
}

LONG Errno(void) { return 0; }

BPTR Open(const char *name, LONG mode)
{
    return (BPTR)fopen(name, mode == MODE_NEWFILE ? "wb" : "rb");
}

void Close(BPTR file)
{
    if (file)
        fclose((FILE *)file);
}

LONG Write(BPTR file, APTR buf, LONG len)
{
    return (LONG)fwrite(buf, 1, (size_t)len, (FILE *)file);
}

LONG DeleteFile(const char *name) { return unlink(name) == 0; }

/* AddPart plakt een bestandsnaam achter een pad, met de Amiga-regel dat
   een pad dat op ':' of '/' eindigt geen extra scheidingsteken krijgt. */
LONG AddPart(char *dir, const char *file, ULONG size)
{
    size_t len = strlen(dir);

    if (len && dir[len - 1] != '/' && dir[len - 1] != ':') {
        if (len + 1 >= size) return 0;
        dir[len++] = '/';
        dir[len] = '\0';
    }
    if (len + strlen(file) >= size) return 0;
    strcat(dir, file);
    return 1;
}

LONG Printf(const char *fmt, ...)
{
    va_list ap;
    int n;
    va_start(ap, fmt);
    n = vprintf(fmt, ap);
    va_end(ap);
    return n;
}
