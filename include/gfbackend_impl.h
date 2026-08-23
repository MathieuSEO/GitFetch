/*
 * gfbackend_impl.h -- de twee backends achter de dispatch in backend.c.
 * Niet bedoeld voor de GUI; die gebruikt gfbackend.h.
 */

#ifndef GFBACKEND_IMPL_H
#define GFBACKEND_IMPL_H

#include "gfbackend.h"

LONG gf_proxy_fetch_releases(struct GFPrefs *prefs, const char *repo,
                             struct ReleaseList *list, ULONG abort_signals);
LONG gf_proxy_download_asset(struct GFPrefs *prefs, const struct Asset *asset,
                             const char *destdir, ULONG abort_signals,
                             gf_progress_fn progress, APTR user);

LONG gf_native_fetch_releases(struct GFPrefs *prefs, const char *repo,
                              struct ReleaseList *list, ULONG abort_signals);
LONG gf_native_download_asset(struct GFPrefs *prefs, const struct Asset *asset,
                              const char *destdir, ULONG abort_signals,
                              gf_progress_fn progress, APTR user);

/* Door beide backends gevuld, voor foutmeldingen in de GUI. */
void gf_set_server_message(const char *msg);

#endif /* GFBACKEND_IMPL_H */
