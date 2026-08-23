/*
 * worker.c -- het proces dat de netwerkkant afhandelt.
 */

#include <exec/types.h>
#include <exec/memory.h>
#include <dos/dos.h>
#include <dos/dostags.h>

#include <proto/exec.h>
#include <proto/dos.h>

#include <string.h>

#include "gitfetch.h"
#include "gfnet.h"
#include "gfbackend.h"
#include "gfworker.h"

/* Niet elke ontvangen buffer een signaal sturen: bij een snelle
   verbinding zijn dat er honderden per seconde en dan besteedt de GUI
   meer tijd aan hertekenen dan aan het werk. */
#define PROGRESS_STEP  (16UL * 1024UL)

static LONG worker_progress(APTR user, ULONG sofar, ULONG total)
{
    struct GFJob *job = (struct GFJob *)user;

    if (SetSignal(0, 0) & SIGBREAKF_CTRL_C)
        return GF_ERR_ABORTED;

    job->sofar = sofar;
    job->total = total;

    if (sofar - job->last_signalled >= PROGRESS_STEP ||
        (total && sofar >= total)) {
        job->last_signalled = sofar;
        if (job->gui_task && job->progress_signal >= 0)
            Signal(job->gui_task, 1UL << job->progress_signal);
    }
    return GF_OK;
}

/*
 * Instappunt van het nieuwe proces. De job komt binnen via de ingebouwde
 * berichtenpoort die elk Amiga-proces heeft; dat is het standaardpatroon
 * en betrouwbaarder dan de job via een gedeelde variabele doorgeven.
 */
static void worker_entry(void)
{
    struct Process *me = (struct Process *)FindTask(NULL);
    struct GFJob *job;
    LONG err;

    WaitPort(&me->pr_MsgPort);
    job = (struct GFJob *)GetMsg(&me->pr_MsgPort);
    if (!job)
        return;

    job->worker_task = (struct Task *)me;
    job->sofar = 0;
    job->total = 0;

    /* Alleen deze task opent bsdsocket.library -- zie gfworker.h. */
    err = gf_net_open();
    if (err == GF_OK) {
        switch (job->type) {
        case GFJOB_FETCH:
            err = gf_fetch_releases(&job->prefs, job->repo, job->list,
                                    SIGBREAKF_CTRL_C);
            break;

        case GFJOB_DOWNLOAD:
            err = gf_download_asset(&job->prefs, &job->asset, job->destdir,
                                    SIGBREAKF_CTRL_C, worker_progress, job);
            break;

        default:
            err = GF_ERR_PROTO;
            break;
        }
        gf_net_close();
    }

    job->result = err;
    job->running = FALSE;

    /* Vanaf hier mag de GUI de job weer aanraken; het proces eindigt
       zodra deze functie terugkeert. */
    ReplyMsg((struct Message *)job);
}

BOOL gf_worker_start(struct GFJob *job, struct MsgPort *replyport)
{
    struct Process *proc;

    job->msg.mn_Node.ln_Type = NT_MESSAGE;
    job->msg.mn_Length = sizeof(struct GFJob);
    job->msg.mn_ReplyPort = replyport;
    job->worker_task = NULL;
    job->result = GF_OK;
    job->sofar = 0;
    job->total = 0;
    job->last_signalled = 0;
    job->running = TRUE;

    proc = CreateNewProcTags(
        NP_Entry,     (ULONG)worker_entry,
        NP_Name,      (ULONG)"GitFetch netwerk",
        NP_StackSize, 65536,   /* TLS heeft flink wat stack nodig */
        NP_Priority,  0,
        NP_Input,     (ULONG)NULL,
        NP_Output,    (ULONG)NULL,
        NP_CloseInput,  FALSE,
        NP_CloseOutput, FALSE,
        TAG_END);

    if (!proc) {
        job->running = FALSE;
        return FALSE;
    }

    PutMsg(&proc->pr_MsgPort, (struct Message *)job);
    return TRUE;
}

void gf_worker_abort(struct GFJob *job)
{
    /* worker_task wordt door de worker zelf gezet; is dat nog niet
       gebeurd, dan is hij nog niet aan het werk en breekt de eerstvolgende
       controle hem alsnog af. */
    if (job->running && job->worker_task)
        Signal(job->worker_task, SIGBREAKF_CTRL_C);
}
