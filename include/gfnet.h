/*
 * gfnet.h -- minimale HTTP/1.0-client op bsdsocket.library.
 *
 * Geen TLS: in fase 1 praat de Amiga alleen plain HTTP met de proxy.
 * De body wordt niet gebufferd maar per blok aan een sink-callback gegeven,
 * zodat dezelfde code zowel een releaselijst in RAM als een asset van
 * meerdere megabytes naar disk kan verwerken.
 */

#ifndef GFNET_H
#define GFNET_H

#include <exec/types.h>

/*
 * Wordt aangeroepen per ontvangen blok. Geeft 0 terug om door te gaan,
 * of een negatieve foutcode om de transfer af te breken.
 * 'total' is 0 als de server geen Content-Length gaf.
 */
typedef LONG (*gf_sink_fn)(APTR user, const UBYTE *data, ULONG len,
                           ULONG sofar, ULONG total);

struct GFRequest {
    const char *host;
    UWORD       port;
    const char *path;
    gf_sink_fn  sink;
    APTR        user;
    ULONG       abort_signals;  /* signalen die de transfer afbreken; 0 = geen */
    ULONG       timeout_secs;   /* 0 = default */
};

LONG gf_net_open(void);
void gf_net_close(void);

/* Voert de GET uit. Geeft GF_OK of een GF_ERR_*-code terug. */
LONG gf_http_get(struct GFRequest *req);

/* Laatste HTTP-statuscode, voor foutmeldingen na GF_ERR_HTTP. */
LONG gf_http_status(void);

/* Aantal body-bytes van de laatste overdracht. */
ULONG gf_http_last_length(void);

#endif /* GFNET_H */
