#!/usr/bin/env bash
# Build BASH! on a Mac: VST3 + AU + Standalone (universal), run the tests,
# make BASH-mac.dmg, and (unless --no-install) install the plugins for you.
#
#   Needs: Xcode (or the Command Line Tools) and CMake  ->  brew install cmake
#   Run:   ./scripts/build-mac.sh
set -euo pipefail
cd "$(dirname "$0")/.."

INSTALL=1
[[ "${1:-}" == "--no-install" ]] && INSTALL=0

echo "==> Configuring"
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DBASH_BUILD_TOOLS=ON

echo "==> Building (first build fetches JUCE and takes a few minutes)"
cmake --build build --config Release -j "$(sysctl -n hw.ncpu)" \
      --target bash_core_tests bash_plugin_tests BASH_VST3 BASH_AU BASH_Standalone

echo "==> Testing"
./build/bash_core_tests
./build/bash_plugin_tests

echo "==> Packaging"
ART=build/plugin/BASH_artefacts/Release
VERSION=$(sed -n 's/^project(BASH VERSION \([0-9.]*\).*/\1/p' CMakeLists.txt)
rm -rf dist BASH-mac.dmg && mkdir -p dist/app dist/vst3 dist/au dist/scripts dist/dmg
cp -R "$ART/Standalone/BASH.app"     dist/app/
cp -R "$ART/VST3/BASH.vst3"          dist/vst3/
cp -R "$ART/AU/BASH.component"       dist/au/
for b in dist/app/BASH.app dist/vst3/BASH.vst3 dist/au/BASH.component; do codesign --force --deep --sign - "$b"; done

# after install: make Logic & co. rescan Audio Units
cat > dist/scripts/postinstall <<'POST'
#!/bin/sh
killall -9 AudioComponentRegistrar 2>/dev/null || true
exit 0
POST
chmod +x dist/scripts/postinstall

pkgbuild --root dist/app  --install-location /Applications                      --identifier com.kalabash.bash.app  --version "$VERSION" dist/BASH-app.pkg
pkgbuild --root dist/vst3 --install-location /Library/Audio/Plug-Ins/VST3       --identifier com.kalabash.bash.vst3 --version "$VERSION" dist/BASH-vst3.pkg
pkgbuild --root dist/au   --install-location /Library/Audio/Plug-Ins/Components --identifier com.kalabash.bash.au   --version "$VERSION" \
         --scripts dist/scripts dist/BASH-au.pkg

cat > dist/distribution.xml <<DIST
<?xml version="1.0" encoding="utf-8"?>
<installer-gui-script minSpecVersion="2">
  <title>BASH! $VERSION</title>
  <welcome mime-type="text/plain"><![CDATA[BASH! - an all-in-one kalabash solution.

This installs:
  - BASH! app (standalone)      -> Applications
  - BASH! VST3 plugin           -> /Library/Audio/Plug-Ins/VST3
  - BASH! Audio Unit (AU)       -> /Library/Audio/Plug-Ins/Components

Restart your DAW afterwards. Untick anything you don't want under Customize.]]></welcome>
  <options customize="allow" require-scripts="false" hostArchitectures="arm64,x86_64"/>
  <domains enable_localSystem="true"/>
  <choices-outline>
    <line choice="app"/>
    <line choice="vst3"/>
    <line choice="au"/>
  </choices-outline>
  <choice id="app"  title="BASH! app (standalone)" description="Runs on its own, no DAW needed."><pkg-ref id="com.kalabash.bash.app"/></choice>
  <choice id="vst3" title="VST3 plugin" description="For Ableton Live, Bitwig, Cubase, Reaper, FL Studio..."><pkg-ref id="com.kalabash.bash.vst3"/></choice>
  <choice id="au"   title="Audio Unit (AU)" description="For Logic Pro, GarageBand..."><pkg-ref id="com.kalabash.bash.au"/></choice>
  <pkg-ref id="com.kalabash.bash.app">BASH-app.pkg</pkg-ref>
  <pkg-ref id="com.kalabash.bash.vst3">BASH-vst3.pkg</pkg-ref>
  <pkg-ref id="com.kalabash.bash.au">BASH-au.pkg</pkg-ref>
</installer-gui-script>
DIST

productbuild --distribution dist/distribution.xml --package-path dist "dist/dmg/Install BASH!.pkg"
cp packaging/INSTALL-mac.txt dist/dmg/README.txt
hdiutil create -volname "BASH! $VERSION" -srcfolder dist/dmg -ov -format UDZO BASH-mac.dmg >/dev/null
echo "    BASH-mac.dmg  (contains 'Install BASH!.pkg')"

if [[ $INSTALL == 1 ]]; then
  echo "==> Installing for this user"
  mkdir -p ~/Library/Audio/Plug-Ins/VST3 ~/Library/Audio/Plug-Ins/Components
  rm -rf ~/Library/Audio/Plug-Ins/VST3/BASH.vst3 ~/Library/Audio/Plug-Ins/Components/BASH.component /Applications/BASH.app
  cp -R dist/vst3/BASH.vst3 ~/Library/Audio/Plug-Ins/VST3/
  cp -R dist/au/BASH.component ~/Library/Audio/Plug-Ins/Components/
  cp -R dist/app/BASH.app /Applications/
  killall -9 AudioComponentRegistrar 2>/dev/null || true
  echo "    Done. Restart your DAW (Logic: it rescans Audio Units on launch)."
fi
