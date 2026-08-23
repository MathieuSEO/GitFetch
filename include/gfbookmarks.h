/*
 * gfbookmarks.h -- onthouden welke repositories je vaker gebruikt.
 *
 * Op een Amiga is "github.com/iemand/project" intypen omslachtig genoeg om
 * het maar een keer te willen doen. De lijst staat in ENVARC:, zodat hij
 * een herstart overleeft.
 */

#ifndef GFBOOKMARKS_H
#define GFBOOKMARKS_H

#include <exec/types.h>
#include "gitfetch.h"

#define GF_MAX_BOOKMARKS  40

struct Bookmarks {
    UWORD count;
    char  repo[GF_MAX_BOOKMARKS][GF_MAX_REPO];
};

void gf_bookmarks_load(struct Bookmarks *bm);
BOOL gf_bookmarks_save(struct Bookmarks *bm);

/* Voegt toe als de repo er nog niet in staat; nieuwste bovenaan.
   Geeft TRUE als de lijst veranderde. */
BOOL gf_bookmarks_add(struct Bookmarks *bm, const char *repo);
BOOL gf_bookmarks_remove(struct Bookmarks *bm, UWORD index);
LONG gf_bookmarks_find(struct Bookmarks *bm, const char *repo);

#endif /* GFBOOKMARKS_H */
