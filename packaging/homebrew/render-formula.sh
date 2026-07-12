#!/usr/bin/env sh

set -eu

if [ "$#" -ne 2 ]; then
    echo "Usage: $0 <tag> <source-archive-sha256>" >&2
    exit 1
fi

tag=$1
sha256=$2
template=packaging/homebrew/Formula/dgramtunneler.rb.in
output=packaging/homebrew/Formula/dgramtunneler.rb

case "$tag" in
    v*) ;;
    *)
        echo "Tag must begin with v (for example: v0.1.0)." >&2
        exit 1
        ;;
esac

case "$sha256" in
    *[!0123456789abcdef]*)
        echo "Source archive SHA-256 must contain only lowercase hexadecimal characters." >&2
        exit 1
        ;;
esac

if [ "${#sha256}" -ne 64 ]; then
    echo "Source archive SHA-256 must be 64 characters long." >&2
    exit 1
fi

sed -e "s/@TAG@/$tag/g" -e "s/@SHA256@/$sha256/g" "$template" > "$output"
echo "Wrote $output"
