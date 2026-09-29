#!/usr/bin/env bash
#
# Copyright (c) 2026, Daily
#
# Copies the content of Unreal Engine's Third Person template (the level, the
# character and its animations, and the input setup) into Content/. The
# project doesn't include it, since every engine install has it.
#
# Usage:
#   UE_ROOT=/path/to/UnrealEngine ./setup.sh
#

set -euo pipefail

: "${UE_ROOT:?Set UE_ROOT to your Unreal Engine directory}"

HERE="$(cd "$(dirname "$0")" && pwd)"
TEMPLATES="$UE_ROOT/Templates"

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
