/*
 * prefs.c -- instellingen uit ToolTypes en omgevingsvariabelen.
 *
 * Amiga-gebruik: ToolTypes in het icoon zijn de gebruikelijke plek, maar
 * vanuit de Shell is ENV: handiger. Beide worden ondersteund, met ENV:
 * als sterkste omdat je dat per sessie kunt overrulen.
 */

#include <exec/types.h>
#include <dos/dos.h>
#include <workbench/startup.h>
#include <workbench/workbench.h>

#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/icon.h>

#include <string.h>
#include <stdlib.h>

#include "gitfetch.h"
#include "gfbackend.h"

/* Kopieert met afkapping en garandeert een afsluitende NUL. Bewust met
   memcpy in plaats van strncpy: dat laatste laat de compiler denken dat de
   NUL zoek kan raken, terwijl we hem er hier juist expliciet in zetten. */
static void set_string(char *dest, LONG size, const char *value)
{
    LONG len;

    if (!value || !*value || size < 1)
        return;

    len = (LONG)strlen(value);
    if (len > size - 1)
        len = size - 1;
    memcpy(dest, value, len);
    dest[len] = '\0';
}

/* Leest een variabele uit ENV:GitFetch/<naam>. */
static BOOL read_env(const char *name, char *buf, LONG size)
{
    char var[64];

    strcpy(var, "GitFetch/");
    strncat(var, name, sizeof(var) - strlen(var) - 1);

    if (GetVar((STRPTR)var, (STRPTR)buf, size, GVF_GLOBAL_ONLY) > 0)
        return TRUE;
    return FALSE;
}

/* icon.library wordt alleen gebruikt om ToolTypes te lezen bij een start
   vanaf Workbench; hier openen en sluiten houdt dat lokaal. */
struct Library *IconBase = NULL;

static void apply_tooltypes(struct GFPrefs *prefs, struct WBStartup *wbs)
{
    struct DiskObject *icon;
    STRPTR *tt, value;

    if (!wbs || wbs->sm_NumArgs < 1)
        return;

    IconBase = OpenLibrary("icon.library", 37);
    if (!IconBase)
        return;

    icon = GetDiskObject((STRPTR)wbs->sm_ArgList[0].wa_Name);
    if (!icon) {
        CloseLibrary(IconBase);
        IconBase = NULL;
        return;
    }

    tt = (STRPTR *)icon->do_ToolTypes;

    if ((value = FindToolType(tt, (STRPTR)"PROXYHOST")) != NULL)
        set_string(prefs->host, sizeof(prefs->host), (const char *)value);
    if ((value = FindToolType(tt, (STRPTR)"PROXYPORT")) != NULL)
        prefs->port = (UWORD)atoi((const char *)value);
    if ((value = FindToolType(tt, (STRPTR)"DESTDIR")) != NULL)
        set_string(prefs->destdir, sizeof(prefs->destdir), (const char *)value);
    if ((value = FindToolType(tt, (STRPTR)"MAXRELEASES")) != NULL)
        prefs->max_releases = (UWORD)atoi((const char *)value);
    if ((value = FindToolType(tt, (STRPTR)"BACKEND")) != NULL)
        prefs->backend = (UWORD)(strnicmp((const char *)value, "native", 6) == 0
                                 ? GF_BACKEND_NATIVE : GF_BACKEND_PROXY);

    FreeDiskObject(icon);
    CloseLibrary(IconBase);
    IconBase = NULL;
}

void gf_prefs_load(struct GFPrefs *prefs, struct WBStartup *wbs)  /* wbs mag NULL zijn */
{
    char buf[256];

    gf_prefs_defaults(prefs);

    apply_tooltypes(prefs, wbs);

    /* ENV: wint van ToolTypes, zodat je vanuit de Shell iets anders kunt
       proberen zonder het icoon aan te passen. */
    if (read_env("ProxyHost", buf, sizeof(buf)))
        set_string(prefs->host, sizeof(prefs->host), buf);
    if (read_env("ProxyPort", buf, sizeof(buf)))
        prefs->port = (UWORD)atoi(buf);
    if (read_env("DestDir", buf, sizeof(buf)))
        set_string(prefs->destdir, sizeof(prefs->destdir), buf);
    if (read_env("MaxReleases", buf, sizeof(buf)))
        prefs->max_releases = (UWORD)atoi(buf);
    /* Vier getallen in een variabele: links, boven, breedte, hoogte. Dat
       scheelt drie bestanden in ENVARC: ten opzichte van elk apart. */
    if (read_env("Window", buf, sizeof(buf))) {
        const char *p = buf;
        WORD v[4];
        LONG i;

        for (i = 0; i < 4; i++) {
            while (*p == ' ')
                p++;
            v[i] = (WORD)atoi(p);
            while (*p && *p != ' ')
                p++;
        }
        if (v[2] > 100 && v[3] > 60) {      /* onzin negeren */
            prefs->win_left   = v[0];
            prefs->win_top    = v[1];
            prefs->win_width  = v[2];
            prefs->win_height = v[3];
        }
    }
    if (read_env("Backend", buf, sizeof(buf)))
        prefs->backend = (UWORD)(strnicmp(buf, "native", 6) == 0
                                 ? GF_BACKEND_NATIVE : GF_BACKEND_PROXY);
    if (read_env("VerifyCert", buf, sizeof(buf)))
        prefs->verify_cert = (UWORD)(atoi(buf) ? 1 : 0);

    if (prefs->port == 0)
        prefs->port = 80;
    if (prefs->max_releases == 0 || prefs->max_releases > GF_MAX_RELEASES)
        prefs->max_releases = 1;
}
