/*
 * json.c -- streaming-parser voor het antwoord van de GitHub API.
 *
 * Geen algemene JSON-bibliotheek: dit leest precies de acht velden die
 * GitFetch nodig heeft en laat de rest voorbijgaan. Dat scheelt op een 68k
 * zowel geheugen als tijd, want het grootste deel van de 240 KB bestaat uit
 * release-notes waar we niets mee doen.
 *
 * De structuur waar we doorheen lopen:
 *
 *   [                          diepte 1
 *     {                        diepte 2  -- een release
 *       "tag_name": "5.27",              -- velden op diepte 2
 *       "author": { ... },     diepte 3  -- genegeerd
 *       "assets": [            diepte 3
 *         {                    diepte 4  -- een asset
 *           "name": "x.lha"              -- velden op diepte 4
 *         }
 *       ]
 *     }
 *   ]
 */

#include <exec/types.h>
#include <string.h>
#include <stdlib.h>

#include "gitfetch.h"
#include "gfjson.h"

/* --- tekstconversie ------------------------------------------------------ */

/*
 * De API levert UTF-8, de Amiga verwacht ISO-8859-1. Tekens tot U+00FF
 * passen een-op-een; de rest (emoji, CJK) laten we weg. Een paar veel
 * voorkomende leestekens krijgen een ASCII-tegenhanger, anders zou er een
 * gat in de tekst vallen waar een streepje of aanhalingsteken hoort.
 */
static void append_unicode(struct GFJson *j, ULONG cp)
{
    char sub[4];
    const char *text = NULL;
    UWORD n, i;

    switch (cp) {
    case 0x2018: case 0x2019: case 0x201A: case 0x201B: text = "'";   break;
    case 0x201C: case 0x201D: case 0x201E: case 0x201F: text = "\"";  break;
    case 0x2013: case 0x2014: case 0x2015: case 0x2212: text = "-";   break;
    case 0x2026: text = "..."; break;
    case 0x2022: case 0x00B7: text = "*";  break;
    case 0x2192: text = "->";  break;
    case 0x2190: text = "<-";  break;
    case 0x00A0: case 0x2009: case 0x200B: text = " "; break;
    default:
        if (cp >= 0x20 && cp <= 0xFF && cp != 0x7F) {
            sub[0] = (char)cp;
            sub[1] = '\0';
            text = sub;
        }
        break;
    }

    if (!text)
        return;                 /* niet weer te geven; overslaan */

    n = (UWORD)strlen(text);
    for (i = 0; i < n; i++) {
        if (j->vallen < GFJ_VAL_MAX - 1)
            j->val[j->vallen++] = text[i];
        else
            j->val_truncated = TRUE;
    }
}

/* --- velden opslaan ------------------------------------------------------ */

/* Kopieren met afkapping en een gegarandeerde afsluitende NUL. Met memcpy
   in plaats van strncpy, anders denkt de compiler dat de NUL zoek kan
   raken -- terwijl we hem er hier juist expliciet in zetten. */
static void copy_field(char *dest, const char *src, LONG size)
{
    LONG len = (LONG)strlen(src);

    if (len > size - 1)
        len = size - 1;
    memcpy(dest, src, len);
    dest[len] = '\0';
}

static BOOL key_is(struct GFJson *j, const char *name)
{
    return (BOOL)(strcmp(j->key, name) == 0);
}

/* Bepaalt of de waarde bij deze key de moeite van het verzamelen waard is. */
static BOOL want_value(struct GFJson *j)
{
    if (j->depth == 2)
        return (BOOL)(key_is(j, "tag_name") || key_is(j, "name") ||
                      key_is(j, "published_at") || key_is(j, "created_at") ||
                      key_is(j, "prerelease") || key_is(j, "draft"));

    if (j->in_assets && j->depth == j->assets_depth + 1)
        return (BOOL)(key_is(j, "name") || key_is(j, "size") ||
                      key_is(j, "browser_download_url"));

    return FALSE;
}

static void store_release_field(struct GFJson *j)
{
    struct Release *rel = j->cur_release;

    if (!rel)
        return;

    if (key_is(j, "tag_name")) {
        copy_field(rel->tag, j->val, GF_MAX_TAG);
    } else if (key_is(j, "name")) {
        copy_field(rel->title, j->val, GF_MAX_TITLE);
    } else if (key_is(j, "published_at") || key_is(j, "created_at")) {
        /* "2026-04-08T11:10:00Z" -> alleen de datum */
        if (!rel->date[0])
            copy_field(rel->date, j->val, 11);
    } else if (key_is(j, "prerelease")) {
        rel->prerelease = (UWORD)(strcmp(j->val, "true") == 0 ? 1 : 0);
    } else if (key_is(j, "draft")) {
        j->release_is_draft = (BOOL)(strcmp(j->val, "true") == 0);
    }
}

