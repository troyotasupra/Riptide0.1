#!/bin/bash
# Plays Riptide on macOS: brings the game's code up to date (quick when nothing has changed), then starts the game in
# its own window. Double-click it in Finder. Set UE_ROOT if Unreal Engine 5.7 isn't in the default place.
UE_ROOT="${UE_ROOT:-/Users/Shared/Epic Games/UE_5.7}"
PROJECT="$(cd "$(dirname "$0")/../.." && pwd)/Riptide.uproject"

echo "Getting Riptide ready..."
if ! "$UE_ROOT/Engine/Build/BatchFiles/Mac/Build.sh" RiptideEditor Mac Development "-Project=$PROJECT" -WaitMutex > /tmp/riptide_build.log 2>&1; then
    echo
    echo "Riptide's code didn't build. The end of the build log:"
    tail -n 25 /tmp/riptide_build.log
    read -r -p "Press Return to close."
    exit 1
fi
open -n "$UE_ROOT/Engine/Binaries/Mac/UnrealEditor.app" --args "$PROJECT" -game
