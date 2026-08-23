# GitFetch -- GitHub-releases downloader voor AmigaOS 3.9
#
#   make test     hosttests (draaien op de Mac, geen cross-compiler nodig)
#   make fuzz     fuzz-test van de parser en de URL-normalisatie
#   make test-proxy  tests voor de proxy (ondertekening, allowlist, tekst)
#   make test-net    end-to-end test van de netwerkketen tegen een proxy
#   make amiga    cross-compileren voor de Amiga (vereist m68k-amigaos-gcc)
#   make gftest   alleen de CLI-testtool
#   make gitfetch alleen het GUI-programma
#   make proxy    lokale proxy starten op poort 8099
#   make dist    distributiepakket met icoon, guide en LhA-archief
#   make clean

# --- cross-compiler ---------------------------------------------------------
AMIGA_PREFIX ?= /opt/amiga
CC_AMIGA     := $(AMIGA_PREFIX)/bin/m68k-amigaos-gcc

# 68020 als ondergrens: AmiSSL vereist dat in fase 2 sowieso, en de doel-
# machine is een 68060. -noixemul om de C-runtime uit ixemul te vermijden;
# dat scheelt afhankelijkheden en geheugen.
# AmiSSL-SDK voor de rechtstreekse verbinding met GitHub.
# Een echte LhA maakt een archief met compressie (lh5). Ontbreekt hij, dan
# valt de build terug op tools/make_lha.py, die wel een geldig archief
# schrijft maar niet comprimeert.
# Binary: https://github.com/amigavision/LhA
LHA ?= $(AMIGA_PREFIX)/bin/amiga-lha

AMISSL_DIR ?= $(AMIGA_PREFIX)/m68k-amigaos/amissl
AMISSL_CFLAGS := -I$(AMISSL_DIR)/include
AMISSL_LIBS   := -L$(AMISSL_DIR)/lib -lamisslauto

# -Wno-pointer-sign: de NDK typeert Amiga-strings als 'unsigned char *'
# (STRPTR), dus elke gewone C-stringliteral levert anders een waarschuwing.
# Dat is inherent aan de OS-headers, niet aan onze code.
# -Os in plaats van -O2: dat scheelt hier zo'n 14 procent aan
# executablegrootte, en dit programma wacht vooral op het netwerk. Kleinere
# code laadt sneller en laat meer geheugen over op een machine met 2 MB.
#
# -fomit-frame-pointer geeft een register terug en scheelt nog wat code.
AMIGA_CFLAGS := -m68020 -Os -noixemul -Wall -Wextra -Wno-pointer-sign \
                -Iinclude $(AMISSL_CFLAGS) -fomit-frame-pointer \
                -ffunction-sections -fdata-sections
# --gc-sections gooit functies weg die nergens gebruikt worden; -s laat de
# symbooltabel weg, die op de Amiga niets doet.
AMIGA_LDFLAGS := -noixemul -Wl,--gc-sections -s

COMMON_SRC := src/parse.c src/url.c src/errors.c src/net.c src/json.c \
              src/backend.c src/backend_proxy.c src/backend_native.c \
              src/locale.c
CLI_SRC    := $(COMMON_SRC) src/prefs.c src/gftest.c
GUI_SRC    := $(COMMON_SRC) src/worker.c src/gui.c src/prefswin.c \
              src/bookmarks.c src/prefs.c src/main.c

# -lamiga levert DoMethod() en de overige amiga.lib-hulpjes. De ReAction-
# classes worden met OpenLibrary geopend en hoeven niet gelinkt te worden.
GUI_LIBS   := $(AMISSL_LIBS) -lamiga

# --- host (tests) -----------------------------------------------------------
CC_HOST      ?= cc
HOST_CFLAGS  := -std=c99 -Wall -Wextra -g -Itest/shim -Iinclude \
                -Dstrnicmp=strncasecmp -Dstricmp=strcasecmp
HOST_SAN     := -fsanitize=undefined -fno-sanitize-recover=all
BUILD        := build

.PHONY: all dist test test-proxy test-json test-net fuzz amiga gftest gitfetch gitfetch-debug proxy clean check-toolchain

all: test

$(BUILD):
	@mkdir -p $(BUILD)

# Alleen de platform-onafhankelijke modules zijn op de host te bouwen;
# net.c en backend_proxy.c hebben bsdsocket en dos.library nodig.
test: | $(BUILD)
	$(CC_HOST) $(HOST_CFLAGS) $(HOST_SAN) -o $(BUILD)/test_parse \
		test/test_parse.c src/parse.c src/url.c
	@$(BUILD)/test_parse

test-proxy:
	@python3 server/test_proxy.py

# End-to-end test van de netwerkketen tegen een draaiende proxy, met een
# POSIX-shim onder net.c. Test wat de hosttests niet raken: het HTTP-verzoek,
# het scheiden van headers en body, en het streamend wegschrijven.
#   make test-net PROXY_HOST=127.0.0.1 PROXY_PORT=8099 REPO=jens-maus/amissl
PROXY_HOST ?= 127.0.0.1
PROXY_PORT ?= 8099
REPO       ?= jens-maus/amissl

