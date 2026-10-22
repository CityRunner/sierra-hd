# Pinned toolchain image for sierra-hd (docs/ASSUMPTIONS.md, "Toolchain").
#
# Everything that can change a build is pinned here:
#   - the base image by digest,
#   - Ubuntu packages through the frozen "noble" release pocket,
#   - upstream LLVM by release and SHA-256 per architecture.
# The image is multi-architecture (linux/amd64 and linux/arm64), so CI on the
# x86-64 and ARM64 runners uses the same toolchain as local development.

# ubuntu:noble-20240423, the release-day image: its packages match the frozen
# "noble" pocket used below.
FROM ubuntu@sha256:562456a05a0dbd62a671c1854868862a4687bf979a96d48ae8e766642cd911e8

ARG LLVM_VERSION=23.1.4
ARG LLVM_SHA256_AMD64=25d814fda50d5fb0084b42be09605d8b1595d49d5e2e03a3345d7e112ff6f9f3
ARG LLVM_SHA256_ARM64=9b27cf263d711aa6ebfd4045ab241d947bd09b3cd19ffa5728f48a80c9eb3a91
ARG TARGETARCH

ENV DEBIAN_FRONTEND=noninteractive \
    LANG=C.UTF-8 \
    TZ=UTC

# Ubuntu packages come from the "noble" release pocket only. That pocket has
# been frozen since the 24.04 release, so every build of this file gets the
# same package versions on both architectures (the snapshot service does not
# cover ubuntu-ports, which serves arm64). Integrity comes from the archive's
# GPG signatures. APT_SCHEME=https and an "extra_ca" build secret are only for
# networks behind a TLS-intercepting proxy (see docker/build-image.sh).
# The arm64 cross packages give the aarch64 sysroot used with qemu-user; they
# are installed in the amd64 image only.
ARG APT_SCHEME=http
RUN --mount=type=secret,id=extra_ca,mode=0444 set -eux; \
    apt_opts=""; \
    if [ -s /run/secrets/extra_ca ]; then apt_opts="-o Acquire::https::CAInfo=/run/secrets/extra_ca"; fi; \
    printf 'Types: deb\nURIs: %s\nSuites: noble\nComponents: main universe\nSigned-By: /usr/share/keyrings/ubuntu-archive-keyring.gpg\n' \
        "$(case "${TARGETARCH}" in amd64) echo "${APT_SCHEME}://archive.ubuntu.com/ubuntu/" ;; \
                                   *) echo "${APT_SCHEME}://ports.ubuntu.com/ubuntu-ports/" ;; esac)" \
        > /etc/apt/sources.list.d/ubuntu.sources; \
    cross=""; \
    if [ "${TARGETARCH}" = "amd64" ]; then \
        cross="libc6-dev-arm64-cross libstdc++-14-dev-arm64-cross libgcc-14-dev-arm64-cross qemu-user"; \
    fi; \
    apt-get ${apt_opts} update; \
    apt-get ${apt_opts} install -y --no-install-recommends \
        ca-certificates curl xz-utils git make binutils pkg-config ccache \
        cmake ninja-build python3 python3-jsonschema \
        libstdc++-14-dev libgcc-14-dev libc6-dev \
        xvfb xauth \
        libx11-dev libxext-dev libxrandr-dev libxcursor-dev libxfixes-dev \
        libxi-dev libxss-dev libxtst-dev libxkbcommon-dev libdrm-dev libgbm-dev \
        libgl-dev libegl-dev libwayland-dev wayland-protocols libdecor-0-dev \
        libasound2-dev libpulse-dev libpipewire-0.3-dev libdbus-1-dev libudev-dev \
        libsdl2-dev libpng-dev zlib1g-dev \
        ${cross}; \
    rm -rf /var/lib/apt/lists/*

# The release-pocket libstdc++ is the GCC 14 pre-release snapshot 14-20240412.
# Its <bits/unicode.h> has a typo (`++this` instead of `++*this` in
# _Grapheme_cluster_view::_Iterator::operator++(int)) that Clang 19 and later
# reject whenever <format> is included. GCC 14.1 fixed it upstream; apply the
# same one-line fix here, to the native and to the arm64 cross headers.
RUN set -eux; \
    for header in /usr/include/c++/14/bits/unicode.h /usr/aarch64-linux-gnu/include/c++/14/bits/unicode.h; do \
        if [ -f "${header}" ]; then \
            grep -q '^	  ++this;$' "${header}"; \
            sed -i 's/^	  ++this;$/	  ++*this;/' "${header}"; \
        fi; \
    done

# Upstream LLVM release. Only the tools this project uses are kept: the full
# release is about 11 GB unpacked.
RUN --mount=type=secret,id=extra_ca,mode=0444 set -eux; \
    curl_opts=""; \
    if [ -s /run/secrets/extra_ca ]; then curl_opts="--cacert /run/secrets/extra_ca"; fi; \
    case "${TARGETARCH}" in \
        amd64) pkg="LLVM-${LLVM_VERSION}-Linux-X64"; sum="${LLVM_SHA256_AMD64}" ;; \
        arm64) pkg="LLVM-${LLVM_VERSION}-Linux-ARM64"; sum="${LLVM_SHA256_ARM64}" ;; \
        *) echo "unsupported architecture ${TARGETARCH}"; exit 1 ;; \
    esac; \
    curl ${curl_opts} -fsSL -o /tmp/llvm.tar.xz \
        "https://github.com/llvm/llvm-project/releases/download/llvmorg-${LLVM_VERSION}/${pkg}.tar.xz"; \
    echo "${sum}  /tmp/llvm.tar.xz" | sha256sum -c -; \
    members=""; \
    for tool in clang clang++ clang-cl clang-cpp 'clang-[0-9]*' \
                lld ld.lld lld-link \
                clang-format git-clang-format clang-tidy run-clang-tidy clang-apply-replacements \
                llvm-ar llvm-ranlib llvm-nm llvm-objcopy llvm-objdump llvm-strip llvm-readelf \
                llvm-symbolizer llvm-profdata llvm-cov; do \
        members="${members} ${pkg}/bin/${tool}"; \
    done; \
    mkdir -p /opt/llvm; \
    tar -xJf /tmp/llvm.tar.xz -C /opt/llvm --strip-components=1 --wildcards \
        ${members} "${pkg}/lib/clang" "${pkg}/share/clang"; \
    rm /tmp/llvm.tar.xz; \
    /opt/llvm/bin/clang --version

ENV PATH=/opt/llvm/bin:${PATH} \
    CC=clang \
    CXX=clang++ \
    QEMU_LD_PREFIX=/usr/aarch64-linux-gnu

RUN git config --system --add safe.directory '*'

WORKDIR /work
