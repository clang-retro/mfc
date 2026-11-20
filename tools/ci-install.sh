#!/bin/sh
# ci-install.sh - installs Ubuntu packages for CI (Wine, Xvfb, MinGW-w64...),
# from a cached copy of their installed files when there is one.
#
#   tools/ci-install.sh CACHE_DIR PACKAGE...
#
# With CACHE_DIR/root.tar there, unpacks it over / and is done: no apt-get
# update, no downloads, no dpkg. Without it, installs PACKAGE... with apt
# (adding i386 for wine32), then writes CACHE_DIR/root.tar with every file
# the install added or changed: those of the packages it installed or
# upgraded, and the update-alternatives links to them (ImageMagick's
# convert, for one). The workflow caches CACHE_DIR under a key naming the
# package list and the runner image, so a new image starts a fresh one.
#
# The packages are not in dpkg's database on a cache hit; nothing after
# this in CI uses apt, so that is fine there, but it is not for a machine
# that keeps running.
#
# Part of clang-retro MFC, an independent implementation of the MFC API.
# SPDX-License-Identifier: AGPL-3.0-only
set -e
cache=$1
shift
sudo dpkg --add-architecture i386

if [ -f "$cache/root.tar" ]; then
  sudo tar -xpf "$cache/root.tar" -C /
  sudo ldconfig
  echo "installed $* from $cache"
  exit 0
fi

tmp=$(mktemp -d)
pkgs() { dpkg-query -W -f='${db:Status-Abbrev} ${binary:Package}=${Version}\n' | awk '$1 == "ii" { print $2 }' | sort; }
pkgs > "$tmp/pkgs.before"

sudo apt-get update
sudo apt-get install -y --no-install-recommends "$@"

pkgs > "$tmp/pkgs.after"

# The new and upgraded packages' files (not directories, which tar makes as
# it needs them, keeping the ones there already as they are).
comm -13 "$tmp/pkgs.before" "$tmp/pkgs.after" | sed 's/=.*//' > "$tmp/new"
xargs -r dpkg -L < "$tmp/new" | sort -u | while read -r f; do
  if [ -L "$f" ] || [ -f "$f" ]; then echo "$f"; fi
done > "$tmp/files"

# The alternatives that point into those files (ImageMagick's convert, for
# one): their links, /etc/alternatives entries and dpkg's records of them,
# for the master link and each slave.
update-alternatives --get-selections | awk '{ print $1, $3 }' | while read -r name value; do
  grep -qxF "$value" "$tmp/files" || continue
  echo "/var/lib/dpkg/alternatives/$name"
  update-alternatives --query "$name" | awk '
    /^Name:/ { print "/etc/alternatives/" $2 }
    /^Link:/ { print $2 }
    /^Slaves:/ { slaves = 1; next }
    /^[^ ]/ { slaves = 0 }
    slaves && NF == 2 { print "/etc/alternatives/" $1; print $2 }'
done | while read -r f; do
  if [ -L "$f" ] || [ -f "$f" ]; then echo "$f"; fi
done >> "$tmp/files"

mkdir -p "$cache"
sudo tar -cf "$cache/root.tar" --no-recursion -T "$tmp/files"
sudo chown "$(id -u):$(id -g)" "$cache/root.tar"
echo "cached $(wc -l < "$tmp/new") packages, $(wc -l < "$tmp/files") files, $(du -h "$cache/root.tar" | cut -f1) in $cache"
rm -rf "$tmp"
