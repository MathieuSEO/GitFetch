/*
 * gfassets.h -- welke bestanden van een release we bewaren.
 *
 * Een release kan meer bestanden hebben dan GF_MAX_ASSETS. Dan gaan de
 * archief- en diskformaten die een Amiga-gebruiker zoekt voor de rest
 * (Windows-installers, Linux-pakketten, checksums). De proxy past
 * dezelfde regels toe; die staan in server/gitfetch_proxy.py.
 *
 * Platformonafhankelijk, zodat het op de host te testen is.
 */

#ifndef GFASSETS_H
#define GFASSETS_H

#include <exec/types.h>
#include "gitfetch.h"

#define GF_RANK_OTHER    1
#define GF_RANK_ARCHIVE  2      /* .lha, .lzx, .adf en verwanten */

/* Hoe graag we dit bestand bewaren als er niet genoeg plek is. */
UWORD gf_asset_rank(const char *name);

/*
 * De broncode-zip die GitHub bij elke release aanbiedt is geen echt
 * bijgevoegd bestand. Beide backends zetten hem als laatste in de lijst,
 * met een naam op -source.zip en grootte 0 (die kent GitHub vooraf niet).
 */
BOOL gf_asset_is_source(const struct Asset *asset);

/* Naam voor die zip: <repo>-<tag>-source.zip, ingekort tot GF_MAX_NAME. */
void gf_source_name(const char *repo, const char *tag, char *out);

/*
 * Neemt een nieuw bestand op in een volle release als het belangrijker is
 * dan het minst belangrijke dat er al staat. Dat valt dan af; de volgorde
 * van de rest blijft gelijk. Telt hoe dan ook een overgeslagen bestand.
 */
void gf_asset_offer(struct Release *rel, const struct Asset *asset);

/* Aantal echte bestanden, dus zonder de broncode-zip. */
UWORD gf_release_files(const struct Release *rel);

#endif /* GFASSETS_H */
