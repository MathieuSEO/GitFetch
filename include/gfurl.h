#ifndef GFURL_H
#define GFURL_H

#include <exec/types.h>

/*
 * Reduceert wat de gebruiker ook plakt tot "owner/repo".
 * Accepteert onder meer:
 *   https://github.com/owner/repo
 *   github.com/owner/repo/releases/latest
 *   www.github.com/owner/repo.git
 *   owner/repo
 * Geeft GF_OK of GF_ERR_URL.
 */
LONG gf_normalize_repo(const char *input, char *out, LONG outsize);

#endif /* GFURL_H */
