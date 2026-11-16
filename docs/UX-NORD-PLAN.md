# Plan: SY1-firmware met een Nord-achtige bediening

Status: het software-redesign is doorgevoerd voor alle 32 subpagina's in firmware, host en browseremulator.
De gedeelde indeling, opgeslagen referenties, precieze EDIT-status, subpaginageheugen en enginegestuurde
envelope-/LFO-/arp-feedback zijn geïmplementeerd. Beoordeling van fysieke leesbaarheid en timing op een
echte FM-1 blijft nodig. Bekijk de [volledige schermgalerij](mockups/nord-redesign/index.html).
De [oorspronkelijke pixelproef A](mockups/nord-pixel/index.html) blijft beschikbaar als ontwerpreferentie.
Vanaf versie 0.2.1 is er daarnaast een globale keuze op GLO → DISPLAY tussen NORD,
GREEN LED (groen op zwart) en GREEN LCD (donkere tekens op groen), met behoud van grafieken en live feedback.
Hieronder staat het oorspronkelijke ontwerpplan.
Gebaseerd op de bestaande SY1-firmware,
de FM-1 met een scherm van 240 × 240 pixels en de gekozen richting: een compact hardwarepaneel
met vier bedieningsvakken en een grote klankvisualisatie.

## Gewenst resultaat

Je ziet onmiddellijk welke klank speelt, waar je bent en wat de vier encoders doen.
Een draai laat zowel de waarde als de bijbehorende vorm veranderen. ARP, Hold en Drone blijven
herkenbaar tijdens het spelen. De rode accenten, ivoorkleurige tekst en amber feedback sluiten
aan bij de vernieuwde website en de Nord Modular/rack-inspiratie.

De eerste uitvoering verandert de presentatie en navigatiefeedback. Bestaande Synth1-parameters,
patchbestanden, MIDI, opslag en de toewijzing van de fysieke knoppen blijven het uitgangspunt.
Macro's, morphing en A/B-snapshots zijn afzonderlijke vervolgfuncties; ze zijn niet nodig om dit
ontwerp goed te laten werken.

## Wat er nu staat

- `app/app.c` heeft acht pagina's met vaste rijen van maximaal vier parameters. SELECT kiest een rij;
  opnieuw op de paginaknop drukken kiest de volgende rij. ARP heeft een eigen korte/lange druk.
- Het scherm bestaat uit een kop van 46 pixels, een middengebied dat meerdere rijen probeert te
  tonen en een statusgebied van 42 pixels. Waarden zijn meestal de interne parametergetallen.
- Het hele scherm wordt opnieuw getekend wanneer een parameter of status verandert. De statusmeting
  loopt vier keer per seconde. Er is nog geen afzonderlijke toestand voor de laatst bediende encoder.
- Meldingen via `app_say()` vervangen tijdelijk de patchkop. Diezelfde functie verzorgt ook
  gesproken feedback. De nieuwe visuele presentatie moet die toegankelijkheid behouden.
- Er is geen aparte referentiekopie van de opgeslagen parameterwaarden voor terugkeermarkeringen.
- De renderer in `base/device/gfx.c` werkt met een herbruikbare tekenstrook, standaard maximaal
  240 × 144 pixels, en ondersteunt rechthoeken, lijnen en tekst. Een volledig framebuffer is niet nodig.

## 1. Een vaste schermindeling

| Zone | Pixels, inclusief grenzen | Inhoud |
| --- | --- | --- |
| Patchkop | y 0–39: 40 px | Patchnummer, categorie, EDIT-indicator en een duidelijke patchnaam |
| Modulestrook | y 40–55: 16 px | Pagina/subpagina, positie zoals 1/2, korte bevestiging rechts |
| Klankvenster | y 56–135: 80 px | Filter, envelope, golfvorm of een passende statusvisualisatie |
| Vier bedieningsvakken | y 136–215: 80 px | Vier kolommen van 60 px: label, indicator en grote waarde |
| Speelstatus | y 216–239: 24 px | Twee compacte regels voor stemmen, octaaf, tempo/clock en speelstand |

Schematisch, met dezelfde vier encoders als nu:

```text
P02          PAD          EDIT
Warm Pad
FILTER / MAIN             1/2

        filtercurve
      met cutoffmarkering

 TYPE   CUTOFF   RESO    AMOUNT
 [..]    [..]    [..]     [..]
 LP24     75      18       78

3/8   OCT +0   120 INT   MIDI •
ARP ON   HOLD OFF
```

De getallen in deze schets zijn bestaande parameterwaarden. Omrekening naar bijvoorbeeld Hz
volgt alleen waar die met de engineberekening overeenkomt.

