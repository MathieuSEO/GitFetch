/*
 * backend_proxy.c -- haalt release-info en assets op via de GitFetch-proxy.
 *
 * Fase 1: plain HTTP, geen TLS. De proxy heeft de JSON al platgeslagen tot
 * regels en de tekst al naar ISO-8859-1 omgezet, dus hier blijft weinig
 * over behalve de verbinding leggen en de bytes doorgeven.
 */

#include <exec/types.h>
#include <exec/memory.h>
#include <dos/dos.h>

#include <proto/exec.h>
#include <proto/dos.h>

#include <string.h>
#include <stdio.h>

#include "gitfetch.h"
#include "gfnet.h"
#include "gfparse.h"
#include "gfbackend.h"
#include "gfbackend_impl.h"

static LONG last_dropped_lines;
static char last_request_path[GF_MAX_PATH];

const char *gf_last_request_path(void)
{
    return last_request_path;
}

LONG gf_last_dropped_lines(void)
{
    return last_dropped_lines;
}

/* --- releaselijst -------------------------------------------------------- */

static LONG parse_sink(APTR user, const UBYTE *data, ULONG len,
                       ULONG sofar, ULONG total)
{
    (void)sofar;
    (void)total;
    return gf_parser_feed((struct GFParser *)user, data, len);
}

LONG gf_proxy_fetch_releases(struct GFPrefs *prefs, const char *repo,
                             struct ReleaseList *list, ULONG abort_signals)
{
    struct GFParser *parser;
    struct GFRequest req;
    /* "/v1/releases?repo=" + repo (max GF_MAX_REPO) + "&max=NN" past net
       in GF_MAX_PATH; met wat marge kan dat niet stil over de rand. */
    char path[GF_MAX_PATH + GF_MAX_REPO];
    LONG err;

    parser = AllocVec(sizeof(struct GFParser), MEMF_ANY);
    if (!parser)
        return GF_ERR_NOMEM;

    gf_parser_init(parser, list);

    /* De repo-naam bestaat alleen uit alfanumeriek en -._ (de proxy
       weigert de rest), dus URL-encoding is hier niet nodig. */
    /* %ld met een long-cast, niet %d: op AmigaOS lezen de printf-varianten
       32-bit argumenten, en met %d komt er nul uit. Dat leverde eerder een
       "max=0" op, waarna de proxy op precies een release terugviel. */
    sprintf(path, "/v1/releases?repo=%s&max=%ld", repo,
            (long)(prefs->max_releases ? prefs->max_releases : 1));

    strncpy(last_request_path, path, sizeof(last_request_path) - 1);
    last_request_path[sizeof(last_request_path) - 1] = '\0';

    memset(&req, 0, sizeof(req));
    req.host = prefs->host;
    req.port = prefs->port;
    req.path = path;
    req.sink = parse_sink;
    req.user = parser;
    req.abort_signals = abort_signals;

    err = gf_http_get(&req);
    if (err == GF_OK)
        err = gf_parser_finish(parser);

    /* Regels die de parser niet kon plaatsen; normaal nul. */
    last_dropped_lines = parser->dropped;

    /* De proxy weet beter waarom iets misging dan wij; geef die tekst door
       zodat de gebruiker "Repository niet gevonden" ziet in plaats van
       "de proxy meldde een fout". */
    if (err == GF_ERR_SERVER && parser->message[0])
        gf_set_server_message(parser->message);

    FreeVec(parser);
    return err;
}

/* --- asset downloaden ---------------------------------------------------- */

struct DownloadState {
    BPTR           file;
    gf_progress_fn progress;
    APTR           user;
    LONG           io_error;
};

static LONG download_sink(APTR user, const UBYTE *data, ULONG len,
                          ULONG sofar, ULONG total)
{
    struct DownloadState *st = (struct DownloadState *)user;

    /* Streamend naar disk: een asset van een paar megabyte hoort niet
       eerst volledig in RAM te staan. */
    if (Write(st->file, (APTR)data, (LONG)len) != (LONG)len) {
        st->io_error = GF_ERR_FILE;
        return GF_ERR_FILE;
    }

    if (st->progress)
        return st->progress(st->user, sofar, total);

    return GF_OK;
}

LONG gf_proxy_download_asset(struct GFPrefs *prefs, const struct Asset *asset,
                             const char *destdir, ULONG abort_signals,
                             gf_progress_fn progress, APTR user)
{
    struct DownloadState st;
    struct GFRequest req;
    char destpath[512];
    LONG err;

    if (!asset || !asset->path[0])
        return GF_ERR_URL;

    if (!destdir || !destdir[0])
        destdir = prefs->destdir;

    strncpy(destpath, destdir, sizeof(destpath) - 1);
    destpath[sizeof(destpath) - 1] = '\0';
    if (!AddPart(destpath, (STRPTR)asset->name, sizeof(destpath)))
        return GF_ERR_FILE;

    memset(&st, 0, sizeof(st));
    st.progress = progress;
    st.user = user;

    st.file = Open(destpath, MODE_NEWFILE);
    if (!st.file)
        return GF_ERR_FILE;

    memset(&req, 0, sizeof(req));
    req.host = prefs->host;
    req.port = prefs->port;
    req.path = asset->path;
    req.sink = download_sink;
    req.user = &st;
    req.abort_signals = abort_signals;
    req.timeout_secs = 60;

    err = gf_http_get(&req);
    Close(st.file);

    if (st.io_error != GF_OK)
        err = st.io_error;

    /* Een halve download laten staan is vragen om een corrupt archief dat
       er op het oog goed uitziet. */
    if (err != GF_OK)
        DeleteFile(destpath);

    return err;
}
