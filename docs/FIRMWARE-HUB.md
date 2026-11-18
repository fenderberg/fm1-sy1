# SY1 on The Firmware Hub

The current submission is for **SY1 by Fenderberg**, repository
<https://github.com/fenderberg/fm1-sy1>, code license MIT.
Release **0.3.1** uses **FM-1_51000301**. The app-owned `app/fm1.toml` defines
project prefix **51**; the release version supplies the last six digits. Development
builds use FM-1_5100. No changes to the fm1-base submodule are needed.

The supplied handover referred to 0.2.1 and proposed 0.2.2. Since 0.3.0 has already
shipped with 500 factory presets, 0.3.1 is the next release. Prefix 47 overlaps
FRASIL's range; the hub catalogue showed no conflict for FM-1_51000301 on 2026-11-16.
Historical package identities in older release records are retained as history.

## Hub card and package

- [`fmhub.json`](../fmhub.json) is in the default branch root. Its description
  distinguishes **500 included presets** from **3,174 online presets in 27 banks**.
- The card links the browser synth, manager and manual. Its image, `docs/img/hub-card.png`, is the NORD Play
  screen enlarged three times pixel for pixel (720 × 720, about 10 KB): the 240 × 240 GREEN LCD screenshot it
  used before was hard to read at the card's size.
- `sy1-0.3.1.fwsc` includes the Felucca update loader. The package identity is decoded
  from its marker bytes by `base/tools/fm1_install.py`; it is not a plain-text file header.
- fm1-base's `device/fm1_flash_ok.h` accepts both Puya **0x856014** and UC **0xB36014**.
- Release assets include the firmware, SHA256SUMS.txt, firmware metadata, hub card,
  Factory 500 ZIP, manifest and complete preset credits.

Run `python3 host/hub_test.py app/build/sy1-0.3.1.fwsc` after building. The publish
script runs this check before deploying the website.

## Preset distribution review

The original readmes of all 27 hosted banks were checked again on 2026-11-16.
The `sounds` declaration names the source authors and the actual repository paths
[`app/site/library/CREDITS.md`](../app/site/library/CREDITS.md) and
[`app/presets/CREDITS.txt`](../app/presets/CREDITS.txt). These sounds retain their
authors' own conditions and are separate from the MIT code license.

| Banks | Maker | Permission in the original notes |
| --- | --- | --- |
| AZur Studio 1, 2, 3 | AZur Studio | Free bank; CC0 declared in each readme |
| Synthhunter | Christoph Mueller | Redistribution and use in music permitted |
| Lunatique | Rob “Lunatique” Chang | Redistribution with the complete original readme |
| Ann 1 | Nolwenn | Free use; no sale or inclusion in a paid library |
| Echopark | Reinhard Reschner / Echopark Studio | Free copying and use; no sale or paid library without permission |
| SOS NordLeadII A-00 through A-19 (20 banks) | SOS | Distribution and use permitted; never sell |

Original bank ZIPs are unchanged. The Factory 500 download preserves its 500 original
patch files and every original source-bank text file byte for byte. Full notes are
also embedded in the firmware. This free distribution retains the restrictions on
sale and paid libraries. Banks whose notes do not permit redistribution are excluded.
`make test-web` verifies bank hashes and readme preservation.

## Physical test status

On 2026-11-16 the exact release package was tested on one physical FM-1:

| From | Installed | Full identity confirmed |
| --- | --- | --- |
| Official M-VAVE V15 | SY1 0.3.1 | FM-1_51000301 |
| SY1 0.3.1 | Dinghy 0.5 | FM-1_995 |
| Dinghy 0.5 | SY1 0.3.1 | FM-1_51000301 |
| SY1 0.3.1 | Official M-VAVE V15 | FM-1_015 |

The SY1 package SHA-256 in both install tests was
`5abfe11f0217e19c93a386f711053e1475060154318b746f593d2f9f638b64f3`.
F001 and F500 were reached on the physical device after both SY1 installs,
with their patchmaker names in the screen-reader output. These checks establish
installation, return and basic browsing, not a listening comparison with Synth1.

Before switching, the complete 262,144-byte user bank and saved settings were
read twice and compared. The private backup contains 513 used slots, the original
selection (user patch 379, internal ID 386), and all 16 bytes of settings.
A local diagnostic build was used to export and restore these bytes; it is not
part of the public release. The restored bank and all saved settings were read back and matched the backup
byte for byte. After reinstalling the public 0.3.1 package, the device reported
513 user slots in use out of 1,024, selected user patch 379 Polarizer Lead, and
no recorded crash. The private data and diagnostic package are kept locally.

Earlier 0.2.1 candidates were installed on one physical FM-1, retaining 513 used
slots and the selected patch. The 0.3.1 install/return tests above were performed separately. SY1 does not read other firmwares' projects or samples; switching can
overwrite their data.

## Existing submission

The existing submission was rechecked on 2026-11-16 against **0.3.1 / FM-1_51000301**.
The seven automatic checks passed: repository source, license, card metadata,
release package, own identity, Felucca loader and both flash chips. The status is
**waiting for Sven**. The community banks are flagged for manual review of their
original author terms; this is not yet approval or a live listing.

The maker's status link is in the hub confirmation email. Its private key is not
committed here. After publishing the new source and release, use **Check again**
on that existing submission. Do not create a duplicate submission or claim hardware
tests that have not happened. Passing automatic checks still requires Sven's review.

References: [hub requirements](https://thefirmwarehub.com/submit/),
[card checker](https://thefirmwarehub.com/kb/#fmhub),
[identity checker](https://thefirmwarehub.com/submit/#check).
