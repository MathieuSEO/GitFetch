/*
 * locale.c -- teksten in de taal van de gebruiker.
 *
 * locale.library kiest zelf de juiste catalog op grond van de
 * taalvoorkeuren in de systeeminstellingen. Ontbreekt de library (OS 2.0)
 * of de catalog, dan geeft gf_str() de ingebouwde Engelse tekst terug en
 * werkt alles gewoon door.
 */

#include <exec/types.h>
#include <libraries/locale.h>

#include <proto/exec.h>
#include <proto/locale.h>

#include "gitfetch.h"
#include "gflocale.h"

/* proto/locale.h typeert deze base als struct LocaleBase *. */
struct LocaleBase *LocaleBase = NULL;
static struct Catalog *catalog = NULL;

/*
 * De ingebouwde teksten. De volgorde moet gelijk zijn aan de opsomming
 * in gflocale.h; de index is tevens het nummer in de catalog.
 */
static const char *builtin[MSG_LAST] = {
    /* venster en menu */
    "GitFetch",
    "GitFetch - fetch releases from GitHub",
    "Project",
    "About...",
    "Quit",
    "Settings",
    "Edit...",

    /* hoofdvenster */
    "Repository",
    "_Fetch",
    "Save",
    "Forget",
    "Remove",
    "_Download",
    "_Stop",
    "Save in",

    /* kolomkoppen */
    "Saved",
    "Version",
    "Date",
    "Files",
    "File",
    "Size",

    /* keuzelijst */
    "latest",
    "last 3",
    "last 5",
    "last 10",
    "all",

    /* statusmeldingen */
    "Type a GitHub address or owner/repo and press Fetch.",
    "Pick a saved repository on the left, or type an address.",
    "Double-click, or press Fetch.",
    "Enter a repository first.",
    "Cannot read an owner/repo from this.",
    "Fetching ",
    "This repository has no releases.",
    "1 release. Pick a file on the right and press Download.",
    "%ld releases. Pick one on the left; its files appear on the right.",
    "Release %s has no files.",
    "Release %s: %ld file(s). Pick one and press Download.",
    "Pick a release first.",
    "Pick a file from the list first.",
    "Downloading ",
    "Done: %s saved in %s",
    "Aborting...",
    "Error: ",
    "Saved: ",
    "No longer saved: ",
    "That one is already in the list.",
    "Pick a saved repository first.",
    "Removed from the list.",
    " (could not write to ENVARC:)",
    "Cannot start the network process.",
    "Settings changed.",
    "%s already exists in %s.\n\nIt is %ld bytes.\nOverwrite it?",
    "Overwrite|Cancel",
    "Download cancelled; the existing file was kept.",
    "GitHub refused the request. Without a token it allows 60 requests "
        "per hour; try again later, or use the proxy.",
    "%s needs %ld KB, but only %ld KB is free on that volume.\n\n"
        "Download anyway?",

    /* instellingenvenster */
    "GitFetch settings",
    "_Connect to GitHub directly (AmiSSL)",
    "_Verify certificate",
    "Proxy address",
    "Port",
    "Fetch releases",
    "_Save",
    "_Use",
    "_Cancel",

    /* informatievenster */
    "About GitFetch",
    "GitFetch %s (25-08-2026)\n"
        "Fetch releases from GitHub on AmigaOS\n\n"
        "by Mathieu Burgerhout\n\n"
        "With thanks to:\n"
        "  Jens Maus and everyone behind AmiSSL\n"
        "  Amiga Cafe - https://amiga.cafe\n"
        "  RVO, for my Amiga revival\n"
        "  Darren Banfi, whose Mint projects started this\n"
        "  and every (vibe) coder still keeping our\n"
        "  old girlfriend alive\n\n"
        "(c) 2026 Mathieu Burgerhout\n\n"
        "Connection: %s",
    "Close",
    "directly via AmiSSL",
    "through the proxy"
};

/*
 * De tabel en de opsomming moeten even lang zijn. Zonder deze controle
 * schuift bij een vergeten regel alles een plaats op en krijg je overal
 * de verkeerde tekst -- zonder waarschuwing.
 */
extern int gf_locale_table_size_check[
    (sizeof(builtin) / sizeof(builtin[0]) == MSG_LAST) ? 1 : -1];

void gf_locale_open(void)
{
    LocaleBase = (struct LocaleBase *)OpenLibrary("locale.library", 38);
    if (!LocaleBase)
        return;             /* OS 2.0 of ouder; blijft Engels */

    catalog = OpenCatalog(NULL, (STRPTR)"GitFetch.catalog",
                          OC_BuiltInLanguage, (ULONG)"english",
                          OC_Version, 0,
                          TAG_END);
}

void gf_locale_close(void)
{
    if (catalog) {
        CloseCatalog(catalog);
        catalog = NULL;
    }
    if (LocaleBase) {
        CloseLibrary((struct Library *)LocaleBase);
        LocaleBase = NULL;
    }
}

const char *gf_str(UWORD id)
{
    if (id >= MSG_LAST)
        return "";

    if (catalog)
        return (const char *)GetCatalogStr(catalog, (LONG)id,
                                           (STRPTR)builtin[id]);

    return builtin[id];
}
