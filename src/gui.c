/*
 * gui.c -- ReAction-venster voor GitFetch.
 *
 * Opzet: een verticale layout met bovenin het invoerveld, in het midden
 * twee lijsten naast elkaar (releases links, bestanden rechts) en onderin
 * de doelmap, een voortgangsbalk en de knoppen.
 *
 * De classes worden met OpenLibrary geopend en de objecten met NewObject()
 * op naam gemaakt. De macro's uit de NDK-headers (WindowObject ... End)
 * leunen op reaction.lib, en dat is een SAS/C-bibliotheek waar gcc niets
 * mee kan; via NewObject() is die afhankelijkheid niet nodig.
 */

#include <exec/types.h>
#include <exec/memory.h>
#include <exec/lists.h>
#include <dos/dos.h>

#include <intuition/intuition.h>
#include <intuition/gadgetclass.h>
#include <intuition/classes.h>
#include <libraries/gadtools.h>

#include <classes/window.h>
#include <gadgets/layout.h>
#include <gadgets/button.h>
#include <gadgets/string.h>
#include <gadgets/listbrowser.h>
#include <gadgets/fuelgauge.h>
#include <gadgets/getfile.h>
#include <gadgets/chooser.h>
#include <gadgets/checkbox.h>
#include <gadgets/integer.h>
#include <images/label.h>

#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/intuition.h>
#include <proto/utility.h>
#include <proto/gadtools.h>

/*
 * De proto-headers van de ReAction-classes leveren de XXX_GetClass()-
 * functies. Die zijn nodig omdat de meeste classes zich niet onder hun
 * naam registreren: NEWOBJ(cls_string), ...) geeft NULL
 * terug, ook al is de library keurig geopend. Alleen button.gadget
 * accepteert de naamvorm -- zie reaction/reaction_macros.h.
 */
#include <proto/window.h>
#include <proto/layout.h>
#include <proto/string.h>
#include <proto/button.h>
#include <proto/listbrowser.h>
#include <proto/fuelgauge.h>
#include <proto/getfile.h>
#include <proto/label.h>
#include <proto/chooser.h>
#include <proto/checkbox.h>
#include <proto/integer.h>

#include <clib/alib_protos.h>   /* DoMethod */

#include <string.h>
#include <stdio.h>

#include "gitfetch.h"
#include "gfgui.h"
#include "gfurl.h"
#include "gfnet.h"
#include "gfworker.h"
#include "gfbookmarks.h"
#include "gflocale.h"

/* --- library bases ------------------------------------------------------- */

struct Library *WindowBase      = NULL;
struct Library *LayoutBase      = NULL;
struct Library *ButtonBase      = NULL;
struct Library *StringBase      = NULL;
struct Library *ListBrowserBase = NULL;
struct Library *FuelGaugeBase   = NULL;
struct Library *GetFileBase     = NULL;
struct Library *LabelBase       = NULL;
struct Library *ChooserBase     = NULL;
struct Library *CheckBoxBase    = NULL;
struct Library *IntegerBase     = NULL;
struct Library *GadToolsBase    = NULL;

struct ClassLib {
    struct Library **base;
    const char      *name;
    BOOL             required;
};

/*
 * Een class wordt op twee manieren aangeduid: via de pointer die de
 * library teruggeeft, en via de naam. NewObject() gebruikt de pointer als
 * die er is en anders de naam.
 *
 * Waarom allebei: de meeste ReAction-classes registreren zich niet onder
 * hun naam, dus de naam alleen levert NULL op. Maar geeft XXX_GetClass()
 * onverwacht NULL terug, dan zou NewObject(NULL, NULL, ...) volgen -- en
 * daar loopt intuition op vast met een Line-1111 trap in plaats van een
 * nette NULL. Met de naam als tweede kans kan dat niet gebeuren.
 */
struct ClassRef cls_window, cls_layout, cls_string, cls_button,
                       cls_listbrowser, cls_fuelgauge, cls_getfile,
                       cls_label, cls_checkbox, cls_integer, cls_chooser;

#define NEWOBJ(c)   NewObject((c).ptr, (c).name

static void fetch_classes(void);
static void clamp_window_prefs(struct GFApp *app);
static void remember_window(struct GFApp *app);
static LONG ask_overwrite(struct GFApp *app, const char *name,
                          const char *dir, LONG size);
static LONG check_space(struct GFApp *app, const char *name,
                        const char *dir, ULONG size);
static void action_bookmark_add(struct GFApp *app);
static void action_bookmark_remove(struct GFApp *app);

/* In prefswin.c; deelt de classes en library bases uit dit bestand. */
BOOL gf_prefs_window(struct GFApp *app);
static void init_list(struct List *l);

/* getfile en fuelgauge zijn prettig maar niet essentieel: ontbreken ze,
   dan draait de rest gewoon door met een string-veld en zonder balk. */
static struct ClassLib class_libs[] = {
    { &WindowBase,      "window.class",                 TRUE  },
    { &LayoutBase,      "gadgets/layout.gadget",        TRUE  },
    { &ButtonBase,      "gadgets/button.gadget",        TRUE  },
    { &StringBase,      "gadgets/string.gadget",        TRUE  },
    { &ListBrowserBase, "gadgets/listbrowser.gadget",   TRUE  },
    { &FuelGaugeBase,   "gadgets/fuelgauge.gadget",     FALSE },
    { &GetFileBase,     "gadgets/getfile.gadget",       FALSE },
    { &LabelBase,       "images/label.image",           FALSE },
    { &ChooserBase,     "gadgets/chooser.gadget",       FALSE },
    { &CheckBoxBase,    "gadgets/checkbox.gadget",      FALSE },
    { &IntegerBase,     "gadgets/integer.gadget",       FALSE },
    { NULL, NULL, FALSE }
};

/* --- kolomindeling van de lijsten ---------------------------------------- */

/* Breedtes zijn percentages van de gadgetbreedte. */
static struct ColumnInfo release_columns[] = {
    { 34, NULL, 0 },
    { 34, NULL, 0 },
    { 32, NULL, 0 },
    { -1, NULL, 0 }
};

static struct ColumnInfo mark_columns[] = {
    { 100, NULL, 0 },
    { -1, NULL, 0 }
};

static struct ColumnInfo asset_columns[] = {
    { 68, NULL, 0 },
    { 32, NULL, 0 },
    { -1, NULL, 0 }
};

/*
 * Hoeveel releases we ophalen. Dit scheelt vooral bij de rechtstreekse
 * verbinding: GitHub stuurt voor 25 releases ongeveer 240 KB, en voor de
 * nieuwste alleen een fractie daarvan.
 */
#define COUNT_CHOICES  5
static UWORD count_msg[COUNT_CHOICES] = {
    MSG_COUNT_NEWEST, MSG_COUNT_3, MSG_COUNT_5, MSG_COUNT_10, MSG_COUNT_ALL
};
/*
 * De laatste keuze ("all") volgt het maximum uit de instellingen; daarom
 * staat er geen getal bij het label. count_values[4] wordt bij het
 * opbouwen van het venster op dat maximum gezet.
 */
static UWORD count_values[] = { 1, 3, 5, 10, GF_MAX_RELEASES };

/*
 * De keuzelijst staat in de onderste rij, niet in de bovenbalk. Daar zat
 * hij eerst, samen met het invoerveld en twee knoppen, en dan perst de
 * layout het smalste element plat tot alleen een pijltje. De chooser
 * berekent zijn breedte namelijk niet uit zijn labels.
 *
 * De lijst wordt met losse nodes gevuld en niet met CHOOSER_LabelArray:
 * die tag kwam pas in versie 45.2, en de classes van OS 3.9 zijn versie 44.
 */
static struct List count_list;

static void build_count_list(void)
{
    UWORD i;

    count_list.lh_Head     = (struct Node *)&count_list.lh_Tail;
    count_list.lh_Tail     = NULL;
    count_list.lh_TailPred = (struct Node *)&count_list.lh_Head;

    for (i = 0; i < COUNT_CHOICES; i++) {
        /* De chooser kopieert de tekst niet; gf_str levert een blijvende
           pointer, dus dat is in orde. */
        struct Node *node = AllocChooserNode(
            CNA_Text, (ULONG)gf_str(count_msg[i]),
            TAG_END);

        if (node)
            AddTail(&count_list, node);
    }
}

