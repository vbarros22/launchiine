## Launchiine (community continuation, v0.2 beta)

A Wii U Menu replacement for [Aroma](https://aroma.foryour.cafe/), based on [launchiine by Maschell](https://github.com/wiiu-env/launchiine).
The original was published as an unfinished work in progress; this fork picks it up and makes it usable day to day.

> **Beta.** It has been tested on one console with ~190 titles (Wii U games, Virtual Console, Wii injects and 30+ homebrew apps).
> Nothing is written to the NAND: deleting `men.rpx` from the SD card brings back the original Wii U Menu.

### What changed compared to the original
- **Stability:** fixed the random freezes and crashes (threading in the icon loader, GPU commands sent from a second thread,
  GUI objects deleted on another thread, file reads overwriting neighbouring memory, a hang on exit).
- **Gamepad controls:** D-pad / left stick to select, A to start, L/R to change pages (the selection follows).
- **Alphabetical order** by full name, system apps first.
- **Folders:** fixed *System* and *Homebrew* folders. Any title can be taken out of its folder (and put back) with Y.
- **Homebrew:** `.rpx` / `.wuhb` apps from `sd:/wiiu/apps` show up and start like in the original menu
  (needs the [homebrew_on_menu](https://github.com/wiiu-env/homebrew_on_menu_plugin) plugin, included in Aroma).
- **Search** (+ button): matches words in the short and the full name, ignores case, accents and punctuation,
  also finds titles inside folders.
- **Faster start:** names are cached on the SD card; the visible page gets its icons first (~1 s), all icons in a few seconds.
- The menu opens on the last started title (inside its folder if needed).
- Names follow the console language, the title bar shows the full name and shrinks the font so it fits.
- ZL opens System Settings.

### Installation
1. Download `men.rpx` from the latest build (Actions tab of this repository, artifact `men-rpx`).
2. Copy it to `sd:/wiiu/environments/aroma/men.rpx`.
3. Start the console as usual; launchiine shows up instead of the Wii U Menu.

To go back to the original menu, delete (or rename) `men.rpx`.

### Controls
| Button | Action |
|---|---|
| D-pad / left stick / touch | Select |
| A (or tap twice) | Start the title / open the folder |
| B | Leave the folder |
| Y | Take the selected title out of its folder / put it back |
| + | Search (empty search shows everything again) |
| L / R | Previous / next page |
| ZL | System Settings |

### Files on the SD card
Everything lives in `sd:/wiiu/launchiine/` and can be deleted at any time:
- `names.txt` – cached title names (rebuilt automatically)
- `lastTitle.txt` – last started title
- `outsideFolders.txt` – titles taken out of their folder

### Known limitations
- No account selection when there is no default account, no account management.
- No update check for games, no download of updates.
- Quick Start Menu and external storage handling rely on Aroma's [AutobootModule](https://github.com/wiiu-env/AutobootModule).
- No Wara Wara Plaza / TV view; the TV mirrors the grid.
- No sound on the splash screen.
- There is no way to open the original Wii U Menu from launchiine yet (delete `men.rpx` instead).
- Leaving System Settings or Wii titles reboots the system and Aroma has to load again; that takes a moment, like with the original menu.

### Reporting problems
Launchiine logs over the network (UDP port 4405, the usual Aroma logging). Please attach the log when reporting a freeze.
A PowerShell one-liner that receives and saves it:
```
$u = New-Object System.Net.Sockets.UdpClient 4405; $ep = New-Object System.Net.IPEndPoint ([System.Net.IPAddress]::Any, 0); while ($true) { $l = [Text.Encoding]::UTF8.GetString($u.Receive([ref]$ep)); $l; Add-Content "$env:USERPROFILE\Downloads\launchiine-log.txt" $l }
```
Builds made with `make VERBOSE=1` additionally log every title, texture and a render heartbeat.

### Building
With Docker (nothing else needed on the host):
```
# Build docker image (only needed once)
docker build . -t launchiine-builder

# make
docker run -it --rm -v ${PWD}:/project launchiine-builder make

# verbose logging build
docker run -it --rm -v ${PWD}:/project launchiine-builder make VERBOSE=1

# make clean
docker run -it --rm -v ${PWD}:/project launchiine-builder make clean
```
Without Docker: install [wut](https://github.com/devkitPro/wut) and [libgui](https://github.com/wiiu-env/libgui), then run `make`.

### Credits
- [Maschell](https://github.com/Maschell) – original launchiine, Aroma, Mocha, libgui and most of the Wii U homebrew environment
- Dimok – the original libgui / GUI code this is built on
- Continuation: vbarros22