Er is steeds één actieve parameterrij zichtbaar. Een duidelijke subpaginanaam en rijpositie
vervangen het overzicht van meerdere kleine rijen. Eerst in de simulator beoordelen of die
afweging goed werkt bij de langere OSC- en FX-pagina's.

## 2. Kleuren en indicatoren krijgen één betekenis

- **Donker antraciet:** achtergrond en rustige paneelvlakken.
- **Ivoor:** leesbare labels, waarden en hoofdvormen.
- **Rood:** vaste modulestrook en instrumentaccenten. Fouten krijgen ook expliciete tekst.
- **Amber:** de laatst bediende encoder, de bijbehorende grafiekmarkering en de EDIT-status.
- **Grijs:** opgeslagen referentie, inactieve onderdelen en scheidingslijnen.

De actieve kolom krijgt tegelijk een kader/achtergrond en een lichtere waarde. Kleur is nooit de
enige aanwijzing. Geen permanente knipperanimaties, decoratieve schaduwen of nagebootste metalen
texturen op het kleine scherm. De hardware-inspiratie zit vooral in de ordening en directe feedback.

Continue parameters krijgen een kleine gesegmenteerde boog of balk. Parameters met een middenstand
krijgen een duidelijke nulmarkering. Golfvormen, filtertypen, schakelaars en categorieën krijgen een
symbool, keuzemarkering of ON/OFF-label; een ring met een schijnbaar continu bereik is daar ongeschikt.

De eerder gemaakte mockups zijn visuele referenties:
[Filter](mockups/nord-visual/filter.png), [Envelope](mockups/nord-visual/envelope.png) en
[Oscillator](mockups/nord-visual/oscillator.png). De definitieve labels, pixelmaten en rijtoewijzing
worden vanuit de echte firmware opgebouwd. Zo blijft OSC rij 1 **OSC1, OSC2, MIX, PW**; de eerdere
oscillatormockup toonde een andere combinatie en is daarvoor geen functionele specificatie.

## 3. Bediening en feedback

| Handeling | Voorgesteld gedrag |
| --- | --- |
| HOME / EDIT / SEL / ENV / LFO / FX / GLO | Open de pagina; de vaste modulestrook maakt de locatie zichtbaar |
| Opnieuw dezelfde paginaknop | Volgende subpagina, met naam en positie |
| SELECT draaien | Subpagina kiezen, met de bestaande eindgrenzen |
| KNOB1–4 draaien | Juiste kolom licht direct op; waarde en visualisatie veranderen samen |
| Een grens bereiken | Focus blijft zichtbaar, ook als de waarde niet verder kan; geen stil lijkende knop |
| ALGORITHM / PRESETS | Categorie en patchnaam veranderen in de vaste patchkop |
| SAVE | Korte bevestiging zoals SAVED 003; referentiemarkeringen worden bijgewerkt |
| REC | Geladen patch herstellen zoals nu; EDIT en referenties terugzetten, Hold/Drone vrijgeven |
| PLAY | Bestaande panic; bevestiging NOTES OFF en speelstatus direct bijwerken |

De laatst bediende kolom blijft herkenbaar totdat een andere encoder of subpagina wordt gekozen.
Bij een nieuwe subpagina is er eerst geen actieve encoder: geen misleidende automatische selectie.
Als kleine extra navigatieverbetering kan elke pagina haar laatst gebruikte subpagina onthouden
voor de lopende sessie. Geen extra flashschrijfacties; de eerste start blijft op HOME.

De huidige draai-versnelling blijft in de eerste versie behouden. Discrete keuzes moeten later
afzonderlijk op bedienbaarheid worden beoordeeld; geen nieuwe fijnregelcombinatie introduceren
zonder eerst vast te stellen welke fysieke bediening beschikbaar en begrijpelijk is.

Korte meldingen komen rechts in de modulestrook. De patchnaam en vier waarden blijven staan.
Waarschuwingen krijgen voorrang op bevestigingen. CPU-overbelasting krijgt een expliciete melding
van de bestaande verlaging van het aantal stemmen, plus een blijvende compacte indicator zolang
de toestand relevant is. De huidige CPU-percentages gaan naar een leesbare diagnostische subpagina
onder GLO; stemmen, tempo, octaaf en speelstand blijven op het gewone scherm.

## 4. Visualisaties per pagina

