#!/bin/sh
# Builds wireguard-go (WireGuard in user space, on a TUN device) for the box: kernel 3.10 has no
# WireGuard. Go and the source are fetched into ../cache; the binary is ../out/wireguard-go,
# static (no cgo), for ARMv7.
# Usage: ./build-wireguard-go.sh
set -eu
HERE=$(cd "$(dirname "$0")" && pwd)
CACHE=$HERE/../cache
GO_VERSION=1.23.12
WG_VERSION=0.0.20250522
mkdir -p "$CACHE" "$HERE/../out"
if [ ! -x "$CACHE/go/bin/go" ]; then
    echo ">> Go $GO_VERSION"
    curl -fsSL "https://go.dev/dl/go$GO_VERSION.linux-amd64.tar.gz" | tar -C "$CACHE" -xzf -
fi
if [ ! -d "$CACHE/wireguard-go-$WG_VERSION" ]; then
    echo ">> wireguard-go $WG_VERSION"
    curl -fsSL "https://git.zx2c4.com/wireguard-go/snapshot/wireguard-go-$WG_VERSION.tar.xz" | tar -C "$CACHE" -xJf -
fi
cd "$CACHE/wireguard-go-$WG_VERSION"
export GOPATH="$CACHE/gopath" GOCACHE="$CACHE/gocache" GOTOOLCHAIN=local
CGO_ENABLED=0 GOOS=linux GOARCH=arm GOARM=7 "$CACHE/go/bin/go" build -trimpath -ldflags '-s -w' \
    -o "$HERE/../out/wireguard-go" .
echo "$HERE/../out/wireguard-go"
