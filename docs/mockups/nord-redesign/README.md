# SY1-schermstijlen — versie 0.3.1

[Open de schermgalerij](index.html): alle **32 subpagina's in drie stijlen**, met **142 echte
schermbeelden van 240 × 240 pixels**. De knoppen bovenaan wisselen tussen 1× en 2×.

## Een stijl kiezen

Op **GLO → DISPLAY (6/6)** kiest KNOB1 tussen:

- **NORD:** rode subpaginastrook, ivoorkleurige tekst en amber encoderfocus.
- **GREEN / Green LED:** groene 5×7 pixelletters en zevensegmentcijfers op zwart.
- **LCD / Green LCD:** donkere letters, cijfers en grafieken op zachtgroen. De actieve encoder krijgt
  een lichter vlak met een donkere markering; opgeslagen referenties blijven zichtbaar.

NORD is de standaard. De keuze is globaal, wordt bij stilte bewaard en beïnvloedt de patch of audio niet.
Oude 12-byte instellingen worden met behoud van de geselecteerde patch, octaaf, tempo, voice-limiet,
categorie en dronekeuze ingelezen. De screenreader houdt de oorspronkelijke tekst en noemt de keuze.

## 500 presets meegeleverd

Vanaf 0.3.0 staan F001–F500 in program flash, naast P01–P08. ALGORITHM één stap links van ALL kiest
FACTORY. Alle gebruikersslots en bestaande programmanummers blijven behouden. SAVE maakt een eigen
kopie; het wissen van gebruikerspatches verwijdert de fabriekspresets niet. De selectie en volledige
makerscredits staan in `app/presets/` en zijn via de manager te bekijken en te downloaden.

## Gemeenschappelijke bediening

De patchkop, subpaginastrook, het klankvenster, vier encoderkolommen en de speelstatus staan op iedere
pagina op dezelfde plaats. EDIT vergelijkt parameters, categorie en naam met de werkelijk opgeslagen
versie; terugdraaien naar die waarden wist EDIT. Subpagina's worden per sectie onthouden.

Eenheden komen uit dezelfde tabellen en formules als de engine. Decay en release tonen een tijdconstante;
kleine beatfracties blijven exact. GLO → DIAGNOSTIC is alleen-lezen en toont CPU, piekbelasting,
stemverlaging en de ruwe Synth1-waarde. GLO → DISPLAY is de zesde subpagina.

De audiozijde publiceert maximaal vijftien keer per seconde een consistente momentopname. De UI leest
hieruit envelope-niveau/stadium, LFO-fase/waarde en arp-positie. Alleen gewijzigde schermzones worden
verstuurd. De stijlkeuze voegt geen framebuffer of DSP-effectbuffer toe. Andere grafieken tonen
instellingen, geen gemeten audio- of filterrespons.

## Bouwen en controleren

```sh
make test test-app test-web
make preview-nord
./build.sh --release 0.3.1
./build.sh --web --release 0.3.1
make test-site VERSION=0.3.1
```

`make preview-nord` controleert basisbediening, performance, live feedback en de drie schermstijlen,
exporteert deze galerij en vernieuwt de screenshots in `app/docs/img` en `docs/img`.
Met `python3 tools/ux_review.py --output /pad/naar/map` kan een aparte export worden gemaakt.

De installeerbare release is `app/build/sy1-0.3.1.fwsc`; de complete website wordt met
`tools/publish_site.sh 0.3.1 [--push]` gebouwd. Browsercontrole: `host/display_browser_test.mjs`, met
`PLAYWRIGHT_MODULE` voor een geïnstalleerde Playwright-module en `SY1_TEST_URL` voor de complete site.

## Validatie op 16 november 2026

- Engine: **202 controles**, inclusief 400 willekeurige patches; geen fouten.
- App: **3064 controles**, met migratie, herstart, ongeldige voorkeuren en minimum-/maximumwaarden;
  geen fouten. Een stijlwissel verandert geen patchwaarden en voegt geen audio-event toe.
- JavaScript/C-reader en ZIP-import: **62 controles**; alle **3.174 presets in 27 banken** geverifieerd.
- Echte hostsimulator: **255 controles**, **32 subpagina's per stijl**, **142 screenshots**.
- De WAV van het basisscenario is byte voor byte gelijk aan die van vóór het redesign.
- Browser: de 500 fabriekspresets zijn direct beschikbaar, inclusief F001/F500-wrapping. De Factory 500-filter
  toont precies 500 items. LED/LCD-keuzes blijven na herladen behouden en terugschakelen naar NORD werkt.
- Firmware: controles voor registers, RAM, pool en enkelvoudige precisie slagen.

De voorafgaande kandidaat **0.2.1-green** is op 16 november 2026 op de fysieke FM-1 geïnstalleerd.
De installer bevestigde **FM-1_47000201**. Vóór en na de update waren 513 patchslots in gebruik;
patch **379 Polarizer Lead** en de uitgelezen filter-/gainwaarden bleven behouden. De nieuwe pagina
meldde **STYLE LCD**. De log meldde geen crash en geen stemverlaging. Leesbaarheid en audiobelasting
tijdens spelen zijn hiermee nog niet volledig beoordeeld.

Bron: [renderer](../../../app/ui/nord.h), [schermstijlen en lettervormen](../../../app/ui/display.h),
[eenheden](../../../app/ui/values.h), [momentopnamen](../../../app/ui/live.h),
[app](../../../app/app.c), [display-scenario](../../../app/tests/display.txt) en
[export](../../../tools/ux_review.py). De [oorspronkelijke pixelproef A](../nord-pixel/index.html)
blijft als ontwerpreferentie beschikbaar.
