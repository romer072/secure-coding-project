#!/usr/bin/env bash

# setup.sh -- installs Pandoc and Typst if not already on PATH.
# Run with: bash setup.sh or ./setup.sh
#
# By default, binaries are installed to /usr/local/bin (requires sudo).
# Edit the install commands below if you prefer a different location
# (e.g. ~/.local/bin -- make sure that directory is on your PATH).

set -euo pipefail

# Install pandoc if needed

if command -v pandoc &>/dev/null; then
  echo "pandoc already installed: $(command -v pandoc) ($(pandoc --version | head -1))"
else
  echo "Installing pandoc..."
  PANDOC_URL="https://github.com/jgm/pandoc/releases/download/3.9.0.2/pandoc-3.9.0.2-linux-amd64.tar.gz"
  PANDOC_TMP=$(mktemp -d)
  trap 'rm -rf "$PANDOC_TMP"' EXIT

  curl -fsSL "$PANDOC_URL" | tar -xz -C "$PANDOC_TMP" --strip-components=2 --wildcards '*/bin/pandoc'
  sudo install -m 0755 "$PANDOC_TMP/pandoc" /usr/local/bin/pandoc
  echo "pandoc installed to /usr/local/bin/pandoc"
  rm -rf "$PANDOC_TMP"
fi

# Install typst if needed

if command -v typst &>/dev/null; then
  echo "typst already installed: $(command -v typst) ($(typst --version))"
else
  echo "Installing typst..."
  TYPST_URL="https://github.com/typst/typst/releases/download/v0.14.2/typst-x86_64-unknown-linux-musl.tar.xz"
  TYPST_TMP=$(mktemp -d)
  trap 'rm -rf "$TYPST_TMP"' EXIT

  curl -fsSL "$TYPST_URL" | tar -xJ -C "$TYPST_TMP" --strip-components=1 --wildcards '*/typst'
  sudo install -m 0755 "$TYPST_TMP/typst" /usr/local/bin/typst
  echo "typst installed to /usr/local/bin/typst"
  rm -rf "$TYPST_TMP"
fi

echo "Done. Run 'make all' to build your report."
