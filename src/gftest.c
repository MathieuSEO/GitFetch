/*
 * gftest.c -- CLI-testtool voor de GitFetch-onderdelen.
 *
 * Bewust los van de GUI: hiermee zijn de netwerklaag, de parser en de
 * download te testen voordat er ook maar een venster bestaat. Ctrl-C is
 * aangesloten op hetzelfde abort-mechanisme dat de GUI straks voor zijn
 * Stop-knop gebruikt.
 *
 *   gftest [-h host] [-p poort] [-d map] owner/repo [assetnaam]
 */

#include <exec/types.h>
#include <exec/memory.h>
#include <dos/dos.h>

#include <proto/exec.h>
#include <proto/dos.h>

#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#include "gitfetch.h"
#include "gfnet.h"
#include "gfbackend.h"
#include "gfurl.h"


/*
 * TLS vraagt veel meer stack dan een gemiddelde Shell meegeeft (vaak 4 KB).
 * AmigaDOS leest deze string uit de binary en past de stack aan.
 */
const char stack_size[] = "$STACK:65536";

static const char *size_text(ULONG bytes, char *buf)
{
    if (bytes >= 1024UL * 1024UL)
        sprintf(buf, "%lu.%lu MB", bytes / (1024UL * 1024UL),
                ((bytes % (1024UL * 1024UL)) * 10UL) / (1024UL * 1024UL));
    else if (bytes >= 1024UL)
        sprintf(buf, "%lu KB", bytes / 1024UL);
    else
        sprintf(buf, "%lu B", bytes);
    return buf;
}

static LONG show_progress(APTR user, ULONG sofar, ULONG total)
{
    static ULONG last_pct = 999;
    ULONG pct;

    (void)user;

    if (SetSignal(0, 0) & SIGBREAKF_CTRL_C) {
        printf("\nAborted.\n");
        return GF_ERR_ABORTED;
    }

    if (total) {
        /* Overflow-veilig zonder floating point: sofar * 100 loopt boven
           42 MB over in 32 bits, dus daarboven eerst delen. */
        if (total >= 42949672UL)
            pct = sofar / (total / 100UL);
        else
            pct = (sofar * 100UL) / total;
        if (pct != last_pct) {
            printf("\r  %lu%%  (%lu / %lu bytes)   ", pct, sofar, total);
            fflush(stdout);
            last_pct = pct;
        }
    } else {
        printf("\r  %lu bytes   ", sofar);
        fflush(stdout);
    }
    return GF_OK;
}

static void print_list(struct ReleaseList *list)
{
    UWORD r, a;
    char sizebuf[32];

    printf("Repository: %s\n", list->repo);
    printf("%ld release(s)\n\n", (long)list->nreleases);

    for (r = 0; r < list->nreleases; r++) {
        struct Release *rel = &list->releases[r];

        printf("[%2ld] %-20s %-12s %s%s\n", (long)r, rel->tag, rel->date,
               rel->title, rel->prerelease ? "  (prerelease)" : "");

        for (a = 0; a < rel->nassets; a++) {
            struct Asset *asset = &rel->assets[a];
            printf("       %-40s %10s\n", asset->name,
                   size_text(asset->size, sizebuf));
        }
        if (rel->nassets == 0)
            printf("       (no files)\n");
        printf("\n");
    }
}

int main(int argc, char **argv)
{
    struct GFPrefs prefs;
    struct ReleaseList *list = NULL;
    char repo[GF_MAX_REPO];
    const char *want_asset = NULL;
    const char *input = NULL;
    LONG err;
    int i;

    /* Eerst ENV:GitFetch/... , daarna pas de commandline -- zo werkt
       SetEnv net als bij het GUI-programma, maar kun je het per aanroep
       nog overrulen. */
    gf_prefs_load(&prefs, NULL);

    for (i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-h") == 0 && i + 1 < argc) {
            strncpy(prefs.host, argv[++i], sizeof(prefs.host) - 1);
        } else if (strcmp(argv[i], "-p") == 0 && i + 1 < argc) {
            prefs.port = (UWORD)atoi(argv[++i]);
        } else if (strcmp(argv[i], "-n") == 0) {
            prefs.backend = GF_BACKEND_NATIVE;
        } else if (strcmp(argv[i], "-k") == 0) {
            prefs.verify_cert = 0;
        } else if (strcmp(argv[i], "-x") == 0) {
            prefs.backend = GF_BACKEND_PROXY;
        } else if (strcmp(argv[i], "-m") == 0 && i + 1 < argc) {
            prefs.max_releases = (UWORD)atoi(argv[++i]);
        } else if (strcmp(argv[i], "-d") == 0 && i + 1 < argc) {
            strncpy(prefs.destdir, argv[++i], sizeof(prefs.destdir) - 1);
        } else if (!input) {
            input = argv[i];
        } else if (!want_asset) {
            want_asset = argv[i];
        }
    }

    if (!input) {
        printf("Usage: gftest [-h host] [-p port] [-d dir] [-m count]\n"
               "              [-n | -x] owner/repo [assetname]\n"
               "  -n  connect to GitHub directly via AmiSSL\n"
               "  -x  go through the proxy (default)\n"
               "  -k  skip certificate check (for testing only)\n");
        return RETURN_WARN;
    }

    if (gf_normalize_repo(input, repo, sizeof(repo)) != GF_OK) {
        printf("Cannot read an owner/repo from: %s\n", input);
        return RETURN_ERROR;
    }

    err = gf_net_open();
    if (err != GF_OK) {
        printf("Error: %s\n", gf_strerror(err));
        return RETURN_FAIL;
    }

    list = AllocVec(sizeof(struct ReleaseList), MEMF_ANY | MEMF_CLEAR);
    if (!list) {
        gf_net_close();
        printf("Out of memory (%ld bytes needed)\n",
               (long)sizeof(struct ReleaseList));
        return RETURN_FAIL;
    }

    if (prefs.backend == GF_BACKEND_NATIVE)
        printf("Fetching %s directly from GitHub (AmiSSL) ...\n", repo);
    else
        printf("Fetching %s through %s:%ld ...\n", repo, prefs.host,
               (long)prefs.port);
    fflush(stdout);
    err = gf_fetch_releases(&prefs, repo, list, SIGBREAKF_CTRL_C);

    if (err != GF_OK) {
        printf("Error: %s", gf_strerror(err));
        if (err == GF_ERR_HTTP)
            printf(" (HTTP %ld)", gf_http_status());
        if (gf_last_server_message()[0])
            printf(": %s", gf_last_server_message());
        printf("\n");
        FreeVec(list);
        gf_net_close();
        return RETURN_ERROR;
    }

    if (!want_asset) {
        print_list(list);
    } else {
        struct Asset *found = NULL;
        UWORD r, a;

        for (r = 0; r < list->nreleases && !found; r++)
            for (a = 0; a < list->releases[r].nassets; a++)
                if (stricmp(list->releases[r].assets[a].name, want_asset) == 0) {
                    found = &list->releases[r].assets[a];
                    break;
                }

        if (!found) {
            printf("No asset named '%s' was found.\n", want_asset);
            FreeVec(list);
            gf_net_close();
            return RETURN_WARN;
        }

        printf("Downloading %s to %s ...\n", found->name, prefs.destdir);
        err = gf_download_asset(&prefs, found, prefs.destdir,
                                SIGBREAKF_CTRL_C, show_progress, NULL);
        printf("\n%s\n", err == GF_OK ? "Done." : gf_strerror(err));
    }

    FreeVec(list);
    gf_net_close();
    return err == GF_OK ? RETURN_OK : RETURN_ERROR;
}
