**Beta** – community continuation of [launchiine by Maschell](https://github.com/wiiu-env/launchiine), a Wii U Menu replacement for Aroma.
Nothing is written to the NAND: deleting `men.rpx` from the SD card brings back the original Wii U Menu.

### Installation
Extract `launchiine-v0.2.zip` to the root of the SD card (or copy `men.rpx` to `sd:/wiiu/environments/aroma/men.rpx`) and start the console as usual.

### Changes since the original WIP
- Fixed the random freezes and crashes (icon loader threading, GPU commands from a second thread, GUI objects deleted on another thread, file reads overwriting neighbouring memory, hangs on exit).
- Gamepad controls: D-pad / stick, A to start, L/R pages, B / Y for folders, on-screen controls hint.
- Alphabetical order by full name, system apps first.
- Fixed *System* and *Homebrew* folders; any title can be taken out of its folder (and put back) with Y.
- Homebrew from `sd:/wiiu/apps` (`.rpx` / `.wuhb`) shows up and starts, via Aroma's homebrew_on_menu plugin.
- Working search (+): short and full names, case/accent/punctuation insensitive, also inside folders.
- Faster start: names cached on the SD card, the visible page loads first.
- Opens on the last started title; names in the console language; full name in the title bar.
- ZL / settings tab opens System Settings.

### Known limitations
No account selection or management, no game update check, Quick Start Menu / external storage handled by Aroma's AutobootModule, no Wara Wara Plaza / TV view, no splash sound, no shortcut to the original Wii U Menu yet.

Please report problems with the network log attached – see the README for a one-line log receiver.
