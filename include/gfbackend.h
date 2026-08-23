/*
 * gfbackend.h -- abstractie tussen de GUI en de manier waarop release-info
 * binnenkomt.
 *
 * Fase 1 (backend_proxy.c) praat plain HTTP met een eigen proxy.
 * Fase 2 (backend_native.c) praat via AmiSSL rechtstreeks met api.github.com.
 * De GUI kent alleen deze twee functies en hoeft dus niet te veranderen.
 */

#ifndef GFBACKEND_H
#define GFBACKEND_H

#include <exec/types.h>
#include "gitfetch.h"

#define GF_BACKEND_PROXY   0    /* plain HTTP via een eigen proxy */
#define GF_BACKEND_NATIVE  1    /* rechtstreeks naar GitHub via AmiSSL */

struct GFPrefs {
    char  host[128];        /* proxy-hostnaam of IP */
    UWORD port;
    char  destdir[256];     /* standaard downloadmap */
    UWORD max_releases;
    UWORD backend;          /* GF_BACKEND_* */
    UWORD verify_cert;      /* certificaat van GitHub controleren (native) */

    /* Waar het venster stond toen je het de vorige keer sloot. Nul
       betekent: nog niets onthouden, gebruik het standaardformaat. */
    WORD  win_left, win_top;
    WORD  win_width, win_height;
};

/* Voortgang tijdens een download. Geef GF_OK terug om door te gaan. */
typedef LONG (*gf_progress_fn)(APTR user, ULONG sofar, ULONG total);

void gf_prefs_defaults(struct GFPrefs *prefs);

/*
 * Laadt de instellingen: eerst de standaardwaarden, dan de ToolTypes van
 * het icoon (alleen bij een start vanaf Workbench), dan ENV:GitFetch/...
 * ENV wint, zodat je vanuit de Shell iets kunt proberen zonder het icoon
 * aan te passen. 'wbs' mag NULL zijn bij een start vanuit de Shell.
 */
struct WBStartup;
void gf_prefs_load(struct GFPrefs *prefs, struct WBStartup *wbs);

/*
 * Deze twee kiezen zelf de backend op grond van prefs->backend. De
 * implementaties zitten in backend_proxy.c en backend_native.c; de GUI
 * kent alleen deze functies.
 */
LONG gf_fetch_releases(struct GFPrefs *prefs, const char *repo,
                       struct ReleaseList *list, ULONG abort_signals);

/*
 * Bij GF_ERR_SERVER: de boodschap die de proxy zelf meegaf, bijvoorbeeld
 * "Repository niet gevonden". Veel bruikbaarder dan de algemene tekst bij
 * de foutcode. Leeg als er geen melding was.
 */
const char *gf_last_server_message(void);

/* Aantal protocolregels dat de parser niet kon plaatsen. Normaal nul; een
   waarde erboven wijst op een formaat- of buffergrensprobleem. */
LONG gf_last_dropped_lines(void);

/* Het pad van het laatste verzoek, inclusief de max-parameter. */
const char *gf_last_request_path(void);

LONG gf_download_asset(struct GFPrefs *prefs, const struct Asset *asset,
                       const char *destdir, ULONG abort_signals,
                       gf_progress_fn progress, APTR user);

#endif /* GFBACKEND_H */
