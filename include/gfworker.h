/*
 * gfworker.h -- netwerkwerk in een apart proces.
 *
 * Waarom een eigen proces en niet gewoon in de GUI-lus: een download van
 * een paar megabyte over een trage lijn duurt tientallen seconden. Zou de
 * GUI dat zelf doen, dan staat het venster al die tijd stil en werkt de
 * Stop-knop niet -- precies wanneer je hem nodig hebt.
 *
 * Even belangrijk: bsdsocket.library houdt per-task administratie bij en
 * mag niet door twee tasks tegelijk via dezelfde library base gebruikt
 * worden. Door al het netwerkverkeer in de worker te houden, opent alleen
 * die task de library en is dat probleem er niet.
 *
 * Terugkoppeling gaat twee kanten op:
 *   - voortgang via een enkel signaal plus twee ULONG-velden (32-bits
 *     schrijfacties zijn atomair op 68k, dus daar is geen slot voor nodig)
 *   - afronding via ReplyMsg naar de reply-port van de GUI
 *   - afbreken via SIGBREAKF_CTRL_C naar de worker-task
 */

#ifndef GFWORKER_H
#define GFWORKER_H

#include <exec/types.h>
#include <exec/ports.h>
#include <dos/dos.h>

#include "gitfetch.h"
#include "gfbackend.h"

#define GFJOB_FETCH     1
#define GFJOB_DOWNLOAD  2

struct GFJob {
    struct Message      msg;            /* moet het eerste veld zijn */

    ULONG               type;
    struct GFPrefs      prefs;
    char                repo[GF_MAX_REPO];
    struct Asset        asset;
    char                destdir[256];

    struct ReleaseList *list;           /* door de GUI gealloceerd */

    struct Task        *gui_task;
    BYTE                progress_signal; /* door de GUI gealloceerd */

    struct Task        *worker_task;    /* door de worker ingevuld */
    ULONG               last_signalled; /* alleen de worker raakt dit aan */
    volatile ULONG      sofar;
    volatile ULONG      total;
    volatile LONG       result;
    volatile BOOL       running;
};

/* Start een worker voor deze job. Geeft FALSE als het proces niet kon
   starten; dan is er ook geen ReplyMsg te verwachten. */
BOOL gf_worker_start(struct GFJob *job, struct MsgPort *replyport);

/* Vraagt de lopende worker netjes te stoppen. */
void gf_worker_abort(struct GFJob *job);

#endif /* GFWORKER_H */
