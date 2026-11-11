#!/usr/bin/env bash
# Build BASH! for Max for Live on a Mac: run the tests, build the bash~ external
# (universal), generate the device, check it, and make BASH-mac.dmg with an installer.
#
#   Needs: Xcode (or the Command Line Tools), CMake (brew install cmake), Python 3, git
#   Run:   ./scripts/build-mac.sh            (add --install to also install it for you)
set -euo pipefail
cd "$(dirname "$0")/.."

INSTALL=0
[[ "${1:-}" == "--install" ]] && INSTALL=1
VERSION=$(sed -n 's/^project(BASH VERSION \([0-9.]*\).*/\1/p' CMakeLists.txt)
MAX_SDK_COMMIT=c03a2922a2a8ff149165a1e2ea134e9321d6e202

echo "==> Core: build + tests"
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release -j "$(sysctl -n hw.ncpu)" --target bash_core_tests bash_params_json
./build/bash_core_tests

echo "==> Max SDK"
if [[ ! -d build/max-sdk-base ]]; then
  git clone -q https://github.com/Cycling74/max-sdk-base.git build/max-sdk-base
fi
git -C build/max-sdk-base checkout -q "$MAX_SDK_COMMIT"

echo "==> bash~ external (arm64 + x86_64)"
cmake -S max -B build/max -DCMAKE_BUILD_TYPE=Release -DMAX_SDK_BASE="$PWD/build/max-sdk-base"
cmake --build build/max --config Release -j "$(sysctl -n hw.ncpu)"
MXO="build/max/externals/bash~.mxo"
test -d "$MXO"
lipo -info "$MXO/Contents/MacOS/bash~"
nm -gU "$MXO/Contents/MacOS/bash~" | grep -q "_ext_main" || { echo "bash~ has no ext_main"; exit 1; }
codesign --force --deep --sign - "$MXO"

echo "==> The devices: BASH!, BASH! Voice, BASH! Sequencer"
./build/bash_params_json > build/params.json
rm -rf dist && mkdir -p dist/device
for v in full voice seq; do python3 max/make_device.py build/params.json dist/device "$v"; done
python3 max/check_device.py "dist/device/BASH!.maxpat" dist/device/bash_ui.js max/javascript
python3 max/check_device.py "dist/device/BASH! Voice.maxpat" dist/device/bash_ui_voice.js max/javascript
python3 max/check_device.py "dist/device/BASH! Sequencer.maxpat" dist/device/bash_ui_seq.js max/javascript

