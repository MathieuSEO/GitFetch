/*
 * backend_native.c -- rechtstreeks met de GitHub API praten via AmiSSL.
 *
 * Gebruikt de HTTP(S)-client die vanaf AmiSSL 5 in de library zit
 * (OSSL_HTTP_get). Die handelt zelf de redirects af, en dat is precies wat
 * een download nodig heeft: een asset-URL bij GitHub verwijst door naar
 * een andere host.
 *
 * Twee dingen om in de gaten te houden ten opzichte van de proxy:
 *
 *  - Het antwoord is fors. Voor 25 releases stuurt GitHub ongeveer 240 KB,
 *    grotendeels release-notes; de proxy maakt daar zo'n 5 KB van. Alles
 *    daarvan moet door de TLS-ontsleuteling van een 68k heen.
 *  - Er zijn twee handshakes per download nodig (API en asset-host), elk
 *    goed voor enkele seconden op een 68060. Vandaar dat dit werk in het
 *    aparte proces hoort en niet in de GUI-lus.
 */

#include <exec/types.h>
#include <exec/memory.h>
#include <dos/dos.h>
#include <dos/datetime.h>

#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/socket.h>
#include <proto/amisslmaster.h>
#include <libraries/amisslmaster.h>
#include <amissl/amissl.h>

#include <string.h>
#include <stdio.h>
#include <errno.h>

#include "gitfetch.h"
#include "gfbackend.h"
#include "gfbackend_impl.h"
#include "gfjson.h"
#include "gflocale.h"

/*
 * Het AmiSSL-voorbeeld gebruikt SAVEDS en STDARGS uit SDI_compiler.h, een
 * losse header uit de Amiga-gemeenschap. Die afhankelijkheid is voor twee
 * macro's niet de moeite; dit zijn de equivalenten voor gcc.
 *
 * STDARGS dwingt af dat argumenten via de stack gaan. Dat is bij gcc
 * meestal al zo, maar de nieuwere amiga-branches kennen register-
 * parameters, en een callback die de library aanroept moet zich aan de
 * afgesproken conventie houden.
 */
/* __saveds heeft alleen zin bij -fbaserel; die bouwen we niet, dus leeg. */
#ifndef SAVEDS
#define SAVEDS
#endif
#ifndef STDARGS
#define STDARGS __stdargs
#endif

extern struct Library *SocketBase;      /* geopend door net.c in de worker */

struct Library *AmiSSLMasterBase = NULL;
struct Library *AmiSSLBase = NULL;
struct Library *AmiSSLExtBase = NULL;

/* AmiSSL wil een plek om errno neer te zetten. Die van de C-runtime
   gebruiken, niet zelf een variabele met die naam maken -- libnix heeft er
   al een en dan botst de linker. */

#define GF_USER_AGENT  "GitFetch/" GF_VERSION " (AmigaOS)"
#define READ_CHUNK     2048

static SSL_CTX *ssl_ctx = NULL;

/* --- openen en sluiten --------------------------------------------------- */

/*
 * AmiSSL houdt, net als bsdsocket, administratie per task bij. Openen en
 * sluiten gebeurt daarom binnen dezelfde task die het werk doet -- in de
 * praktijk het worker-proces.
 */
/*
 * Een Amiga zonder werkende accu-klok begint in 1978. Elk TLS-certificaat
 * is dan "nog niet geldig" en de handshake mislukt met een melding die
 * niets over de datum zegt. Dat kost je een avond zoeken, dus we kijken
 * er van tevoren naar.
 *
 * 15340 = 1 januari 2020, geteld in dagen vanaf 1 januari 1978 (het
 * nulpunt van DateStamp).
 */
#define GF_MIN_PLAUSIBLE_DAY  15340

static BOOL clock_is_plausible(void)
{
    struct DateStamp ds;

    DateStamp(&ds);
    return (BOOL)(ds.ds_Days >= GF_MIN_PLAUSIBLE_DAY);
}