static UWORD count_index_for(UWORD n)
{
    UWORD i;

    for (i = 0; i < COUNT_CHOICES; i++)
        if (count_values[i] >= n)
            return i;
    return COUNT_CHOICES - 1;
}

/* --- menu ---------------------------------------------------------------- */

#define MID_ABOUT  1
#define MID_QUIT   2
#define MID_PREFS  3

static struct NewMenu gf_menu[] = {
    { NM_TITLE, NULL,   NULL,        0, 0, NULL },
    { NM_ITEM,  NULL,   (STRPTR)"?", 0, 0, (APTR)MID_ABOUT },
    { NM_ITEM,  NM_BARLABEL,         NULL,        0, 0, NULL },
    { NM_ITEM,  NULL,  (STRPTR)"Q", 0, 0, (APTR)MID_QUIT },
    { NM_TITLE, NULL, NULL,     0, 0, NULL },
    { NM_ITEM,  NULL, (STRPTR)"I", 0, 0, (APTR)MID_PREFS },
    { NM_END,   NULL,                NULL,        0, 0, NULL }
};

/* --- hulpjes ------------------------------------------------------------- */

static void set_status(struct GFApp *app, const char *text)
{
    if (app->gadgets[GID_STATUS] && app->window)
        SetGadgetAttrs((struct Gadget *)app->gadgets[GID_STATUS], app->window,
                       NULL, STRINGA_TextVal, (ULONG)text, TAG_END);
}

static const char *size_text(ULONG bytes, char *buf)
{
    if (bytes >= 1024UL * 1024UL)
        sprintf(buf, "%lu,%lu MB", bytes / (1024UL * 1024UL),
                ((bytes % (1024UL * 1024UL)) * 10UL) / (1024UL * 1024UL));
    else if (bytes >= 1024UL)
        sprintf(buf, "%lu KB", bytes / 1024UL);
    else if (bytes > 0)
        sprintf(buf, "%lu B", bytes);
    else
        strcpy(buf, "-");
    return buf;
}

static void enable_gadget(struct GFApp *app, WORD gid, BOOL on)
{
    if (app->gadgets[gid] && app->window)
        SetGadgetAttrs((struct Gadget *)app->gadgets[gid], app->window, NULL,
                       GA_Disabled, (ULONG)(on ? FALSE : TRUE), TAG_END);
}

/* --- lijsten vullen ------------------------------------------------------ */

/*
 * Een exec-lijst leegmaken. Dit is precies wat NewList() doet, maar dan
 * zonder afhankelijkheid van amiga.lib: een lijstkop die niet goed is
 * geinitialiseerd laat AddTail() maar een enkele node overhouden, en dat
 * is lastig te herkennen omdat er niets crasht.
 */
static void init_list(struct List *l)
{
    l->lh_Head     = (struct Node *)&l->lh_Tail;
    l->lh_Tail     = NULL;
    l->lh_TailPred = (struct Node *)&l->lh_Head;
}

static void detach_list(struct GFApp *app, WORD gid, struct List *list,
                        BOOL *attached)
{
    if (!app->gadgets[gid])
        return;

    /* Een listbrowser mag nooit een lijst onder zich hebben die verandert;
       eerst loskoppelen, dan pas opruimen. */
    if (*attached) {
        SetGadgetAttrs((struct Gadget *)app->gadgets[gid], app->window, NULL,
                       LISTBROWSER_Labels, (ULONG)NULL, TAG_END);
        *attached = FALSE;
    }
    FreeListBrowserList(list);
    init_list(list);
}

static void attach_list(struct GFApp *app, WORD gid, struct List *list,
                        BOOL *attached)
{
    if (!app->gadgets[gid])
        return;

    SetGadgetAttrs((struct Gadget *)app->gadgets[gid], app->window, NULL,
                   LISTBROWSER_Labels, (ULONG)list,
                   TAG_END);
    *attached = TRUE;
}

static void fill_mark_list(struct GFApp *app)
{
    UWORD i;

    detach_list(app, GID_MARKS, &app->mark_list, &app->mark_list_attached);

    for (i = 0; i < app->marks.count; i++) {
        struct Node *node = AllocListBrowserNode(1,
            LBNA_Column,       0,
                LBNCA_CopyText, TRUE,
                LBNCA_Text,     (ULONG)app->marks.repo[i],
            TAG_END);

        if (node)
            AddTail(&app->mark_list, node);
    }

    attach_list(app, GID_MARKS, &app->mark_list, &app->mark_list_attached);
}

static void fill_release_list(struct GFApp *app)
{
    UWORD i;

    detach_list(app, GID_RELEASES, &app->release_list,
                &app->release_list_attached);

    GF_TRACE1("releases in de lijst:", app->list->nreleases);

    for (i = 0; i < app->list->nreleases; i++) {
        struct Release *rel = &app->list->releases[i];
        struct Node *node;
        char countbuf[24];

        /* Een lege kolom is verspilde ruimte; het aantal bestanden en een
           prerelease-markering zijn allebei nuttig om te zien. */
        if (rel->nassets == 0)
            strcpy(countbuf, "geen");
        else
            sprintf(countbuf, "%ld", (long)rel->nassets);
        if (rel->prerelease)
            strncat(countbuf, " (beta)", sizeof(countbuf) - strlen(countbuf) - 1);

        node = AllocListBrowserNode(3,
            LBNA_Column,       0,
                LBNCA_CopyText, TRUE,
                LBNCA_Text,     (ULONG)rel->tag,
            LBNA_Column,       1,
                LBNCA_CopyText, TRUE,
                LBNCA_Text,     (ULONG)rel->date,
            LBNA_Column,       2,
                LBNCA_CopyText, TRUE,
                LBNCA_Text,     (ULONG)countbuf,
            TAG_END);

        GF_TRACEP(rel->tag, node);
        if (node)
            AddTail(&app->release_list, node);
    }

    attach_list(app, GID_RELEASES, &app->release_list,
                &app->release_list_attached);
}

static void fill_asset_list(struct GFApp *app, WORD release_index)
{
    struct Release *rel;
    UWORD i;
    char sizebuf[32];

    detach_list(app, GID_ASSETS, &app->asset_list, &app->asset_list_attached);

    if (release_index >= 0 && release_index < (WORD)app->list->nreleases) {
        rel = &app->list->releases[release_index];

        for (i = 0; i < rel->nassets; i++) {
            struct Asset *asset = &rel->assets[i];
            struct Node *node;

            size_text(asset->size, sizebuf);
            node = AllocListBrowserNode(2,
                LBNA_Column,       0,
                    LBNCA_CopyText, TRUE,
                    LBNCA_Text,     (ULONG)asset->name,
                LBNA_Column,       1,
                    LBNCA_CopyText, TRUE,
                    LBNCA_Text,     (ULONG)sizebuf,
                TAG_END);

            if (node)
                AddTail(&app->asset_list, node);
        }
    }

    attach_list(app, GID_ASSETS, &app->asset_list, &app->asset_list_attached);
}

/* --- acties -------------------------------------------------------------- */

static void update_button_states(struct GFApp *app)
{
    enable_gadget(app, GID_FETCH,    !app->job_active);
    enable_gadget(app, GID_DOWNLOAD, !app->job_active);
    enable_gadget(app, GID_STOP,      app->job_active);
}