| Pagina | Eerste bruikbare visualisatie | Latere uitbreiding |
| --- | --- | --- |
| PLAY | Compacte klankvorm bij de bestaande filterrij; ruime patchidentiteit | Eventueel muzikale macro's als afzonderlijk project |
| FILTER | Schematische vorm van het gekozen filter, cutoff en resonance uitgelicht | Modulatiebereik tonen als de benodigde toestand beschikbaar is |
| ENV | AMP- of FILTER-ADSR voor de gekozen rij; actieve parameter markeren | Eén afspeelpunt voor een duidelijk gekozen stem |
| OSC | Ingestelde golfvormen en mix; PW-markering waar van toepassing | Extra schema's voor sync, ring/FM en mod-envelope |
| LFO | Golfvorm, bestemming, snelheid/diepte en ON/SYNC-toestand | Fasepunt op basis van echte engine-informatie |
| FX | Schema van de gekozen sectie: effect, EQ, delay of chorus | Eenvoudige tijd-/feedbackmarkeringen waar inhoudelijk zinvol |
| ARP | Richting, bereik en duidelijke ARP/HOLD/DRONE-status | Actuele gespeelde noot of stap, zonder sequencerbediening |
| VOICE / GLO | POLY/MONO/LEGATO, unison, categorie en drone-instellingen passend bij de rij | Diagnostiek voor belasting en stemverlaging |

Niet elke rij heeft een zinvolle curve: bij schakelaars en globale instellingen is een groot
helder toestandspaneel beter. Uitgeschakelde modules worden herkenbaar als OFF/BYPASS; de waarden
blijven leesbaar en bewerkbaar volgens de bestaande werking.

Filter- en golfvormtekeningen tonen de **instelling**, niet een gemeten frequentierespons of
oscilloscoop. Bij de eerste versie gebruiken we eenvoudige schematische vormen. LPDL volgt de
bestaande enginebenadering en krijgt geen grafiek die een extra filteralgoritme suggereert.
ADSR-tijden moeten op een bruikbare visuele schaal passen; een extreem lange release mag de attack
niet onzichtbaar maken. Segmenten en echte waarden blijven daarom herkenbaar bij een aangepaste schaal.

## 5. Waarden en opgeslagen referenties

Een kleine parameterbeschrijving bepaalt naam, bereik, waardetype, indicator en eventuele eenheid.
Gebruik dezelfde DSP-tabellen/omrekeningen als de engine voor ms, s en Hz; verzin geen lineaire
omrekening van de bestaande 0–127-getallen. Begin met de bestaande getallen waar de betekenis
nog niet betrouwbaar naar een fysieke eenheid is te vertalen. De Synth1-nummering en ruwe waarden
blijven beschikbaar voor diagnose en uitwisseling.

Voor terugkeermarkeringen is een RAM-kopie van de geladen/opgeslagen patch nodig, inclusief de
handmatige categorie. Die wordt vernieuwd na laden, succesvol bewaren, herstel en relevante
patchvervanging via SysEx. Tijdens draaien worden geen patches uit flash gelezen.
Globale instellingen, zoals tempo of de voice-limiet, krijgen geen markering die ten onrechte
een opgeslagen patchwaarde suggereert.

Maak onderscheid tussen de indicator per parameter en de algemene EDIT-status. Idealiter verdwijnt
EDIT wanneer alle patchwaarden én de categorie weer gelijk zijn aan de referentie. Dat vergt een
bewuste aanpassing van de huidige dirty-logica en een regressiecheck voor SAVE, REC en import.

## 6. ARP, Hold, Drone en toegankelijkheid

- ARP kort blijft aan/uit; 0,5 seconde vasthouden blijft de instellingen openen of verder bladeren.
- SEQ behoudt Hold met arp en Drone zonder arp. Het scherm gebruikt herkenbare teksten:
  ARP ON, HOLD ON, DRONE STACK of DRONE REPLACE.
- ARP- en SEQ-leds blijven de speelstand tonen. Een instellingenpagina openen verandert die stand niet.
- MIDI-clock krijgt een bronlabel, bijvoorbeeld 120 EXT tegenover 120 INT. Geen fase-lock suggereren:
  de huidige arp volgt gemeten BPM en is nog niet op de transportgrid uitgelijnd.
- Dezelfde naam en waarde die je ziet worden uitgesproken. Focus, pagina, save en fouttoestanden
  blijven toegankelijk zonder dat je op kleur of een bewegende grafiek moet vertrouwen.
- Grafiekanimaties genereren geen stroom gesproken meldingen.

## 7. Uitvoering in vier afgebakende stappen

