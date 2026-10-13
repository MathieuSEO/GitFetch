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
 *
 * De $STACK:-string leest alleen de Shell van OS 3.2 en OS 4; de Makefile
 * houdt hem vast met -Wl,-u,_stack_size, anders gooit --gc-sections hem
 * weg. Voor OS 3.5 en 3.9 zorgt __stack: daarmee wisselt de swapstack-
 * module van libnix bij het opstarten naar een stack van deze grootte als
 * de meegegeven stack kleiner is. Die module komt mee via -u ___stkinit.
 */
const char stack_size[] = "$STACK:65536";
unsigned long __stack = 65536;

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
    "$VER: GitFetch " GF_VERSION " (" GF_DATE ")";

/*
 * Het sjabloon voor de Shell. REPO is optioneel: zonder opent GitFetch
 * zoals altijd, met haalt hij die repository meteen op. Zo kan een
 * browser als IBrowse hem vanuit het contextmenu van een link starten.
 */
#define TEMPLATE  "REPO"

int main(int argc, char **argv)
{
    struct GFPrefs prefs;
    struct WBStartup *wbs = NULL;
    struct RDArgs *rda = NULL;
    LONG args[1] = { 0 };
    LONG rc;

    /* Vanaf Workbench is argc nul en wijst argv naar de WBStartup. */
    if (argc == 0)
        wbs = (struct WBStartup *)argv;

    /* Eerst de instellingen: die bepalen ook de taal van de catalog. */
    gf_prefs_load(&prefs, wbs);
    gf_locale_open(prefs.language);

    if (!wbs) {
        rda = ReadArgs((STRPTR)TEMPLATE, args, NULL);
        if (!rda) {
            PrintFault(IoErr(), (STRPTR)"GitFetch");
            gf_locale_close();
            return RETURN_ERROR;
        }
    }

    /* Geen controle op een ingestelde proxy: de standaard is een directe
       verbinding, en gf_prefs_defaults zet host toch al op een waarde.
       Wie de proxy kiest zonder adres krijgt een nette fout bij het
       ophalen zelf. */

    rc = gf_gui_run(&prefs, (const char *)args[0]);

    if (rda)
        FreeArgs(rda);
    gf_locale_close();
    return rc;
}
