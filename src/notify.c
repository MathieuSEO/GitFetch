/*
 * notify.c -- meldingen naar Herald, Ranchero of Ringhio.
 *
 * Alle drie hebben een publieke ARexx-poort die een gewoon RexxMsg
 * aanneemt. Er hoeft dus geen ARexx-interpreter te draaien: het bericht
 * gaat rechtstreeks met PutMsg naar de poort. Alleen rexxsyslib.library
 * is nodig, om het bericht te maken; die staat op elk systeem met OS 2.0.
 *
 * Een app moet zich eerst aanmelden (REGISTERAPP) voordat een melding
 * wordt aangenomen. Dat gebeurt een keer per server per sessie; het
 * antwoord "already registered" bij een tweede GitFetch is geen fout.
 */

#include <exec/types.h>
#include <exec/ports.h>
#include <dos/dos.h>
#include <rexx/storage.h>
#include <rexx/rxslib.h>

#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/rexxsyslib.h>

#include <string.h>
#include <stdio.h>

#include "gitfetch.h"
#include "gfnotify.h"

struct RxsLib *RexxSysBase = NULL;

static struct MsgPort *reply_port = NULL;

/*
 * Kwam er ooit geen antwoord, dan staat dat bericht nog uit en mogen we
 * de poort niet meer opruimen: een laat antwoord zou in vrijgegeven
 * geheugen belanden. Dan liever een paar honderd bytes laten liggen en
 * verder geen meldingen meer sturen.
 */
static BOOL wedged = FALSE;

/* Hoe lang we op een antwoord wachten, in 1/50 seconde. */
#define REPLY_TICKS  100

struct Server {
    const char *port;       /* naam van de ARexx-poort */
    const char *command;    /* commando voor een melding */
    BOOL        registered;
};

/*
 * Herald kent NOTIFY, Ranchero RANCHERO en (ook in Ringhio-modus) RINGHIO,
 * Ringhio op OS 4 RINGHIO. De eerste die draait, krijgt de melding.
 */
static struct Server servers[] = {
    { "HERALD",   "NOTIFY",   FALSE },
    { "RANCHERO", "RANCHERO", FALSE },
    { "RINGHIO",  "RINGHIO",  FALSE },
    { NULL, NULL, FALSE }
};

static char icon_path[256];

void gf_notify_open(void)
{
    BPTR dir;

    RexxSysBase = (struct RxsLib *)OpenLibrary("rexxsyslib.library", 36);
    if (!RexxSysBase)
        return;

    reply_port = CreateMsgPort();

    /*
     * Het icoon dat Herald bij elke melding laat zien. Een absoluut pad:
     * PROGDIR: zou voor Herald naar zijn eigen map wijzen.
     */
    icon_path[0] = '\0';
    dir = GetProgramDir();
    if (dir && NameFromLock(dir, (STRPTR)icon_path, sizeof(icon_path)))
        AddPart((STRPTR)icon_path, (STRPTR)"GitFetch", sizeof(icon_path));
    else
        icon_path[0] = '\0';
}

void gf_notify_close(void)
{
    if (reply_port && !wedged)
        DeleteMsgPort(reply_port);
    reply_port = NULL;

    if (RexxSysBase) {
        CloseLibrary((struct Library *)RexxSysBase);
        RexxSysBase = NULL;
    }
}

/*
 * Stuurt een commando en wacht op het antwoord. FALSE als de poort er
 * niet (meer) is of de server het weigerde.
 */
