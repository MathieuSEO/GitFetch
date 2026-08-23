/*
 * net.c -- minimale HTTP/1.0-client op bsdsocket.library.
 *
 * Bewust klein gehouden: geen keep-alive, geen chunked encoding, geen
 * redirects. De proxy spreekt HTTP/1.0 met Connection: close en stuurt
 * altijd een Content-Length, dus meer is niet nodig. Alles wat we niet
 * ondersteunen geeft een expliciete fout in plaats van stille onzin.
 */

#include <exec/types.h>
#include <exec/memory.h>
#include <dos/dos.h>

#include <sys/types.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <netinet/in.h>
#include <netdb.h>
#include <arpa/inet.h>

#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/socket.h>

#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#include "gitfetch.h"
#include "gfnet.h"

struct Library *SocketBase = NULL;

static LONG last_status = 0;
static ULONG last_length = 0;

#define RECV_BUF    4096
#define HDR_BUF     2048
#define DEF_TIMEOUT 30

/*
 * devices/timer.h en de netwerk-sys/time.h definieren allebei een
 * struct timeval, met verschillende veldnamen (tv_secs/tv_micro versus
 * tv_sec/tv_usec). Dit bestand includeert bewust alleen de netwerkvariant.
 * Levert de toolchain toch de OS-variant, dan is dit de enige regel die
 * aangepast hoeft te worden.
 */
#define GF_TV_SET(tv, s)  do { (tv).tv_sec = (s); (tv).tv_usec = 0; } while (0)

/*
 * Verbinding met een leesbuffer. Header-parsing en body-lezing delen deze
 * buffer, zodat bytes die na de lege regel al binnen waren niet verloren
 * gaan -- precies de plek waar handgeschreven HTTP-clients meestal misgaan.
 */
struct Conn {
    LONG  sock;
    UBYTE buf[RECV_BUF];
    LONG  pos;              /* eerste ongebruikte byte in buf */
    LONG  len;              /* aantal geldige bytes in buf */
    ULONG abort_signals;
    ULONG timeout;
    BOOL  eof;
};

LONG gf_net_open(void)
{
    if (SocketBase)
        return GF_OK;
    SocketBase = OpenLibrary("bsdsocket.library", 4);
    return SocketBase ? GF_OK : GF_ERR_SOCKET;
}

void gf_net_close(void)
{
    if (SocketBase) {
        CloseLibrary(SocketBase);
        SocketBase = NULL;
    }
}

LONG gf_http_status(void)
{
    return last_status;
}

ULONG gf_http_last_length(void)
{
    return last_length;
}

/*
 * Wacht tot de socket leesbaar is of tot een abort-signaal binnenkomt.
 * WaitSelect() in plaats van select(), omdat alleen die tegelijk op
 * socket-activiteit en op Amiga-signalen kan wachten -- essentieel voor
 * een Stop-knop die ook tijdens een trage transfer werkt.
 */
static LONG wait_readable(struct Conn *c)
{
    fd_set readset;
    struct timeval tv;
    ULONG sigmask = c->abort_signals;
    LONG res;

    FD_ZERO(&readset);
    FD_SET(c->sock, &readset);
    GF_TV_SET(tv, c->timeout ? c->timeout : DEF_TIMEOUT);

    res = WaitSelect(c->sock + 1, &readset, NULL, NULL, &tv,
                     c->abort_signals ? &sigmask : NULL);

    if (res < 0)
        return GF_ERR_RECV;
    if (c->abort_signals && (sigmask & c->abort_signals))
        return GF_ERR_ABORTED;
    if (res == 0)
        return GF_ERR_RECV;         /* timeout */
    return GF_OK;
}

/* Vult de buffer bij als hij leeg is. GF_OK met c->len == 0 betekent EOF. */
static LONG conn_fill(struct Conn *c)
{
    LONG err, n;

    if (c->pos < c->len)
        return GF_OK;
    if (c->eof) {
        c->pos = c->len = 0;
        return GF_OK;
    }

    err = wait_readable(c);
    if (err != GF_OK)
        return err;

    n = recv(c->sock, c->buf, sizeof(c->buf), 0);
    if (n < 0)
        return GF_ERR_RECV;
    if (n == 0)
        c->eof = TRUE;

    c->pos = 0;
    c->len = n;
    return GF_OK;
}

static LONG send_all(LONG sock, const char *data, LONG len)
{
    LONG sent = 0;

    while (sent < len) {
        LONG n = send(sock, (APTR)(data + sent), len - sent, 0);
        if (n <= 0)
            return GF_ERR_SEND;
        sent += n;
    }
    return GF_OK;
}

static LONG resolve(const char *host, struct sockaddr_in *addr)
{
    struct hostent *he;
    ULONG ip;

    memset(addr, 0, sizeof(*addr));
    addr->sin_family = AF_INET;

    /* Eerst als kaal IP-adres proberen; scheelt een DNS-lookup en werkt
       ook als er geen nameserver is ingesteld. */
    ip = inet_addr((STRPTR)host);
    if (ip != (ULONG)-1) {
        addr->sin_addr.s_addr = ip;
        return GF_OK;
    }

    he = gethostbyname((STRPTR)host);
    if (!he || !he->h_addr_list || !he->h_addr_list[0])
        return GF_ERR_DNS;

    memcpy(&addr->sin_addr, he->h_addr_list[0], sizeof(struct in_addr));
    return GF_OK;
}

