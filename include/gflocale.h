/*
 * gflocale.h -- vertaalbare teksten.
 *
 * Volgt de gebruikelijke opzet op AmigaOS: de teksten in de broncode zijn
 * Engels en dienen als terugval, vertalingen komen uit een catalog die
 * locale.library laadt op grond van de voorkeuren van de gebruiker.
 * Ontbreekt de catalog, dan werkt het programma gewoon in het Engels.
 *
 * Nieuwe tekst toevoegen: een id onderaan de lijst hieronder, de Engelse
 * tekst op dezelfde plaats in de tabel in locale.c, en de vertaling in
 * catalogs/nederlands.ct. "make catalogs" controleert of alles klopt.
 */

#ifndef GFLOCALE_H
#define GFLOCALE_H

#include <exec/types.h>

enum {
    /* venster en menu */
    MSG_WINDOW_TITLE,
    MSG_SCREEN_TITLE,
    MSG_MENU_PROJECT,
    MSG_MENU_ABOUT,
    MSG_MENU_QUIT,
    MSG_MENU_SETTINGS,
    MSG_MENU_EDIT_SETTINGS,

    /* hoofdvenster */
    MSG_REPOSITORY,
    MSG_FETCH,
    MSG_SAVE_MARK,
    MSG_FORGET_MARK,
    MSG_DELETE_MARK,
    MSG_DOWNLOAD,
    MSG_STOP,
    MSG_SAVE_IN,

    /* kolomkoppen */
    MSG_COL_SAVED,
    MSG_COL_VERSION,
    MSG_COL_DATE,
    MSG_COL_FILES,
    MSG_COL_FILE,
    MSG_COL_SIZE,

    /* keuzelijst */
    MSG_COUNT_NEWEST,
    MSG_COUNT_3,
    MSG_COUNT_5,
    MSG_COUNT_10,
    MSG_COUNT_ALL,

    /* statusmeldingen */
    MSG_HINT_TYPE,
    MSG_HINT_PICK,
    MSG_HINT_DOUBLECLICK,
    MSG_NEED_REPO,
    MSG_BAD_REPO,
    MSG_FETCHING,
    MSG_NO_RELEASES,
    MSG_ONE_RELEASE,
    MSG_N_RELEASES,
    MSG_RELEASE_NO_FILES,
    MSG_RELEASE_FILES,
    MSG_PICK_RELEASE,
    MSG_PICK_FILE,
    MSG_DOWNLOADING,
    MSG_DONE_SAVED,
    MSG_ABORTING,
    MSG_ERROR_PREFIX,
    MSG_SAVED_MARK,
    MSG_UNSAVED_MARK,
    MSG_MARK_EXISTS,
    MSG_PICK_MARK,
    MSG_MARK_REMOVED,
    MSG_ENVARC_FAILED,
    MSG_NO_WORKER,
    MSG_SETTINGS_CHANGED,
    MSG_FILE_EXISTS,
    MSG_OVERWRITE_GADGETS,
    MSG_DOWNLOAD_CANCELLED,
    MSG_RATE_LIMIT_HINT,
    MSG_NO_SPACE,

    /* instellingenvenster */
    MSG_PREFS_TITLE,
    MSG_PREFS_NATIVE,
    MSG_PREFS_VERIFY,
    MSG_PREFS_HOST,
    MSG_PREFS_PORT,
    MSG_PREFS_COUNT,
    MSG_PREFS_SAVE,
    MSG_PREFS_USE,
    MSG_PREFS_CANCEL,

    /* informatievenster */
    MSG_ABOUT_TITLE,
    MSG_ABOUT_BODY,
    MSG_ABOUT_CLOSE,
    MSG_VIA_NATIVE,
    MSG_VIA_PROXY,

    /*
     * Hieronder alles wat er in 0.2 bij kwam. Nieuwe teksten komen steeds
     * onderaan: de positie is het nummer in de catalog, en een bestaande
     * vertaling klopt alleen zolang die nummers niet verschuiven.
     */

    /* bestandenlijst */
    MSG_FILES_NONE,
    MSG_FILES_SOURCE,
    MSG_BETA,
    MSG_RELEASE_SOURCE_ONLY,
    MSG_RELEASE_FILES_MORE,
    MSG_SIZE_MB,
    MSG_SIZE_KB,
    MSG_SIZE_B,

    /* notificaties */
    MSG_NOTIFY_DONE,
    MSG_NOTIFY_FAILED,
    MSG_NOTIFY_SAVED,
    MSG_PREFS_NOTIFY,

    /* foutmeldingen, in de volgorde van de GF_ERR_-codes (-1 t/m -14) */
    MSG_ERR_NOMEM,
    MSG_ERR_SOCKET,
    MSG_ERR_DNS,
    MSG_ERR_CONNECT,
    MSG_ERR_SEND,
    MSG_ERR_RECV,
    MSG_ERR_HTTP,
    MSG_ERR_PROTO,
    MSG_ERR_SERVER,
    MSG_ERR_ABORTED,
    MSG_ERR_FILE,
    MSG_ERR_URL,
    MSG_ERR_TLS,
    MSG_ERR_CLOCK,
    MSG_ERR_UNKNOWN,

    /* rechtstreekse verbinding */
    MSG_TLS_NO_SOCKET,
    MSG_TLS_CLOCK,
    MSG_TLS_NO_AMISSL,
    MSG_TLS_INIT,
    MSG_TLS_CONTEXT,
    MSG_TLS_NO_CA,
    MSG_TLS_NO_ANSWER,

    MSG_LAST
};

/* Opent de catalog voor de ingestelde taal; zonder catalog blijft alles
   Engels. Veilig om aan te roepen als locale.library ontbreekt. Met een
   taalnaam ("nederlands") gaat die voor de systeeminstelling; NULL of
   leeg volgt Prefs/Locale. */
void gf_locale_open(const char *language);
void gf_locale_close(void);

/* De vertaalde tekst, of de ingebouwde Engelse als er geen catalog is. */
const char *gf_str(UWORD id);

/* gf_strerror() in de taal van de gebruiker. */
const char *gf_error_str(LONG code);

#endif /* GFLOCALE_H */