static void action_fetch(struct GFApp *app)
{
    ULONG text = 0;
    char repo[GF_MAX_REPO];
    char status[GF_MAX_ERR];

    if (app->job_active)
        return;

    GetAttr(STRINGA_TextVal, app->gadgets[GID_REPO], &text);
    if (!text || !*(char *)text) {
        set_status(app, gf_str(MSG_NEED_REPO));
        return;
    }

    if (gf_normalize_repo((const char *)text, repo, sizeof(repo)) != GF_OK) {
        set_status(app, gf_str(MSG_BAD_REPO));
        return;
    }

    /* De genormaliseerde vorm terugschrijven, zodat zichtbaar is wat er
       daadwerkelijk opgevraagd wordt. */
    SetGadgetAttrs((struct Gadget *)app->gadgets[GID_REPO], app->window, NULL,
                   STRINGA_TextVal, (ULONG)repo, TAG_END);

    memset(&app->job, 0, sizeof(app->job));
    app->job.type = GFJOB_FETCH;
    app->job.prefs = app->prefs;
    app->job.list = app->list;
    app->job.gui_task = FindTask(NULL);
    app->job.progress_signal = app->progress_signal;
    strcpy(app->job.repo, repo);

    if (!gf_worker_start(&app->job, app->replyport)) {
        set_status(app, gf_str(MSG_NO_WORKER));
        return;
    }

    app->job_active = TRUE;
    update_button_states(app);

    strcpy(status, gf_str(MSG_FETCHING));
    strncat(status, repo, sizeof(status) - strlen(status) - 1);
    strncat(status, " ...", sizeof(status) - strlen(status) - 1);
    set_status(app, status);
}

static void action_download(struct GFApp *app)
{
    ULONG selected_asset = ~0UL;
    ULONG drawer = 0;
    struct Release *rel;
    struct Asset *asset;
    char status[GF_MAX_ERR];

    if (app->job_active)
        return;

    if (app->selected_release < 0 ||
        app->selected_release >= (WORD)app->list->nreleases) {
        set_status(app, gf_str(MSG_PICK_RELEASE));
        return;
    }
    rel = &app->list->releases[app->selected_release];

    GetAttr(LISTBROWSER_Selected, app->gadgets[GID_ASSETS], &selected_asset);
    if (selected_asset == ~0UL || (WORD)selected_asset >= (WORD)rel->nassets) {
        set_status(app, gf_str(MSG_PICK_FILE));
        return;
    }
    asset = &rel->assets[selected_asset];

    /*
     * Controleren of het bestand er al staat. Dit hoort hier en niet in de
     * worker: die heeft geen venster om een vraag op te stellen. De check
     * gebeurt dus voordat het proces start.
     */
    {
        char destpath[512];
        const char *dir = NULL;
        ULONG drawer = 0;
        BPTR lock;

        if (app->gadgets[GID_DEST]) {
            GetAttr(GETFILE_Drawer, app->gadgets[GID_DEST], &drawer);
            if (drawer && *(char *)drawer)
                dir = (const char *)drawer;
        }
        if (!dir)
            dir = app->prefs.destdir;

        strncpy(destpath, dir, sizeof(destpath) - 1);
        destpath[sizeof(destpath) - 1] = '\0';

        if (AddPart(destpath, (STRPTR)asset->name, sizeof(destpath)) &&
            (lock = Lock((STRPTR)destpath, ACCESS_READ)) != 0) {
            struct FileInfoBlock *fib;
            LONG existing_size = 0;
            LONG answer;

            fib = AllocDosObject(DOS_FIB, NULL);
            if (fib) {
                if (Examine(lock, fib))
                    existing_size = fib->fib_Size;
                FreeDosObject(DOS_FIB, fib);
            }
            UnLock(lock);

            answer = ask_overwrite(app, asset->name, dir, existing_size);
            if (!answer) {
                set_status(app, gf_str(MSG_DOWNLOAD_CANCELLED));
                return;
            }
        }

        /* Voorkomt een half bestand op een vol volume. RAM: is de
           standaardbestemming, en dat is nu juist de plek die volloopt. */
        if (!check_space(app, asset->name, dir, asset->size)) {
            set_status(app, gf_str(MSG_DOWNLOAD_CANCELLED));
            return;
        }
    }

    memset(&app->job, 0, sizeof(app->job));
    app->job.type = GFJOB_DOWNLOAD;
    app->job.prefs = app->prefs;
    app->job.asset = *asset;
    app->job.gui_task = FindTask(NULL);
    app->job.progress_signal = app->progress_signal;

    if (app->gadgets[GID_DEST]) {
        GetAttr(GETFILE_Drawer, app->gadgets[GID_DEST], &drawer);
        if (drawer && *(char *)drawer)
            strncpy(app->job.destdir, (const char *)drawer,
                    sizeof(app->job.destdir) - 1);
    }
    if (!app->job.destdir[0])
        strcpy(app->job.destdir, app->prefs.destdir);

    if (!gf_worker_start(&app->job, app->replyport)) {
        set_status(app, gf_str(MSG_NO_WORKER));
        return;
    }

    app->job_active = TRUE;
    update_button_states(app);

    if (app->gadgets[GID_GAUGE])
        SetGadgetAttrs((struct Gadget *)app->gadgets[GID_GAUGE], app->window,
                       NULL, FUELGAUGE_Max, 100, FUELGAUGE_Level, 0, TAG_END);

    strcpy(status, gf_str(MSG_DOWNLOADING));
    strncat(status, asset->name, sizeof(status) - strlen(status) - 1);
    strncat(status, " ...", sizeof(status) - strlen(status) - 1);
    set_status(app, status);
}

/* Haalt de genormaliseerde repo uit het invoerveld. */
static BOOL current_repo(struct GFApp *app, char *out, LONG size)
{
    ULONG text = 0;

    GetAttr(STRINGA_TextVal, app->gadgets[GID_REPO], &text);
    if (!text || !*(char *)text)
        return FALSE;

    return (BOOL)(gf_normalize_repo((const char *)text, out, size) == GF_OK);
}

static void action_bookmark_add(struct GFApp *app)
{
    char repo[GF_MAX_REPO];
    char msg[GF_MAX_ERR];

    if (!current_repo(app, repo, sizeof(repo))) {
        set_status(app, gf_str(MSG_NEED_REPO));
        return;
    }

    if (gf_bookmarks_find(&app->marks, repo) >= 0) {
        set_status(app, gf_str(MSG_MARK_EXISTS));
        return;
    }

    gf_bookmarks_add(&app->marks, repo);
    strcpy(msg, gf_str(MSG_SAVED_MARK));
    strncat(msg, repo, sizeof(msg) - strlen(msg) - 1);

    fill_mark_list(app);
    if (!gf_bookmarks_save(&app->marks))
        strncat(msg, gf_str(MSG_ENVARC_FAILED),
                sizeof(msg) - strlen(msg) - 1);

    set_status(app, msg);
}

static void action_bookmark_remove(struct GFApp *app)
{
    ULONG sel = ~0UL;

    GetAttr(LISTBROWSER_Selected, app->gadgets[GID_MARKS], &sel);
    if (sel == ~0UL || (WORD)sel >= (WORD)app->marks.count) {
        set_status(app, gf_str(MSG_PICK_MARK));
        return;
    }

    gf_bookmarks_remove(&app->marks, (UWORD)sel);
    fill_mark_list(app);
    gf_bookmarks_save(&app->marks);
    set_status(app, gf_str(MSG_MARK_REMOVED));
}

static void action_stop(struct GFApp *app)
{
    if (!app->job_active)
        return;
    set_status(app, gf_str(MSG_ABORTING));
    gf_worker_abort(&app->job);
}

/* --- signalen van de worker ---------------------------------------------- */

static void update_progress(struct GFApp *app)
{
    ULONG sofar, total, pct;

    if (!app->job_active || !app->gadgets[GID_GAUGE])
        return;

    sofar = app->job.sofar;
    total = app->job.total;
    if (!total)
        return;

    /* Overflow-veilig zonder floating point. */
    if (total >= 42949672UL)
        pct = sofar / (total / 100UL);
    else
        pct = (sofar * 100UL) / total;
    if (pct > 100)
        pct = 100;

    SetGadgetAttrs((struct Gadget *)app->gadgets[GID_GAUGE], app->window, NULL,
                   FUELGAUGE_Level, pct, TAG_END);
}

