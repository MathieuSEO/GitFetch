/*
 * native_stub.c -- vervangt backend_native.c in de hostbuild.
 *
 * De rechtstreekse verbinding leunt op AmiSSL en bestaat alleen op de
 * Amiga. Voor de hosttests van het proxy-pad volstaat een stub die netjes
 * meldt dat deze weg hier niet beschikbaar is.
 */

#include <exec/types.h>
#include "gitfetch.h"
#include "gfbackend.h"
#include "gfbackend_impl.h"

LONG gf_native_fetch_releases(struct GFPrefs *prefs, const char *repo,
                              struct ReleaseList *list, ULONG abort_signals)
{
    (void)prefs; (void)repo; (void)list; (void)abort_signals;
    gf_set_server_message("AmiSSL is alleen op de Amiga beschikbaar");
    return GF_ERR_TLS;
}

LONG gf_native_download_asset(struct GFPrefs *prefs, const struct Asset *asset,
                              const char *destdir, ULONG abort_signals,
                              gf_progress_fn progress, APTR user)
{
    (void)prefs; (void)asset; (void)destdir; (void)abort_signals;
    (void)progress; (void)user;
    gf_set_server_message("AmiSSL is alleen op de Amiga beschikbaar");
    return GF_ERR_TLS;
}
