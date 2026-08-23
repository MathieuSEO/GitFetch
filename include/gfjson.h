/*
 * gfjson.h -- streaming-parser voor het antwoord van de GitHub API.
 *
 * Voedt hetzelfde ReleaseList-model als de protocolparser, zodat de GUI
 * niet merkt welke backend de gegevens aanleverde.
 *
 * Streamend, en dat is hier geen luxe: het antwoord voor 25 releases is
 * ongeveer 240 KB, grotendeels release-notes die we niet nodig hebben.
 * Alleen de velden die ertoe doen worden verzameld; de rest glijdt voorbij
 * zonder ooit in het geheugen te staan.
 */

#ifndef GFJSON_H
#define GFJSON_H

#include <exec/types.h>
#include "gitfetch.h"

#define GFJ_KEY_MAX   24
#define GFJ_VAL_MAX   GF_MAX_PATH

struct GFJson {
    struct ReleaseList *list;

    /* lexer */
    UWORD  depth;
    BOOL   in_string;
    BOOL   escape;
    UWORD  hex_left;        /* resterende hex-tekens van een \uXXXX */
    UWORD  utf8_left;       /* resterende vervolgbytes van een UTF-8-teken */
    ULONG  codepoint;

    /* wat we op dit moment aan het lezen zijn */
    BOOL   reading_key;
    BOOL   collecting;      /* verzamelen we deze waarde? */
    char   key[GFJ_KEY_MAX];
    UWORD  keylen;
    char   val[GFJ_VAL_MAX];
    UWORD  vallen;
    BOOL   val_truncated;

    /* structuur */
    char   last_key[GFJ_KEY_MAX];   /* key vlak voor een { of [ */
    BOOL   in_assets;
    UWORD  assets_depth;
    struct Release *cur_release;
    struct Asset   *cur_asset;
    BOOL   release_is_draft;

    LONG   dropped;         /* overgeslagen releases/assets door ruimtegebrek */
};

void gf_json_init(struct GFJson *j, struct ReleaseList *list);
LONG gf_json_feed(struct GFJson *j, const UBYTE *data, ULONG len);
LONG gf_json_finish(struct GFJson *j);

#endif /* GFJSON_H */