| Stap | Werk | Klaar wanneer |
| --- | --- | --- |
| A. Echte pixelproef | HOME, FILTER en AMP ENV in de hostsimulator met de bestaande fonts; nog geen engine-animaties | Screenshots op 240 × 240 zijn leesbaar; vier kolommen passen; lange namen, uiterste waarden en meldingen lopen niet over |
| B. Bedienbare eerste versie | Gedeelde indeling op alle pagina's; rijlabels, encoderfocus, eenvoudige visualisaties en vaste speelstatus | Alle bestaande bediening blijft bereikbaar; simulator- en appcontroles slagen; emulator laat dezelfde bediening zien |
| C. Waarden en referenties | Eenheden waar correct, opgeslagen markeringen, nauwkeurige EDIT-status en meldingsprioriteit | Laden/bewaren/herstel/import en terugdraaien naar de opgeslagen waarde werken voorspelbaar |
| D. Levendige feedback | Alleen waar nuttig een echt envelope-/LFO-/arp-punt; fysieke timing en tekenbelasting meten | Reactie voelt direct; audioweergave en beschikbare stemmen verslechteren niet in dezelfde meetpatches |

Na A kunnen de schermen worden beoordeeld voordat alle pagina's worden omgezet. Dat is een ontwerpmoment,
geen reden om alvast de DSP, patchindeling of opslag te verbouwen.

## 8. Technische aanpak en grenzen

- Houd de nieuwe tekenhelpers, kleuren en schermmetadata bij de app. Wijzig de `base`-submodule
  alleen als een aantoonbaar ontbrekende primitief dat nodig maakt; bogen kunnen uit korte lijnen bestaan.
- Breid de rijmetadata uit met een korte naam en een visualisatietype. Parametertoewijzingen blijven
  uit dezelfde rijtabellen komen; de grafiek krijgt geen afzonderlijke kopie van de bedieningstoestand.
- Teken kop, grafiek, encoderstrook en status afzonderlijk bij wanneer ze veranderen. De voorgestelde
  zones passen afzonderlijk binnen de bestaande tekenstrook; geen nieuwe volledige framebuffer.
- Parameterfeedback heeft voorrang. Ontwerprichting voor animatie: maximaal 10–15 beelden per seconde,
  alleen op de zichtbare pagina en wanneer er daadwerkelijk iets beweegt. Dit zijn doelen om te meten,
  geen al bewezen hardwareprestaties.
- Fase/voice-animatie pas na een kleine consistente momentopname uit de audiozijde. Geen onbegrensd
  meelezen van veranderende voice-structuren en geen grafiekberekeningen in de audio-interrupt.
- Kies bij een envelopepunt expliciet de recentst gestarte stem en volg die door de release.
  Een springende keuze tussen de stemmen zou onduidelijke feedback geven.
- Geen FFT, audio-oscilloscoop, extra effectbuffers of tweede synthlaag in deze UX-aanpassing.

## 9. Controle voordat firmware naar de FM-1 gaat

1. Simulatorbeelden voor alle subpagina's, lege cellen, lange patchnamen, minimale/maximale waarden,
   opgeslagen/gewijzigde patches en meldingen. Ook beoordelen op werkelijke schermgrootte.
2. Bestaande controles: `make test`, `make test-app` en `make test-web`; simulator-scenario's
   `app/tests/basic.txt` en `app/tests/performance.txt`. Gerichte aanvullingen voor nieuwe UX-toestand.
3. Bij eenzelfde geladen patch dezelfde parameter- en MIDI-events controleren. De presentatie mag
   geen andere klankinstellingen veroorzaken.
4. Emulator: toetsenbord en aanwijzer, encoderfocus, screenreader, ARP/Hold/Drone en lange patchnamen.
5. Fysieke FM-1: reactie op snelle encoderbewegingen, leesbaarheid, geen flikkeren, piekbelasting
   en stemverlaging vergelijken met dezelfde patches en akkoorden vóór de UX-wijziging.
6. Doel voor zichtbare encoderfeedback: binnen ongeveer 50 ms. Als metingen meer kosten laten zien,
   eerst animatie of grafiekdetail terugschakelen. De huidige audiocapaciteit is het uitgangspunt.

## Aanbevolen eerste opdracht

Voer **stap A** uit: maak de nieuwe indeling echt op 240 × 240 in de hostsimulator voor HOME,
FILTER en AMP ENV. Gebruik de huidige vier parameterkoppelingen, een actieve kolom en een
eenvoudige statische grafiek. Lever drie schermbeelden plus varianten voor EDIT, SAVE en Hold/Drone.
Daarmee worden leesbaarheid en bediening concreet voordat de volledige firmware wordt omgezet.
