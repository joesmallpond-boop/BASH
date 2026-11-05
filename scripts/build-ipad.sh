#!/usr/bin/env bash
# Make the iPad Xcode project (standalone app + AUv3 for AUM, Logic for iPad, Cubasis...)
# and open it. Then pick your iPad as the run destination in Xcode and press Run.
#
#   Needs: Xcode, CMake (brew install cmake), and an Apple Developer team
#   Run:   ./scripts/build-ipad.sh YOURTEAMID
#          (Team ID: developer.apple.com > Account > Membership details)
set -euo pipefail
cd "$(dirname "$0")/.."

TEAM="${1:-}"
if [[ -z "$TEAM" ]]; then
  echo "Usage: $0 YOUR_TEAM_ID"; exit 1
fi

cmake -S . -B build-ios -G Xcode \
  -DCMAKE_SYSTEM_NAME=iOS \
  -DCMAKE_OSX_DEPLOYMENT_TARGET=15.0 \
  -DBASH_BUILD_TESTS=OFF \
  -DCMAKE_XCODE_ATTRIBUTE_DEVELOPMENT_TEAM="$TEAM"

open build-ios/BASH.xcodeproj
echo "In Xcode: choose the 'BASH_Standalone' scheme and your iPad, then Run."
echo "The AUv3 is installed with the app and shows up in AUM / Logic for iPad / Cubasis."
