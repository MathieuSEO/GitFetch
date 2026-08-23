/*
 * test_json.c -- voert een JSON-bestand aan de parser en print het
 * resultaat in een vorm die met een referentie te vergelijken is.
 *
 *   test_json <bestand> [blokgrootte]
 *
 * De blokgrootte is er om te controleren dat het niet uitmaakt waar de
 * grens tussen twee ontvangen blokken valt -- juist daar gaan streaming-
 * parsers stuk.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "gitfetch.h"
#include "gfjson.h"

int main(int argc, char **argv)
{
    FILE *f;
    struct GFJson *j;
    struct ReleaseList *list;
    unsigned char buf[65536];
    size_t chunk = 4096, n;
    UWORD r, a;

    if (argc < 2) {
        printf("Gebruik: test_json <bestand> [blokgrootte]\n");
        return 2;
    }
    if (argc > 2)
        chunk = (size_t)atoi(argv[2]);
    if (chunk == 0 || chunk > sizeof(buf))
        chunk = sizeof(buf);

    f = fopen(argv[1], "rb");
    if (!f) { printf("kan %s niet openen\n", argv[1]); return 1; }

    j = malloc(sizeof(struct GFJson));
    list = malloc(sizeof(struct ReleaseList));
    gf_json_init(j, list);

    while ((n = fread(buf, 1, chunk, f)) > 0)
        gf_json_feed(j, buf, (ULONG)n);
    gf_json_finish(j);
    fclose(f);

    printf("releases=%u dropped=%ld\n", (unsigned)list->nreleases,
           (long)j->dropped);
    for (r = 0; r < list->nreleases; r++) {
        struct Release *rel = &list->releases[r];
        printf("R|%s|%s|%u|%s\n", rel->tag, rel->date,
               (unsigned)rel->prerelease, rel->title);
        for (a = 0; a < rel->nassets; a++)
            printf("A|%s|%lu|%s\n", rel->assets[a].name,
                   (unsigned long)rel->assets[a].size, rel->assets[a].path);
    }

    free(list); free(j);
    return 0;
}
