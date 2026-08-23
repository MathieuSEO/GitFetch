/*
 * gfparse.h -- streaming-parser voor het GITFETCH-protocol.
 *
 * Krijgt de body blok voor blok binnen (zoals gfnet die aanlevert) en
 * bouwt daar regel voor regel een ReleaseList uit op. Streamend, zodat er
 * nooit een kopie van het hele antwoord in RAM hoeft te staan.
 */

#ifndef GFPARSE_H
#define GFPARSE_H

#include <exec/types.h>
#include "gitfetch.h"

struct GFParser {
    struct ReleaseList *list;
    char  line[GF_MAX_LINE];
    LONG  linelen;
    BOOL  overflow;         /* huidige regel te lang: rest tot \n weggooien */
    BOOL  saw_header;       /* #GITFETCH gezien */
    BOOL  saw_end;          /* #END gezien */
    LONG  status;           /* 0 = OK, anders de code uit #STATUS ERR */
    char  message[GF_MAX_ERR];
    LONG  dropped;          /* aantal genegeerde regels, voor diagnose */
};

void gf_parser_init(struct GFParser *p, struct ReleaseList *list);
LONG gf_parser_feed(struct GFParser *p, const UBYTE *data, ULONG len);
LONG gf_parser_finish(struct GFParser *p);

#endif /* GFPARSE_H */
