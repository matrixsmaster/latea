#!/bin/bash
set -e

APP="latea"
DEST="/usr/bin"
SHARED="/usr/share/latea"
APPSTORE=~/.local/share/applications

make clean all

if [ ! -d "$DEST" ]; then
    sudo mkdir -p "$DEST"
fi

if [ ! -d "$SHARED" ]; then
    sudo mkdir -p "$SHARED"
fi

sudo cp -av "$APP" "$DEST/"
sudo cp -av "$APP.png" "$SHARED/"

[ "z$1" = "zupdate" ] && exit 0

if [ ! -d "APPSTORE" ]; then
    mkdir -p "$APPSTORE"
fi

cp -av "$APP.desktop" "$APPSTORE/"
update-desktop-database "$APPSTORE"
