/*
 * gitfetch.h -- gedeelde datastructuren voor GitFetch.
 *
 * De veldlengtes komen exact overeen met de afkapping die de proxy toepast,
 * zodat de client nooit hoeft af te kappen en met vaste buffers toekan.
 * Dat scheelt op een 68k een hoop allocatie-administratie.
 */

#ifndef GITFETCH_H
#define GITFETCH_H

#include <exec/types.h>

#define GF_VERSION      "0.2"
#define GF_PROTO        1

#define GF_MAX_TAG      32      /* proxy kapt af op 31 + NUL */
#define GF_MAX_DATE     12
#define GF_MAX_TITLE    80
#define GF_MAX_NAME     64
#define GF_MAX_PATH     160     /* langste gemeten pad is ~110 tekens */
#define GF_MAX_REPO     132
#define GF_MAX_LINE     320     /* langste protocolregel + marge */
#define GF_MAX_ERR      160

#define GF_MAX_RELEASES 25
/*
 * Twaalf bijlagen per release. Twintig kostte 114 KB aan gereserveerde
 * ruimte in een enkele AllocVec, en dat telt op een machine met 2 MB. De
 * praktijk zit ver onder dit getal: AmiSSL levert er drie per release,
 * MintPRINT een. Wie meer heeft, ziet de eerste twaalf.
 */
#define GF_MAX_ASSETS   12

struct Asset {
    char  name[GF_MAX_NAME];
    ULONG size;
    char  path[GF_MAX_PATH];
};

struct Release {
    char  tag[GF_MAX_TAG];
    char  date[GF_MAX_DATE];
    char  title[GF_MAX_TITLE];
    UWORD prerelease;
    UWORD nassets;
    struct Asset assets[GF_MAX_ASSETS];
};

/* Eén blok van ~120 KB; met AllocVec in één keer te halen en te geven. */
struct ReleaseList {
    char  repo[GF_MAX_REPO];
    UWORD nreleases;
    struct Release releases[GF_MAX_RELEASES];
};

/* Foutcodes. Alles negatief, zodat een positieve waarde altijd voortgang is. */
#define GF_OK             0
#define GF_ERR_NOMEM     -1
#define GF_ERR_SOCKET    -2     /* bsdsocket.library niet open of socket() faalt */
#define GF_ERR_DNS       -3
#define GF_ERR_CONNECT   -4
#define GF_ERR_SEND      -5
#define GF_ERR_RECV      -6
#define GF_ERR_HTTP      -7     /* server gaf geen 200 */
#define GF_ERR_PROTO     -8     /* antwoord past niet op het GITFETCH-formaat */
#define GF_ERR_SERVER    -9     /* #STATUS ERR van de proxy */
#define GF_ERR_ABORTED  -10
#define GF_ERR_FILE     -11
#define GF_ERR_URL      -12     /* onbegrijpelijke repo-invoer */
#define GF_ERR_TLS     -13     /* AmiSSL ontbreekt of start niet */
#define GF_ERR_CLOCK   -14     /* systeemdatum te ver in het verleden */

/*
 * De eigen repository, die als eerste bewaarde regel klaarstaat bij een
 * verse installatie. Zo is de nieuwste versie van GitFetch met GitFetch
 * zelf op te halen.
 */
#define GF_HOME_REPO   "MathieuSEO/GitFetch"

const char *gf_strerror(LONG code);

/*
 * Diagnostiek. Met -DGF_DEBUG print het programma zijn stappen naar de
 * Shell; zonder die vlag valt alles weg.
 */
#ifdef GF_DEBUG
#include <proto/dos.h>
#define GF_TRACE(msg)        Printf("  gf: %s\n", (ULONG)(msg))
#define GF_TRACE1(msg, a)    Printf("  gf: %s %ld\n", (ULONG)(msg), (LONG)(a))
#define GF_TRACE2(msg, a, b) Printf("  gf: %s %s %ld\n", (ULONG)(msg), \
                                    (ULONG)(a), (LONG)(b))
#define GF_TRACEP(msg, p)    Printf("  gf: %s -> 0x%08lx\n", (ULONG)(msg), \
                                    (ULONG)(p))
#else
#define GF_TRACE(msg)
#define GF_TRACE1(msg, a)
#define GF_TRACE2(msg, a, b)
#define GF_TRACEP(msg, p)
#endif

#endif /* GITFETCH_H */