echo "==> Packaging"
PKGROOT="dist/root/Library/Application Support/Kalabash/BASH"
PKG="$PKGROOT/Max Package/BASH"
mkdir -p "$PKG/externals" "$PKG/javascript" "$PKG/patchers" dist/scripts dist/dmg
cp -R "$MXO" "$PKG/externals/"
cp max/javascript/*.js dist/device/bash_ui*.js "$PKG/javascript/"
cp dist/device/*.maxpat "$PKG/patchers/"
mkdir -p "$PKGROOT/Kalabash"
cp dist/device/*.amxd "$PKGROOT/Kalabash/"
cp -R "$MXO" "$PKGROOT/Kalabash/"
cp max/javascript/*.js dist/device/bash_ui*.js "$PKGROOT/Kalabash/"
cat > "$PKG/package-info.json" <<JSON
{ "name": "BASH", "displayname": "BASH!", "version": "$VERSION", "author": "Kalabash",
  "description": "BASH! for Max for Live: the bash~ instrument and its display scripts.",
  "max_version_min": "8.5", "os": { "macintosh": { "platform": ["ia64", "aarch64"] } } }
JSON

# after install: copy into the user's Max Packages (Max 8 and 9) and Live's User Library
cat > dist/scripts/postinstall <<'POST'
#!/bin/sh
# Puts one "Kalabash" folder in the user's Live User Library: the three devices + bash~ + the
# display scripts together (Max looks in a device's own folder). Logs to /tmp/kalabash-bash-install.log
# and never fails the install.
LOG=/tmp/kalabash-bash-install.log
exec >>"$LOG" 2>&1
echo "---- $(date)"
SRC="/Library/Application Support/Kalabash/BASH"
USER_NAME=$(stat -f%Su /dev/console)
HOME_DIR=$(dscl . -read "/Users/$USER_NAME" NFSHomeDirectory 2>/dev/null | awk '{print $2}')
[ -z "$HOME_DIR" ] && HOME_DIR="/Users/$USER_NAME"
echo "user $USER_NAME home $HOME_DIR"
UL="$HOME_DIR/Music/Ableton/User Library"
DEST="$UL/Kalabash"
mkdir -p "$DEST" || echo "could not create $DEST"
rm -rf "$DEST/bash~.mxo"
cp -R "$SRC/Kalabash/." "$DEST/" && echo "copied to $DEST" || echo "copy to $DEST failed"
# tidy up where 1.0 / 1.1.0 put things
rm -f "$UL/Presets/Instruments/Max Instrument/BASH!.amxd"
rm -rf "$UL/Presets/Instruments/Max Instrument/Kalabash" "$UL/Presets/MIDI Effects/Max MIDI Effect/Kalabash"
chown -R "$USER_NAME" "$DEST" 2>/dev/null
xattr -dr com.apple.quarantine "$DEST" 2>/dev/null
# Max's Packages too (macOS may refuse this one; the folder above is what the devices use)
for MAXV in "Max 9" "Max 8"; do
  P="$HOME_DIR/Documents/$MAXV/Packages"
  ( mkdir -p "$P" && rm -rf "$P/BASH" && cp -R "$SRC/Max Package/BASH" "$P/BASH" && chown -R "$USER_NAME" "$P/BASH" ) 2>/dev/null && echo "package -> $P" || echo "package -> $P refused"
done
exit 0
POST
chmod +x dist/scripts/postinstall

pkgbuild --root dist/root --install-location / --identifier com.kalabash.bash.m4l --version "$VERSION" \
         --scripts dist/scripts dist/BASH-m4l.pkg
cat > dist/distribution.xml <<DIST
<?xml version="1.0" encoding="utf-8"?>
<installer-gui-script minSpecVersion="2">
  <title>BASH! $VERSION by Kalabash, for Ableton Live</title>
  <welcome mime-type="text/plain"><![CDATA[BASH! by Kalabash - for Ableton Live (Max for Live).

This puts a Kalabash folder in your Live User Library with three devices:
  - BASH!            sequencer + synth / slicer / drums
  - BASH! Voice      the synth / slicer / drums on their own
  - BASH! Sequencer  the sequencers, as MIDI for any instrument or Drum Rack
In Live: Browser > User Library > Kalabash.

Needs Ableton Live 11 or 12 Suite (or Max for Live).
Quit Live before installing: Live keeps the old engine loaded until it restarts.]]></welcome>
  <options customize="never" require-scripts="false" hostArchitectures="arm64,x86_64"/>
  <domains enable_localSystem="true"/>
  <choices-outline><line choice="m4l"/></choices-outline>
  <choice id="m4l" title="BASH! devices by Kalabash"><pkg-ref id="com.kalabash.bash.m4l"/></choice>
  <pkg-ref id="com.kalabash.bash.m4l">BASH-m4l.pkg</pkg-ref>
</installer-gui-script>
DIST
productbuild --distribution dist/distribution.xml --package-path dist "dist/dmg/Install BASH!.pkg"
cp packaging/INSTALL-mac.txt dist/dmg/README.txt
# no installer needed: the same Kalabash folder, to drag into Live's User Library
cp -R "$PKGROOT/Kalabash" "dist/dmg/Kalabash"
rm -f BASH-mac.dmg
hdiutil create -volname "BASH! $VERSION" -srcfolder dist/dmg -ov -format UDZO BASH-mac.dmg >/dev/null
echo "    BASH-mac.dmg"

if [[ $INSTALL == 1 ]]; then
  echo "==> Installing for this user"
  UL=~/Music/Ableton/"User Library"
  rm -rf "$UL/Kalabash" && cp -R "$PKGROOT/Kalabash" "$UL/Kalabash"
  echo "    Done. In Live: Browser > User Library > Kalabash"
fi
