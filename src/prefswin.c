/*
 * prefswin.c -- het instellingenvenster.
 *
 * Een eigen venster met de keuze tussen de twee manieren om GitHub te
 * bereiken, de proxy-gegevens en wat kleinere voorkeuren. Bewaren schrijft
 * naar ENVARC:, zodat de instellingen een herstart overleven.
 *
 * Het deelt de classes en library bases met gui.c; die zijn daar al
 * geopend en gecontroleerd.
 */

#include <exec/types.h>
#include <exec/memory.h>
#include <dos/dos.h>
#include <dos/var.h>

#include <intuition/intuition.h>
#include <intuition/gadgetclass.h>
#include <intuition/classes.h>

#include <classes/window.h>
#include <gadgets/layout.h>
#include <gadgets/button.h>
#include <gadgets/string.h>
#include <gadgets/checkbox.h>
#include <gadgets/integer.h>
#include <images/label.h>

#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/intuition.h>
#include <proto/utility.h>

#include <clib/alib_protos.h>

#include <string.h>
#include <stdio.h>

#include "gitfetch.h"
#include "gfgui.h"
#include "gfbackend.h"
#include "gflocale.h"

/* Uit gui.c. */
extern struct ClassRef cls_window, cls_layout, cls_string, cls_button,
                       cls_label, cls_checkbox, cls_integer;

#define NEWOBJ(c)   NewObject((c).ptr, (c).name

#define PID_NATIVE   1
#define PID_VERIFY   2
#define PID_HOST     3
#define PID_PORT     4
#define PID_MAX      5
#define PID_SAVE     6
#define PID_USE      7
#define PID_CANCEL   8

struct PrefsWin {
    Object        *win;
    struct Window *window;
    Object        *g[10];
};

static Object *make_label(const char *text)
{
    if (!cls_label.ptr && !cls_label.name)
        return NULL;
    return NEWOBJ(cls_label), LABEL_Text, (ULONG)text, TAG_END);
}

/* Schrijft een variabele naar ENV: en ENVARC:, zodat hij een herstart
   overleeft. */
static void save_var(const char *name, const char *value)
{
    char var[64];

    strcpy(var, "GitFetch/");
    strncat(var, name, sizeof(var) - strlen(var) - 1);

    SetVar((STRPTR)var, (STRPTR)value, -1,
           GVF_GLOBAL_ONLY | GVF_SAVE_VAR);
}

void gf_save_window_prefs(struct GFPrefs *p)
{
    char buf[48];

    if (p->win_width <= 0 || p->win_height <= 0)
        return;

    sprintf(buf, "%ld %ld %ld %ld", (long)p->win_left, (long)p->win_top,
            (long)p->win_width, (long)p->win_height);
    save_var("Window", buf);
}

static void save_prefs(struct GFPrefs *p)
{
    char buf[48];

    save_var("ProxyHost", p->host);
    sprintf(buf, "%ld", (long)p->port);
    save_var("ProxyPort", buf);
    save_var("DestDir", p->destdir);
    sprintf(buf, "%ld", (long)p->max_releases);
    save_var("MaxReleases", buf);
    save_var("Backend", p->backend == GF_BACKEND_NATIVE ? "native" : "proxy");
    sprintf(buf, "%ld", (long)p->verify_cert);
    save_var("VerifyCert", buf);
    gf_save_window_prefs(p);
}

/* Leest de gadgets uit in de meegegeven prefs. */
static void collect(struct PrefsWin *pw, struct GFPrefs *p)
{
    ULONG v = 0;

    if (pw->g[PID_HOST]) {
        GetAttr(STRINGA_TextVal, pw->g[PID_HOST], &v);
        if (v) {
            strncpy(p->host, (const char *)v, sizeof(p->host) - 1);
            p->host[sizeof(p->host) - 1] = '\0';
        }
    }
    if (pw->g[PID_PORT]) {
        GetAttr(INTEGER_Number, pw->g[PID_PORT], &v);
        if (v > 0 && v < 65536)
            p->port = (UWORD)v;
    }
    if (pw->g[PID_MAX]) {
        GetAttr(INTEGER_Number, pw->g[PID_MAX], &v);
        if (v >= 1 && v <= GF_MAX_RELEASES)
            p->max_releases = (UWORD)v;
    }
    if (pw->g[PID_NATIVE]) {
        GetAttr(GA_Selected, pw->g[PID_NATIVE], &v);
        p->backend = (UWORD)(v ? GF_BACKEND_NATIVE : GF_BACKEND_PROXY);
    }
    if (pw->g[PID_VERIFY]) {
        GetAttr(GA_Selected, pw->g[PID_VERIFY], &v);
        p->verify_cert = (UWORD)(v ? 1 : 0);
    }
}

