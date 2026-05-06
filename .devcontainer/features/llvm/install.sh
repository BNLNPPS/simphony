#!/bin/bash
set -euo pipefail

# Devcontainer feature option:
# "version" in devcontainer-feature.json becomes VERSION here.
LLVM_REQUESTED_VERSION="${VERSION:-latest}"

. /etc/os-release

apt-get update -y
apt-get install -y --no-install-recommends \
    ca-certificates \
    gnupg \
    wget

install -d -m 0755 /etc/apt/keyrings

wget -qO /etc/apt/keyrings/apt.llvm.org.asc https://apt.llvm.org/llvm-snapshot.gpg.key

# Use the generic apt.llvm.org suite.
cat > /etc/apt/sources.list.d/llvm.list <<EOF
deb [signed-by=/etc/apt/keyrings/apt.llvm.org.asc] https://apt.llvm.org/${VERSION_CODENAME}/ llvm-toolchain-${VERSION_CODENAME} main
EOF

apt-get update -y

if [ "${LLVM_REQUESTED_VERSION}" = "latest" ] || [ -z "${LLVM_REQUESTED_VERSION}" ]; then
    LLVM_VERSION="$(
        apt-cache search '^clang-format-[0-9]+$' \
            | awk '{print $1}' \
            | sed 's/^clang-format-//' \
            | sort -V \
            | tail -n 1
    )"

    if [ -z "${LLVM_VERSION}" ]; then
        echo "ERROR: Could not determine latest available LLVM version." >&2
        exit 1
    fi
else
    LLVM_VERSION="${LLVM_REQUESTED_VERSION}"
fi

echo "Using LLVM version: ${LLVM_VERSION}"

apt-get install -y --no-install-recommends \
    clang-format-${LLVM_VERSION} \
    clangd-${LLVM_VERSION} \
    clang-tidy-${LLVM_VERSION}

ln -sf /usr/bin/clang-format-${LLVM_VERSION} /usr/local/bin/clang-format
ln -sf /usr/bin/clangd-${LLVM_VERSION} /usr/local/bin/clangd
ln -sf /usr/bin/clang-tidy-${LLVM_VERSION} /usr/local/bin/clang-tidy

apt-get clean
rm -rf /var/lib/apt/lists/*
