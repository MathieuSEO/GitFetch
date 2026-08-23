/*
 * test_parse.c -- hosttests voor de protocolparser en de URL-normalisatie.
 *
 * Draait op de Mac, niet op de Amiga: deze twee modules gebruiken alleen
 * string.h en de exec-typedefs, dus ze zijn hier te testen zonder emulator
 * of cross-compiler.
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "gitfetch.h"
#include "gfparse.h"
#include "gfurl.h"

static int failures = 0;
static int checks = 0;

static void check(int cond, const char *what)
{
    checks++;
    if (!cond) {
        failures++;
        printf("  FAIL: %s\n", what);
    }
}

static void check_str(const char *got, const char *want, const char *what)
{
    checks++;
    if (strcmp(got, want) != 0) {
        failures++;
        printf("  FAIL: %s -- kreeg '%s', verwachtte '%s'\n", what, got, want);
    }
}

/* Voert de tekst in blokken van 'chunk' bytes aan de parser. chunk == 0
   betekent alles in een keer. */
static LONG run_parser(const char *text, struct ReleaseList *list, int chunk)
{
    struct GFParser p;
    size_t len = strlen(text), off = 0;

    gf_parser_init(&p, list);
    if (chunk <= 0)
        chunk = (int)len;

    while (off < len) {
        size_t n = (len - off) < (size_t)chunk ? (len - off) : (size_t)chunk;
        gf_parser_feed(&p, (const UBYTE *)text + off, (ULONG)n);
        off += n;
    }
    return gf_parser_finish(&p);
}

static const char *GOED =
    "#GITFETCH 1\n"
    "#STATUS OK\n"
    "#REPO jens-maus/amissl\n"
    "R\t0\t5.27\t2026-04-08\t0\tAmiSSL 5.27\n"
    "A\t0\t0\tAmiSSL-5.27-OS3.lha\t4294781\t/v1/asset?id=a/b/1/2.xyz\n"
    "A\t0\t1\tAmiSSL-5.27-SDK.lha\t2583127\t/v1/asset?id=a/b/2/2.xyz\n"
    "R\t1\t5.26\t2026-01-28\t1\tAmiSSL 5.26 beta\n"
    "A\t1\t0\tAmiSSL-5.26-OS3.lha\t4288161\t/v1/asset?id=a/b/3/2.xyz\n"
    "#END\n";

static void test_happy_path(void)
{
    struct ReleaseList list;
    LONG err;

    printf("test: normale releaselijst\n");
    err = run_parser(GOED, &list, 0);
    check(err == GF_OK, "parser geeft GF_OK");
    check_str(list.repo, "jens-maus/amissl", "repo-naam");
    check(list.nreleases == 2, "twee releases");
    check_str(list.releases[0].tag, "5.27", "tag van release 0");
    check_str(list.releases[0].date, "2026-04-08", "datum van release 0");
    check_str(list.releases[0].title, "AmiSSL 5.27", "titel van release 0");
    check(list.releases[0].prerelease == 0, "release 0 is geen prerelease");
    check(list.releases[0].nassets == 2, "release 0 heeft 2 assets");
    check_str(list.releases[0].assets[0].name, "AmiSSL-5.27-OS3.lha",
              "naam van asset 0");
    check(list.releases[0].assets[0].size == 4294781, "grootte van asset 0");
    check_str(list.releases[0].assets[0].path, "/v1/asset?id=a/b/1/2.xyz",
              "pad van asset 0");
    check(list.releases[1].prerelease == 1, "release 1 is prerelease");
    check(list.releases[1].nassets == 1, "release 1 heeft 1 asset");
}

static void test_streaming_equivalence(void)
{
    struct ReleaseList whole, piecewise;
    int chunk;

    printf("test: blokgrootte mag niets uitmaken\n");
    run_parser(GOED, &whole, 0);

    for (chunk = 1; chunk <= 17; chunk++) {
        char what[64];
        run_parser(GOED, &piecewise, chunk);
        sprintf(what, "identiek resultaat bij blokken van %d byte(s)", chunk);
        check(memcmp(&whole, &piecewise, sizeof(whole)) == 0, what);
    }
}

