#!/usr/bin/env bash
#
# Copyright (c) 2026, Daily
#
# Copies the content of Unreal Engine's Third Person template (the level, the
# character and its animations, and the input setup) into Content/, and creates
# the demo's materials. The project doesn't include them, since every engine
# install has the template. It creates the materials in the editor, with
# Scripts/create_materials.py, so build the demo first.
#
# It's for Linux and macOS.
#
# Usage:
#   UE_ROOT=/path/to/UnrealEngine ./setup.sh
#

set -euo pipefail

: "${UE_ROOT:?Set UE_ROOT to your Unreal Engine directory}"

HERE="$(cd "$(dirname "$0")" && pwd)"
TEMPLATES="$UE_ROOT/Templates"

if [[ "$(uname -s)" == Darwin ]]; then
    PLATFORM=Mac
    MODULE=libUnrealEditor-PipecatDemo.dylib
    # macOS apps keep their logs in ~/Library/Logs.
    LOG="$HOME/Library/Logs/Unreal Engine/PipecatDemoEditor/PipecatDemo.log"
else
    PLATFORM=Linux
    MODULE=libUnrealEditor-PipecatDemo.so
    LOG="$HERE/Saved/Logs/PipecatDemo.log"
fi

if [[ ! -f "$HERE/Binaries/$PLATFORM/$MODULE" ]]; then
    echo "Build the demo first, e.g.:"
    echo "  \"$UE_ROOT/Engine/Build/BatchFiles/$PLATFORM/Build.sh\" PipecatDemoEditor $PLATFORM Development -Project=\"$HERE/PipecatDemo.uproject\""
    exit 1
fi

mkdir -p "$HERE/Content"
cp -R "$TEMPLATES/TP_ThirdPersonBP/Content/." "$HERE/Content/"
# The template's shared packs, each mounted in its own folder, e.g. /Game/Input.
for pack in LevelPrototyping Characters Input; do
    mkdir -p "$HERE/Content/$pack"
    cp -R "$TEMPLATES/TemplateResources/High/$pack/Content/." "$HERE/Content/$pack/"
done
# The engine's templates are read-only, and the editor needs to save them.
chmod -R u+w "$HERE/Content"

echo "Copied the Third Person template content into $HERE/Content"

if ! "$UE_ROOT/Engine/Binaries/$PLATFORM/UnrealEditor-Cmd" "$HERE/PipecatDemo.uproject" \
    -run=pythonscript -script="$HERE/Scripts/create_materials.py" \
    -EnablePlugins=PythonScriptPlugin -unattended -nosplash -nullrhi > /dev/null 2>&1; then
    echo "Unable to create the demo's materials, see $LOG"
    exit 1
fi
echo "Created the demo's materials in $HERE/Content/Pipecat"
