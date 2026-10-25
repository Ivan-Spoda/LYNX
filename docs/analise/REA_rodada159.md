# Round 159 (report159): opt-in crash / freeze reports (A) and a prefilled GitHub bug report (C)

2026-10-25. The user chose "A + C" for crash reporting: an opt-in automatic upload of crash / freeze reports to our
own server (lynx-share.com, Hostinger shared hosting with PHP) and a "Report on GitHub" button that opens the bug
form prefilled. It builds on round 157a (crash files with module+offset frames, hang watchdog files, the session
breadcrumb and crash-history.log, `--symbolize`, the LYNX-symbols PDB artifact, the "run X in a separate process?"
start-up offer).

Evidence markers: [CODE] in the source (file named); [TEST] core test (tests/CrashReport159Tests.inc); [RUN] done
with the Windows build / the live server; [SRV] checked on lynx-share.com.

## 1. What the user sees

- **After a crash or freeze**, at the next start, one dialog "LYNX closed unexpectedly" [CODE src/Main.cpp
  `showUncleanNotice`, src/ui/CrashReporter.cpp `ReportPanel`]:
  "LYNX closed unexpectedly last time. Send the report to help fix it?" followed by the previous notice (last step,
  backups, and what LYNX did about it: GPU drawing off after a graphics-driver crash, or the plugin whose editor was
  opening). Controls:
  - *Run X in a separate process from now on* (ticked) — only when a plugin's editor was opening (replaces round
    157a's separate Ok/Cancel box when the dialog asks);
  - *Include file names (projects, samples); otherwise only their extension is sent* (off);
  - *Always send without asking* (off; ticking it and Send sets the consent to "Always send");
  - **Send** / **Show what will be sent** (expands a read-only box with the exact JSON; it follows the file-names
    tick) / **Don't send**;
  - **Report on GitHub instead** (opens the bug form prefilled and the folder with the crash file) and **Open the
    folder with the report** (selects the crash / hang file in Explorer).
  After sending, the hint bar says "Crash report sent, thank you (id …)" or that it will be tried again.
- **Help › Report a problem…**: the same dialog for a manual report, with "What happened?", "E-mail (optional, only if
  you want an answer)" and "Attach the newest crash / freeze files and this session's steps" (ticked; files of the
  last 7 days).
- **Options › General settings › Crash reports**: *Ask after a crash or freeze* (default) / *Always send* / *Never
  send*, with a note on what a report holds. "Never" keeps the old notices and sends nothing; "Always" sends unasked
  and adds "A crash report is being sent…" to the notice.
- pt-BR for every new string (resources/i18n/pt-BR.txt).

## 2. What is sent

`crashreport::buildJson` (src/core/CrashReport.cpp, no JUCE; the dialog shows exactly this text), schema 1:

```
schema, id (random UUID v4), kind (crash | hang | unclean | manual), test, created,
app {version, build ("release windows-x64"), commit (git, from the build), language},
system {os, osVersion (e.g. "24H2 (build 26100.4061)"), cpu, cores, memoryMb, gpus [{name, driver}], screens [{width, height, scale}]},
audio {driver, device, sampleRate, bufferSize}   (the device of that session, recorded in its session file)
settings {gpuRendering, pluginSandbox, pluginSubstitutes},
crash {file, text}, hang {file, text},          (the autosave\crash / hang file of that session)
steps [last 60 session steps], plugins [{name, vendor, format, version}] (third-party plugins loaded in that session),
user {text, email} (manual reports only), notes [what the size cap shortened]
```

- GPU names + driver versions: Windows reads the display adapter class key
  (`HKLM\SYSTEM\CurrentControlSet\Control\Class\{4d36e968-…}\00NN` DriverDesc / DriverVersion — readable by any
  user, no extra API); Linux the DRM cards' PCI ids and driver, plus the NVIDIA driver version file.
- The session file now keeps the last **60** steps (was 10), an `info.audio=` line (updated when the device changes)
  and one `plugin=name|vendor|format|version[|separate process]` line per third-party plugin loaded [CODE
  src/platform/CrashGuard.cpp `setSessionInfo` / `notePlugin`, src/engine/PluginHost.cpp].
- **Anonymiser** (`crashreport::Anonymiser`) on every text: the home folder and any `\Users\x`, `/Users/x`,
  `/home/x` segment → `<user>`; the logon / full name words anywhere → `<user>`; the computer name → `<machine>`;
  UNC servers → `\\<host>`; "OneDrive - Company" → `OneDrive - <org>`; project / audio / MIDI / archive file names →
  `<file>.<ext>` and their unknown folders → `<dir>` (common folders such as Desktop, Documents, Music, OneDrive,
  Área de Trabalho, Documentos stay) unless "include file names" is ticked; invalid UTF-8 repaired. Plugin modules
  (.dll / .vst3 / .clap) and crash frames stay readable. Never sent: audio, projects, the machine name, the address.
- **Size cap**: 512 KB of JSON (the server takes 4 MB raw / 1 MB gzip); larger crash / hang texts keep head and tail
  ("[... cut ...]"), then steps / plugins / user text are shortened, listed in `notes`. Sent gzip-compressed.

## 3. Sending

- `crashreporter::send` writes `%APPDATA%\LYNX\crash-reports\pending\<id>.json.gz` and wakes one background thread
  (never the message thread) that POSTs it to `https://lynx-share.com/api/crash.php` (`Content-Encoding: gzip`, 20 s
  timeout; Windows / macOS: `juce::URL`; Linux: the `curl` program, since LYNX is built with `JUCE_USE_CURL=0`).
  Success → removed, id logged in `crash-reports\sent.log`. Offline / timeout / 429 / 5xx → kept with a `.tries`
  mark and tried **once** more at the next start, then dropped; any other 4xx → dropped. Shutdown gives an upload in
  flight up to 3 s. Consent "Never" also stops pending retries.
