/*
 * main.c -- opstarten van GitFetch.
 *
 * Werkt zowel vanuit de Shell als vanaf Workbench. Bij Workbench-start
 * komt de WBStartup binnen zodat de ToolTypes van het icoon gelezen
 * kunnen worden.
 */

#include <exec/types.h>
#include <dos/dos.h>
#include <workbench/startup.h>

#include <proto/exec.h>
#include <proto/dos.h>

#include <string.h>

#include "gitfetch.h"
#include "gfbackend.h"
#include "gfgui.h"
#include "gflocale.h"


/*
 * TLS vraagt veel meer stack dan een gemiddelde Shell meegeeft (vaak 4 KB).
 * AmigaDOS leest deze string uit de binary en past de stack aan.
 */
const char stack_size[] = "$STACK:65536";

/*
 * De $VER:-string hoort in elke Amiga-binary; het version-commando leest
 * hem hieruit.
 *
 * Niets in de code verwijst naar deze variabele, dus --gc-sections zou
 * hem weggooien en dan verdwijnt de string stilletjes uit de binary. De
 * Makefile houdt hem vast met -Wl,-u,_version_tag; het used-attribuut
 * alleen bleek daarvoor niet genoeg met deze binutils.
 */
const char version_tag[] =
    "$VER: GitFetch " GF_VERSION " (25.8.2026)";

int main(int argc, char **argv)
{
    struct GFPrefs prefs;
    struct WBStartup *wbs = NULL;
    LONG rc;

    /* Vanaf Workbench is argc nul en wijst argv naar de WBStartup. */
    if (argc == 0)
        wbs = (struct WBStartup *)argv;

    gf_locale_open();
    gf_prefs_load(&prefs, wbs);

    /* Geen controle op een ingestelde proxy: de standaard is een directe
       verbinding, en gf_prefs_defaults zet host toch al op een waarde.
       Wie de proxy kiest zonder adres krijgt een nette fout bij het
       ophalen zelf. */

    rc = gf_gui_run(&prefs);

    gf_locale_close();
    return rc;
}