static void handle_worker_reply(struct GFApp *app)
{
    struct GFJob *job;
    char status[GF_MAX_ERR];

    while ((job = (struct GFJob *)GetMsg(app->replyport)) != NULL) {
        app->job_active = FALSE;

        if (job->type == GFJOB_FETCH) {
            GF_TRACE1("resultaatcode:", job->result);
            GF_TRACE(gf_last_request_path());
            GF_TRACE1("bytes ontvangen:", gf_http_last_length());
            GF_TRACE1("HTTP-status:", gf_http_status());
            GF_TRACE1("max_releases uit prefs:", app->prefs.max_releases);
            GF_TRACE1("genegeerde regels:", gf_last_dropped_lines());
            GF_TRACE1("releases geparsed:", app->list->nreleases);
        }

        if (job->result == GF_OK) {
            if (job->type == GFJOB_FETCH) {
                char count[GF_MAX_ERR];

                /* Het instellingenvenster kan het aantal hebben gewijzigd;
                   de knop hoort dat te laten zien. */
                if (app->gadgets[GID_COUNT]) {
                    app->count_index =
                        count_index_for(app->prefs.max_releases);
                    SetGadgetAttrs((struct Gadget *)app->gadgets[GID_COUNT],
                                   app->window, NULL, CHOOSER_Active,
                                   (ULONG)app->count_index, TAG_END);
                }

                app->selected_release = app->list->nreleases ? 0 : -1;
                fill_release_list(app);
                fill_asset_list(app, app->selected_release);

                if (app->list->nreleases == 0) {
                    set_status(app, gf_str(MSG_NO_RELEASES));
                } else if (app->list->nreleases == 1) {
                    set_status(app, gf_str(MSG_ONE_RELEASE));
                } else {
                    sprintf(count, gf_str(MSG_N_RELEASES),
                            (long)app->list->nreleases);
                    set_status(app, count);
                }
            } else {
                /*
                 * Eigen buffer: de melding bevat de bestandsnaam (64) en
                 * het doelpad (256), en dat past niet in status[160]. De
                 * gebruiker kiest die map zelf, dus dit was met een lang
                 * pad een stack-overschrijving -- op een machine zonder
                 * MMU neemt dat het hele systeem mee.
                 */
                char done[GF_MAX_NAME + sizeof(job->destdir) + 32];

                sprintf(done, gf_str(MSG_DONE_SAVED), job->asset.name,
                        job->destdir);
                set_status(app, done);
            }
        } else {
            /* De proxy weet vaak preciezer wat er mis is dan onze eigen
               foutcode; toon die tekst als hij er is. */
            if (gf_last_server_message()[0]) {
                strcpy(status, gf_str(MSG_ERROR_PREFIX));
                strncat(status, gf_last_server_message(),
                        sizeof(status) - strlen(status) - 1);
            } else {
                strcpy(status, gf_str(MSG_ERROR_PREFIX));
                strncat(status, gf_strerror(job->result),
                        sizeof(status) - strlen(status) - 1);
            }
            set_status(app, status);

            if (job->type == GFJOB_FETCH) {
                /*
                 * De lijst eerst echt leegmaken. Bij een afgebroken
                 * transfer staat er een gedeeltelijk gevulde lijst in het
                 * geheugen, en die tonen wekt de indruk dat dit alle
                 * releases zijn -- terwijl er juist iets misging.
                 */
                app->list->nreleases = 0;
                app->list->repo[0] = '\0';
                app->selected_release = -1;
                fill_release_list(app);
                fill_asset_list(app, -1);
            }
        }

        if (app->gadgets[GID_GAUGE])
            SetGadgetAttrs((struct Gadget *)app->gadgets[GID_GAUGE],
                           app->window, NULL, FUELGAUGE_Level, 0, TAG_END);

        update_button_states(app);
    }
}

/* --- venster opbouwen ---------------------------------------------------- */

static Object *make_label(const char *text)
{
    if (!cls_label.ptr && !cls_label.name)
        return NULL;
    return NEWOBJ(cls_label),
                     LABEL_Text, (ULONG)text,
                     TAG_END);
}

/*
 * window.class op OS 3.9 neemt een kant-en-klare menustrip aan; die maken
 * we zelf met GadTools. Daarvoor is VisualInfo nodig, en dat hangt aan een
 * scherm -- vandaar het publieke scherm vooraf vergrendelen.
 */
/*
 * De kolomkoppen en menuteksten staan in tabellen die de compiler statisch
 * aanlegt, en daar past geen functieaanroep in. Ze worden hier ingevuld,
 * nadat de catalog geopend is.
 */
static void localise_tables(void)
{
    release_columns[0].ci_Title = (STRPTR)gf_str(MSG_COL_VERSION);
    release_columns[1].ci_Title = (STRPTR)gf_str(MSG_COL_DATE);
    release_columns[2].ci_Title = (STRPTR)gf_str(MSG_COL_FILES);
    mark_columns[0].ci_Title    = (STRPTR)gf_str(MSG_COL_SAVED);
    asset_columns[0].ci_Title   = (STRPTR)gf_str(MSG_COL_FILE);
    asset_columns[1].ci_Title   = (STRPTR)gf_str(MSG_COL_SIZE);

    gf_menu[0].nm_Label = (STRPTR)gf_str(MSG_MENU_PROJECT);
    gf_menu[1].nm_Label = (STRPTR)gf_str(MSG_MENU_ABOUT);
    /* gf_menu[2] is de scheidingslijn */
    gf_menu[3].nm_Label = (STRPTR)gf_str(MSG_MENU_QUIT);
    gf_menu[4].nm_Label = (STRPTR)gf_str(MSG_MENU_SETTINGS);
    gf_menu[5].nm_Label = (STRPTR)gf_str(MSG_MENU_EDIT_SETTINGS);
}

/*
 * De onthouden vensterplaats bijstellen op het scherm van nu. Zonder deze
 * controle opent GitFetch buiten beeld zodra iemand naar een kleinere
 * schermmodus overstapt -- en dan is hij met geen muis meer te pakken.
 */
static void clamp_window_prefs(struct GFApp *app)
{
    struct Screen *screen;
    struct GFPrefs *p = &app->prefs;

    if (p->win_width <= 0 || p->win_height <= 0)
        return;

    screen = LockPubScreen(NULL);
    if (!screen)
        return;

    if (p->win_width > screen->Width)
        p->win_width = screen->Width;
    if (p->win_height > screen->Height)
        p->win_height = screen->Height;
    if (p->win_left < 0 || p->win_left + p->win_width > screen->Width)
        p->win_left = 0;
    if (p->win_top < 0 || p->win_top + p->win_height > screen->Height)
        p->win_top = (WORD)(screen->BarHeight + 1);

    UnlockPubScreen(NULL, screen);
}

/* Legt vast waar het venster nu staat. */
static void remember_window(struct GFApp *app)
{
    if (!app->window)
        return;

    app->prefs.win_left   = app->window->LeftEdge;
    app->prefs.win_top    = app->window->TopEdge;
    app->prefs.win_width  = app->window->Width;
    app->prefs.win_height = app->window->Height;
}

static void build_menu(struct GFApp *app)
{
    struct Screen *screen;

    if (!GadToolsBase)
        return;

    screen = LockPubScreen(NULL);
    if (!screen)
        return;

    app->visualinfo = GetVisualInfoA(screen, NULL);
    if (app->visualinfo) {
        app->menu = CreateMenus(gf_menu, TAG_END);
        if (app->menu &&
            !LayoutMenus(app->menu, app->visualinfo,
                         GTMN_NewLookMenus, TRUE, TAG_END)) {
            FreeMenus(app->menu);
            app->menu = NULL;
        }
    }

    UnlockPubScreen(NULL, screen);
}

static void free_menu(struct GFApp *app)
{
    if (app->menu) {
        FreeMenus(app->menu);
        app->menu = NULL;
    }
    if (app->visualinfo) {
        FreeVisualInfo(app->visualinfo);
        app->visualinfo = NULL;
    }
}

/* Bij een mislukte opbouw wil je weten welk object het begaf, niet alleen
   dat er iets misging -- op de Amiga is er geen debugger bij de hand. */
static const char *build_failure = NULL;

static Object *checked(Object *obj, const char *what)
{
    if (!obj && !build_failure)
        build_failure = what;
    return obj;
}

const char *gf_gui_failure(void)
{
    return build_failure ? build_failure : "onbekend";
}

