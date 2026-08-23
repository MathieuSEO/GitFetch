# Het GITFETCH-protocol

Plain HTTP, ISO-8859-1, regel- en tabgebaseerd. Bewust zo gekozen: op een
68k is dit met `FGets()` en `strchr()` te verwerken zonder allocaties,
zonder state machine en zonder JSON-parser.

De proxy doet het werk dat op de Amiga duur is: JSON platslaan, UTF-8 naar
ISO-8859-1 translitereren (release-titels bevatten emoji), tabs en
control-tekens strippen, en velden afkappen op de maximale lengtes uit
`include/gitfetch.h`.

## GET /v1/releases?repo=owner/naam&max=15

```
#GITFETCH 1
#STATUS OK
#REPO jens-maus/amissl
R<TAB>0<TAB>5.27<TAB>2026-04-08<TAB>0<TAB>AmiSSL 5.27
A<TAB>0<TAB>0<TAB>AmiSSL-5.27-OS3.lha<TAB>4294781<TAB>/v1/asset?id=...
A<TAB>0<TAB>1<TAB>AmiSSL-5.27-SDK.lha<TAB>2583127<TAB>/v1/asset?id=...
R<TAB>1<TAB>5.26<TAB>2026-01-28<TAB>0<TAB>AmiSSL 5.26
#END
```

| Regel | Velden |
|---|---|
| `R` | release-index, tag, datum (JJJJ-MM-DD), prerelease (0/1), titel |
| `A` | release-index, asset-index, bestandsnaam, grootte in bytes, download-pad |

Regels die met `#` beginnen zijn besturingsregels. Assets horen direct
achter hun eigen `R`-regel te staan; een `A` met een release-index die niet
bij de laatst gelezen release hoort wordt genegeerd in plaats van aan de
verkeerde release gehangen.

Fout:

```
#GITFETCH 1
#STATUS ERR 404 Repository niet gevonden
#END
```

**`#END` is verplicht.** Ontbreekt het, dan is de transfer halverwege
afgebroken en meldt de client een fout. Een halve lijst tonen alsof het de
hele is, is erger dan een foutmelding: je zou de nieuwste release missen
zonder het te merken.

## GET /v1/asset?id=&lt;opaque&gt;

Antwoordt met `200`, een `Content-Length` (nodig voor de voortgangsbalk) en
de bytes van het bestand, rechtstreeks doorgestreamd vanaf GitHub.

Het `id` is een HMAC-getekende, in tijd begrensde verwijzing naar
owner/repo/asset-id. Zonder die ondertekening zou de proxy een open relay
zijn waarmee iedereen bandbreedte van de server kan wegtrekken. De upstream
wordt daarnaast hard geallowlist op `api.github.com` en de
`*.githubusercontent.com`-hosts.

Doorverwijzen naar de echte GitHub-URL kan niet: die is HTTPS-only en de
Amiga heeft in fase 1 geen TLS.

## GET /v1/hello

```
#GITFETCH 1
#STATUS OK
#TIME 1787315581
#END
```

Voor een "test de verbinding"-knop.
