/*
 * bookmarks.c -- de lijst met onthouden repositories.
 *
 * Opslag is een gewoon tekstbestand in ENVARC:, een regel per repository.
 * Dat is met de hand te bewerken en te kopieren, wat prettiger is dan een
 * eigen binair formaat voor een lijstje namen.
 */

#include <exec/types.h>
#include <dos/dos.h>

#include <proto/exec.h>
#include <proto/dos.h>

#include <string.h>

#include "gitfetch.h"
#include "gfbookmarks.h"

/* Kopieren met afkapping en een gegarandeerde afsluitende NUL; met memcpy
   in plaats van strncpy, anders denkt de compiler dat de NUL zoek raakt. */
static void set_repo(char *dest, const char *src)
{
    LONG len = (LONG)strlen(src);

    if (len > GF_MAX_REPO - 1)
        len = GF_MAX_REPO - 1;
    memcpy(dest, src, len);
    dest[len] = '\0';
}

#define BM_PATH_ARC  "ENVARC:GitFetch/Bookmarks"
#define BM_PATH_ENV  "ENV:GitFetch/Bookmarks"

static void trim(char *s)
{
    LONG len = (LONG)strlen(s);

    while (len > 0 && (s[len - 1] == '\n' || s[len - 1] == '\r' ||
                       s[len - 1] == ' ' || s[len - 1] == '\t'))
        s[--len] = '\0';
}

static BOOL load_from(struct Bookmarks *bm, const char *path)
{
    BPTR file;
    char line[GF_MAX_REPO];

    file = Open((STRPTR)path, MODE_OLDFILE);
    if (!file)
        return FALSE;

    while (bm->count < GF_MAX_BOOKMARKS &&
           FGets(file, (STRPTR)line, sizeof(line))) {
        trim(line);
        if (line[0] && line[0] != '#') {
            set_repo(bm->repo[bm->count], line);
            bm->count++;
        }
    }

    Close(file);
    return TRUE;
}

void gf_bookmarks_load(struct Bookmarks *bm)
{
    memset(bm, 0, sizeof(*bm));

    /* ENV: eerst: daar staat wat deze sessie is gewijzigd. Bestaat dat
       niet, dan de bewaarde lijst uit ENVARC:. */
    if (!load_from(bm, BM_PATH_ENV))
        load_from(bm, BM_PATH_ARC);

    /*
     * Bij een verse installatie staat de eigen repository alvast klaar.
     * Dat maakt meteen duidelijk hoe de lijst werkt, en het is de manier
     * waarop je aan een nieuwere GitFetch komt. Wie hem niet wil, haalt
     * hem met Remove weg; hij komt dan niet terug, want vanaf dat moment
     * is er een bewaarde lijst.
     */
    if (bm->count == 0)
        set_repo(bm->repo[bm->count++], GF_HOME_REPO);
}

static BOOL save_to(struct Bookmarks *bm, const char *path)
{
    BPTR file;
    UWORD i;

    file = Open((STRPTR)path, MODE_NEWFILE);
    if (!file)
        return FALSE;

    for (i = 0; i < bm->count; i++) {
        if (FPuts(file, (STRPTR)bm->repo[i]) != 0 ||
            FPuts(file, (STRPTR)"\n") != 0) {
            Close(file);
            return FALSE;
        }
    }

    Close(file);
    return TRUE;
}

BOOL gf_bookmarks_save(struct Bookmarks *bm)
{
    BOOL ok;

    /* ENV: is de werkkopie, ENVARC: overleeft een herstart. Lukt alleen
       de eerste, dan is de lijst deze sessie nog bruikbaar. */
    ok = save_to(bm, BM_PATH_ENV);
    if (!save_to(bm, BM_PATH_ARC))
        return ok;

    return TRUE;
}

LONG gf_bookmarks_find(struct Bookmarks *bm, const char *repo)
{
    UWORD i;

    for (i = 0; i < bm->count; i++)
        if (stricmp(bm->repo[i], repo) == 0)
            return (LONG)i;

    return -1;
}

BOOL gf_bookmarks_add(struct Bookmarks *bm, const char *repo)
{
    UWORD i;

    if (!repo || !repo[0])
        return FALSE;
    if (gf_bookmarks_find(bm, repo) >= 0)
        return FALSE;           /* staat er al in */

    /* Nieuwste bovenaan: wat je net gebruikte wil je zo weer bij de hand. */
    if (bm->count < GF_MAX_BOOKMARKS)
        bm->count++;

    for (i = (UWORD)(bm->count - 1); i > 0; i--)
        memcpy(bm->repo[i], bm->repo[i - 1], GF_MAX_REPO);

    set_repo(bm->repo[0], repo);
    return TRUE;
}

BOOL gf_bookmarks_remove(struct Bookmarks *bm, UWORD index)
{
    UWORD i;

    if (index >= bm->count)
        return FALSE;

    for (i = index; i + 1 < bm->count; i++)
        memcpy(bm->repo[i], bm->repo[i + 1], GF_MAX_REPO);

    bm->count--;
    return TRUE;
}