static BOOL build_window(struct GFApp *app)
{
    Object *root, *toprow, *countrow = NULL, *lists, *bottomrow, *buttons;
    struct TagItem tags[32];
    LONG n;

    build_failure = NULL;
    localise_tables();
    clamp_window_prefs(app);
    GF_TRACE("menu opbouwen");
    build_menu(app);
    GF_TRACEP("menu", app->menu);

    app->gadgets[GID_REPO] = NEWOBJ(cls_string),
        GA_ID,            GID_REPO,
        GA_RelVerify,     TRUE,
        STRINGA_MaxChars, 200,
        TAG_END);

    GF_TRACEP("string.gadget object", app->gadgets[GID_REPO]);
    app->gadgets[GID_FETCH] = NEWOBJ(cls_button),
        GA_ID,        GID_FETCH,
        GA_RelVerify, TRUE,
        GA_Text,      (ULONG)gf_str(MSG_FETCH),
        TAG_END);

    /* Bewaren hoort bij wat er in het veld staat, dus die knop staat
       bovenaan. Verwijderen hoort bij wat je in de lijst aanwijst en
       staat daarom onder die lijst -- niet als wisselknop bovenin. */
    app->gadgets[GID_MARK_ADD] = NEWOBJ(cls_button),
        GA_ID,        GID_MARK_ADD,
        GA_RelVerify, TRUE,
        GA_Text,      (ULONG)gf_str(MSG_SAVE_MARK),
        TAG_END);

    GF_TRACEP("button.gadget object", app->gadgets[GID_FETCH]);
    /* "all" betekent: zoveel als in de instellingen is toegestaan. */
    count_values[COUNT_CHOICES - 1] =
        app->prefs.max_releases > 10 ? app->prefs.max_releases
                                     : GF_MAX_RELEASES;

    app->count_index = count_index_for(app->prefs.max_releases);

    if (ChooserBase) {
        build_count_list();
        app->gadgets[GID_COUNT] = NEWOBJ(cls_chooser),
            GA_ID,              GID_COUNT,
            GA_RelVerify,       TRUE,
            CHOOSER_Labels,     (ULONG)&count_list,
            CHOOSER_Active,     (ULONG)app->count_index,
            /* PopUp in plaats van DropDown: die toont de gekozen regel
               in het gadget zelf, met het pijltje ervoor -- zoals de
               keuzevelden in de systeemvoorkeuren. DropDown liet alleen
               een pijltje over. */
            CHOOSER_PopUp,      TRUE,
            CHOOSER_AutoFit,    TRUE,
            TAG_END);
    }

    app->gadgets[GID_MARKS] = NEWOBJ(cls_listbrowser),
        GA_ID,                       GID_MARKS,
        GA_RelVerify,                TRUE,
        LISTBROWSER_ColumnInfo,      (ULONG)mark_columns,
        LISTBROWSER_ColumnTitles,    TRUE,
        LISTBROWSER_ShowSelected,    TRUE,
        LISTBROWSER_HorizSeparators, TRUE,
        TAG_END);

    app->gadgets[GID_MARK_DEL] = NEWOBJ(cls_button),
        GA_ID, GID_MARK_DEL, GA_RelVerify, TRUE,
        GA_Text, (ULONG)gf_str(MSG_DELETE_MARK), TAG_END);

    app->gadgets[GID_RELEASES] = NEWOBJ(cls_listbrowser),
        GA_ID,                     GID_RELEASES,
        GA_RelVerify,              TRUE,
        LISTBROWSER_ColumnInfo,    (ULONG)release_columns,
        LISTBROWSER_ColumnTitles,  TRUE,
        LISTBROWSER_ShowSelected,  TRUE,
        LISTBROWSER_HorizSeparators, TRUE,
        TAG_END);

    GF_TRACEP("listbrowser 1", app->gadgets[GID_RELEASES]);
    app->gadgets[GID_ASSETS] = NEWOBJ(cls_listbrowser),
        GA_ID,                     GID_ASSETS,
        GA_RelVerify,              TRUE,
        LISTBROWSER_ColumnInfo,    (ULONG)asset_columns,
        LISTBROWSER_ColumnTitles,  TRUE,
        LISTBROWSER_ShowSelected,  TRUE,
        LISTBROWSER_HorizSeparators, TRUE,
        TAG_END);

    GF_TRACEP("listbrowser 2", app->gadgets[GID_ASSETS]);
    if (GetFileBase)
        app->gadgets[GID_DEST] = NEWOBJ(cls_getfile),
            GA_ID,               GID_DEST,
            GA_RelVerify,        TRUE,
            GETFILE_Drawer,      (ULONG)app->prefs.destdir,
            GETFILE_DrawersOnly, TRUE,
            GETFILE_TitleText,   (ULONG)gf_str(MSG_SAVE_IN),
            TAG_END);

    GF_TRACEP("getfile.gadget", app->gadgets[GID_DEST]);
    if (FuelGaugeBase)
        app->gadgets[GID_GAUGE] = NEWOBJ(cls_fuelgauge),
            GA_ID,                   GID_GAUGE,
            FUELGAUGE_Min,           0,
            FUELGAUGE_Max,           100,
            FUELGAUGE_Level,         0,
            FUELGAUGE_Percent,       TRUE,
            FUELGAUGE_Justification, FGJ_CENTER,
            TAG_END);

    /* Statusregel als niet-bewerkbaar tekstveld: eenvoudig bij te werken
       en het schaalt netjes mee met de layout. */
    GF_TRACEP("fuelgauge.gadget", app->gadgets[GID_GAUGE]);
    app->gadgets[GID_STATUS] = NEWOBJ(cls_string),
        GA_ID,            GID_STATUS,
        GA_ReadOnly,      TRUE,
        STRINGA_TextVal,  (ULONG)"",
        TAG_END);

    app->gadgets[GID_DOWNLOAD] = NEWOBJ(cls_button),
        GA_ID,        GID_DOWNLOAD,
        GA_RelVerify, TRUE,
        GA_Text,      (ULONG)gf_str(MSG_DOWNLOAD),
        TAG_END);

    app->gadgets[GID_STOP] = NEWOBJ(cls_button),
        GA_ID,        GID_STOP,
        GA_RelVerify, TRUE,
        GA_Text,      (ULONG)gf_str(MSG_STOP),
        GA_Disabled,  TRUE,
        TAG_END);

    if (!checked(app->gadgets[GID_REPO],     "string.gadget")     ||
        !checked(app->gadgets[GID_FETCH],    "button.gadget")     ||
        !checked(app->gadgets[GID_RELEASES], "listbrowser.gadget (releases)") ||
        !checked(app->gadgets[GID_ASSETS],   "listbrowser.gadget (assets)")   ||
        !checked(app->gadgets[GID_DOWNLOAD], "button.gadget (download)")      ||
        !checked(app->gadgets[GID_STOP],     "button.gadget (stop)")          ||
        !checked(app->gadgets[GID_STATUS],   "string.gadget (status)"))
        return FALSE;

    GF_TRACE("gadgets klaar, nu de layouts");
    {
        struct TagItem t[16];
        LONG k = 0;

#define TOP_TAG(tag, data) do { \
        t[k].ti_Tag = (Tag)(tag); t[k].ti_Data = (ULONG)(data); k++; \
    } while (0)

        TOP_TAG(LAYOUT_Orientation, LAYOUT_ORIENT_HORIZ);
        TOP_TAG(LAYOUT_SpaceInner, TRUE);
        TOP_TAG(LAYOUT_AddChild, app->gadgets[GID_REPO]);
        TOP_TAG(CHILD_Label, make_label(gf_str(MSG_REPOSITORY)));
        TOP_TAG(LAYOUT_AddChild, app->gadgets[GID_MARK_ADD]);
        TOP_TAG(CHILD_WeightedWidth, 0);
        TOP_TAG(LAYOUT_AddChild, app->gadgets[GID_FETCH]);
        TOP_TAG(CHILD_WeightedWidth, 0);
        TOP_TAG(TAG_END, 0);
