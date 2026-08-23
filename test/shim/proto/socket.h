/* Shim: bsdsocket.library-eigenaardigheden op POSIX. */
#ifndef PROTO_SOCKET_H
#define PROTO_SOCKET_H
#include <exec/types.h>
#include <sys/select.h>
/* bsdsocket heeft geen close() maar CloseSocket(), en WaitSelect() in
   plaats van select() omdat het ook op Amiga-signalen kan wachten. */
LONG CloseSocket(LONG sock);
LONG WaitSelect(LONG nfds, fd_set *r, fd_set *w, fd_set *e,
                struct timeval *tv, ULONG *sigmask);
LONG Errno(void);
#endif