static void store_asset_field(struct GFJson *j)
{
    struct Asset *asset = j->cur_asset;

    if (!asset)
        return;

    if (key_is(j, "name"))
        copy_field(asset->name, j->val, GF_MAX_NAME);
    else if (key_is(j, "size"))
        asset->size = (ULONG)strtoul(j->val, NULL, 10);
    else if (key_is(j, "browser_download_url"))
        copy_field(asset->path, j->val, GF_MAX_PATH);
}

static void value_done(struct GFJson *j)
{
    j->val[j->vallen] = '\0';

    if (j->depth == 2)
        store_release_field(j);
    else if (j->in_assets && j->depth == j->assets_depth + 1)
        store_asset_field(j);

    j->collecting = FALSE;
    j->vallen = 0;
    j->key[0] = '\0';
    j->keylen = 0;
}

/* --- structuur ----------------------------------------------------------- */

static void value_done(struct GFJson *j);

static void open_brace(struct GFJson *j)
{
    j->depth++;

    if (j->depth == 2) {
        /* Nieuwe release. */
        if (j->list->nreleases < GF_MAX_RELEASES) {
            j->cur_release = &j->list->releases[j->list->nreleases];
            memset(j->cur_release, 0, sizeof(struct Release));
        } else {
            j->cur_release = NULL;
            j->dropped++;
        }
        j->release_is_draft = FALSE;
        j->in_assets = FALSE;
    } else if (j->in_assets && j->depth == j->assets_depth + 1) {
        /* Nieuwe asset binnen de huidige release. */
        if (j->cur_release && j->cur_release->nassets < GF_MAX_ASSETS) {
            j->cur_asset = &j->cur_release->assets[j->cur_release->nassets];
            memset(j->cur_asset, 0, sizeof(struct Asset));
        } else {
            j->cur_asset = NULL;
            if (j->cur_release)
                j->dropped++;
        }
    }
}

static void close_brace(struct GFJson *j)
{
    if (j->depth == 2) {
        /* Release afgerond: pas nu weten we of het een concept was. */
        if (j->cur_release && !j->release_is_draft && j->cur_release->tag[0]) {
            if (!j->cur_release->title[0])
                copy_field(j->cur_release->title, j->cur_release->tag,
                           GF_MAX_TITLE);
            j->list->nreleases++;
        }
        j->cur_release = NULL;
    } else if (j->in_assets && j->depth == j->assets_depth + 1) {
        /* Asset afgerond; zonder naam of URL valt er niets te downloaden. */
        if (j->cur_asset && j->cur_asset->name[0] && j->cur_asset->path[0])
            j->cur_release->nassets++;
        j->cur_asset = NULL;
    }

    if (j->depth > 0)
        j->depth--;
}

static void open_bracket(struct GFJson *j)
{
    j->depth++;

    if (j->depth == 3 && strcmp(j->last_key, "assets") == 0) {
        j->in_assets = TRUE;
        j->assets_depth = j->depth;
    }
}

static void close_bracket(struct GFJson *j)
{
    if (j->in_assets && j->depth == j->assets_depth)
        j->in_assets = FALSE;

    if (j->depth > 0)
        j->depth--;
}

/* --- de eigenlijke lus --------------------------------------------------- */

void gf_json_init(struct GFJson *j, struct ReleaseList *list)
{
    memset(j, 0, sizeof(*j));
    j->list = list;
    memset(list, 0, sizeof(*list));
}

