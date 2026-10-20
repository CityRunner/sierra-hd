#!/usr/bin/env bash
# Prints the image tag: the name plus a hash of the Dockerfile, so a changed
# toolchain always gets a new tag.
set -euo pipefail
here="$(cd "$(dirname "$0")" && pwd)"
echo "sierra-hd-toolchain:$(sha256sum "${here}/Dockerfile" | cut -c1-16)"
