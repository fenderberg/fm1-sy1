# SY1 — Nord-paneel met klankvisualisatie

Drie statische ontwerpvoorstellen voor het vierkante FM-1-scherm (240 × 240), gemaakt op 15 november 2026 met de ingebouwde image_gen-tool. Dit zijn vergrote stijlmockups; de app is niet aangepast. Typografie, onderlinge uitlijning en exacte grafiekverhoudingen moeten bij implementatie op de echte schermresolutie worden vastgelegd.

De richting combineert vier vaste bedieningsvakken met een grote visualisatie van het onderdeel dat je bewerkt. De vakken horen van links naar rechts bij de vier fysieke encoders. Rood geeft het instrument zijn karakter; amber wijst de actieve parameter aan. Een markering of stippellijn geeft de opgeslagen stand weer.

- [Filter](filter.png): cutoff en resonance worden zichtbaar in de filtercurve. Knop 2 is actief.
- [Envelope](envelope.png): ADSR-curve met de attack uitgelicht. Het witte punt verbeeldt de voorgestelde animatie tijdens een noot; deze afbeelding zelf beweegt niet.
- [Oscillator](oscillator.png): pulsgolf met de breedte uitgelicht. Knop 3 is actief.

Voorgestelde feedback: het bediende vak licht op, de visualisatie beweegt mee met de instelling, en de opgeslagen stand blijft herkenbaar. De amber stip bij de patchnaam betekent dat de patch is gewijzigd. De onderste regel toont MIDI-activiteit, actieve stemmen en tempo.

De volledige gebruikte prompts staan in [prompts.json](prompts.json).