# Vergelijkt de JSON-parser met een referentie-implementatie bij allerlei
# blokgroottes; de testdata staan in test/data.
test-json: | $(BUILD)
	$(CC_HOST) $(HOST_CFLAGS) $(HOST_SAN) -o $(BUILD)/test_json \
		test/test_json.c src/json.c
	@for f in test/data/*.json; do \
		for c in 1 3 13 511 4096; do \
			$(BUILD)/test_json $$f $$c > $(BUILD)/got.txt; \
			python3 test/json_reference.py $$f > $(BUILD)/ref.txt; \
			if ! diff -q $(BUILD)/ref.txt $(BUILD)/got.txt >/dev/null; then \
				echo "VERSCHIL: $$f blok $$c"; \
				diff $(BUILD)/ref.txt $(BUILD)/got.txt | head -5; exit 1; \
			fi; \
		done; \
		echo "  $$f: identiek aan de referentie"; \
	done

test-net: | $(BUILD)
	$(CC_HOST) $(HOST_CFLAGS) -o $(BUILD)/test_net \
		test/test_net.c test/shim/shim.c test/shim/native_stub.c \
		src/net.c src/parse.c src/url.c src/errors.c src/json.c \
		src/backend.c src/backend_proxy.c
	@$(BUILD)/test_net $(PROXY_HOST) $(PROXY_PORT) $(REPO)

fuzz: | $(BUILD)
	$(CC_HOST) $(HOST_CFLAGS) $(HOST_SAN) -o $(BUILD)/fuzz_parse \
		test/fuzz_parse.c src/parse.c src/url.c src/json.c
	@$(BUILD)/fuzz_parse

check-toolchain:
	@test -d $(AMISSL_DIR)/include || { \
		echo "AmiSSL-SDK niet gevonden op $(AMISSL_DIR)."; \
		echo "Zie doc/toolchain.md."; exit 1; }
	@test -x $(CC_AMIGA) || { \
		echo "m68k-amigaos-gcc niet gevonden op $(CC_AMIGA)."; \
		echo "Zie doc/toolchain.md voor de installatie."; \
		exit 1; }

amiga: gftest gitfetch

gftest: check-toolchain | $(BUILD)
	$(CC_AMIGA) $(AMIGA_CFLAGS) -o $(BUILD)/gftest $(CLI_SRC) \
		$(AMIGA_LDFLAGS) $(AMISSL_LIBS) -lamiga
	@echo "Klaar: $(BUILD)/gftest"

# Diagnostische build: print elke stap van de vensteropbouw naar de Shell.
# Ook zonder -fomit-frame-pointer en met -O1, zodat een crashadres beter te
# herleiden is.
gitfetch-debug: check-toolchain | $(BUILD)
	$(CC_AMIGA) -m68020 -O1 -noixemul -Wall -Wextra -Wno-pointer-sign \
		-Iinclude -DGF_DEBUG -o $(BUILD)/GitFetchDebug $(GUI_SRC) \
		$(AMIGA_LDFLAGS) $(GUI_LIBS)
	@echo "Klaar: $(BUILD)/GitFetchDebug"

gitfetch: check-toolchain | $(BUILD)
	$(CC_AMIGA) $(AMIGA_CFLAGS) -o $(BUILD)/GitFetch $(GUI_SRC) \
		$(AMIGA_LDFLAGS) $(GUI_LIBS)
	@echo "Klaar: $(BUILD)/GitFetch"

proxy:
	GITFETCH_SECRET=$${GITFETCH_SECRET:-lokale-test-sleutel} \
	GITFETCH_PORT=$${GITFETCH_PORT:-8099} \
	python3 server/gitfetch_proxy.py

# Bouwt het distributiepakket: icoon, documentatie, proxy en archief.
# dist/ wordt volledig opgebouwd uit build/ en pkg/; de bronbestanden van
# het pakket (guide, installatiescript, Aminet-beschrijving) staan in pkg/.
dist: amiga
	@rm -rf dist
	@mkdir -p dist/GitFetch/server
	cp pkg/GitFetch.guide pkg/Install dist/GitFetch/
	cp pkg/GitFetch.readme dist/
	python3 tools/make_icon.py tool   dist/GitFetch/GitFetch.info
	python3 tools/make_icon.py guide  dist/GitFetch/GitFetch.guide.info
	python3 tools/make_icon.py installer dist/GitFetch/Install.info
	python3 tools/make_icon.py drawer dist/GitFetch.info
	cp $(BUILD)/GitFetch $(BUILD)/gftest dist/GitFetch/
	cp server/gitfetch_proxy.py server/test_proxy.py dist/GitFetch/server/
	cp doc/proxy-deploy.en.md dist/GitFetch/server/README-proxy.en.md
	cp doc/proxy-deploy.nl.md dist/GitFetch/server/README-proxy.nl.md
	@cd dist && rm -f GitFetch.lha && \
	if [ -x "$(LHA)" ]; then \
		"$(LHA)" a --ignore-mac-files GitFetch.lha GitFetch \
			GitFetch.info GitFetch.readme > /dev/null && \
		echo "archief met compressie (lh5) via $(LHA)"; \
	else \
		echo "$(LHA) niet gevonden; archief zonder compressie"; \
		python3 ../tools/make_lha.py GitFetch.lha . GitFetch \
			GitFetch.info GitFetch.readme > /dev/null; \
	fi
	@echo "Klaar: dist/GitFetch.lha"

clean:
	rm -rf $(BUILD)
