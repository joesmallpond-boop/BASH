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
rm -rf dmg BASH-mac.dmg && mkdir -p dmg
cp -R "$ART/VST3/BASH.vst3" "$ART/AU/BASH.component" "$ART/Standalone/BASH.app" dmg/
for b in dmg/BASH.vst3 dmg/BASH.component dmg/BASH.app; do codesign --force --deep --sign - "$b"; done
cp packaging/INSTALL-mac.txt dmg/
hdiutil create -volname "BASH!" -srcfolder dmg -ov -format UDZO BASH-mac.dmg >/dev/null
echo "    BASH-mac.dmg"

if [[ $INSTALL == 1 ]]; then
  echo "==> Installing for this user"
  mkdir -p ~/Library/Audio/Plug-Ins/VST3 ~/Library/Audio/Plug-Ins/Components
  rm -rf ~/Library/Audio/Plug-Ins/VST3/BASH.vst3 ~/Library/Audio/Plug-Ins/Components/BASH.component /Applications/BASH.app
  cp -R dmg/BASH.vst3 ~/Library/Audio/Plug-Ins/VST3/
  cp -R dmg/BASH.component ~/Library/Audio/Plug-Ins/Components/
  cp -R dmg/BASH.app /Applications/
  killall -9 AudioComponentRegistrar 2>/dev/null || true
  echo "    Done. Restart your DAW (Logic: it rescans Audio Units on launch)."
fi
