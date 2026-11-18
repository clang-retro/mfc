#!/bin/sh
# fetch-wtl.sh - downloads the Windows Template Library's headers for
# tests/wtltest. WTL is not part of this library (it is Microsoft's, under
# the Microsoft Public License); the test builds when its headers are
# there, and is skipped otherwise.
#
#   tools/fetch-wtl.sh [DIR]
#
# Puts WTL 10's headers in DIR/include (default: build/wtl/include), from
# the NuGet package, checked against its SHA-256.
#
# Part of clang-retro MFC, an independent implementation of the MFC API.
# SPDX-License-Identifier: AGPL-3.0-only
set -e
DIR=${1:-build/wtl}
VERSION=10.1.0
URL=https://api.nuget.org/v3-flatcontainer/wtl/$VERSION/wtl.$VERSION.nupkg
SHA256=28aabecf3f32ca6299e94f3d41043058114d8c341e47b3d9117dfe95da6af37d

if [ -f "$DIR/include/atlapp.h" ] && [ "$(cat "$DIR/VERSION" 2>/dev/null)" = "$VERSION" ]; then
  echo "WTL $VERSION is in $DIR/include"
  exit 0
fi
mkdir -p "$DIR"
pkg=$DIR/wtl.$VERSION.nupkg
curl -fsSL --retry 3 -o "$pkg" "$URL"
echo "$SHA256  $pkg" | sha256sum -c - >/dev/null
rm -rf "$DIR/include" "$DIR/unpack"
mkdir -p "$DIR/unpack"
(cd "$DIR/unpack" && unzip -q "../wtl.$VERSION.nupkg" 'lib/native/include/*')
mv "$DIR/unpack/lib/native/include" "$DIR/include"
rm -rf "$DIR/unpack" "$pkg"
echo "$VERSION" > "$DIR/VERSION"
echo "WTL $VERSION is in $DIR/include"
