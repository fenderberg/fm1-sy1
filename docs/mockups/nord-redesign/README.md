# Volledig Nord-redesign van SY1

[Open de schermgalerij](index.html): alle **31 subpagina's**, met in totaal 57 echte schermbeelden van
240 × 240 pixels. De knoppen bovenaan wisselen tussen 1× en 2×. De [oorspronkelijke pixelproef A](../nord-pixel/index.html)
blijft beschikbaar om te vergelijken.

## Doorgevoerd

- Vaste patchkop, rode subpaginastrook, klankvenster, vier encoderkolommen en speelstatus op alle pagina's.
- Amber encoderfocus, ook tegen een parametergrens. Nieuwe subpagina's starten zonder focus.
- Subpaginageheugen per sectie tijdens de sessie; geen extra flashschrijfacties.
- Opgeslagen markeringen en een exacte EDIT-status voor de parameters, categorie en patchnaam.
  Terugdraaien naar de opgeslagen versie wist EDIT. Laden, opslaan, REC en ontvangen patches vernieuwen
  de referentie. Een ontvangen patch vervangt lopende onopgeslagen edits niet.
- Schakelaars en keuzes hebben afzonderlijke posities; bipolaire regelaars hebben een nulmarkering.
- Eenheden uit dezelfde gemeten tabellen/formules als de engine: attack, envelope-tijdconstanten,
  sustain, pitch, LFO, arp, delay, chorus rate en EQ. Kleine beatfracties worden exact als breuk getoond.
- Een vaste ARP/Hold/Drone-status, MIDI-activiteit en INT/EXT-clockbron. CPU-beperking blijft herkenbaar
  via LIMIT; GLO heeft een vijfde subpagina met alleen-lezen diagnostiek en de ruwe Synth1-waarde.
- Meldingen verbergen de patchnaam niet. Waarschuwingen hebben voorrang op routinefeedback.
- De screenreader leest de subpagina, vier waarden/eenheden en speelstand; animaties blijven stil.

## Bewegende feedback

De audiozijde publiceert maximaal vijftien keer per seconde een kleine consistente momentopname.
De UI leest daaruit envelope-niveau/stadium, LFO-fase/waarde, actieve stemmen en arp-positie.
De envelope volgt de nieuwste stem, ook tijdens release, en springt na het einde niet terug naar een oudere
vastgehouden noot. Een generatiecontrole voorkomt dat een patchwissel oude animatiedata overneemt.

Random LFO's tonen recente echte waarden. Andere LFO's tonen hun fasepunt op de ingestelde golfvorm.
Een arp-stap is een weergave van wat de engine speelt, zonder sequencerbediening. De overige grafieken zijn
schematische instellingenbeelden, geen gemeten audio- of filterrespons.

Alleen gewijzigde schermzones worden naar het scherm gestuurd. Een lopende LFO tekent de patchkop en
encoderstrook niet opnieuw. De vijf zones passen afzonderlijk in de bestaande RGB565-tekenstrook;
er is geen extra framebuffer of DSP-effectbuffer toegevoegd.

## Bouwen en opnieuw controleren

```sh
make test
make test-app
make test-web
make preview-nord
./build.sh --web
./build.sh
```

`make preview-nord` bouwt de normale hostsimulator, draait de basis-, performance- en UX-scenario's en
exporteert alle subpagina's naar deze map. Een andere uitvoermap kan met
`python3 tools/ux_review.py --output /pad/naar/map`.

De installeerbare ontwikkelbuild staat in `app/build/sy1.fwsc`; de browseremulator in `app/build/emu`.
Een release met de website en bijbehorende screenreaderwoorden wordt met het bestaande
`tools/publish_site.sh VERSION [--push]` gebouwd.

## Validatie op 16 november 2026

- Engine: **202 controles**, inclusief 400 willekeurige patches; geen fouten.
- App: **220 controles**, inclusief alle subpagina's bij standaard-, minimum- en maximumwaarden;
  opslag, referenties, import, navigatie, animatie en onafhankelijk tekenen; geen fouten.
- JavaScript/C-patchreader: **62 controles**, plus integriteit van alle 3.174 openbare presets; geen fouten.
- Echte hostsimulator: **130 controles**, alle 31 subpagina's en 57 schermbeelden; geen fouten.
- De WAV van het basisscenario is byte voor byte gelijk aan de simulator van vóór het redesign.
- Firmware en WebAssembly bouwen. De firmwarecontroles voor registers, RAM, pool en enkelvoudige
  precisie slagen. Image: 178612 bytes; RAM data/bss: 39128/98304; pool: 237320/344064 bytes.
- Browsercontrole gebruikt het echte WebAssembly-programma voor bediening, encoder/pagina-navigatie,
  envelope- en LFO-beweging. De galerij ondersteunt oorspronkelijke resolutie, vergroting en mobiel.

De aanvullende macOS AddressSanitizer-run kon niet starten: de lokale Clang-runtime faalt tijdens zijn
eigen malloc-initialisatie. De bovenstaande gewone controles zijn wel uitgevoerd.

Op 16 november 2026 is deze build over USB op de fysieke FM-1 geïnstalleerd en na de herstart gecontroleerd.
Het apparaat meldt FM-1_4700; de nieuwe volledige subpagina-/parameterfeedback werkt en de 513 gebruikte
patchslots zijn na de update nog aanwezig. De log meldt geen crash en geen nieuwe stemverlaging.
Een volledige beoordeling van fysieke schermleesbaarheid, tekentijd en audiobelasting tijdens het spelen
is hiermee nog niet afgerond.

Bron: [schermrenderer](../../../app/ui/nord.h), [waarden](../../../app/ui/values.h),
[momentopnamen](../../../app/ui/live.h), [app](../../../app/app.c),
[UX-scenario](../../../app/tests/ux.txt) en [bouw-/exportsysteem](../../../tools/ux_review.py).
