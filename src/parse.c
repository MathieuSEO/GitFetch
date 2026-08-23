/*
 * parse.c -- streaming-parser voor het GITFETCH-protocol.
 *
 * Het formaat is opzettelijk regel- en tabgebaseerd, zodat hier geen
 * allocaties, geen recursie en geen tokenizer aan te pas komen.
 */

#include <exec/types.h>
#include <string.h>
#include <stdlib.h>

#include "gitfetch.h"
#include "gfparse.h"

/* Knipt het volgende tab-gescheiden veld af en schuift de cursor door. */
static char *next_field(char **cursor)
{
    char *start = *cursor;
    char *tab;

    if (!start)
        return NULL;

    tab = strchr(start, '\t');
    if (tab) {
        *tab = '\0';
        *cursor = tab + 1;
    } else {
        *cursor = NULL;
    }
    return start;
}

static void copy_field(char *dest, const char *src, LONG size)
{
    if (!src) {
        dest[0] = '\0';
        return;
    }
    strncpy(dest, src, size - 1);
    dest[size - 1] = '\0';
}

static void handle_release(struct GFParser *p, char *cursor)
{
    struct Release *rel;
    char *idx, *tag, *date, *pre, *title;

    if (p->list->nreleases >= GF_MAX_RELEASES) {
        p->dropped++;
        return;
    }

    idx   = next_field(&cursor);
    tag   = next_field(&cursor);
    date  = next_field(&cursor);
    pre   = next_field(&cursor);
    title = next_field(&cursor);

    if (!tag) {                 /* zonder tag valt er niets te tonen */
        p->dropped++;
        return;
    }
    (void)idx;

    rel = &p->list->releases[p->list->nreleases];
    memset(rel, 0, sizeof(*rel));
    copy_field(rel->tag,   tag,   GF_MAX_TAG);
    copy_field(rel->date,  date,  GF_MAX_DATE);
    copy_field(rel->title, title, GF_MAX_TITLE);
    rel->prerelease = (pre && *pre == '1') ? 1 : 0;
    rel->nassets = 0;

    if (rel->title[0] == '\0')
        copy_field(rel->title, tag, GF_MAX_TITLE);

    p->list->nreleases++;
}

static void handle_asset(struct GFParser *p, char *cursor)
{
    struct Release *rel;
    struct Asset *asset;
    char *ridx, *aidx, *name, *size, *path;
    LONG release_index;

    ridx = next_field(&cursor);
    aidx = next_field(&cursor);
    name = next_field(&cursor);
    size = next_field(&cursor);
    path = next_field(&cursor);
    (void)aidx;

    if (!ridx || !name || !path) {
        p->dropped++;
        return;
    }

    /* Assets horen direct achter hun release te komen. Klopt de index niet
       met de laatst gelezen release, dan negeren we de regel in plaats van
       hem aan de verkeerde release te hangen. */
    release_index = atol(ridx);
    if (p->list->nreleases == 0 ||
        release_index != (LONG)(p->list->nreleases - 1)) {
        p->dropped++;
        return;
    }

    rel = &p->list->releases[p->list->nreleases - 1];
    if (rel->nassets >= GF_MAX_ASSETS) {
        p->dropped++;
        return;
    }

    asset = &rel->assets[rel->nassets];
    memset(asset, 0, sizeof(*asset));
    copy_field(asset->name, name, GF_MAX_NAME);
    copy_field(asset->path, path, GF_MAX_PATH);
    asset->size = size ? (ULONG)strtoul(size, NULL, 10) : 0;
    rel->nassets++;
}

static void handle_line(struct GFParser *p, char *line)
{
    if (line[0] == '\0')
        return;

    if (line[0] == '#') {
        if (strncmp(line, "#GITFETCH", 9) == 0) {
            p->saw_header = TRUE;
        } else if (strncmp(line, "#STATUS ", 8) == 0) {
            if (strncmp(line + 8, "ERR", 3) == 0) {
                char *rest = line + 11;
                while (*rest == ' ')
                    rest++;
                p->status = atol(rest);
                while (*rest && *rest != ' ')
                    rest++;
                while (*rest == ' ')
                    rest++;
                copy_field(p->message, rest, GF_MAX_ERR);
                if (p->status == 0)
                    p->status = -1;
            }
        } else if (strncmp(line, "#REPO ", 6) == 0) {
            copy_field(p->list->repo, line + 6, GF_MAX_REPO);
        } else if (strncmp(line, "#END", 4) == 0) {
            p->saw_end = TRUE;
        }
        return;
    }

    if (line[0] == 'R' && line[1] == '\t')
        handle_release(p, line + 2);
    else if (line[0] == 'A' && line[1] == '\t')
        handle_asset(p, line + 2);
    else
        p->dropped++;
}

void gf_parser_init(struct GFParser *p, struct ReleaseList *list)
{
    memset(p, 0, sizeof(*p));
    p->list = list;
    memset(list, 0, sizeof(*list));
}

LONG gf_parser_feed(struct GFParser *p, const UBYTE *data, ULONG len)
{
    ULONG i;

    for (i = 0; i < len; i++) {
        UBYTE ch = data[i];

        if (ch == '\r')
            continue;

        if (ch == '\n') {
            if (!p->overflow) {
                p->line[p->linelen] = '\0';
                handle_line(p, p->line);
            } else {
                p->dropped++;
            }
            p->linelen = 0;
            p->overflow = FALSE;
            continue;
        }

        if (p->linelen < GF_MAX_LINE - 1)
            p->line[p->linelen++] = ch;
        else
            p->overflow = TRUE;     /* rest van de regel weggooien */
    }
    return GF_OK;
}

LONG gf_parser_finish(struct GFParser *p)
{
    /* Een laatste regel zonder afsluitende newline alsnog verwerken. */
    if (p->linelen > 0 && !p->overflow) {
        p->line[p->linelen] = '\0';
        handle_line(p, p->line);
        p->linelen = 0;
    }

    if (!p->saw_header)
        return GF_ERR_PROTO;
    if (p->status != 0)
        return GF_ERR_SERVER;
    /* Zonder #END is de transfer halverwege afgebroken. Een halve lijst
       tonen alsof het de hele is, is erger dan een foutmelding. */
    if (!p->saw_end)
        return GF_ERR_PROTO;
    return GF_OK;
}