static BOOL send_command(const char *port_name, const char *command)
{
    struct RexxMsg *msg;
    struct MsgPort *target;
    struct Message *reply = NULL;
    LONG ticks;
    BOOL ok;

    msg = CreateRexxMsg(reply_port, NULL, NULL);
    if (!msg)
        return FALSE;

    msg->rm_Args[0] = (STRPTR)CreateArgstring((STRPTR)command,
                                              (ULONG)strlen(command));
    if (!msg->rm_Args[0]) {
        DeleteRexxMsg(msg);
        return FALSE;
    }
    /* CreateRexxMsg laat de naam leeg, en een server die IsRexxMsg()
       controleert, negeert zo'n bericht zonder te antwoorden. */
    msg->rm_Node.mn_Node.ln_Name = (char *)"REXX";
    /* Met RXFF_RESULT, zoals een script met OPTIONS RESULTS: dan zegt de
       server in rm_Result2 wat hij ervan vond. */
    msg->rm_Action = RXCOMM | RXFF_RESULT;

    GF_TRACE(port_name);
    GF_TRACE(command);

    /* Tussen FindPort en PutMsg mag de poort niet verdwijnen. */
    Forbid();
    target = FindPort((STRPTR)port_name);
    if (target)
        PutMsg(target, (struct Message *)msg);
    Permit();

    if (!target) {
        DeleteArgstring((UBYTE *)msg->rm_Args[0]);
        DeleteRexxMsg(msg);
        return FALSE;
    }

    for (ticks = 0; ticks < REPLY_TICKS; ticks += 2) {
        if ((reply = GetMsg(reply_port)) != NULL)
            break;
        Delay(2);
    }
    if (!reply) {
        /* De poort blijft liggen, maar mag niemand meer wekken: komt het
           antwoord pas als GitFetch al weg is, dan zou PutMsg anders een
           signaal naar een verdwenen task sturen. Zo komt het alleen in
           de wachtrij van een poort die niemand meer leest. */
        Forbid();
        reply_port->mp_Flags = PA_IGNORE;
        reply_port->mp_SigTask = NULL;
        Permit();
        wedged = TRUE;
        GF_TRACE("melding: geen antwoord binnen de tijd");
        return FALSE;
    }

    ok = (BOOL)(msg->rm_Result1 == 0);
    GF_TRACE1("melding: rc", msg->rm_Result1);
    if (ok && msg->rm_Result2) {
        GF_TRACE((const char *)msg->rm_Result2);
        DeleteArgstring((UBYTE *)msg->rm_Result2);
    }
    DeleteArgstring((UBYTE *)msg->rm_Args[0]);
    DeleteRexxMsg(msg);
    return ok;
}

/* Kopieert met " en * ge-escapet als *" en **, zoals ReadArgs dat wil. */
static void quote_value(char *out, LONG size, const char *in)
{
    LONG n = 0;

    while (*in && n < size - 3) {
        if (*in == '"' || *in == '*')
            out[n++] = '*';
        out[n++] = *in++;
    }
    out[n] = '\0';
}

void gf_notify(BOOL failed, const char *title, const char *text)
{
    struct Server *s;
    char quoted[64];
    char line[512];

    if (!RexxSysBase || !reply_port || wedged) {
        GF_TRACE("melding: geen rexxsyslib, reply-port of al vastgelopen");
        return;
    }

    for (s = servers; s->port; s++) {
        Forbid();
        if (FindPort((STRPTR)s->port)) {
            Permit();
            break;
        }
        Permit();
    }
    if (!s->port) {
        GF_TRACE("melding: geen HERALD-, RANCHERO- of RINGHIO-poort");
        return;                 /* er draait geen notificatieserver */
    }

    if (!s->registered) {
        /* Een " of * in het pad zou ReadArgs bij de server laten struikelen,
           en dan weigert hij de aanmelding en daarmee elke melding. Zonder
           icoon werkt het gewoon. */
        if (icon_path[0] && !strpbrk(icon_path, "\"*"))
            sprintf(line, "REGISTERAPP APP=GitFetch ICON=\"%s\"", icon_path);
        else
            strcpy(line, "REGISTERAPP APP=GitFetch");
        /* Het antwoord doet er niet toe: "already registered" betekent
           dat het al in orde was. */
        send_command(s->port, line);
        if (wedged)
            return;
        s->registered = TRUE;
    }

    quote_value(quoted, sizeof(quoted), title);
    /* De tekst gaat als laatste: TEXT/F neemt de rest van de regel. */
    sprintf(line, "%s APP=GitFetch PRI=%ld TITLE=\"%s\" ", s->command,
            (long)(failed ? 5 : 0), quoted);
    strncat(line, text, sizeof(line) - strlen(line) - 1);

    send_command(s->port, line);
}
