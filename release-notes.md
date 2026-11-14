**Beta** – community continuation of [launchiine by Maschell](https://github.com/wiiu-env/launchiine), a Wii U Menu replacement for Aroma.
Nothing is written to the NAND: deleting `men.rpx` from the SD card brings back the original Wii U Menu.

### Installation
Extract the `launchiine-*.zip` of this release to the root of the SD card (or copy `men.rpx` to `sd:/wiiu/environments/aroma/men.rpx`) and start the console as usual.

### New in v0.3
**Wii U Menu look** (all graphics are new drawings, no Nintendo files are included)
- Light theme: bright background with soft bubbles, glass tiles behind the icons, cyan selection with corner brackets and a *Start* band.
- Name bubble over the selected icon (below it on the top row).
- Paging like the original: the neighbouring pages peek in at the screen edges, big white arrows, page dots at the top, eased scrolling.
- Folders: grid icon with the folder name, the folder color tints the background inside, bottom bar with the name, a wrench (folder settings) and a back tab.
- Dialogs in the Wii U Menu style: white panel, dark header, glossy buttons, *Back* button.
- The TV is rendered at the console's resolution (1080p when set) instead of a stretched 720p picture.

**Features**
- **User folders:** Y on a title → *New folder...*, move titles between folders, rename and delete folders; empty folders disappear.
- **Folder colors:** *Change color* for every folder, the built-in System and Homebrew folders too.
- **Accounts (experimental):** the current user's Mii in the top right corner, X or a tap opens the user picker; asks at boot when there is no default account. Switching between accounts has only been tested with a single account – please report how it works for you.
- **Game update notice:** launchiine checks Nintendo's version list (like the Wii U Menu does) and marks titles with a newer update with an orange badge and *Update available*. It only tells; updating still happens through the original menu / System Settings.
- **Icon cache:** icons are cached on the SD card, the menu shows all icons in a fraction of a second after the first start.
- Touch: tap an icon to select it, tap the selected icon to open it.

### Known limitations
- No splash screen sound, no menu music from the original menu.
- No Wara Wara Plaza; the TV mirrors the grid.
- No shortcut to the original Wii U Menu yet (delete `men.rpx` instead).
- Updates are only announced, not downloaded.
- Quick Start Menu and external storage handling rely on Aroma's AutobootModule.

Please report problems with the network log attached – see the README for a one-line log receiver.

*Developed with AI assistance (Claude by Anthropic) and tested on a real console.*
