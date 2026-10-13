/*
 * assets.c -- keuze en naamgeving van de bestanden in een release.
 */

#include <exec/types.h>
#include <string.h>

#include "gitfetch.h"
#include "gfassets.h"

#define SOURCE_SUFFIX  "-source.zip"

/*
 * De formaten waar een Amiga-gebruiker naar zoekt. .zip staat erbij
 * omdat steeds meer Amiga-software zo verschijnt; dat Windows- en
 * Linux-builds ook vaak .zip zijn, nemen we op de koop toe.
 */
static const char *archive_suffixes[] = {
    ".lha", ".lzh", ".lzx", ".zip", ".adf", ".adz", ".dms", ".hdf",
    ".7z", ".tar.gz", ".tgz", NULL
};

static char lower(char c)
{
    return (c >= 'A' && c <= 'Z') ? (char)(c - 'A' + 'a') : c;
}

/* Eindigt 'name' op 'suffix', zonder op hoofdletters te letten? */
static BOOL ends_with(const char *name, const char *suffix)
{
    size_t n = strlen(name), s = strlen(suffix), i;

    if (s > n)
        return FALSE;
    name += n - s;
    for (i = 0; i < s; i++)
        if (lower(name[i]) != lower(suffix[i]))
            return FALSE;
    return TRUE;
}

UWORD gf_asset_rank(const char *name)
{
    UWORD i;

    for (i = 0; archive_suffixes[i]; i++)
        if (ends_with(name, archive_suffixes[i]))
            return GF_RANK_ARCHIVE;
    return GF_RANK_OTHER;
}

BOOL gf_asset_is_source(const struct Asset *asset)
{
    return (BOOL)(asset->size == 0 && ends_with(asset->name, SOURCE_SUFFIX));
}

void gf_source_name(const char *repo, const char *tag, char *out)
{
    const char *name = strchr(repo, '/');
    size_t room = GF_MAX_NAME - 1 - strlen(SOURCE_SUFFIX);
    size_t len;

    name = name ? name + 1 : repo;

    /* Eerst de repo-naam, dan de tag; bij een lange combinatie gaat de
       tag voor, want die onderscheidt de ene zip van de andere. */
    len = strlen(tag);
    if (len > room)
        len = room;
    out[0] = '\0';
    if (*name && len + 1 < room) {
        size_t nlen = strlen(name);
        if (nlen > room - len - 1)
            nlen = room - len - 1;
        memcpy(out, name, nlen);
        out[nlen] = '-';
        out[nlen + 1] = '\0';
    }
    strncat(out, tag, len);
    strcat(out, SOURCE_SUFFIX);
}

void gf_asset_offer(struct Release *rel, const struct Asset *asset)
{
    UWORD rank = gf_asset_rank(asset->name);
    WORD i, victim = -1;

    rel->nskipped++;

    /* Achteraan beginnen: van gelijke rang valt het laatste af, zodat
       wat de auteur bovenaan zette blijft staan. */
    for (i = (WORD)rel->nassets - 1; i >= 0; i--) {
        if (gf_asset_rank(rel->assets[i].name) < rank) {
            victim = i;
            break;
        }
    }
    if (victim < 0)
        return;

    memmove(&rel->assets[victim], &rel->assets[victim + 1],
            (rel->nassets - 1 - victim) * sizeof(struct Asset));
    rel->assets[rel->nassets - 1] = *asset;
}

UWORD gf_release_files(const struct Release *rel)
{
    if (rel->nassets && gf_asset_is_source(&rel->assets[rel->nassets - 1]))
        return (UWORD)(rel->nassets - 1);
    return rel->nassets;
}
