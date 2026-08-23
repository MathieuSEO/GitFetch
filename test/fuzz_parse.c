/*
 * fuzz_parse.c -- gooit vervormde en willekeurige invoer naar de parser.
 *
 * De parser verwerkt data die van het netwerk komt, dus hij moet tegen
 * onzin kunnen: afgekapte regels, ontbrekende velden, losse tabs, lange
 * velden, binaire rommel. Deterministisch (vaste seed) zodat een fout
 * reproduceerbaar is.
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "gitfetch.h"
#include "gfparse.h"
#include "gfurl.h"
#include "gfjson.h"

static unsigned long seed = 20260821UL;

static unsigned long rnd(void)
{
    seed = seed * 1103515245UL + 12345UL;
    return (seed >> 16) & 0x7fffffffUL;
}

static const char *ZAAD =
    "#GITFETCH 1\n#STATUS OK\n#REPO owner/repo\n"
    "R\t0\tv1.0\t2026-01-01\t0\tEerste release\n"
    "A\t0\t0\ttool.lha\t12345\t/v1/asset?id=a/b/1/2.sig\n"
    "R\t1\tv0.9\t2025-12-01\t1\tBeta\n"
    "A\t1\t0\tbeta.lha\t999\t/v1/asset?id=a/b/2/2.sig\n"
    "#END\n";

int main(void)
{
    struct ReleaseList *list = malloc(sizeof(struct ReleaseList));
    struct GFParser *p = malloc(sizeof(struct GFParser));
    char *buf = malloc(65536);
    int round;

    if (!list || !p || !buf)
        return 1;

    printf("fuzz: 200000 rondes vervormde invoer\n");

    for (round = 0; round < 200000; round++) {
        size_t len;
        int mode = (int)(rnd() % 4);
        size_t i, off;

        if (mode == 0) {
            /* Volledig willekeurige bytes. */
            len = rnd() % 4096;
            for (i = 0; i < len; i++)
                buf[i] = (char)(rnd() & 0xff);
        } else if (mode == 1) {
            /* Geldig zaad, willekeurig afgekapt. */
            len = rnd() % (strlen(ZAAD) + 1);
            memcpy(buf, ZAAD, len);
        } else if (mode == 2) {
            /* Geldig zaad met een paar bytes vervangen. */
            len = strlen(ZAAD);
            memcpy(buf, ZAAD, len);
            for (i = 0; i < 1 + rnd() % 8; i++)
                buf[rnd() % len] = (char)(rnd() & 0xff);
        } else {
            /* Veel te lange velden en tabs. */
            len = 0;
            off = (size_t)snprintf(buf, 4096, "#GITFETCH 1\n#STATUS OK\nR\t0\t");
            len = off;
            for (i = 0; i < rnd() % 3000; i++)
                buf[len++] = (char)("abcXYZ\t"[rnd() % 7]);
            buf[len++] = '\n';
            memcpy(buf + len, "#END\n", 5);
            len += 5;
        }

        gf_parser_init(p, list);

        /* In willekeurige blokjes voeren, zodat regelovergangen tussen
           blokken ook geraakt worden. */
        off = 0;
        while (off < len) {
            size_t n = 1 + rnd() % 64;
            if (n > len - off)
                n = len - off;
            gf_parser_feed(p, (const UBYTE *)buf + off, (ULONG)n);
            off += n;
        }
        gf_parser_finish(p);

        /* Invarianten die altijd moeten gelden, wat er ook binnenkwam. */
        if (list->nreleases > GF_MAX_RELEASES) {
            printf("FOUT ronde %d: nreleases = %u\n", round,
                   (unsigned)list->nreleases);
            return 1;
        }
        for (i = 0; i < list->nreleases; i++) {
            if (list->releases[i].nassets > GF_MAX_ASSETS) {
                printf("FOUT ronde %d: nassets = %u\n", round,
                       (unsigned)list->releases[i].nassets);
                return 1;
            }
            if (list->releases[i].tag[GF_MAX_TAG - 1] != '\0' ||
                list->releases[i].title[GF_MAX_TITLE - 1] != '\0') {
                printf("FOUT ronde %d: string niet ge-NUL-termineerd\n", round);
                return 1;
            }
        }
    }

    printf("fuzz: URL-normalisatie met willekeurige invoer\n");
    for (round = 0; round < 200000; round++) {
        char in[256], out[GF_MAX_REPO];
        size_t len = rnd() % 255, i;

        for (i = 0; i < len; i++)
            in[i] = (char)(rnd() % 2 ? (rnd() & 0x7f) : "abc/.-:@"[rnd() % 8]);
        in[len] = '\0';

        memset(out, 0xAA, sizeof(out));
        if (gf_normalize_repo(in, out, sizeof(out)) == GF_OK) {
            /* Bij succes moet er een geldige, afgesloten owner/repo staan. */
            if (memchr(out, '\0', sizeof(out)) == NULL) {
                printf("FOUT ronde %d: uitvoer niet ge-NUL-termineerd\n", round);
                return 1;
            }
            if (strchr(out, '/') == NULL) {
                printf("FOUT ronde %d: geen schuine streep in '%s'\n",
                       round, out);
                return 1;
            }
        }
    }

    /* De JSON-parser krijgt netwerkdata binnen en moet net zo goed tegen
       onzin kunnen als de protocolparser. */
    printf("fuzz: JSON-parser met vervormde invoer\n");
    {
        struct GFJson *j = malloc(sizeof(struct GFJson));
        const char *ZAAD_J =
            "[{\"tag_name\":\"1.0\",\"name\":\"Eerste\",\"draft\":false,"
            "\"prerelease\":false,\"published_at\":\"2026-01-01T00:00:00Z\","
            "\"assets\":[{\"name\":\"a.lha\",\"size\":123,"
            "\"browser_download_url\":\"https://x/a.lha\"}]}]";
        size_t zl = strlen(ZAAD_J);

        if (!j) return 1;
        for (round = 0; round < 100000; round++) {
            size_t len, i, off;
            int mode = (int)(rnd() % 4);

            if (mode == 0) {
                len = rnd() % 2048;
                for (i = 0; i < len; i++) buf[i] = (char)(rnd() & 0xff);
            } else if (mode == 1) {
                len = rnd() % (zl + 1);
                memcpy(buf, ZAAD_J, len);
            } else if (mode == 2) {
                len = zl; memcpy(buf, ZAAD_J, len);
                for (i = 0; i < 1 + rnd() % 8; i++)
                    buf[rnd() % len] = (char)(rnd() & 0xff);
            } else {
                /* diep geneste haakjes: test de diepte-administratie */
                len = 0;
                for (i = 0; i < rnd() % 500; i++)
                    buf[len++] = (rnd() % 2) ? '[' : '{';
                for (i = 0; i < rnd() % 500; i++)
                    buf[len++] = (rnd() % 2) ? ']' : '}';
            }

            gf_json_init(j, list);
            off = 0;
            while (off < len) {
                size_t n = 1 + rnd() % 64;
                if (n > len - off) n = len - off;
                gf_json_feed(j, (const UBYTE *)buf + off, (ULONG)n);
                off += n;
            }
            gf_json_finish(j);

            if (list->nreleases > GF_MAX_RELEASES) {
                printf("FOUT ronde %d: nreleases = %u\n", round,
                       (unsigned)list->nreleases); return 1;
            }
            for (i = 0; i < list->nreleases; i++) {
                if (list->releases[i].nassets > GF_MAX_ASSETS) {
                    printf("FOUT ronde %d: nassets = %u\n", round,
                           (unsigned)list->releases[i].nassets); return 1;
                }
                if (list->releases[i].tag[GF_MAX_TAG - 1] != '\0' ||
                    list->releases[i].title[GF_MAX_TITLE - 1] != '\0') {
                    printf("FOUT ronde %d: string niet afgesloten\n", round);
                    return 1;
                }
            }
        }
        free(j);
    }

    printf("fuzz: geen crashes, geen geschonden invarianten\n");
    free(buf); free(p); free(list);
    return 0;
}
