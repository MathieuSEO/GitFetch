/*
 * errors.c -- foutcodes naar Nederlandse tekst.
 */

#include <exec/types.h>
#include "gitfetch.h"
#include "gfnet.h"

const char *gf_strerror(LONG code)
{
    switch (code) {
    case GF_OK:          return "Succeeded";
    case GF_ERR_NOMEM:   return "Out of memory";
    case GF_ERR_SOCKET:  return "Cannot open bsdsocket.library "
                                "(is the TCP/IP stack running?)";
    case GF_ERR_DNS:     return "Server name not found";
    case GF_ERR_CONNECT: return "Cannot connect to the server";
    case GF_ERR_SEND:    return "Sending failed";
    case GF_ERR_RECV:    return "Connection lost or timed out";
    case GF_ERR_HTTP:    return "The server returned an error";
    case GF_ERR_PROTO:   return "Unexpected reply from the server";
    case GF_ERR_SERVER:  return "The proxy reported an error";
    case GF_ERR_ABORTED: return "Aborted";
    case GF_ERR_FILE:    return "Cannot write the file";
    case GF_ERR_URL:     return "Cannot make sense of that repository";
    case GF_ERR_TLS:     return "AmiSSL 5 is required for a direct "
                                "connection to GitHub";
    case GF_ERR_CLOCK:   return "The Amiga clock is wrong";
    default:             return "Unknown error";
    }
}
