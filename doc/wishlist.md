# Wensenlijst

Ideeën die de moeite waard lijken maar niet in 0.1 zitten. Bewust apart
gehouden: het programma moet klein blijven. Op een Amiga met 2 MB telt elke
kilobyte, en een functie die niemand gebruikt kost geheugen bij iedereen.

Per punt staat er een inschatting van wat het aan code kost, want dat is
hier net zo goed een afweging als het nut.

## Overwogen voor een volgende versie

**Werken op OS 3.0 tot 3.4 met ClassAct**
ReAction is voortgekomen uit ClassAct en gebruikt dezelfde klassenamen en
tags. De ClassAct-classes melden zich echter als versie 41/42, terwijl
`open_classes()` hard versie 44 eist -- daar loopt het op stuk. De vijf
classes die GitFetch echt nodig heeft (window, layout, button, string,
listbrowser) zitten bevestigd in ClassAct 2.0, met de tags die wij
gebruiken.

Het voorstel is de versie-eis per class te zetten: nul voor die vijf, en
44 blijven eisen voor de optionele (chooser, getfile, fuelgauge). Dat
laatste is geen detail: `AllocChooserNode` is een library-functie, en die
aanroepen op een oudere chooser is een sprong in het niets. Onbekende
tags worden onschadelijk genegeerd, ontbrekende functies niet.

Kosten: een paar regels, geen noemenswaardige bytes. Vraagt wel een
testronde onder emulatie met OS 3.1 plus de ClassAct-classes; niet alles
kon vooraf bevestigd worden, met name het menu en de dubbelklik.


**Snelheid tijdens het downloaden (KB/s)**
De cijfers zijn er al: `sofar` en de starttijd. Op een trage lijn is het
verschil tussen "traag" en "vastgelopen" nu niet te zien, wat mensen
onnodig ongerust maakt. Kosten: klein, een paar honderd bytes.

**Laatst gebruikte repository onthouden**
Bij het starten meteen het laatste adres in het veld. Kosten: klein, sluit
aan op de bestaande prefs.

**Sorteerbare kolommen**
`LISTBROWSER_TitleClickable` bestaat al in de class; het is vooral de
sorteerlogica die erbij komt. Nut is beperkt: releases staan al op datum.
Kosten: middel.

**Filteren in de bestandenlijst**
Nuttig bij releases met tientallen bestanden, wat zeldzaam is bij
Amiga-software. Kosten: middel.

## Bewust niet

**Uitpakken en installeren na het downloaden**
Buiten de opzet gehouden: GitFetch haalt binnen, de rest doe je met de
gereedschappen die je al vertrouwt. Dat scheelt ook xadmaster of lha als
afhankelijkheid.

**Een NTP-client om de klok te zetten**
De klok zetten is een systeemtaak, en daar bestaan goede programma's voor
(Roadshow heeft er een ingebouwd, en SNTP staat op Aminet). Twee
programma's die aan dezelfde klok zitten geeft meer gedoe dan het oplost.
GitFetch controleert de datum wel en zegt het als hij niet klopt.

**ARexx-poort**
Op AmigaOS gebruikelijk voor scriptbaarheid, maar het vraagt een
commandotabel, een eigen poort en documentatie. Bij versie 0.1 met een
handjevol gebruikers weegt dat niet op tegen de omvang. Kosten: groot.

**Meer talen**
De opzet ligt er: alle teksten lopen via `gf_str()` en `locale.library`
wordt geopend. Een vertaling is een catalog in `LOCALE:Catalogs/<taal>/`
en kost geen enkele coderegel. Wacht op iemand die er een wil maken.
