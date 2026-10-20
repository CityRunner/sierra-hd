#!/usr/bin/env bash
# Builds the pinned toolchain image (docker/Dockerfile) for the host
# architecture and tags it sierra-hd-toolchain:<content hash>.
#
# Behind a TLS-intercepting proxy, set SIERRA_HD_EXTRA_CA to the proxy's CA
# bundle: the build then reaches the Ubuntu archive over HTTPS through
# HTTPS_PROXY and trusts that bundle. The bundle is passed as a build secret,
# so it never ends up in an image layer.
set -euo pipefail

here="$(cd "$(dirname "$0")" && pwd)"
tag="$("${here}/image-tag.sh")"

args=(--tag "${tag}" --file "${here}/Dockerfile")
if [[ -n "${SIERRA_HD_EXTRA_CA:-}" ]]; then
    args+=(--network host
           --secret "id=extra_ca,src=${SIERRA_HD_EXTRA_CA}"
           --build-arg APT_SCHEME=https
           --build-arg "HTTPS_PROXY=${HTTPS_PROXY:-}")
fi

docker build "${args[@]}" "$@" "${here}"
echo "${tag}"