static LONG amissl_open(struct GFPrefs *prefs)
{
    if (ssl_ctx)
        return GF_OK;

    if (!SocketBase) {
        gf_set_server_message("bsdsocket.library is niet open");
        return GF_ERR_SOCKET;
    }

    if (prefs->verify_cert && !clock_is_plausible()) {
        gf_set_server_message("De datum van de Amiga staat voor 2020; "
                              "certificaten lijken dan nog niet geldig. "
                              "Zet de klok goed, bijvoorbeeld: "
                              "date 21-aug-26 12:00:00");
        return GF_ERR_CLOCK;
    }

    AmiSSLMasterBase = OpenLibrary("amisslmaster.library",
                                   AMISSLMASTER_MIN_VERSION);
    if (!AmiSSLMasterBase) {
        gf_set_server_message("amisslmaster.library niet gevonden; "
                              "is AmiSSL 5 geinstalleerd?");
        return GF_ERR_TLS;
    }

    if (OpenAmiSSLTags(AMISSL_CURRENT_VERSION,
                       AmiSSL_UsesOpenSSLStructs, (ULONG)FALSE,
                       AmiSSL_GetAmiSSLBase, (ULONG)&AmiSSLBase,
                       AmiSSL_GetAmiSSLExtBase, (ULONG)&AmiSSLExtBase,
                       AmiSSL_SocketBase, (ULONG)SocketBase,
                       AmiSSL_ErrNoPtr, (ULONG)&errno,
                       TAG_DONE) != 0) {
        gf_set_server_message("Kan AmiSSL niet initialiseren");
        CloseLibrary(AmiSSLMasterBase);
        AmiSSLMasterBase = NULL;
        return GF_ERR_TLS;
    }

    ssl_ctx = SSL_CTX_new(TLS_client_method());
    if (!ssl_ctx) {
        gf_set_server_message("Kan geen TLS-context maken");
        CloseAmiSSL();
        CloseLibrary(AmiSSLMasterBase);
        AmiSSLMasterBase = NULL;
        return GF_ERR_TLS;
    }

    /* Certificaten controleren waar dat kan. Ontbreekt de CA-bundle, dan
       is dat geen reden om de verbinding te weigeren -- maar de gebruiker
       hoort het wel te weten. */
    if (prefs->verify_cert) {
        if (SSL_CTX_set_default_verify_paths(ssl_ctx) == 1) {
            SSL_CTX_set_verify(ssl_ctx, SSL_VERIFY_PEER, NULL);
        } else {
            /* Zonder CA-bundle kan er niets gecontroleerd worden. De
               verbinding weigeren zou hier onnodig streng zijn, maar de
               gebruiker hoort het te weten. */
            SSL_CTX_set_verify(ssl_ctx, SSL_VERIFY_NONE, NULL);
            gf_set_server_message("Let op: geen CA-bundle gevonden, "
                                  "certificaat niet gecontroleerd");
        }
    } else {
        SSL_CTX_set_verify(ssl_ctx, SSL_VERIFY_NONE, NULL);
    }

    return GF_OK;
}

static void amissl_close(void)
{
    if (ssl_ctx) {
        SSL_CTX_free(ssl_ctx);
        ssl_ctx = NULL;
    }
    if (AmiSSLBase) {
        CloseAmiSSL();
        AmiSSLBase = NULL;
        AmiSSLExtBase = NULL;
    }
    if (AmiSSLMasterBase) {
        CloseLibrary(AmiSSLMasterBase);
        AmiSSLMasterBase = NULL;
    }
}

/* --- callbacks ----------------------------------------------------------- */

/*
 * Zet TLS op de verbinding zodra OSSL_HTTP_get daarom vraagt. SAVEDS en
 * STDARGS zijn nodig omdat de library deze functie aanroept en dan niet op
 * onze registers of aanroepconventie kan rekenen.
 */
static SAVEDS STDARGS BIO *tls_callback(BIO *bio, void *arg, int connect,
                                        int detail)
{
    if (connect && detail) {
        BIO *sbio = BIO_new_ssl((SSL_CTX *)arg, 1);
        bio = sbio ? BIO_push(sbio, bio) : NULL;
    }
    return bio;
}

static SAVEDS STDARGS void conf_free_stub(CONF_VALUE *val)
{
    X509V3_conf_free(val);
}

/* --- gemeenschappelijke ophaalfunctie ------------------------------------ */

typedef LONG (*gf_chunk_fn)(APTR user, const UBYTE *data, ULONG len);

/*
 * Haalt een URL op en geeft de inhoud blok voor blok door. Geeft GF_OK bij
 * succes. Redirects worden door OSSL_HTTP_get zelf gevolgd.
 */
