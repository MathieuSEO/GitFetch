/*
 * backend.c -- kiest tussen de proxy en de rechtstreekse verbinding.
 *
 * De GUI roept alleen gf_fetch_releases() en gf_download_asset() aan en
 * merkt niets van de keuze. Dat maakt het ook mogelijk om per repository
 * terug te vallen: mislukt de directe verbinding, dan is de proxy nog een
 * optie zonder dat er iets aan de bovenlaag verandert.
 */

#include <exec/types.h>
#include <string.h>

#include "gitfetch.h"
#include "gfbackend.h"
#include "gfbackend_impl.h"

static char server_message[GF_MAX_ERR];

void gf_set_server_message(const char *msg)
{
    if (!msg) {
        server_message[0] = '\0';
        return;
    }
    strncpy(server_message, msg, sizeof(server_message) - 1);
    server_message[sizeof(server_message) - 1] = '\0';
}

const char *gf_last_server_message(void)
{
    return server_message;
}

void gf_prefs_defaults(struct GFPrefs *prefs)
{
    memset(prefs, 0, sizeof(*prefs));
    strcpy(prefs->host, "localhost");
    prefs->port = 8080;
    strcpy(prefs->destdir, "RAM:");
    /* Standaard alleen de nieuwste: dat is meestal wat je zoekt, en het
       scheelt bij de rechtstreekse verbinding ruim 200 KB aan verkeer. */
    prefs->max_releases = 1;
    /* Rechtstreeks als standaard: dan werkt het zonder dat er ergens een
       proxy van de gebruiker hoeft te draaien. */
    prefs->backend = GF_BACKEND_NATIVE;
    prefs->verify_cert = 1;
}

LONG gf_fetch_releases(struct GFPrefs *prefs, const char *repo,
                       struct ReleaseList *list, ULONG abort_signals)
{
    gf_set_server_message(NULL);

    if (prefs->backend == GF_BACKEND_NATIVE)
        return gf_native_fetch_releases(prefs, repo, list, abort_signals);

    return gf_proxy_fetch_releases(prefs, repo, list, abort_signals);
}

LONG gf_download_asset(struct GFPrefs *prefs, const struct Asset *asset,
                       const char *destdir, ULONG abort_signals,
                       gf_progress_fn progress, APTR user)
{
    gf_set_server_message(NULL);

    if (prefs->backend == GF_BACKEND_NATIVE)
        return gf_native_download_asset(prefs, asset, destdir, abort_signals,
                                        progress, user);

    return gf_proxy_download_asset(prefs, asset, destdir, abort_signals,
                                   progress, user);
}
