# CyberFoil
![GitHub all releases downloads](https://img.shields.io/github/downloads/luketanti/CyberFoil/total)
![GitHub latest release downloads](https://img.shields.io/github/downloads/luketanti/CyberFoil/latest/total)
[![Discord](https://img.shields.io/badge/Discord-Join%20Server-5865F2?logo=discord&logoColor=white)](https://discord.gg/gGy7hWxJeP)


## Features
- Simple homebrew file installer
- Works best with AeroFoil
- Save Data Backup manager: browse console/server saves, upload with notes, download specific own save data versions, and delete save data files from server

## Save Sync (Only works with compatible AeroFoil)
- Available when the account has backup access.
- Shows local console saves and remote server save data backups in a dedicated `Saves` section.
- Upload supports version notes.
- Download/delete of remote save data backups supports per-version selection with an in-page selector layout.

## Offline Metadata/Icons
- CyberFoil can use local title metadata and local icons from `sdmc:/switch/CyberFoil/offline_db/`.

## Configuration
- `sdmc:/switch/CyberFoil/config.json` supports optional `httpUserAgentMode` and `httpUserAgent`.
- `httpUserAgentMode` can be `default`, `chrome`, `safari`, `firefox`, `tinfoil`, or `custom`.
- `default` uses `cyberfoil`.
- `tinfoil` sends an empty User-Agent value.
- Preset/custom modes are applied across network requests (downloads, shop, and save sync).
- `httpUserAgent` is used when `httpUserAgentMode` is `custom`.

## Release Package Layout
- Build output: `cyberfoil.nro`
- SD layout in release zip: `switch/CyberFoil/cyberfoil.nro`

## Note
- Uses [XorTroll's Plutonium](https://github.com/XorTroll/Plutonium) for a pretty graphical interface



