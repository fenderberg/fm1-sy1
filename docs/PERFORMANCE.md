# SY1 als performance-synth

Doel: snel een passende klank vinden, met twee handen spelen en tijdens het spelen hoorbaar kunnen vormen.
Geen sequencer, opname, patroonpagina's of songmodus. De speelwijze is geïnspireerd op
[NCC](https://github.com/fenderberg/fm1-nc-companion/blob/main/docs/MANUAL.md); de implementatie gebruikt SY1's eigen engine.

## Nu aanwezig

| Bediening | Functie |
| --- | --- |
| ARP kort | Arp aan/uit, op elke pagina |
| ARP 0,5 seconde | Arp-instellingen; opnieuw vasthouden voor de volgende rij |
| SEQ met arp | HOLD: het akkoord blijft lopen; een nieuw gebaar na volledig loslaten vervangt het |
| SEQ zonder arp | Drone: STACK of REPLACE, instelbaar op ARP rij 2 / GLO rij 4 |
| ALGORITHM | Categorie; lege categorieën overslaan |
| PRESETS | Klanken binnen de categorie |
| GLO rij 4, KNOB3 + SAVE | Categorie van een patch corrigeren en bewaren |
| REC | Geladen patch herstellen, hold/drone loslaten |
| PLAY / MIDI Stop | Noten en hold/drone stoppen |

STACK voegt noten toe; nogmaals spelen zet een drone-noot uit. REPLACE houdt het laatste akkoord vast:
noten toevoegen zolang er nog een toets ingedrukt is, daarna begint het volgende gebaar een nieuw akkoord.
De release van de oude klank kan doorlopen onder de attack van de nieuwe, binnen de bestaande voice-limiet.
Arp aan/uit wisselt hetzelfde vastgehouden akkoord tussen hold en drone. Het sustainpedaal blijft onafhankelijk.

Categorieën: ALL, BASS, PAD, LEAD, KEYS, PLUCK, ARP, FX, OTHER en USER. Naamherkenning helpt bestaande
banken op weg; handmatig indelen blijft nodig bij namen als "Preset 037". De categorie staat buiten de 99
Synth1-parameters. Oude patches en bestaande importtools blijven werken.

De ARP- en SEQ-leds tonen hun speelstand, ook op de instellingenpagina. Het scherm toont de actieve
categorie en ARP/HLD/DRN. Een patchwissel, panic of herstart laat geen hold/drone achter. Automatisch
instellingen bewaren wacht tot er geen actieve stemmen zijn. Als een volle eventqueue een note-off zou
verliezen, stopt de engine de noten. Zonder MIDI-clock keert het interne tempo na twee seconden terug.

## Voorstel voor de volgende stappen

Dit zijn voorstellen; ze zijn nog niet geïmplementeerd.

1. **Een vaste performance-pagina met vier muzikale macro's.** HOME krijgt naast de bestaande filterrij
   bijvoorbeeld BRIGHT, SHAPE, MOTION en SPACE. Eén encoder mag meerdere passende parameters tegelijk
   bewegen, met de geladen patch als uitgangspunt. Maak de toewijzing per patch instelbaar en zichtbaar.
   Eerst parameterovergangen glad maken en op de FM-1 beluisteren; filter en MASTER hebben al smoothing,
   maar niet iedere huidige parameter verdraagt snelle veranderingen even goed.
2. **A/B-snapshots en terugveren.** Twee standen van dezelfde patch vastleggen, vergelijken en met één
   encoder ertussen bewegen. Continue parameters interpoleren; golfvormen, filtertype en voice-modus pas
   op een gekozen omslagpunt wisselen. Een tijdelijke ingreep loslaten brengt de vorige stand terug.
   Begin binnen één patch: de huidige patchloader stopt stemmen en wist effectbuffers, dus naadloos
   tussen willekeurige presets wisselen vraagt eerst ander voice- en effectbeheer.
3. **Favorieten en een eigen speelvolgorde.** Een kleine set klanken klaarzetten voor een optreden,
   met directe selectie en een herkenbaar favorietenfilter. Zo hoef je live niet door een grote bank.
   Handmatig gekozen volgorde; geen automatische patchwissels.
4. **Tijdelijke effect-throws.** Een knop vasthouden voor extra delay, drive of modulatie; loslaten herstelt
   de vorige waarden terwijl bestaande delaystaarten uitklinken. Eerst toevoegen zonder extra DSP-buffers.
   Knoppencombinaties moeten de normale toetsen, pagina's en arp/hold bereikbaar laten.
5. **Arp echt op de externe clock-grid.** Nu volgt hij het gemeten BPM. Volgende stap is fase-uitlijning
   op MIDI-clock, voorspelbare Start/Continue/Stop en vloeiende overname als clock wegvalt. Daarna swing,
   gespeelde nootvolgorde en velocity per noot. Geen programmeerbare stappen.
6. **Taptempo en speelhulpen.** Taptempo, een optionele schaal/toonsoort en akkoordgrepen op de lokale
   toetsen. Laat externe MIDI standaard ongemoeid en toon altijd welke transformatie aanstaat.

Mijn volgorde: eerst macro's en vloeiende parameterovergangen, daarna snapshots, favorieten en throws.
Voor samenspelen met de Circuit krijgt echte clock-uitlijning voorrang op snapshots.
Een tweede laag/split, nieuwe effecten en een reverb volgen pas na metingen: de FM-1 haalt nu ongeveer
zeven stemmen en gebruikt nog één core. De bestaande klank live betrouwbaar houden gaat voor extra DSP.

## Validatie van deze stap

`make test`, `make test-app` en `make test-web` controleren klankengine, hold/drone, pedaal, paneel/MIDI,
categorieën, opslag, herstart, volle eventqueue en importcompatibiliteit. De simulator draait
`app/tests/basic.txt` en `app/tests/performance.txt`; die laatste maakt hold/drone-schermbeelden.
Firmware en browseremulator worden lokaal gebouwd. Fysieke knoppen, timing onder belasting en de
muzikale overgang van akkoorden moeten nog op een echte FM-1 worden beoordeeld.