#undef TOP_TAG

        toprow = NewObjectA(cls_layout.ptr, cls_layout.name, t);
    }

    GF_TRACEP("toprow", toprow);
    {
        /* De bewaarde repositories krijgen een eigen kolom met knoppen
           eronder; daarnaast de releases en hun bestanden. */
        Object *markcol, *markbuttons;

        markbuttons = NEWOBJ(cls_layout),
            LAYOUT_Orientation, LAYOUT_ORIENT_HORIZ,
            LAYOUT_SpaceInner,  TRUE,
            LAYOUT_AddChild,    (ULONG)app->gadgets[GID_MARK_DEL],
            TAG_END);

        markcol = NEWOBJ(cls_layout),
            LAYOUT_Orientation, LAYOUT_ORIENT_VERT,
            LAYOUT_SpaceInner,  TRUE,
            LAYOUT_AddChild,    (ULONG)app->gadgets[GID_MARKS],
            LAYOUT_AddChild,    (ULONG)markbuttons,
                CHILD_WeightedHeight, 0,
            TAG_END);

        /*
     * De keuze hoeveel releases opgehaald worden staat vlak onder het
     * invoerveld: daar heeft hij de volle breedte, en hij hoort bij het
     * ophalen -- dus dicht bij die knop.
     */
    if (app->gadgets[GID_COUNT]) {
        countrow = NEWOBJ(cls_layout),
            LAYOUT_Orientation, LAYOUT_ORIENT_HORIZ,
            LAYOUT_SpaceInner,  TRUE,
            LAYOUT_AddChild,    (ULONG)app->gadgets[GID_COUNT],
                CHILD_Label,    (ULONG)make_label(gf_str(MSG_PREFS_COUNT)),
            TAG_END);
    }

    lists = NEWOBJ(cls_layout),
            LAYOUT_Orientation, LAYOUT_ORIENT_HORIZ,
            LAYOUT_SpaceInner,  TRUE,
            LAYOUT_AddChild,    (ULONG)markcol,
                CHILD_WeightedWidth, 26,
            LAYOUT_AddChild,    (ULONG)app->gadgets[GID_RELEASES],
                CHILD_WeightedWidth, 33,
            LAYOUT_AddChild,    (ULONG)app->gadgets[GID_ASSETS],
                CHILD_WeightedWidth, 41,
            TAG_END);
    }

    GF_TRACEP("lists", lists);
    /*
     * De voortgangsbalk deelt een rij met de knoppen. Op een PAL-scherm
     * van 640x256 telt elke regel: het venster moet daar in zijn geheel
     * op passen, inclusief titelbalk.
     */
    {
        struct TagItem b[12];
        LONG k = 0;

#define BTN_TAG(tag, data) do { \
        b[k].ti_Tag = (Tag)(tag); b[k].ti_Data = (ULONG)(data); k++; \
    } while (0)

        BTN_TAG(LAYOUT_Orientation, LAYOUT_ORIENT_HORIZ);
        BTN_TAG(LAYOUT_SpaceInner, TRUE);
        if (app->gadgets[GID_GAUGE]) {
            BTN_TAG(LAYOUT_AddChild, app->gadgets[GID_GAUGE]);
            BTN_TAG(CHILD_WeightedWidth, 100);
        }
        BTN_TAG(LAYOUT_AddChild, app->gadgets[GID_DOWNLOAD]);
        BTN_TAG(CHILD_WeightedWidth, 0);
        BTN_TAG(LAYOUT_AddChild, app->gadgets[GID_STOP]);
        BTN_TAG(CHILD_WeightedWidth, 0);
        BTN_TAG(TAG_END, 0);
#undef BTN_TAG

        buttons = NewObjectA(cls_layout.ptr, cls_layout.name, b);
    }

    if (!checked(toprow, "toprow") || !checked(lists, "lists") ||
        !checked(buttons, "buttons"))
        return FALSE;

    /*
     * De onderste rij heeft optionele kinderen (getfile en fuelgauge zijn
     * niet essentieel). LAYOUT_AddChild werkt alleen bij het aanmaken van
     * het object -- kinderen er achteraf met SetAttrs bij zetten doet niets.
     * Daarom de taglijst eerst opbouwen en het object in een keer maken.
     */
    n = 0;
#define ADD_TAG(t, d) do { \
        tags[n].ti_Tag = (Tag)(t); tags[n].ti_Data = (ULONG)(d); n++; \
    } while (0)

    ADD_TAG(LAYOUT_Orientation, LAYOUT_ORIENT_VERT);
    ADD_TAG(LAYOUT_SpaceInner, TRUE);

    if (app->gadgets[GID_DEST]) {
        ADD_TAG(LAYOUT_AddChild, app->gadgets[GID_DEST]);
        ADD_TAG(CHILD_Label, make_label(gf_str(MSG_SAVE_IN)));
        ADD_TAG(CHILD_WeightedHeight, 0);
    }
    ADD_TAG(LAYOUT_AddChild, app->gadgets[GID_STATUS]);
    ADD_TAG(CHILD_WeightedHeight, 0);
    ADD_TAG(LAYOUT_AddChild, buttons);
    ADD_TAG(CHILD_WeightedHeight, 0);
    ADD_TAG(TAG_END, 0);
#undef ADD_TAG

    bottomrow = NewObjectA(cls_layout.ptr, cls_layout.name, tags);
    if (!checked(bottomrow, "bottomrow"))
        return FALSE;

    GF_TRACEP("bottomrow", bottomrow);
    {
        struct TagItem r[16];
        LONG q = 0;

#define ROOT_TAG(tag, data) do { \
        r[q].ti_Tag = (Tag)(tag); r[q].ti_Data = (ULONG)(data); q++; \
    } while (0)

        ROOT_TAG(LAYOUT_Orientation, LAYOUT_ORIENT_VERT);
        ROOT_TAG(LAYOUT_SpaceOuter, TRUE);
        ROOT_TAG(LAYOUT_SpaceInner, TRUE);
        ROOT_TAG(LAYOUT_AddChild, toprow);
        ROOT_TAG(CHILD_WeightedHeight, 0);
        if (countrow) {
            ROOT_TAG(LAYOUT_AddChild, countrow);
            ROOT_TAG(CHILD_WeightedHeight, 0);
        }
        ROOT_TAG(LAYOUT_AddChild, lists);   /* de lijsten krijgen de ruimte */
        ROOT_TAG(LAYOUT_AddChild, bottomrow);
        ROOT_TAG(CHILD_WeightedHeight, 0);
        ROOT_TAG(TAG_END, 0);
#undef ROOT_TAG

        root = NewObjectA(cls_layout.ptr, cls_layout.name, r);
    }

    if (!checked(root, "root-layout"))
        return FALSE;

    GF_TRACEP("root", root);
    GF_TRACE("window.class object maken");
    app->winobj = NEWOBJ(cls_window),
        WA_Title,          (ULONG)gf_str(MSG_WINDOW_TITLE),
        WA_ScreenTitle,    (ULONG)gf_str(MSG_SCREEN_TITLE),
        WA_Activate,       TRUE,
        WA_DepthGadget,    TRUE,
        WA_DragBar,        TRUE,
        WA_CloseGadget,    TRUE,
        WA_SizeGadget,     TRUE,
        /*
         * Bescheiden beginformaat: op een standaard PAL-scherm (640x256)
         * moet het venster er in zijn geheel op passen. Groter mag altijd;
         * de layout schaalt mee.
         */
        WA_Left,           (ULONG)app->prefs.win_left,
        WA_Top,            (ULONG)app->prefs.win_top,
        WA_Width,          (ULONG)(app->prefs.win_width > 0
                                    ? app->prefs.win_width : 0),
        WA_Height,         (ULONG)(app->prefs.win_height > 0
                                    ? app->prefs.win_height : 0),
        WA_InnerWidth,     (ULONG)(app->prefs.win_width > 0 ? 0 : 600),
        WA_InnerHeight,    (ULONG)(app->prefs.win_height > 0 ? 0 : 180),
        WINDOW_Position,   WPOS_CENTERSCREEN,
        /* Wegklikken naar Workbench: een download van een paar minuten
           wil je niet in beeld hoeven houden. */
        WINDOW_IconifyGadget, TRUE,
        WINDOW_IconTitle,     (ULONG)"GitFetch",
        WINDOW_ParentGroup,(ULONG)root,
        WINDOW_MenuStrip,  (ULONG)app->menu,
        TAG_END);

    return checked(app->winobj, "window.class") ? TRUE : FALSE;
}