static LONG fetch_url(const char *url, const char *accept,
                      gf_chunk_fn sink, APTR user, ULONG abort_signals,
                      ULONG *total_out)
{
    STACK_OF(CONF_VALUE) *headers = NULL;
    BIO *bio;
    UBYTE *buffer;
    LONG err = GF_OK;
    ULONG total = 0;

    buffer = AllocVec(READ_CHUNK, MEMF_ANY);
    if (!buffer)
        return GF_ERR_NOMEM;

    /* GitHub weigert verzoeken zonder User-Agent. */
    X509V3_add_value("User-Agent", GF_USER_AGENT, &headers);
    if (accept)
        X509V3_add_value("Accept", accept, &headers);

    bio = OSSL_HTTP_get(url, NULL /* proxy */, NULL /* no_proxy */,
                        NULL /* bio */, NULL /* rbio */,
                        (BIO *(*)(BIO *, void *, int, int))tls_callback,
                        ssl_ctx, 0 /* buf_size */, headers,
                        NULL /* content type */, 0 /* expect_asn1 */,
                        0 /* max_resp_len */, 0 /* timeout */);

    if (!bio) {
        /*
         * OpenSSL houdt bij waarom iets misging; zonder die tekst is
         * "geen verbinding" niet te onderscheiden van een verlopen
         * certificaat, een verkeerd gezette klok of een DNS-probleem.
         */
        unsigned long e = ERR_peek_last_error();

        if (e) {
            char reason[GF_MAX_ERR];

            ERR_error_string_n(e, reason, sizeof(reason));

            /*
             * Een weigering van de HTTP-laag is bijna altijd de limiet van
             * 60 verzoeken per uur die GitHub aanhoudt voor wie zich niet
             * aanmeldt. De kale OpenSSL-tekst zegt dat niet, en dan zoek
             * je het in de verkeerde hoek.
             */
            if (ERR_GET_LIB(e) == ERR_LIB_HTTP)
                gf_set_server_message(gf_str(MSG_RATE_LIMIT_HINT));
            else
                gf_set_server_message(reason);
        } else {
            gf_set_server_message("Geen antwoord van GitHub "
                                  "(netwerk, DNS of klok?)");
        }
        ERR_clear_error();
        err = GF_ERR_CONNECT;
    } else {
        int length;

        while ((length = BIO_read(bio, buffer, READ_CHUNK)) > 0) {
            if (abort_signals && (SetSignal(0, 0) & abort_signals)) {
                err = GF_ERR_ABORTED;
                break;
            }
            total += (ULONG)length;
            err = sink(user, buffer, (ULONG)length);
            if (err != GF_OK)
                break;
        }
        BIO_free(bio);
    }

    if (headers)
        sk_CONF_VALUE_pop_free(headers, (void (*)(CONF_VALUE *))conf_free_stub);
    FreeVec(buffer);

    if (total_out)
        *total_out = total;
    return err;
}

/* --- releaselijst -------------------------------------------------------- */

static LONG json_sink(APTR user, const UBYTE *data, ULONG len)
{
    return gf_json_feed((struct GFJson *)user, data, len);
}

LONG gf_native_fetch_releases(struct GFPrefs *prefs, const char *repo,
                              struct ReleaseList *list, ULONG abort_signals)
{
    struct GFJson *json;
    char url[256];
    LONG err;

    err = amissl_open(prefs);
    if (err != GF_OK)
        return err;

    json = AllocVec(sizeof(struct GFJson), MEMF_ANY);
    if (!json) {
        amissl_close();
        return GF_ERR_NOMEM;
    }
    gf_json_init(json, list);

    sprintf(url, "https://api.github.com/repos/%s/releases?per_page=%ld",
            repo, (long)(prefs->max_releases ? prefs->max_releases : 1));

    err = fetch_url(url, "application/vnd.github+json",
                    json_sink, json, abort_signals, NULL);

    if (err == GF_OK)
        err = gf_json_finish(json);

    strncpy(list->repo, repo, GF_MAX_REPO - 1);
    list->repo[GF_MAX_REPO - 1] = '\0';

    FreeVec(json);
    amissl_close();
    return err;
}

/* --- asset downloaden ---------------------------------------------------- */

struct NativeDownload {
    BPTR           file;
    gf_progress_fn progress;
    APTR           user;
    ULONG          sofar;
    ULONG          total;
    LONG           io_error;
};

static LONG download_sink(APTR user, const UBYTE *data, ULONG len)
{
    struct NativeDownload *st = (struct NativeDownload *)user;

    if (Write(st->file, (APTR)data, (LONG)len) != (LONG)len) {
        st->io_error = GF_ERR_FILE;
        return GF_ERR_FILE;
    }

    st->sofar += len;
    if (st->progress)
        return st->progress(st->user, st->sofar, st->total);

    return GF_OK;
}

LONG gf_native_download_asset(struct GFPrefs *prefs, const struct Asset *asset,
                              const char *destdir, ULONG abort_signals,
                              gf_progress_fn progress, APTR user)
{
    struct NativeDownload st;
    char destpath[512];
    LONG err;

    if (!asset || !asset->path[0])
        return GF_ERR_URL;

    err = amissl_open(prefs);
    if (err != GF_OK)
        return err;

    if (!destdir || !destdir[0])
        destdir = prefs->destdir;

    strncpy(destpath, destdir, sizeof(destpath) - 1);
    destpath[sizeof(destpath) - 1] = '\0';
    if (!AddPart(destpath, (STRPTR)asset->name, sizeof(destpath))) {
        amissl_close();
        return GF_ERR_FILE;
    }

    memset(&st, 0, sizeof(st));
    st.progress = progress;
    st.user = user;
    /* De grootte komt uit de releaselijst; OSSL_HTTP_get geeft de
       Content-Length van de uiteindelijke host niet apart terug. */
    st.total = asset->size;

    st.file = Open(destpath, MODE_NEWFILE);
    if (!st.file) {
        amissl_close();
        return GF_ERR_FILE;
    }

    err = fetch_url(asset->path, NULL, download_sink, &st, abort_signals, NULL);
    Close(st.file);

    if (st.io_error != GF_OK)
        err = st.io_error;

    /* Een halve download achterlaten levert een archief op dat er goed
       uitziet maar het niet is. */
    if (err != GF_OK)
        DeleteFile(destpath);

    amissl_close();
    return err;
}
