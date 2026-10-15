# Privacy policy

This program will not transfer any information to other networked systems unless specifically requested by the
user or the person installing or operating it.

## What LYNX does and does not do

- **No network connections.** LYNX contains no update checker, telemetry, crash reporting, analytics, advertising,
  online account or licence check, and it never downloads anything. The source code is public, so this can be
  verified: the app does not use any network API (JUCE's `URL`/`WebInputStream`, sockets, WinINet or WinHTTP), and
  JUCE's web browser component and its curl support are disabled at build time (`JUCE_WEB_BROWSER=0`,
  `JUCE_USE_CURL=0`).
- **No personal data is collected.** Nothing leaves your computer.
- **Local files only.** LYNX writes only to files you choose (projects, exports) and to its own settings folder
  `%APPDATA%\LYNX` (language, plugin list, autosaves and similar preferences). Delete that folder to remove them.
  It also registers the `.lynx` file type for the current user (`HKEY_CURRENT_USER\Software\Classes`) so project
  files open in LYNX.
- **Plugin sandbox.** When a plugin is run in its own process, LYNX talks to that process through a local
  inter-process channel on the same computer; nothing is sent over a network.
- **Crash logs** are written to a local file only, never uploaded.

## Third-party plugins

VST3 and CLAP plugins that **you** load are separate programs from other vendors. They run inside LYNX (or its
sandbox process) with your permissions and may do things LYNX does not, including network access. Their behaviour is
governed by their own privacy policies.

## Website

The website [lynx-share.com](https://lynx-share.com) is a static page. It asks the GitHub API for the latest release
so that its download buttons point to it, and remembers your language choice in your browser's local storage. It sets
no cookies and runs no analytics.

## Contact

Questions: open an issue at <https://github.com/Ka1-S/LYNX/issues>.