- `LYNX.exe --send-test-report out.txt [--dry]`: a report marked `"test": true` from the newest crash / hang file;
  writes the JSON next to out.txt and the result (`LYNX_CRASH_ENDPOINT` overrides the URL).

## 4. GitHub (C)

`crashreport::githubIssueUrl`: `https://github.com/Ka1-S/LYNX/issues/new?template=bug_report.yml&title=…&version=…
&os=…&os-details=…&audio=…&kind=…&steps=…&plugins=…&logs=…` (field ids of .github/ISSUE_TEMPLATE/bug_report.yml,
branch feature/issuetpl). `os` / `kind` use the form's dropdown labels ("Windows 11", "LYNX closed (crash)", "LYNX
froze (not responding)"). RFC 3986 percent-encoding; at most 8000 bytes: the longest of logs / steps / plugins is cut
with "\n…" (never inside an escape or a UTF-8 character). The logs field asks to attach the crash file and quotes its
first 40 lines (anonymised, never file names). Opened with `Process::openDocument` (no re-encoding by juce::URL).

## 5. Server (website/api, deployed with scripts/deploy-site.sh)

- `crash.php`: POST only; body ≤ 4 MB, gzip ≤ 1 MB, inflated incrementally with a 4 MB stop (a 20 MB zip bomb is
  refused with 413); schema 1 with a whitelist of top-level fields, UUID v4 id, kind, version / build / commit
  patterns, typed sections, ≤ 500 steps / plugins. Rate limit: 30 reports per address per day and 3000 per day in
  all, counted under `HMAC-SHA256(day | address, secret salt)` in `rate/<day>.json` (deleted the next day); the
  address itself is never written. A report is re-encoded with a `received` time and stored as
  `<storage>/<YYYY-MM>/<id>.json.gz` (0600) plus a line in `index.jsonl`; the same id twice is stored once. Errors
  are short JSON (`{"ok":false,"error":"…"}`), `display_errors` off, exceptions logged server-side only.
- Storage: the first writable of `~/lynx-crash-reports` (the account's home), the domain folder, or (fallback)
  `public_html/api/_storage` denied by .htaccess. [SRV] it is **/home/u258086853/lynx-crash-reports**, outside
  public_html (`reports.php?action=where` → `"public": false`).
- `reports.php` (admin): `Authorization: Bearer <key>` (or `X-Admin-Key`), compared with `hash_equals` against the
  key's SHA-256 in the file; `list`, `get` (the gzip), `delete` (POST), `where`. The 256-bit key is only in
  `~/.config/lynx/crash-admin-key` (chmod 600).
- `api/.htaccess`: no listing, `_*` files denied, Authorization passed to PHP.
- Coordinator tool (outside git): `~/.claude/jobs/f8789b49/tmp/coord/crash-fetch.sh` downloads new reports to
  `/mnt/d/lynx-work/crash-reports/<YYYY-MM>/`, fetches that release's LYNX.exe (release zip) + LYNX.pdb (CI
  "LYNX-symbols" artifact) with gh into `symbols/<version>/`, names the frames with `--symbolize`
  (`<id>.symbolized.txt`) and prints a summary by version, kind, faulting module and top LYNX frame, plus the newest
  20; `--show <id>`, `--delete <id>`, `--delete-tests`, `--tests`.

## 6. Results

- [TEST] tests/CrashReport159Tests.inc (8 tests: JSON schema, anonymiser, size cap, GitHub URL length / encoding /
  dropdown labels) pass in build-local with the FL Data dir (see the landing log).
- [RUN] 2026-10-25, Windows build of feature/report159 merged with origin/main (fa8f2601):
  `LYNX.exe --send-test-report test1.txt` → `result=OK`, 1307 bytes, id 74f1cc87-…; the JSON had kind "crash" (the
  newest autosave\crash file), Windows 11 25H2 build 26200, Ryzen 5 3600, RTX 3060 + driver 32.0.16.1692, both screens;
  no user name, machine name or e-mail anywhere in it (grep). Audio was empty, as expected for a CLI run that opened no
  device.
- [SRV] `crash-fetch.sh --tests --no-symbols` listed it (version 0.1.15, kind crash, GPU, last step);
  `--delete-tests` deleted it on the server; a second list showed 0 reports. Local test copies removed from
  /mnt/d/lynx-work/crash-reports.
- [SRV] negative checks: GET → 405; POST `{"schema":1}` → `{"ok":false,"error":"bad id"}`; POST garbage with
  `Content-Encoding: gzip` → `{"ok":false,"error":"not JSON"}`.
- The .github/ISSUE_TEMPLATE/bug_report.yml now on main (feature/issuetpl) has exactly the ids and dropdown labels
  `githubIssueUrl` uses (version, os, os-details, audio, kind, steps, plugins, logs; "Windows 11", "LYNX closed
  (crash)", "LYNX froze (not responding)").
- Last-minute fix: Settings › Crash reports note box 32 → 46 px (its two lines were clipped).

## Where I stopped

Done and landed (after the BSOD resume): server deployed, client + dialog + settings + GitHub URL, end-to-end test
with the live server, test reports deleted. Not done / for later:
- website/index.html has a "Crash reports" privacy paragraph (EN + pt-BR) that is NOT deployed yet: deploy with
  scripts/deploy-site.sh together with the 0.1.16 release (it describes a 0.1.16 feature).
- The dialogs were not re-screenshotted after the resume (only the CLI path was re-run); the Settings note height fix
  is a layout-only change.
- Server: a bad gzip body answers "not JSON" rather than "bad gzip" (harmless).
