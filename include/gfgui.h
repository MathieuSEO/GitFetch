#ifndef GFGUI_H
#define GFGUI_H

#include <exec/types.h>
#include <exec/ports.h>
#include <intuition/classes.h>

#include "gitfetch.h"
#include "gfbackend.h"
#include "gfworker.h"
#include "gfbookmarks.h"

/* Gadget-ids; ook de sleutel in de event-lus. */
#define GID_REPO      1
#define GID_FETCH     2
#define GID_RELEASES  3
#define GID_ASSETS    4
#define GID_DEST      5
#define GID_DOWNLOAD  6
#define GID_STOP      7
#define GID_GAUGE     8
#define GID_STATUS    9
#define GID_COUNT    10     /* dropdown: hoeveel releases ophalen */
#define GID_MARKS    11     /* lijst met onthouden repositories */
#define GID_MARK_ADD 12
#define GID_MARK_DEL 13

struct GFApp {
    struct GFPrefs      prefs;
    struct ReleaseList *list;

    Object             *winobj;
    struct Window      *window;
    Object             *gadgets[20];
    struct MsgPort     *replyport;
    struct Menu        *menu;
    APTR                visualinfo;

    struct Bookmarks    marks;
    struct List         mark_list;
    BOOL                mark_list_attached;

    struct List         release_list;
    struct List         asset_list;
    BOOL                release_list_attached;
    BOOL                asset_list_attached;

    struct GFJob        job;
    BOOL                job_active;
    BYTE                progress_signal;

    BOOL                iconified;
    UWORD               count_index;    /* stand van de doorschakelknop */
    WORD                selected_release;
    BOOL                done;
};

/*
 * Een class wordt met pointer en naam aangeduid; NewObject gebruikt de
 * pointer als die er is en anders de naam. Zie de uitleg in gui.c.
 */
struct ClassRef {
    struct IClass *ptr;
    CONST_STRPTR   name;
};

LONG gf_gui_run(struct GFPrefs *prefs);
BOOL gf_prefs_window(struct GFApp *app);

/* Bewaart alleen de vensterplaats; wordt bij het afsluiten aangeroepen,
   zodat je daar niet apart om hoeft te vragen. */
void gf_save_window_prefs(struct GFPrefs *prefs);

#endif /* GFGUI_H */