/* Zoekt een header op, hoofdletterongevoelig. Geeft de waarde of NULL. */
static const char *find_header(const char *headers, const char *name)
{
    LONG namelen = strlen(name);
    const char *p = headers;

    /* Statusregel overslaan. */
    while (*p && *p != '\n')
        p++;
    if (*p)
        p++;

    while (*p) {
        if (strnicmp(p, name, namelen) == 0 && p[namelen] == ':') {
            p += namelen + 1;
            while (*p == ' ' || *p == '\t')
                p++;
            return p;
        }
        while (*p && *p != '\n')
            p++;
        if (*p)
            p++;
    }
    return NULL;
}

/* Leest headers tot en met de lege regel; laat de body in de conn-buffer. */
static LONG read_headers(struct Conn *c, char *hdrbuf, LONG hdrmax)
{
    LONG hdrlen = 0;
    LONG nl_run = 0;        /* aantal opeenvolgende regeleindes */

    for (;;) {
        LONG err = conn_fill(c);
        UBYTE ch;

        if (err != GF_OK)
            return err;
        if (c->pos >= c->len)
            return GF_ERR_PROTO;    /* EOF midden in de headers */

        ch = c->buf[c->pos++];

        if (hdrlen < hdrmax - 1)
            hdrbuf[hdrlen++] = ch;
        else
            return GF_ERR_PROTO;    /* absurd grote headers */

        if (ch == '\n') {
            if (++nl_run == 2)
                break;
        } else if (ch != '\r') {
            nl_run = 0;
        }
    }

    hdrbuf[hdrlen] = '\0';
    return GF_OK;
}

LONG gf_http_get(struct GFRequest *req)
{
    struct Conn *c;
    struct sockaddr_in addr;
    char *request;
    char *hdrbuf;
    LONG err;
    ULONG content_length = 0;
    ULONG received = 0;
    const char *val;

    last_status = 0;
    last_length = 0;

    if (!SocketBase)
        return GF_ERR_SOCKET;

    /* Conn is te groot voor de stack van een Amiga-proces. */
    c = AllocVec(sizeof(struct Conn), MEMF_ANY | MEMF_CLEAR);
    hdrbuf = AllocVec(HDR_BUF, MEMF_ANY);
    request = AllocVec(GF_MAX_PATH + 256, MEMF_ANY);
    if (!c || !hdrbuf || !request) {
        err = GF_ERR_NOMEM;
        goto cleanup;
    }

    c->sock = -1;
    c->abort_signals = req->abort_signals;
    c->timeout = req->timeout_secs;

    err = resolve(req->host, &addr);
    if (err != GF_OK)
        goto cleanup;
    addr.sin_port = htons(req->port ? req->port : 80);

    c->sock = socket(AF_INET, SOCK_STREAM, 0);
    if (c->sock < 0) {
        err = GF_ERR_SOCKET;
        goto cleanup;
    }

    if (connect(c->sock, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        err = GF_ERR_CONNECT;
        goto cleanup;
    }

    /* HTTP/1.0 met expliciete close: geen keep-alive-afhandeling nodig,
       de server sluit netjes af als hij klaar is. */
    sprintf(request,
            "GET %s HTTP/1.0\r\n"
            "Host: %s\r\n"
            "User-Agent: GitFetch/" GF_VERSION " (AmigaOS)\r\n"
            "Accept: */*\r\n"
            "Connection: close\r\n"
            "\r\n",
            req->path, req->host);

    err = send_all(c->sock, request, strlen(request));
    if (err != GF_OK)
        goto cleanup;

    err = read_headers(c, hdrbuf, HDR_BUF);
    if (err != GF_OK)
        goto cleanup;

    if (strncmp(hdrbuf, "HTTP/1.", 7) != 0) {
        err = GF_ERR_PROTO;
        goto cleanup;
    }
    last_status = atol(hdrbuf + 9);
    if (last_status != 200) {
        err = GF_ERR_HTTP;
        goto cleanup;
    }

    /* Chunked wordt niet ondersteund; liever een duidelijke fout dan een
       body met lengte-prefixen erdoorheen. */
    val = find_header(hdrbuf, "Transfer-Encoding");
    if (val && strnicmp(val, "chunked", 7) == 0) {
        err = GF_ERR_PROTO;
        goto cleanup;
    }

    val = find_header(hdrbuf, "Content-Length");
    if (val)
        content_length = (ULONG)atol(val);

    /* --- body doorgeven aan de sink --- */
    for (;;) {
        LONG avail;

        err = conn_fill(c);
        if (err != GF_OK)
            goto cleanup;

        avail = c->len - c->pos;
        if (avail <= 0)
            break;                  /* EOF: server heeft afgesloten */

        received += avail;
        last_length = received;
        if (req->sink) {
            err = req->sink(req->user, c->buf + c->pos, (ULONG)avail,
                            received, content_length);
            if (err != GF_OK)
                goto cleanup;
        }
        c->pos = c->len;

        if (content_length && received >= content_length)
            break;
    }

    /* Een afgekapte transfer mag niet als succes doorgaan: anders schrijft
       de client een half archief naar disk zonder het te merken. */
    if (content_length && received < content_length) {
        err = GF_ERR_RECV;
        goto cleanup;
    }

    err = GF_OK;

cleanup:
    if (c && c->sock >= 0)
        CloseSocket(c->sock);
    if (request)
        FreeVec(request);
    if (hdrbuf)
        FreeVec(hdrbuf);
    if (c)
        FreeVec(c);
    return err;
}
