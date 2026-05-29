#!/bin/sh
# xcull installer.
#
#   curl -fsSL https://raw.githubusercontent.com/xcull/xcull/main/install.sh | sh
#
# Downloads the latest prebuilt Linux release, verifies its sha256, and
# installs the binary (plus man page when present). Honors two env vars:
#   PREFIX          install prefix          (default /usr/local)
#   XCULL_VERSION   pin a tag, e.g. v1.0.1  (default: latest release)
set -eu

REPO="xcull/xcull"
PREFIX="${PREFIX:-/usr/local}"

err()  { printf 'xcull-install: %s\n' "$1" >&2; exit 1; }
info() { printf 'xcull-install: %s\n' "$1" >&2; }
have() { command -v "$1" >/dev/null 2>&1; }

have tar || err "tar is required"
have curl || have wget || err "curl or wget is required"

fetch_to() { # fetch_to URL FILE  (fails non-zero on HTTP error)
    if have curl; then curl -fsSL -o "$2" "$1"
    else wget -qO "$2" "$1"; fi
}

os=$(uname -s)
[ "$os" = "Linux" ] || err "prebuilt binaries are Linux-only ($os detected); build from source: https://github.com/$REPO#from-source"

case "$(uname -m)" in
    x86_64|amd64)   arch=x86_64 ;;
    aarch64|arm64)  arch=arm64  ;;
    *) err "unsupported architecture: $(uname -m)" ;;
esac

ver="${XCULL_VERSION:-}"
if [ -z "$ver" ]; then
    # follow the releases/latest redirect to learn the newest tag (no API quota)
    if have curl; then
        ver=$(curl -fsSLI -o /dev/null -w '%{url_effective}' \
              "https://github.com/$REPO/releases/latest" | sed 's#.*/tag/##')
    else
        ver=$(wget -qSO /dev/null "https://github.com/$REPO/releases/latest" 2>&1 \
              | sed -n 's#.*/tag/##p' | tail -n1)
    fi
    [ -n "$ver" ] || err "could not determine latest version; set XCULL_VERSION=vX.Y.Z"
fi

asset="xcull-${ver}-linux-${arch}.tar.gz"
base="https://github.com/$REPO/releases/download/${ver}"

tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT INT TERM

info "downloading ${asset} (${ver})"
fetch_to "${base}/${asset}" "${tmp}/${asset}" || err "download failed: ${base}/${asset}"

# verify the checksum when both the sidecar and a hasher are available
if fetch_to "${base}/${asset}.sha256" "${tmp}/${asset}.sha256" 2>/dev/null; then
    if have sha256sum; then
        ( cd "$tmp" && sha256sum -c "${asset}.sha256" >/dev/null 2>&1 ) \
            || err "checksum mismatch for ${asset}"
        info "checksum verified"
    fi
fi

tar -xzf "${tmp}/${asset}" -C "$tmp"
[ -f "${tmp}/xcull" ] || err "archive did not contain the xcull binary"

bindir="${PREFIX}/bin"
sudo=""
if [ "$(id -u)" -ne 0 ] && [ ! -w "$(dirname "$bindir")" ]; then
    have sudo && sudo="sudo" || err "cannot write ${bindir} and sudo is unavailable; set PREFIX to a writable dir"
fi

$sudo install -d "$bindir"
$sudo install -m755 "${tmp}/xcull" "${bindir}/xcull"
info "installed ${ver} to ${bindir}/xcull"

# best-effort man page if the tarball shipped one
if [ -f "${tmp}/xcull.1" ]; then
    man1="${PREFIX}/share/man/man1"
    $sudo install -d "$man1" 2>/dev/null && \
        $sudo install -m644 "${tmp}/xcull.1" "${man1}/xcull.1" 2>/dev/null || true
fi

"${bindir}/xcull" --version 2>/dev/null | head -n1 || true
