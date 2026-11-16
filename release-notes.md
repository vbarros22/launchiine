**Beta** – community continuation of [launchiine by Maschell](https://github.com/wiiu-env/launchiine), a Wii U Menu replacement for Aroma.
Nothing is written to the NAND: deleting `men.rpx` from the SD card brings back the original Wii U Menu.

### Installation
Extract `launchiine-v0.3.1.zip` to the root of the SD card (or copy `men.rpx` to `sd:/wiiu/environments/aroma/men.rpx` and `launchiine_helper.wps` to `sd:/wiiu/environments/aroma/plugins/`) and start the console as usual.

### New in v0.3.1
- **Switch screens** (X or the new button top right), like the Wii U Menu: icon grid on the TV, game info on the GamePad – for playing with a Pro Controller or Wii Remote. Remembered across restarts, like the ZR choice for the TV.
- The Mii moved to the top left corner in a glass tile, with a badge for accounts linked to an online network. The settings tab is gone (ZL opens the menu); - opens the user picker.
- **TV view:** homebrew, system apps and games without a cover show their (rounded) icon right away; covers load almost instantly (kept in memory for the pages around the selection, uploaded without decoding); no more stutter from the description text; eShop games found by their short GameTDB id; English fills in missing translations; Virtual Console injects are looked up by name.
- ZR (TV: game info / icon grid) actually works now.

### Everything since Maschell's launchiine
**Wii U Menu look** (all graphics are new drawings, no Nintendo files are included)
- Light theme, glass tiles, cyan selection with a *Start* band, name bubble over the selected icon.
- Paging like the original: neighbouring pages peek in, big white arrows, page dots, eased scrolling.
- Folders with a grid icon and their name, colored background inside, name bar, folder settings and back tab.
- Dialogs with a white panel, dark header, glossy buttons and a *Back* button.
- The TV is rendered at the console's resolution (1080p when set).
- **TV view** with cover, publisher, year, genre, players and official description from [GameTDB](https://www.gametdb.com).

**Features**
- Stability: the random freezes and crashes of the original are fixed.
- Gamepad, touch, Pro Controller and Wii Remote controls; alphabetical order; search (+).
- Fixed *System* and *Homebrew* folders plus your own folders (create, move, rename, delete, change color).
- Homebrew from `sd:/wiiu/apps` (with Aroma's homebrew_on_menu plugin).
- **Accounts (experimental):** user picker, asks at boot without a default account. Only tested with a single account – please report how it works for you.
- **Game update notice:** titles with a newer update get an orange badge and *Update available* (only announced, not downloaded).
- Names and icons cached on the SD card: the menu is complete in well under a second.
- Opens on the last started title; names follow the console language.
- **Original Wii U Menu:** ZL → *Original Wii U Menu* starts Nintendo's menu once; the included `launchiine_helper.wps` plugin brings launchiine back for the next start.
- Works with the GamePad's Quick Start Menu (handled by Aroma).

### Known limitations
- No splash screen sound, no menu music from the original menu.
- No Wara Wara Plaza. Some games have no cover or description on GameTDB; not every Virtual Console inject is found yet.
- Updates are only announced, not downloaded.

Tested on a Wii U with the latest system update and the latest Aroma (November 2026), also together with Pretendo Network / Inkay.

Please report problems with the network log attached – see the README for a one-line log receiver.

*Developed with AI assistance (Claude by Anthropic) and tested on a real console.*