/* --- event-lus ----------------------------------------------------------- */

/*
 * Het informatievenster. EasyRequest is hiervoor de gebruikelijke weg op
 * AmigaOS: het volgt vanzelf het scherm en de systeeminstellingen, en het
 * hoeft niets te onthouden.
 */
/*
 * Hoeveel bytes er vrij zijn op het volume waar 'dir' op staat, of 0 als
 * dat niet te bepalen is.
 *
 * Deelt eerst en vermenigvuldigt daarna, want blokken maal blokgrootte
 * loopt op een grote harde schijf over de 32 bits heen.
 */
static ULONG free_space(const char *dir)
{
    struct InfoData *id;
    BPTR lock;
    ULONG kb = 0;

    lock = Lock((STRPTR)dir, ACCESS_READ);
    if (!lock)
        return 0;

    /* Info() wil een long-word uitgelijnde InfoData. AllocVec levert dat;
       een struct op de stack is op m68k maar op 2 bytes uitgelijnd. */
    id = AllocVec(sizeof(struct InfoData), MEMF_ANY);
    if (id) {
        if (Info(lock, id) && id->id_NumBlocks >= id->id_NumBlocksUsed) {
            ULONG blocks = (ULONG)(id->id_NumBlocks - id->id_NumBlocksUsed);
            ULONG bsize  = (ULONG)id->id_BytesPerBlock;

            kb = (bsize >= 1024) ? blocks * (bsize / 1024)
                                 : blocks / (1024 / (bsize ? bsize : 512));
        }
        FreeVec(id);
    }

    UnLock(lock);
    return kb;
}

/*
 * Waarschuwt als er te weinig ruimte lijkt. Geeft niet-nul om door te gaan.
 * Bij twijfel (ruimte onbekend) gaat het gewoon door: liever een download
 * die halverwege strandt dan een die ten onrechte geweigerd wordt.
 */
static LONG check_space(struct GFApp *app, const char *name,
                        const char *dir, ULONG size)
{
    struct EasyStruct es;
    ULONG free_kb = free_space(dir);
    ULONG need_kb = (size + 1023) / 1024;

    if (free_kb == 0 || free_kb >= need_kb)
        return 1;

    es.es_StructSize   = sizeof(struct EasyStruct);
    es.es_Flags        = 0;
    es.es_Title        = (STRPTR)gf_str(MSG_WINDOW_TITLE);
    es.es_TextFormat   = (STRPTR)gf_str(MSG_NO_SPACE);
    es.es_GadgetFormat = (STRPTR)gf_str(MSG_OVERWRITE_GADGETS);

    return EasyRequest(app->window, &es, NULL,
                       (ULONG)name, (ULONG)need_kb, (ULONG)free_kb);
}

/*
 * Vraagt of een bestaand bestand overschreven mag worden. Geeft niet-nul
 * bij overschrijven.
 */
static LONG ask_overwrite(struct GFApp *app, const char *name,
                          const char *dir, LONG size)
{
    struct EasyStruct es;

    es.es_StructSize   = sizeof(struct EasyStruct);
    es.es_Flags        = 0;
    es.es_Title        = (STRPTR)gf_str(MSG_WINDOW_TITLE);
    es.es_TextFormat   = (STRPTR)gf_str(MSG_FILE_EXISTS);
    es.es_GadgetFormat = (STRPTR)gf_str(MSG_OVERWRITE_GADGETS);

    return EasyRequest(app->window, &es, NULL,
                       (ULONG)name, (ULONG)dir, (ULONG)size);
}

static void show_about(struct GFApp *app)
{
    struct EasyStruct es;

    es.es_StructSize   = sizeof(struct EasyStruct);
    es.es_Flags        = 0;
    es.es_Title        = (STRPTR)gf_str(MSG_ABOUT_TITLE);
    es.es_TextFormat   = (STRPTR)gf_str(MSG_ABOUT_BODY);
    es.es_GadgetFormat = (STRPTR)gf_str(MSG_ABOUT_CLOSE);

    EasyRequest(app->window, &es, NULL,
                (ULONG)GF_VERSION,
                (ULONG)(app->prefs.backend == GF_BACKEND_NATIVE
                        ? gf_str(MSG_VIA_NATIVE)
                        : gf_str(MSG_VIA_PROXY)));
}

static void handle_menu(struct GFApp *app, UWORD code)
{
    struct MenuItem *item;

    while (code != MENUNULL) {
        item = ItemAddress(app->menu, code);
        if (!item)
            break;

        switch ((ULONG)GTMENUITEM_USERDATA(item)) {
        case MID_ABOUT:
            show_about(app);
            break;
        case MID_PREFS:
            if (gf_prefs_window(app))
                set_status(app, gf_str(MSG_SETTINGS_CHANGED));
            break;
        case MID_QUIT:
            app->done = TRUE;
            break;
        }
        code = item->NextSelect;
    }
}

static void handle_window_input(struct GFApp *app)
{
    ULONG result;
    UWORD code = 0;

    while ((result = DoMethod(app->winobj, WM_HANDLEINPUT, &code))
           != WMHI_LASTMSG) {
        switch (result & WMHI_CLASSMASK) {
        case WMHI_CLOSEWINDOW:
            remember_window(app);
            app->done = TRUE;
            break;

        case WMHI_ICONIFY:
            remember_window(app);
            /* Het venster verdwijnt; de lopende download gaat gewoon door,
               want die draait in een eigen proces. */
            DoMethod(app->winobj, WM_ICONIFY);
            app->window = NULL;
            app->iconified = TRUE;
            break;

        case WMHI_UNICONIFY:
            app->window = (struct Window *)DoMethod(app->winobj, WM_OPEN);
            app->iconified = FALSE;
            break;

        case WMHI_MENUPICK:
            handle_menu(app, code);
            break;

        case WMHI_GADGETUP:
            switch (result & WMHI_GADGETMASK) {
            case GID_FETCH:
            case GID_REPO:          /* Enter in het tekstveld haalt ook op */
                action_fetch(app);
                break;

            case GID_RELEASES: {
                ULONG sel = ~0UL;
                GetAttr(LISTBROWSER_Selected, app->gadgets[GID_RELEASES], &sel);
                if (sel != ~0UL && (WORD)sel < (WORD)app->list->nreleases) {
                    struct Release *rel;
                    char msg[GF_MAX_ERR];

                    app->selected_release = (WORD)sel;
                    fill_asset_list(app, app->selected_release);

                    rel = &app->list->releases[app->selected_release];
                    if (rel->nassets == 0) {
                        sprintf(msg, gf_str(MSG_RELEASE_NO_FILES), rel->tag);
                    } else {
                        sprintf(msg, gf_str(MSG_RELEASE_FILES),
                                rel->tag, (long)rel->nassets);
                    }
                    set_status(app, msg);
                }
                break;
            }

            case GID_COUNT: {
                /* Minder releases ophalen scheelt vooral bij de
                   rechtstreekse verbinding een hoop verkeer. */
                ULONG active = 0;

                GetAttr(CHOOSER_Active, app->gadgets[GID_COUNT], &active);
                if (active < COUNT_CHOICES) {
                    app->count_index = (UWORD)active;
                    app->prefs.max_releases = count_values[active];
                }
                break;
            }

            case GID_MARKS: {
                ULONG sel = ~0UL, event = 0;

                GetAttr(LISTBROWSER_Selected, app->gadgets[GID_MARKS], &sel);
                GetAttr(LISTBROWSER_RelEvent, app->gadgets[GID_MARKS], &event);

                if (sel != ~0UL && (WORD)sel < (WORD)app->marks.count) {
                    SetGadgetAttrs((struct Gadget *)app->gadgets[GID_REPO],
                                   app->window, NULL,
                                   STRINGA_TextVal,
                                   (ULONG)app->marks.repo[sel], TAG_END);
                
                    /*
                     * Alleen aanklikken zet de naam in het veld; pas een
                     * dubbelklik haalt op. Anders kost het langslopen van
                     * de lijst bij elke regel een verbinding en een paar
                     * honderd kilobyte.
                     */
                    if (event & LBRE_DOUBLECLICK)
                        action_fetch(app);
                    else
                        set_status(app, gf_str(MSG_HINT_DOUBLECLICK));
                }
                break;
            }

            case GID_MARK_ADD:
                action_bookmark_add(app);
                break;

            case GID_MARK_DEL:
                action_bookmark_remove(app);
                break;

            case GID_DOWNLOAD:
                action_download(app);
                break;

            case GID_STOP:
                action_stop(app);
                break;
            }
            break;
        }
    }
}