LONG gf_json_feed(struct GFJson *j, const UBYTE *data, ULONG len)
{
    ULONG i;

    for (i = 0; i < len; i++) {
        UBYTE c = data[i];

        /* --- binnen een string --- */
        if (j->in_string) {
            /* Vervolgbytes van een UTF-8-teken. */
            if (j->utf8_left) {
                if ((c & 0xC0) == 0x80) {
                    j->codepoint = (j->codepoint << 6) | (ULONG)(c & 0x3F);
                    if (--j->utf8_left == 0 && j->collecting)
                        append_unicode(j, j->codepoint);
                } else {
                    j->utf8_left = 0;   /* kapotte reeks; opnieuw beginnen */
                }
                continue;
            }

            /* Hex-tekens van een \uXXXX-escape. */
            if (j->hex_left) {
                UWORD digit = 0;

                if (c >= '0' && c <= '9')      digit = (UWORD)(c - '0');
                else if (c >= 'a' && c <= 'f') digit = (UWORD)(c - 'a' + 10);
                else if (c >= 'A' && c <= 'F') digit = (UWORD)(c - 'A' + 10);

                j->codepoint = (j->codepoint << 4) | digit;
                if (--j->hex_left == 0 && j->collecting)
                    append_unicode(j, j->codepoint);
                continue;
            }

            if (j->escape) {
                j->escape = FALSE;
                switch (c) {
                case 'u':
                    j->hex_left = 4;
                    j->codepoint = 0;
                    continue;
                case 'n': case 'r': case 't': c = ' '; break;
                case 'b': case 'f':           continue;
                default:                      break;  /* " \\ / blijven zichzelf */
                }
                if (j->collecting)
                    append_unicode(j, (ULONG)c);
                else if (j->reading_key && j->keylen < GFJ_KEY_MAX - 1)
                    j->key[j->keylen++] = (char)c;
                continue;
            }

            if (c == '\\') {
                j->escape = TRUE;
                continue;
            }

            if (c == '"') {
                j->in_string = FALSE;
                if (j->reading_key) {
                    j->key[j->keylen] = '\0';
                    j->reading_key = FALSE;
                    copy_field(j->last_key, j->key, GFJ_KEY_MAX);
                } else if (j->collecting) {
                    value_done(j);
                }
                continue;
            }

            if (c < 0x80) {
                if (j->collecting)
                    append_unicode(j, (ULONG)c);
                else if (j->reading_key && j->keylen < GFJ_KEY_MAX - 1)
                    j->key[j->keylen++] = (char)c;
                continue;
            }

            /* Kop van een UTF-8-teken: onthoud hoeveel bytes er nog volgen.
               Keys zijn altijd ASCII, dus dit speelt alleen bij waarden. */
            if ((c & 0xE0) == 0xC0) {
                j->codepoint = (ULONG)(c & 0x1F);
                j->utf8_left = 1;
            } else if ((c & 0xF0) == 0xE0) {
                j->codepoint = (ULONG)(c & 0x0F);
                j->utf8_left = 2;
            } else if ((c & 0xF8) == 0xF0) {
                j->codepoint = (ULONG)(c & 0x07);
                j->utf8_left = 3;   /* buiten Latin-1; append_unicode laat het vallen */
            }
            continue;
        }

        /* --- buiten een string --- */
        switch (c) {
        case '"':
            j->in_string = TRUE;
            j->escape = FALSE;
            if (j->reading_key) {
                j->keylen = 0;
            } else if (j->collecting) {
                j->vallen = 0;
            }
            break;

        case '{':
            open_brace(j);
            j->reading_key = TRUE;
            j->key[0] = '\0';
            j->keylen = 0;
            break;

        case '}':
            /* Een getal of boolean als laatste veld van een object wordt
               niet door een komma afgesloten; zonder dit blijft die waarde
               liggen. Nu onschadelijk omdat GitHub "draft" nooit als
               laatste zet, maar dat is geen garantie om op te bouwen. */
            if (j->collecting)
                value_done(j);
            close_brace(j);
            j->reading_key = FALSE;
            break;

        case '[':
            open_bracket(j);
            break;

        case ']':
            if (j->collecting)
                value_done(j);
            close_bracket(j);
            break;

        case ':':
            /* Na de dubbele punt volgt de waarde bij de gelezen key. */
            j->collecting = want_value(j);
            j->vallen = 0;
            j->val_truncated = FALSE;
            break;

        case ',':
            if (j->collecting)
                value_done(j);          /* getal of boolean zonder aanhalingstekens */
            j->reading_key = TRUE;
            j->keylen = 0;
            break;

        default:
            /* Getallen, true/false/null buiten aanhalingstekens. */
            if (j->collecting && c > ' ') {
                if (j->vallen < GFJ_VAL_MAX - 1)
                    j->val[j->vallen++] = (char)c;
            }
            break;
        }
    }
    return GF_OK;
}

LONG gf_json_finish(struct GFJson *j)
{
    if (j->collecting)
        value_done(j);

    if (j->list->nreleases == 0)
        return GF_OK;           /* geldige lege lijst is geen fout */

    return GF_OK;
}
