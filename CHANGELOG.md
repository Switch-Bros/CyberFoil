# Changelog

## 1.4.1
- Add one-press **Backup save data** flow to upload all save data to server.
- Create and use a backup note before bulk save uploads.
- Improve backup UX with shop target display, status/loading states, and better user selection UI.
- Support nickname-only user selection in backup dialogs.
- Add optional filter in Shop settings to show only base titles in the **All** section.
- Add low-latency navigation click sound (`click.wav`) across menu/list/grid navigation.
- Improve held up/down behavior in Shop list so click sound plays on each repeated item move.
- Wrap main menu grid text to prevent localization overflow on long labels.
- Localize backup menu/info strings and shop base-only setting across all locale files.

## 1.3.11
- Improve MTP install reliability across repeated sessions.
- Play success sound when MTP install completes.
- Show MTP transfer spinner/status with MiB/GiB progress.
- Keep MTP completion status inline on the install screen.
- Add optional Album drive exposure toggle for MTP mode.
- Set Album drive exposure default to disabled.
- Improve Album drive compatibility with Windows Explorer.
- Improve settings tab/list focus navigation and highlighting.
- Localize main menu info popup and hint text.
- Improve eShop grid/list behavior and refresh state handling.
- Normalize shop search with diacritic-insensitive matching.
- Refresh shop grid immediately after applying a search filter.
- Show active shop search term in the top info bar.

## 1.3.9
- Fix MTP install re-entry reliability.
- Play success sound when MTP install completes.
- Add tap-to-select on main menu and settings.
- Update eShop grid selection visuals (green highlight + selected overlay).

## 1.3.8
- Add options to hide installed titles and the Installed section in eShop.
- Respect base install status for updates/DLC visibility.