/* --- opzetten en afbreken ------------------------------------------------ */

static BOOL open_classes(void)
{
    struct ClassLib *cl;

    GadToolsBase = OpenLibrary("gadtools.library", 39);

    for (cl = class_libs; cl->base; cl++) {
        *cl->base = OpenLibrary(cl->name, 44);
        GF_TRACEP(cl->name, *cl->base);
        if (!*cl->base && cl->required) {
            Printf("Cannot open %s (version 44 or newer required).\n",
                   (ULONG)cl->name);
            return FALSE;
        }
    }
    fetch_classes();
    return TRUE;
}

/*
 * De class-pointers ophalen. Alleen aanroepen als de bijbehorende library
 * geopend is: de inline-stubs springen via die base, en met een lege base
 * is dat een sprong het niets in.
 */
static void fetch_classes(void)
{
    cls_window.name      = (CONST_STRPTR)"window.class";
    cls_layout.name      = (CONST_STRPTR)"layout.gadget";
    cls_string.name      = (CONST_STRPTR)"string.gadget";
    cls_button.name      = (CONST_STRPTR)"button.gadget";
    cls_listbrowser.name = (CONST_STRPTR)"listbrowser.gadget";
    cls_fuelgauge.name   = (CONST_STRPTR)"fuelgauge.gadget";
    cls_getfile.name     = (CONST_STRPTR)"getfile.gadget";
    cls_label.name       = (CONST_STRPTR)"label.image";
    cls_chooser.name     = (CONST_STRPTR)"chooser.gadget";
    cls_checkbox.name    = (CONST_STRPTR)"checkbox.gadget";
    cls_integer.name     = (CONST_STRPTR)"integer.gadget";

    cls_window.ptr      = WindowBase      ? WINDOW_GetClass()      : NULL;
    cls_layout.ptr      = LayoutBase      ? LAYOUT_GetClass()      : NULL;
    cls_string.ptr      = StringBase      ? STRING_GetClass()      : NULL;
    cls_button.ptr      = ButtonBase      ? BUTTON_GetClass()      : NULL;
    cls_listbrowser.ptr = ListBrowserBase ? LISTBROWSER_GetClass() : NULL;
    cls_fuelgauge.ptr   = FuelGaugeBase   ? FUELGAUGE_GetClass()   : NULL;
    cls_getfile.ptr     = GetFileBase     ? GETFILE_GetClass()     : NULL;
    cls_label.ptr       = LabelBase       ? LABEL_GetClass()       : NULL;
    cls_chooser.ptr     = ChooserBase     ? CHOOSER_GetClass()     : NULL;
    cls_checkbox.ptr    = CheckBoxBase    ? CHECKBOX_GetClass()    : NULL;
    cls_integer.ptr     = IntegerBase     ? INTEGER_GetClass()     : NULL;

    GF_TRACEP("window.class",       cls_window.ptr);
    GF_TRACEP("layout.gadget",      cls_layout.ptr);
    GF_TRACEP("string.gadget",      cls_string.ptr);
    GF_TRACEP("button.gadget",      cls_button.ptr);
    GF_TRACEP("listbrowser.gadget", cls_listbrowser.ptr);
    GF_TRACEP("fuelgauge.gadget",   cls_fuelgauge.ptr);
    GF_TRACEP("getfile.gadget",     cls_getfile.ptr);
    GF_TRACEP("label.image",        cls_label.ptr);
}

static void close_classes(void)
{
    struct ClassLib *cl;

    for (cl = class_libs; cl->base; cl++) {
        if (*cl->base) {
            CloseLibrary(*cl->base);
            *cl->base = NULL;
        }
    }
    if (GadToolsBase) {
        CloseLibrary(GadToolsBase);
        GadToolsBase = NULL;
    }
}

LONG gf_gui_run(struct GFPrefs *prefs)
{
    struct GFApp *app;
    ULONG winsig = 0, portsig, progsig, sigs;
    LONG rc = RETURN_OK;

    app = AllocVec(sizeof(struct GFApp), MEMF_ANY | MEMF_CLEAR);
    if (!app)
        return RETURN_FAIL;

    app->prefs = *prefs;
    app->selected_release = -1;
    app->progress_signal = -1;

    app->list = AllocVec(sizeof(struct ReleaseList), MEMF_ANY | MEMF_CLEAR);
    init_list(&app->mark_list);
    init_list(&app->release_list);
    init_list(&app->asset_list);

    gf_bookmarks_load(&app->marks);

    app->replyport = CreateMsgPort();
    app->progress_signal = AllocSignal(-1);

    if (!app->list || !app->replyport || app->progress_signal < 0) {
        rc = RETURN_FAIL;
        goto cleanup;
    }

    if (!open_classes()) {
        Printf("GitFetch needs the ReAction classes "
               "(AmigaOS 3.5 or newer).\n");
        rc = RETURN_FAIL;
        goto cleanup;
    }

    if (!build_window(app)) {
        Printf("Cannot build the window: %s is missing.\n",
               (ULONG)gf_gui_failure());
        rc = RETURN_FAIL;
        goto cleanup;
    }

    GF_TRACE("venster openen");
    app->window = (struct Window *)DoMethod(app->winobj, WM_OPEN);
    GF_TRACEP("window", app->window);
    if (!app->window) {
        Printf("Cannot open the window.\n");
        rc = RETURN_FAIL;
        goto cleanup;
    }

    GetAttr(WINDOW_SigMask, app->winobj, &winsig);
    portsig = 1UL << app->replyport->mp_SigBit;
    progsig = 1UL << app->progress_signal;

    fill_mark_list(app);

    if (app->marks.count)
        set_status(app, gf_str(MSG_HINT_PICK));
    else
        set_status(app, gf_str(MSG_HINT_TYPE));

    while (!app->done) {
        /* Bij het iconificeren wisselt het venster van signaal naar dat van
           het AppIcon, dus elke ronde opnieuw opvragen. */
        GetAttr(WINDOW_SigMask, app->winobj, &winsig);

        sigs = Wait(winsig | portsig | progsig | SIGBREAKF_CTRL_C);

        if (sigs & progsig)
            update_progress(app);

        if (sigs & portsig)
            handle_worker_reply(app);

        if (sigs & winsig)
            handle_window_input(app);

        if (sigs & SIGBREAKF_CTRL_C)
            app->done = TRUE;
    }

    /* Een lopende download mag het venster niet overleven: de worker
       schrijft in geheugen dat we zo vrijgeven. */
    if (app->job_active) {
        set_status(app, gf_str(MSG_ABORTING));
        gf_worker_abort(&app->job);
        while (app->job_active) {
            WaitPort(app->replyport);
            handle_worker_reply(app);
        }
    }

cleanup:
    if (app->window)
        remember_window(app);
    if (app->prefs.win_width > 0)
        gf_save_window_prefs(&app->prefs);

    if (app->winobj) {
        detach_list(app, GID_MARKS, &app->mark_list,
                    &app->mark_list_attached);
        detach_list(app, GID_RELEASES, &app->release_list,
                    &app->release_list_attached);
        detach_list(app, GID_ASSETS, &app->asset_list,
                    &app->asset_list_attached);
        DisposeObject(app->winobj);     /* ruimt ook alle kinderen op */
    }
    free_menu(app);
    close_classes();

    if (app->progress_signal >= 0)
        FreeSignal(app->progress_signal);
    if (app->replyport)
        DeleteMsgPort(app->replyport);
    if (app->list)
        FreeVec(app->list);
    FreeVec(app);

    return rc;
}
