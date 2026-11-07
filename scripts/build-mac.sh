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
cp dist/device/*.amxd "$PKGROOT/"
cat > "$PKG/package-info.json" <<JSON
{ "name": "BASH", "displayname": "BASH!", "version": "$VERSION", "author": "Kalabash",
  "description": "BASH! for Max for Live: the bash~ instrument and its display scripts.",
  "max_version_min": "8.5", "os": { "macintosh": { "platform": ["ia64", "aarch64"] } } }
JSON

# after install: copy into the user's Max Packages (Max 8 and 9) and Live's User Library
cat > dist/scripts/postinstall <<'POST'
#!/bin/sh
SRC="/Library/Application Support/Kalabash/BASH"
USER_NAME=$(stat -f%Su /dev/console)
HOME_DIR=$(dscl . -read "/Users/$USER_NAME" NFSHomeDirectory 2>/dev/null | awk '{print $2}')
[ -z "$HOME_DIR" ] && HOME_DIR="/Users/$USER_NAME"
for MAXV in "Max 9" "Max 8"; do
  DEST="$HOME_DIR/Documents/$MAXV/Packages"
  mkdir -p "$DEST"
  rm -rf "$DEST/BASH"
  cp -R "$SRC/Max Package/BASH" "$DEST/BASH"
  chown -R "$USER_NAME" "$HOME_DIR/Documents/$MAXV"
done
# Live's browser: User Library > Presets > ... > Kalabash
UL="$HOME_DIR/Music/Ableton/User Library/Presets"
INST="$UL/Instruments/Max Instrument/Kalabash"
MIDI="$UL/MIDI Effects/Max MIDI Effect/Kalabash"
mkdir -p "$INST" "$MIDI"
rm -f "$UL/Instruments/Max Instrument/BASH!.amxd"   # 1.0.x put BASH! straight in Max Instrument
cp "$SRC/BASH!.amxd" "$SRC/BASH! Voice.amxd" "$INST/"
cp "$SRC/BASH! Sequencer.amxd" "$MIDI/"
chown -R "$USER_NAME" "$UL/Instruments" "$UL/MIDI Effects"
xattr -dr com.apple.quarantine "$HOME_DIR/Documents/Max 9/Packages/BASH" "$HOME_DIR/Documents/Max 8/Packages/BASH" "$INST" "$MIDI" 2>/dev/null || true
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

This installs three devices into your Live User Library, in Kalabash folders:
  - BASH!            (Instruments > Max Instrument > Kalabash)  sequencer + synth / slicer / drums
  - BASH! Voice      (Instruments > Max Instrument > Kalabash)  the synth / slicer / drums on their own
  - BASH! Sequencer  (MIDI Effects > Max MIDI Effect > Kalabash) the sequencers, as MIDI for anything
and the BASH package (the bash~ object + display scripts) -> Documents/Max 9 and Max 8 Packages

Needs Ableton Live 11 or 12 Suite (or Max for Live). Quit Live first.]]></welcome>
  <options customize="never" require-scripts="false" hostArchitectures="arm64,x86_64"/>
  <domains enable_localSystem="true"/>
  <choices-outline><line choice="m4l"/></choices-outline>
  <choice id="m4l" title="BASH! devices by Kalabash"><pkg-ref id="com.kalabash.bash.m4l"/></choice>
  <pkg-ref id="com.kalabash.bash.m4l">BASH-m4l.pkg</pkg-ref>
</installer-gui-script>
DIST
productbuild --distribution dist/distribution.xml --package-path dist "dist/dmg/Install BASH!.pkg"
cp packaging/INSTALL-mac.txt dist/dmg/README.txt
mkdir -p "dist/dmg/Manual install"
cp -R "$PKG" "dist/dmg/Manual install/BASH"
cp dist/device/*.amxd "dist/dmg/Manual install/"
rm -f BASH-mac.dmg
hdiutil create -volname "BASH! $VERSION" -srcfolder dist/dmg -ov -format UDZO BASH-mac.dmg >/dev/null
echo "    BASH-mac.dmg"

if [[ $INSTALL == 1 ]]; then
  echo "==> Installing for this user"
  for MAXV in "Max 9" "Max 8"; do
    mkdir -p ~/Documents/"$MAXV"/Packages && rm -rf ~/Documents/"$MAXV"/Packages/BASH
    cp -R "$PKG" ~/Documents/"$MAXV"/Packages/BASH
  done
  UL=~/Music/Ableton/"User Library"/Presets
  mkdir -p "$UL/Instruments/Max Instrument/Kalabash" "$UL/MIDI Effects/Max MIDI Effect/Kalabash"
  cp "dist/device/BASH!.amxd" "dist/device/BASH! Voice.amxd" "$UL/Instruments/Max Instrument/Kalabash/"
  cp "dist/device/BASH! Sequencer.amxd" "$UL/MIDI Effects/Max MIDI Effect/Kalabash/"
  echo "    Done. In Live: User Library > Presets > Instruments / MIDI Effects > ... > Kalabash"
fi
