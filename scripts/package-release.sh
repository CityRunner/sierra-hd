#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Packages a release build of sierra-hd (spec section 10.4):
#   package-release.sh <build directory> <platform name> <output directory>
# The archive holds the program, the hint files, the user README and the
# licences; never any game data. Linux and macOS get .tar.gz, Windows .zip.
set -euo pipefail

build=$1
platform=$2
out=$3
root=$(cd "$(dirname "$0")/.." && pwd)
version=$(sed -n 's/^    VERSION \([0-9.]*\)$/\1/p' "$root/CMakeLists.txt")
name="sierra-hd-$version-$platform"
stage=$(mktemp -d)
trap 'rm -rf "$stage"' EXIT

mkdir -p "$stage/$name/licenses" "$out"
exe="$build/src/app/sierra-hd"
[[ -f "$exe.exe" ]] && exe="$exe.exe"
cp "$exe" "$stage/$name/"
cp -r "$root/hints" "$stage/$name/hints"
cp "$root/docs/USER-README.md" "$stage/$name/README.md"
cp "$root/LICENSE" "$stage/$name/licenses/sierra-hd-LICENSE.txt"
cp "$root/THIRD_PARTY.md" "$stage/$name/licenses/"
cp "$root/third_party/SDL3/LICENSE.txt" "$stage/$name/licenses/SDL3-LICENSE.txt"
cp "$root/third_party/nuked-opl3/LICENSE" "$stage/$name/licenses/Nuked-OPL3-LICENSE.txt"

if [[ "$platform" == windows-* ]]; then
    (cd "$stage" && powershell -NoProfile -Command "Compress-Archive -Path '$name' -DestinationPath '$name.zip'")
    mv "$stage/$name.zip" "$out/"
else
    tar -C "$stage" -czf "$out/$name.tar.gz" "$name"
fi
echo "$out/$name"
