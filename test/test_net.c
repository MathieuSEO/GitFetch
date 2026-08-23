/*
 * test_net.c -- end-to-end test van de netwerkketen tegen een draaiende
 * proxy, met de POSIX-shim onder net.c en backend_proxy.c.
 *
 * Dit test wat de hosttests niet raken: het opbouwen van het HTTP-verzoek,
 * het scheiden van headers en body, de sink-keten naar de parser, en het
 * streamend wegschrijven van een download.
 *
 *   test_net <host> <poort> <owner/repo> [assetnaam] [doelmap]
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "gitfetch.h"
#include "gfnet.h"
#include "gfbackend.h"
#include "gfurl.h"

static LONG progress(APTR user, ULONG sofar, ULONG total)
{
    (void)user;
    if (total)
        printf("\r  %lu / %lu bytes", (unsigned long)sofar,
               (unsigned long)total);
    else
        printf("\r  %lu bytes", (unsigned long)sofar);
    fflush(stdout);
    return GF_OK;
}

int main(int argc, char **argv)
{
    struct GFPrefs prefs;
    struct ReleaseList *list;
    char repo[GF_MAX_REPO];
    LONG err;
    UWORD r, a;

    if (argc < 4) {
        printf("Gebruik: test_net <host> <poort> <owner/repo> "
               "[assetnaam] [doelmap]\n");
        return 2;
    }

    gf_prefs_defaults(&prefs);
    strncpy(prefs.host, argv[1], sizeof(prefs.host) - 1);
    prefs.port = (UWORD)atoi(argv[2]);
    if (argc > 5)
        strncpy(prefs.destdir, argv[5], sizeof(prefs.destdir) - 1);

    if (gf_normalize_repo(argv[3], repo, sizeof(repo)) != GF_OK) {
        printf("Onbegrijpelijke repo: %s\n", argv[3]);
        return 1;
    }

    if (gf_net_open() != GF_OK) {
        printf("Kan het netwerk niet openen\n");
        return 1;
    }

    list = calloc(1, sizeof(struct ReleaseList));
    printf("Ophalen van %s via %s:%d\n", repo, prefs.host, (int)prefs.port);

    err = gf_fetch_releases(&prefs, repo, list, 0);
    if (err != GF_OK) {
        printf("FOUT: %s", gf_strerror(err));
        if (err == GF_ERR_HTTP)
            printf(" (HTTP %ld)", (long)gf_http_status());
        if (err == GF_ERR_SERVER && gf_last_server_message()[0])
            printf(": %s", gf_last_server_message());
        printf("\n");
        return 1;
    }

    printf("repo=%s, %d release(s)\n", list->repo, (int)list->nreleases);
    for (r = 0; r < list->nreleases && r < 3; r++) {
        struct Release *rel = &list->releases[r];
        printf("  [%d] %-14s %-12s %s%s\n", (int)r, rel->tag, rel->date,
               rel->title, rel->prerelease ? " (prerelease)" : "");
        for (a = 0; a < rel->nassets; a++)
            printf("        %-34s %10lu bytes\n", rel->assets[a].name,
                   (unsigned long)rel->assets[a].size);
    }

    if (argc > 4) {
        struct Asset *found = NULL;

        for (r = 0; r < list->nreleases && !found; r++)
            for (a = 0; a < list->releases[r].nassets; a++)
                if (strcmp(list->releases[r].assets[a].name, argv[4]) == 0) {
                    found = &list->releases[r].assets[a];
                    break;
                }

        if (!found) {
            printf("Asset '%s' niet gevonden\n", argv[4]);
            return 1;
        }

        printf("Downloaden van %s (%lu bytes) naar %s\n", found->name,
               (unsigned long)found->size, prefs.destdir);
        err = gf_download_asset(&prefs, found, prefs.destdir, 0,
                                progress, NULL);
        printf("\n%s\n", err == GF_OK ? "klaar" : gf_strerror(err));
        if (err != GF_OK)
            return 1;
    }

    gf_net_close();
    free(list);
    return 0;
}
