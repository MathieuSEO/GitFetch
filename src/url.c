/*
 * url.c -- invoer van de gebruiker terugbrengen tot "owner/repo".
 *
 * Mensen plakken van alles: de repo-pagina, de releases-pagina, een
 * clone-URL. Dat allemaal accepteren scheelt frustratie op een machine
 * waar tekst intypen sowieso al omslachtig is.
 */

#include <exec/types.h>
#include <string.h>

#include "gitfetch.h"
#include "gfurl.h"

static BOOL alnum(char c)
{
    return (BOOL)((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                  (c >= '0' && c <= '9'));
}

/* GitHub-gebruikersnamen bestaan alleen uit alfanumeriek en koppeltekens.
   Juist die beperking maakt ze te onderscheiden van een hostnaam: staat er
   een punt in, dan plakte iemand een URL van een andere dienst
   (gitlab.com/owner/repo) en heeft doorgaan geen zin. */
static BOOL owner_char(char c)
{
    return (BOOL)(alnum(c) || c == '-');
}

/* Repository-namen mogen wel punten en underscores bevatten. */
static BOOL repo_char(char c)
{
    return (BOOL)(alnum(c) || c == '-' || c == '_' || c == '.');
}

static BOOL skip_prefix(const char **p, const char *prefix)
{
    LONG len = strlen(prefix);

    if (strnicmp(*p, prefix, len) == 0) {
        *p += len;
        return TRUE;
    }
    return FALSE;
}

LONG gf_normalize_repo(const char *input, char *out, LONG outsize)
{
    const char *p = input;
    const char *owner_start, *repo_start;
    LONG owner_len = 0, repo_len = 0, total;

    if (!input || !out || outsize < 4)
        return GF_ERR_URL;

    while (*p == ' ' || *p == '\t')
        p++;

    skip_prefix(&p, "https://");
    skip_prefix(&p, "http://");
    skip_prefix(&p, "www.");
    skip_prefix(&p, "github.com/");
    skip_prefix(&p, "github.com:");     /* git@github.com:owner/repo.git */
    if (skip_prefix(&p, "git@"))
        skip_prefix(&p, "github.com:");

    while (*p == '/')
        p++;

    owner_start = p;
    while (owner_char(*p)) {
        p++;
        owner_len++;
    }
    if (owner_len == 0 || *p != '/')
        return GF_ERR_URL;
    p++;

    repo_start = p;
    while (repo_char(*p)) {
        p++;
        repo_len++;
    }
    if (repo_len == 0)
        return GF_ERR_URL;

    /* Alles hierna (/releases, /tree/main, ?tab=...) is voor ons irrelevant. */

    /* ".git"-suffix van clone-URLs eraf. */
    if (repo_len > 4 && strnicmp(repo_start + repo_len - 4, ".git", 4) == 0)
        repo_len -= 4;
    if (repo_len == 0)
        return GF_ERR_URL;

    total = owner_len + 1 + repo_len;
    if (total >= outsize)
        return GF_ERR_URL;

    memcpy(out, owner_start, owner_len);
    out[owner_len] = '/';
    memcpy(out + owner_len + 1, repo_start, repo_len);
    out[total] = '\0';
    return GF_OK;
}
