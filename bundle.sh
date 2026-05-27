#!/bin/bash
set -e

APP="latea"
APPDIR="build/AppDir"
TOOLS_DIR="${TOOLS_DIR:-$HOME/tools}"
LINUXDEPLOY="${LINUXDEPLOY:-$TOOLS_DIR/linuxdeploy-x86_64.AppImage}"
APPIMAGETOOL="${APPIMAGETOOL:-$TOOLS_DIR/appimagetool-x86_64.AppImage}"
APPIMAGE="${APPIMAGE:-$APP-x86_64.AppImage}"
APPIMAGE_RUNTIME="build/runtime-x86_64"
AVX_FLAGS="${AVX_FLAGS:--mavx2 -mfma}"

make clean all AVX_FLAGS="$AVX_FLAGS"

rm -rf "$APPDIR"
mkdir -p "$APPDIR/usr/bin" "$APPDIR/usr/share/icons/hicolor/256x256/apps"

cp "$APP" "$APPDIR/usr/bin/"
cp "$APP.png" "$APPDIR/"
cp "$APP.png" "$APPDIR/usr/share/icons/hicolor/256x256/apps/"

sed 's|Exec=/usr/bin/latea %F|Exec=latea %F|; s|Icon=/usr/share/latea/latea.png|Icon=latea|' "$APP.desktop" > "$APPDIR/$APP.desktop"

cp appimage_runner.sh "$APPDIR/AppRun"
chmod +x "$APPDIR/AppRun"

APPIMAGE_EXTRACT_AND_RUN=1 PATH="$TOOLS_DIR:$PATH" "$LINUXDEPLOY" --appdir "$APPDIR" --executable "$APPDIR/usr/bin/$APP" --desktop-file "$APPDIR/$APP.desktop" --icon-file "$APP.png"
head -c "$("$APPIMAGETOOL" --appimage-offset)" "$APPIMAGETOOL" > "$APPIMAGE_RUNTIME"
APPIMAGE_EXTRACT_AND_RUN=1 "$APPIMAGETOOL" --runtime-file "$APPIMAGE_RUNTIME" -n "$APPDIR" "$APPIMAGE"

rm -rf "$APPDIR"