/*
 * De velden die niet van toepassing zijn uitgrijzen. GA_Disabled is een
 * standaard-attribuut van elk ReAction-gadget, dus hier is niets bijzonders
 * voor nodig: bij de rechtstreekse verbinding zijn de proxy-gegevens
 * betekenisloos, en bij de proxy geldt dat voor de certificaatcontrole.
 */
static void update_enabled(struct PrefsWin *pw)
{
    ULONG native = 0;
    BOOL  use_native;
    WORD  proxy_fields[] = { PID_HOST, PID_PORT, 0 };
    WORD  i;

    if (!pw->g[PID_NATIVE] || !pw->window)
        return;

    GetAttr(GA_Selected, pw->g[PID_NATIVE], &native);
    use_native = (BOOL)(native != 0);

    for (i = 0; proxy_fields[i]; i++)
        if (pw->g[proxy_fields[i]])
            SetGadgetAttrs((struct Gadget *)pw->g[proxy_fields[i]],
                           pw->window, NULL,
                           GA_Disabled, (ULONG)use_native, TAG_END);

    if (pw->g[PID_VERIFY])
        SetGadgetAttrs((struct Gadget *)pw->g[PID_VERIFY], pw->window, NULL,
                       GA_Disabled, (ULONG)(!use_native), TAG_END);

    /*
     * SetGadgetAttrs verandert de toestand, maar niet elk gadget tekent
     * zichzelf daarop opnieuw: het string-veld bleef er ingeschakeld
     * uitzien tot je het aanraakte. WM_RETHINK laat de layout alles
     * opnieuw opbouwen en tekenen.
     */
    DoMethod(pw->win, WM_RETHINK);
}

static BOOL build(struct PrefsWin *pw, struct GFPrefs *p)
{
    Object *root, *buttons;

    pw->g[PID_NATIVE] = NEWOBJ(cls_checkbox),
        GA_ID,        PID_NATIVE,
        GA_RelVerify, TRUE,
        GA_Text,      (ULONG)gf_str(MSG_PREFS_NATIVE),
        GA_Selected,  (ULONG)(p->backend == GF_BACKEND_NATIVE),
        TAG_END);

    pw->g[PID_VERIFY] = NEWOBJ(cls_checkbox),
        GA_ID,        PID_VERIFY,
        GA_RelVerify, TRUE,
        GA_Text,      (ULONG)gf_str(MSG_PREFS_VERIFY),
        GA_Selected,  (ULONG)(p->verify_cert != 0),
        TAG_END);

    pw->g[PID_HOST] = NEWOBJ(cls_string),
        GA_ID,            PID_HOST,
        GA_RelVerify,     TRUE,
        STRINGA_TextVal,  (ULONG)p->host,
        STRINGA_MaxChars, 120,
        TAG_END);

    pw->g[PID_PORT] = NEWOBJ(cls_integer),
        GA_ID,            PID_PORT,
        GA_RelVerify,     TRUE,
        INTEGER_Number,   (ULONG)p->port,
        INTEGER_Minimum,  1,
        INTEGER_Maximum,  65535,
        TAG_END);

    pw->g[PID_MAX] = NEWOBJ(cls_integer),
        GA_ID,            PID_MAX,
        GA_RelVerify,     TRUE,
        INTEGER_Number,   (ULONG)p->max_releases,
        INTEGER_Minimum,  1,
        INTEGER_Maximum,  GF_MAX_RELEASES,
        TAG_END);

    pw->g[PID_SAVE]   = NEWOBJ(cls_button), GA_ID, PID_SAVE,
                        GA_RelVerify, TRUE, GA_Text, (ULONG)gf_str(MSG_PREFS_SAVE), TAG_END);
    pw->g[PID_USE]    = NEWOBJ(cls_button), GA_ID, PID_USE,
                        GA_RelVerify, TRUE, GA_Text, (ULONG)gf_str(MSG_PREFS_USE), TAG_END);
    pw->g[PID_CANCEL] = NEWOBJ(cls_button), GA_ID, PID_CANCEL,
                        GA_RelVerify, TRUE, GA_Text, (ULONG)gf_str(MSG_PREFS_CANCEL), TAG_END);

    if (!pw->g[PID_HOST] || !pw->g[PID_SAVE] || !pw->g[PID_CANCEL])
        return FALSE;

    buttons = NEWOBJ(cls_layout),
        LAYOUT_Orientation, LAYOUT_ORIENT_HORIZ,
        LAYOUT_SpaceInner,  TRUE,
        LAYOUT_EvenSize,    TRUE,
        LAYOUT_AddChild,    (ULONG)pw->g[PID_SAVE],
        LAYOUT_AddChild,    (ULONG)pw->g[PID_USE],
        LAYOUT_AddChild,    (ULONG)pw->g[PID_CANCEL],
        TAG_END);

    root = NEWOBJ(cls_layout),
        LAYOUT_Orientation, LAYOUT_ORIENT_VERT,
        LAYOUT_SpaceOuter,  TRUE,
        LAYOUT_SpaceInner,  TRUE,
        LAYOUT_AddChild,    (ULONG)pw->g[PID_NATIVE],
            CHILD_WeightedHeight, 0,
        LAYOUT_AddChild,    (ULONG)pw->g[PID_VERIFY],
            CHILD_WeightedHeight, 0,
        LAYOUT_AddChild,    (ULONG)pw->g[PID_HOST],
            CHILD_Label,    (ULONG)make_label(gf_str(MSG_PREFS_HOST)),
            CHILD_WeightedHeight, 0,
        LAYOUT_AddChild,    (ULONG)pw->g[PID_PORT],
            CHILD_Label,    (ULONG)make_label(gf_str(MSG_PREFS_PORT)),
            CHILD_WeightedHeight, 0,
        LAYOUT_AddChild,    (ULONG)pw->g[PID_MAX],
            CHILD_Label,    (ULONG)make_label(gf_str(MSG_PREFS_COUNT)),
            CHILD_WeightedHeight, 0,
        LAYOUT_AddChild,    (ULONG)buttons,
            CHILD_WeightedHeight, 0,
        TAG_END);

    if (!buttons || !root)
        return FALSE;

    pw->win = NEWOBJ(cls_window),
        WA_Title,           (ULONG)gf_str(MSG_PREFS_TITLE),
        WA_Activate,        TRUE,
        WA_DragBar,         TRUE,
        WA_DepthGadget,     TRUE,
        WA_CloseGadget,     TRUE,
        WA_SizeGadget,      TRUE,
        WA_InnerWidth,      330,
        WINDOW_Position,    WPOS_CENTERSCREEN,
        WINDOW_ParentGroup, (ULONG)root,
        TAG_END);

    return pw->win ? TRUE : FALSE;
}

