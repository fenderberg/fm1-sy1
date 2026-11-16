# Nord UX — pixelproef A

Dit is het archief van de eerste ontwerpstap. De UI is inmiddels volledig doorgevoerd in de normale builds.
Bekijk de [actuele schermgalerij](../nord-redesign/index.html). De onderstaande beelden en validatie beschrijven
de oorspronkelijke hostproef; `make preview-nord` genereert nu de actuele volledige galerij.

[Open de schermgalerij](index.html) voor alle zestien beelden op hun oorspronkelijke resolutie of 2× vergroot.
Dit zijn PNG's uit de echte hostsimulator, met de bestaande firmwarefonts; geen gegenereerde ontwerpbeelden.

De proef omvat HOME rij 1, FILTER rij 1 en AMP ENV rij 1. Andere pagina's/rijen vallen terug op het bestaande
scherm. De nieuwe code is alleen ingeschakeld in de aparte hostbuild met `SY1_NORD_PIXEL_PREVIEW`.
Een compileercontrole weigert die vlag voor de hardware en browseremulator. Normale builds houden de huidige UI.

## Beoordelen

| Scherm | Waarop letten |
| --- | --- |
| [HOME](01-home.png) | Patchnaam, vier vaste bedieningsvakken en filterinstelling |
| [Filter / EDIT](02-filter-edit.png) | Amber focus op de tweede encoder en zichtbare EDIT-status |
| [AMP ENV / attack](06-envelope-attack.png) | Grotere waarden, ADSR-vorm en actieve eerste kolom |
| [Opgeslagen](04-filter-saved.png) | SAVED 001 zonder de patchnaam te vervangen |
| [Hold](08-home-hold.png) / [Drone](09-home-drone.png) | Vaste speelstatus, met onderscheid tussen arp en drone |
| [Lange naam](13-long-name.png) | Een via SysEx geïmporteerde naam van 23 brede letters wordt op fontbreedte afgekapt |

Verder bevat de galerij resonance, release, een pagina zonder encoderfocus, de minimale en maximale cutoff,
extreme ADSR-instellingen en de HP12-, BP12- en LPDL-filterstanden.

De indeling gebruikt precies 240 pixels hoogte: patchkop 40, modulestrook 16, grafiek 80, encoders 80, status 24.
Elke zone past afzonderlijk binnen de bestaande tekenstrook. De vier fysieke parameterkoppelingen zijn behouden.
Ook een draai tegen de parametergrens geeft focusfeedback. Een pagina-/rijwissel maakt de focus leeg.

De grafieken zijn schematische instellingenbeelden. Er is nog geen fase-/voice-animatie, omzetting naar Hz/ms,
opgeslagen referentiemarkering of nieuwe dirty-logica; dat hoort bij de latere stappen. Deze proef tekent de vijf
zones opnieuw bij een redraw. Optimalisatie van afzonderlijke zones en meting op echte hardware volgen later.

## Oorspronkelijke werkwijze

Benodigd: de bestaande hostbuild-vereisten (C-compiler, Python met Pillow) en Node voor de SysEx-testfixture.
De proef bouwde een afzonderlijke `app/build/nord-preview/sy1_nord_host`, draaide `basic.txt` en
`performance.txt` met beide versies en vergeleek de WAV van het basisscenario byte voor byte.

## Validatie

- De bestaande basis- en performance-scenario's slagen in de normale en de proefsimulator.
- De audio van het basisscenario is byte voor byte gelijk tussen beide builds.
- Het UX-scenario heeft 42 controles voor pagina's, encoderfocus, bestaande parameters, opslaan, Hold/Drone,
  grenswaarden en gesproken feedback. De aanvullende importproef controleert patchselectie en drie filtertypen.
- De 240 × 240-beelden zijn visueel beoordeeld; de galerij biedt weergave op 1× en 2×.
- Dit geeft geen uitspraak over fysieke schermgrootte, tekentijd of audiobelasting op een echte FM-1.

Actuele bron: [tekenhelpers](../../../app/ui/nord.h), [UX-scenario](../../../app/tests/nord-preview.txt),
[bouwscript](../../../tools/ux_review.py) en [het plan](../../UX-NORD-PLAN.md).
