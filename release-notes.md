**Beta** – community continuation of [launchiine by Maschell](https://github.com/wiiu-env/launchiine), a Wii U Menu replacement for Aroma.
Nothing is written to the NAND: deleting `men.rpx` from the SD card brings back the original Wii U Menu.

### Installation
Extract `launchiine-v0.3.1.zip` to the root of the SD card (or copy `men.rpx` to `sd:/wiiu/environments/aroma/men.rpx` and `launchiine_helper.wps` to `sd:/wiiu/environments/aroma/plugins/`) and start the console as usual.
Coming from v0.2.1: just overwrite the files. Your folders and settings in `sd:/wiiu/launchiine/` are kept; the new `launchiine_helper.wps` plugin is only needed for the *Original Wii U Menu* option.

## What's new since v0.2.1
v0.3.1 includes everything from v0.3 – this is the full list of changes since v0.2.1.

### Looks like the Wii U Menu
All graphics are new drawings, no Nintendo files are included.
- **Light theme** like the original: bright background with floating bubbles, glass tiles behind every icon, cyan selection frame with a *Start* band.
- **Name bubble** over the selected icon (below it on the top row) instead of a title bar.
- **Paging like the original:** the neighbouring pages peek in at the screen edges, big white page arrows, page dots and eased scrolling.
- **Folders like the original:** folder icon in the folder color with a 3x3 grid and the folder's name, colored background inside the folder, name bar at the bottom with a settings (wrench) and a back tab.
- **Dialogs like the original:** white panel, dark header, glossy options with selection brackets and a *Back* button; light user picker.
- **Your Mii** in the top left corner in a glass tile, with a badge for accounts linked to an online network (Nintendo Network / Pretendo).
- **Sharper on the TV:** rendered at the console's resolution (1080p when set) with higher resolution tiles; the GamePad layout stays the same.
- Button hints at the bottom of the GamePad: *A Open · B Back · Y Move / New folder · + Search · X Screens · - User · L/R Pages · ZL Menu*.

### TV view with game info (new)
- The TV shows the selected game's **cover, title, publisher, year, genre, players and official description** from [GameTDB](https://www.gametdb.com), in the console language with English filling in missing translations.
- Downloaded once and kept on the SD card (`sd:/wiiu/launchiine/`); covers of the pages around the selection are kept in memory, so they show up almost instantly.
- eShop games are found by their short GameTDB id; Virtual Console injects are looked up by name.
- Homebrew, system apps and games without a cover show their icon instead.
- Smooth: the background and texts are drawn once and reused, no stutter while browsing or changing pages.
- **ZR** switches the TV between game info and the mirrored icon grid.

### Switch screens (new)
- **X** or the new button in the top right corner swaps the screens like the Wii U Menu: the icon grid on the TV and the game info on the GamePad – for playing with a Pro Controller or Wii Remote.
- The choice (and the ZR choice) is remembered across restarts.

### New features
- **User folders:** create your own folders, move titles in and out with Y, rename, delete and **change the folder color**. The fixed *System* and *Homebrew* folders stay.
- **Accounts (experimental):** the current user's Mii on screen, a user picker (- or a tap on the Mii) and the question at boot when the console has no default account. Only tested with a single account – please report how it works for you.
- **Game update notice:** launchiine checks Nintendo's title version list; titles with a newer update get an orange badge and *Update available* (only announced, not downloaded).
- **Original Wii U Menu:** ZL opens a small menu with *System Settings* and *Original Wii U Menu*. The original menu starts once; the included `launchiine_helper.wps` plugin brings launchiine back for the next start.
- **Touch:** tap an icon to select it, tap the selected icon to open it; folder buttons and dialogs work by touch.
- Works with the GamePad's **Quick Start Menu** (handled by Aroma).

### Faster and more stable
- **Icon cache** on the SD card (in addition to the name cache): the menu is complete in well under a second; cached icons are checked against the real files in the background.
- The hidden screen is no longer drawn, and slow frames are written to the network log to track down stutter.
- Fixed the frame timing of the main loop.
- Faster SD card reads (large blocks).

### Removed
- The settings tab (gear) – ZL opens the menu instead, the corner now belongs to the Mii.

## Already in v0.2.1 (since Maschell's WIP)
- Fixed the random freezes and crashes (icon loader threading, GPU commands from a second thread, GUI objects deleted on another thread, file reads overwriting neighbouring memory, hangs on exit).
- GamePad, Pro Controller and Wii Remote controls; alphabetical order by full name, system apps first.
- Fixed *System* and *Homebrew* folders; homebrew from `sd:/wiiu/apps` (`.rpx` / `.wuhb`) via Aroma's homebrew_on_menu plugin.
- Search (+): short and full names, ignores case, accents and punctuation, also finds titles inside folders.
- Names cached on the SD card; opens on the last started title; names in the console language.

### Known limitations
- No splash screen sound, no menu music from the original menu.
- No Wara Wara Plaza.
- Some games have no cover or description on GameTDB; not every Virtual Console inject is found yet (e.g. some GameCube injects).
- Updates are only announced, not downloaded.

Tested on a Wii U with the latest system update and the latest Aroma (November 2026), also together with Pretendo Network / Inkay.

Please report problems with the network log attached – see the README for a one-line log receiver.

*Developed with AI assistance (Claude by Anthropic) and tested on a real console. All credit for launchiine, Aroma, Mocha and libgui goes to Maschell and the wiiu-env contributors.*