static void test_server_error(void)
{
    struct ReleaseList list;
    struct GFParser p;
    const char *text =
        "#GITFETCH 1\n"
        "#STATUS ERR 404 Repository niet gevonden\n"
        "#END\n";

    printf("test: foutmelding van de proxy\n");
    gf_parser_init(&p, &list);
    gf_parser_feed(&p, (const UBYTE *)text, (ULONG)strlen(text));
    check(gf_parser_finish(&p) == GF_ERR_SERVER, "geeft GF_ERR_SERVER");
    check(p.status == 404, "statuscode 404");
    check_str(p.message, "Repository niet gevonden", "foutboodschap");
}

static void test_truncated(void)
{
    struct ReleaseList list;
    const char *text =
        "#GITFETCH 1\n"
        "#STATUS OK\n"
        "R\t0\t5.27\t2026-04-08\t0\tAmiSSL 5.27\n";

    printf("test: afgebroken transfer\n");
    /* Zonder #END is de lijst mogelijk incompleet; dat moet een fout geven
       en niet stilletjes een halve lijst opleveren. */
    check(run_parser(text, &list, 0) == GF_ERR_PROTO,
          "ontbrekende #END geeft GF_ERR_PROTO");
}

static void test_garbage(void)
{
    struct ReleaseList list;
    const char *text = "dit is helemaal geen gitfetch-antwoord\n<html>\n";

    printf("test: onzin-antwoord\n");
    check(run_parser(text, &list, 0) == GF_ERR_PROTO,
          "onzin geeft GF_ERR_PROTO");
}

