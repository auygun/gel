#!/bin/sh
# Installs the latest gel release binary on Linux and macOS.
#
# Usage:
#   curl -fsSL https://raw.githubusercontent.com/auygun/gel/main/install.sh | sh
#
# Set GEL_VERSION to a release tag (e.g. GEL_VERSION=v0.15-beta) to install
# a specific version instead of the latest.

set -eu

repo="auygun/gel"

# --- Detect the platform and pick the release asset. ----------------------
os="$(uname -s)"
arch="$(uname -m)"

case "$os-$arch" in
Linux-x86_64) asset="gel-linux-x64" ;;
Darwin-arm64) asset="gel-macos-arm64" ;;
Darwin-x86_64) asset="gel-macos-x64" ;;
*)
  echo "error: unsupported platform: $os ($arch)" >&2
  echo "Available builds: Linux x86_64, macOS arm64 and x86_64." >&2
  exit 1
  ;;
esac

# --- Resolve the download URL. --------------------------------------------
if [ -n "${GEL_VERSION:-}" ]; then
  url="https://github.com/$repo/releases/download/$GEL_VERSION/$asset"
else
  # List releases (newest first, prereleases included) and take the first.
  # /releases/latest would skip prerelease-only repos.
  tag="$(curl -fsSL "https://api.github.com/repos/$repo/releases?per_page=20" \
    | grep -o '"tag_name": *"[^"]*"' | head -n1 | cut -d'"' -f4)"
  if [ -z "$tag" ]; then
    echo "error: could not determine the latest release of $repo" >&2
    exit 1
  fi
  url="https://github.com/$repo/releases/download/$tag/$asset"
fi

echo "Downloading $asset from $url"
tmp="$(mktemp)"
trap 'rm -f "$tmp"' EXIT
curl -fL --progress-bar -o "$tmp" "$url"
chmod +x "$tmp"

# --- Pick an install directory. --------------------------------------------
in_path() {
  case ":$PATH:" in
  *":$1:"*) return 0 ;;
  *) return 1 ;;
  esac
}

if in_path "$HOME/.local/bin"; then
  dest_dir="$HOME/.local/bin"
elif in_path /usr/local/bin; then
  dest_dir="/usr/local/bin"
else
  dest_dir="$HOME/.local/bin"
fi

if [ ! -d "$dest_dir" ] && ! mkdir -p "$dest_dir" 2>/dev/null; then
  sudo mkdir -p "$dest_dir"
fi

if [ -w "$dest_dir" ]; then
  mv "$tmp" "$dest_dir/gel"
else
  sudo mv "$tmp" "$dest_dir/gel"
fi

echo "Installed gel to $dest_dir/gel"
case ":$PATH:" in
*":$dest_dir:"*) ;;
*) echo "note: $dest_dir is not in your PATH; add it to run 'gel' from the terminal." ;;
esac
if [ "$os" = "Darwin" ]; then
  echo "You can also move it to /Applications to launch it from the Dock or Spotlight."
fi