BOOL gf_prefs_window(struct GFApp *app)
{
    struct PrefsWin *pw;
    struct GFPrefs work;
    ULONG sigmask = 0, sigs, result;
    UWORD code = 0;
    BOOL done = FALSE, changed = FALSE;

    pw = AllocVec(sizeof(struct PrefsWin), MEMF_ANY | MEMF_CLEAR);
    if (!pw)
        return FALSE;

    /* Op een kopie werken: annuleren mag niets achterlaten. */
    work = app->prefs;

    if (!build(pw, &work))
        goto cleanup;

    pw->window = (struct Window *)DoMethod(pw->win, WM_OPEN);
    if (!pw->window)
        goto cleanup;

    GetAttr(WINDOW_SigMask, pw->win, &sigmask);
    update_enabled(pw);

    while (!done) {
        sigs = Wait(sigmask | SIGBREAKF_CTRL_C);
        if (sigs & SIGBREAKF_CTRL_C)
            break;

        while ((result = DoMethod(pw->win, WM_HANDLEINPUT, &code))
               != WMHI_LASTMSG) {
            switch (result & WMHI_CLASSMASK) {
            case WMHI_CLOSEWINDOW:
                done = TRUE;
                break;

            case WMHI_GADGETUP:
                switch (result & WMHI_GADGETMASK) {
                case PID_NATIVE:
                    update_enabled(pw);
                    break;

                case PID_SAVE:
                    collect(pw, &work);
                    app->prefs = work;
                    save_prefs(&work);
                    changed = TRUE;
                    done = TRUE;
                    break;

                case PID_USE:
                    collect(pw, &work);
                    app->prefs = work;
                    changed = TRUE;
                    done = TRUE;
                    break;

                case PID_CANCEL:
                    done = TRUE;
                    break;
                }
                break;
            }
        }
    }

cleanup:
    if (pw->win)
        DisposeObject(pw->win);
    FreeVec(pw);
    return changed;
}