static void test_overflow_limits(void)
{
    struct ReleaseList list;
    struct GFParser p;
    char buf[8192];
    int i;

    printf("test: te veel releases en te lange regels\n");

    /* Meer releases dan er passen: de rest moet genegeerd worden, niet
       over het einde van de array heen schrijven. */
    gf_parser_init(&p, &list);
    gf_parser_feed(&p, (const UBYTE *)"#GITFETCH 1\n#STATUS OK\n", 23);
    for (i = 0; i < GF_MAX_RELEASES + 10; i++) {
        sprintf(buf, "R\t%d\tv%d\t2026-01-01\t0\tTitel %d\n", i, i, i);
        gf_parser_feed(&p, (const UBYTE *)buf, (ULONG)strlen(buf));
    }
    gf_parser_feed(&p, (const UBYTE *)"#END\n", 5);
    check(gf_parser_finish(&p) == GF_OK, "parser blijft heel");
    check(list.nreleases == GF_MAX_RELEASES, "afgekapt op GF_MAX_RELEASES");
    check(p.dropped == 10, "tien regels genegeerd");

    /* Absurd lange regel: moet genegeerd worden, niet over line[] heen. */
    gf_parser_init(&p, &list);
    gf_parser_feed(&p, (const UBYTE *)"#GITFETCH 1\n#STATUS OK\n", 23);
    memset(buf, 'x', sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = '\0';
    memcpy(buf, "R\t0\t", 4);
    gf_parser_feed(&p, (const UBYTE *)buf, (ULONG)strlen(buf));
    gf_parser_feed(&p, (const UBYTE *)"\n#END\n", 6);
    check(gf_parser_finish(&p) == GF_OK, "lange regel laat parser heel");
    check(list.nreleases == 0, "te lange regel wordt genegeerd");
}

static void test_orphan_asset(void)
{
    struct ReleaseList list;
    const char *text =
        "#GITFETCH 1\n"
        "#STATUS OK\n"
        "A\t0\t0\tzwevend.lha\t100\t/v1/asset?id=x\n"
        "R\t0\tv1\t2026-01-01\t0\tEerste\n"
        "A\t5\t0\tverkeerde-index.lha\t100\t/v1/asset?id=y\n"
        "A\t0\t0\tgoed.lha\t100\t/v1/asset?id=z\n"
        "#END\n";

    printf("test: assets met een verkeerde release-index\n");
    check(run_parser(text, &list, 0) == GF_OK, "parser geeft GF_OK");
    check(list.nreleases == 1, "een release");
    check(list.releases[0].nassets == 1, "alleen de kloppende asset");
    check_str(list.releases[0].assets[0].name, "goed.lha", "juiste asset");
}

static void test_missing_fields(void)
{
    struct ReleaseList list;
    const char *text =
        "#GITFETCH 1\n"
        "#STATUS OK\n"
        "R\t0\tv1\n"                                  /* geen datum/titel */
        "A\t0\t0\tkaal.lha\n"                         /* geen grootte/pad */
        "R\t1\n"                                      /* zelfs geen tag */
        "#END\n";

    printf("test: regels met ontbrekende velden\n");
    check(run_parser(text, &list, 0) == GF_OK, "parser geeft GF_OK");
    check(list.nreleases == 1, "alleen de release met een tag");
    check_str(list.releases[0].title, "v1", "titel valt terug op de tag");
    check(list.releases[0].nassets == 0, "asset zonder pad genegeerd");
}

static void test_url_normalisation(void)
{
    struct {
        const char *in;
        const char *want;   /* NULL = moet falen */
    } cases[] = {
        { "https://github.com/jens-maus/amissl",              "jens-maus/amissl" },
        { "http://github.com/jens-maus/amissl",               "jens-maus/amissl" },
        { "https://www.github.com/jens-maus/amissl",          "jens-maus/amissl" },
        { "github.com/jens-maus/amissl",                      "jens-maus/amissl" },
        { "jens-maus/amissl",                                 "jens-maus/amissl" },
        { "https://github.com/jens-maus/amissl/releases",     "jens-maus/amissl" },
        { "https://github.com/jens-maus/amissl/releases/latest", "jens-maus/amissl" },
        { "https://github.com/jens-maus/amissl/tree/master",  "jens-maus/amissl" },
        { "https://github.com/jens-maus/amissl/",             "jens-maus/amissl" },
        { "https://github.com/jens-maus/amissl.git",          "jens-maus/amissl" },
        { "git@github.com:jens-maus/amissl.git",              "jens-maus/amissl" },
        { "  github.com/owner/repo  ",                        "owner/repo" },
        { "owner/repo.name",                                  "owner/repo.name" },
        { "https://github.com/thomas-luebker/amipkg",         "thomas-luebker/amipkg" },
        { "",                                                 NULL },
        { "geen-slash",                                       NULL },
        { "https://github.com/",                              NULL },
        { "/owner/",                                          NULL },
        { "https://gitlab.com/owner/repo",                    NULL },
        { "https://codeberg.org/bebbo/amiga-gcc",             NULL },
        { "bitbucket.org/team/project",                       NULL },
        { "aminet.net/util/libs",                             NULL },
        { "https://github.com/a/b",                           "a/b" },
        { "https://github.com/User-Name123/repo_x.y-z",       "User-Name123/repo_x.y-z" },
        { NULL, NULL }
    };
    int i;

    printf("test: URL-normalisatie\n");
    for (i = 0; cases[i].in; i++) {
        char out[GF_MAX_REPO];
        LONG err = gf_normalize_repo(cases[i].in, out, sizeof(out));

        if (cases[i].want) {
            checks++;
            if (err != GF_OK) {
                failures++;
                printf("  FAIL: '%s' werd afgewezen\n", cases[i].in);
            } else if (strcmp(out, cases[i].want) != 0) {
                failures++;
                printf("  FAIL: '%s' -> '%s', verwachtte '%s'\n",
                       cases[i].in, out, cases[i].want);
            }
        } else {
            checks++;
            if (err == GF_OK) {
                failures++;
                printf("  FAIL: '%s' had afgewezen moeten worden "
                       "(werd '%s')\n", cases[i].in, out);
            }
        }
    }
}

int main(void)
{
    printf("GitFetch hosttests\n\n");
    test_happy_path();
    test_streaming_equivalence();
    test_server_error();
    test_truncated();
    test_garbage();
    test_overflow_limits();
    test_orphan_asset();
    test_missing_fields();
    test_url_normalisation();

    printf("\n%d controles, %d mislukt\n", checks, failures);
    return failures ? 1 : 0;
}
