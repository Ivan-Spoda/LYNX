#!/usr/bin/env bash
# Linux packages of a built LYNX binary: an AppImage (one file, runs on most distributions) and a .tar.gz
# (folder with install.sh for a per-user install). Both carry the .desktop entry, the icon and the .lynx file type.
#
# usage: scripts/package-linux.sh <LYNX binary> <label> <output folder> [Demo.lynx]
# Variables: APPIMAGETOOL (path to appimagetool; downloaded into ~/opt when missing), ARCH (default x86_64)
set -euo pipefail
cd "$(dirname "$0")/.."

BIN="$1"; LABEL="$2"; OUTDIR="$3"; DEMO="${4:-}"
ARCH="${ARCH:-x86_64}"
NAME="LYNX-$LABEL-linux-$ARCH"
APPIMAGETOOL_VERSION=1.9.0
WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT
mkdir -p "$OUTDIR"

# --- folder for the .tar.gz --------------------------------------------------------------------------------
PKG="$WORK/$NAME"
mkdir -p "$PKG"
install -m 755 "$BIN" "$PKG/LYNX"
strip --strip-unneeded "$PKG/LYNX" 2> /dev/null || true
install -m 644 resources/linux/lynx.desktop resources/linux/lynx-mime.xml LICENSE THIRD_PARTY.md PRIVACY.md "$PKG/"
install -m 644 resources/lynx_icon_256.png "$PKG/lynx.png"
install -m 755 resources/linux/install.sh "$PKG/install.sh"
[[ -n "$DEMO" && -f "$DEMO" ]] && install -m 644 "$DEMO" "$PKG/Demo.lynx"
tar -C "$WORK" -czf "$OUTDIR/$NAME.tar.gz" "$NAME"

# --- AppImage ----------------------------------------------------------------------------------------------
APPDIR="$WORK/LYNX.AppDir"
mkdir -p "$APPDIR/usr/bin" "$APPDIR/usr/share/applications" "$APPDIR/usr/share/icons/hicolor/256x256/apps" "$APPDIR/usr/share/mime/packages"
install -m 755 "$PKG/LYNX" "$APPDIR/usr/bin/LYNX"
install -m 644 resources/linux/lynx.desktop "$APPDIR/usr/share/applications/lynx.desktop"
install -m 644 resources/linux/lynx.desktop "$APPDIR/lynx.desktop"
install -m 644 resources/lynx_icon_256.png "$APPDIR/usr/share/icons/hicolor/256x256/apps/lynx.png"
install -m 644 resources/lynx_icon_256.png "$APPDIR/lynx.png"
ln -sf lynx.png "$APPDIR/.DirIcon"
install -m 644 resources/linux/lynx-mime.xml "$APPDIR/usr/share/mime/packages/lynx-mime.xml"
for f in LICENSE THIRD_PARTY.md PRIVACY.md; do install -m 644 "$f" "$APPDIR/usr/share/$f"; done
cat > "$APPDIR/AppRun" <<'EOF'
#!/bin/sh
here="$(dirname "$(readlink -f "$0")")"
exec "$here/usr/bin/LYNX" "$@"
EOF
chmod 755 "$APPDIR/AppRun"

TOOL="${APPIMAGETOOL:-$HOME/opt/appimagetool-$APPIMAGETOOL_VERSION-x86_64.AppImage}"
if [[ ! -x "$TOOL" ]]; then
    mkdir -p "$(dirname "$TOOL")"
    curl -fsSL -o "$TOOL" "https://github.com/AppImage/appimagetool/releases/download/$APPIMAGETOOL_VERSION/appimagetool-x86_64.AppImage"
    chmod +x "$TOOL"
fi
# no FUSE needed to run the tool itself (containers, CI)
APPIMAGE_EXTRACT_AND_RUN=1 ARCH="$ARCH" "$TOOL" --no-appstream "$APPDIR" "$OUTDIR/$NAME.AppImage" > "$WORK/appimagetool.log" 2>&1 \
    || { cat "$WORK/appimagetool.log"; exit 1; }
chmod 755 "$OUTDIR/$NAME.AppImage"
ls -la "$OUTDIR/$NAME.tar.gz" "$OUTDIR/$NAME.AppImage"
