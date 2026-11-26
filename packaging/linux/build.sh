#!/usr/bin/env bash
# Usage: build.sh <arch|fedora|opensuse> <src_dir> <out_dir>
set -euo pipefail

if [[ $# -ne 3 ]]; then
    echo "Usage: $0 <arch|fedora|opensuse> <src_dir> <out_dir>" >&2
    exit 1
fi

DISTRO="$1"
SRC_DIR="$(cd "$2" && pwd)"
mkdir -p "$3"
OUT_DIR="$(cd "$3" && pwd)"
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

cmake_var() {
    sed -n "s/^set *(RELEASE_SERVICE_VERSION_$1 \"\([0-9]*\)\")/\1/p" "${SRC_DIR}/CMakeLists.txt"
}
VERSION="$(cmake_var MAJOR).$(cmake_var MINOR).$(cmake_var MICRO)"

git_src() {
    git -c "safe.directory=${SRC_DIR}" -C "$SRC_DIR" "$@" 2>/dev/null
}

# Release tags are v<CMakeLists.txt version>-mirrorphone.<N>; N becomes the
# package release. Everything else is a snapshot.
if [[ "${GITHUB_REF:-}" == refs/tags/* ]]; then
    TAG="${GITHUB_REF#refs/tags/}"
    TAG_RE="^v${VERSION//./\\.}-mirrorphone\\.([0-9]+)$"
    if [[ ! "$TAG" =~ $TAG_RE ]]; then
        echo "Tag ${TAG} should be v${VERSION}-mirrorphone.<N>" >&2
        exit 1
    fi
    PKGVER="$VERSION"
    PKGREL="${BASH_REMATCH[1]}"
    RPM_RELEASE="$PKGREL"
else
    COUNT="${COMMIT_COUNT:-$(git_src rev-list --count HEAD || echo 0)}"
    SHA="${SHORT_SHA:-$(git_src rev-parse --short=7 HEAD || echo unknown)}"
    PKGVER="${VERSION}.r${COUNT}.g${SHA}"
    PKGREL="1"
    RPM_RELEASE="0.r${COUNT}.g${SHA}"
fi

WORK_DIR="$(mktemp -d)"
trap 'rm -rf "$WORK_DIR"' EXIT

make_tarball() {
    local root="$1" dest="$2"
    mkdir -p "${WORK_DIR}/${root}"
    tar -C "$SRC_DIR" \
        --exclude='./.*' --exclude='./build*' --exclude='./out' --exclude='./docs' \
        -cf - . | tar -C "${WORK_DIR}/${root}" -xf -
    tar -C "$WORK_DIR" -czf "$dest" "$root"
    rm -rf "${WORK_DIR:?}/${root}"
}

build_rpm() {
    local topdir="${WORK_DIR}/rpmbuild"
    local spec="${topdir}/SPECS/kdeconnect-kde.spec"
    mkdir -p "${topdir}"/{SOURCES,SPECS}

    make_tarball "kdeconnect-${VERSION}" "${topdir}/SOURCES/kdeconnect-${VERSION}.tar.gz"
    sed -e "s|@RPM_VERSION@|${VERSION}|g" -e "s|@RPM_RELEASE@|${RPM_RELEASE}|g" \
        "${SCRIPT_DIR}/${DISTRO}/kdeconnect-kde.spec.in" > "$spec"

    rpmbuild -bb --define "_topdir ${topdir}" "$spec"
    find "${topdir}/RPMS" -name '*.rpm' ! -name '*-debug*' -exec cp -f {} "${OUT_DIR}/" \;
}

case "$DISTRO" in
    arch)
        if [[ ${EUID} -eq 0 ]]; then
            pacman -Syu --noconfirm --needed base-devel sudo
        fi
        PKG_DIR="${WORK_DIR}/pkgbuild"
        mkdir -p "$PKG_DIR"
        make_tarball "kdeconnect-${PKGVER}" "${PKG_DIR}/kdeconnect-${PKGVER}.tar.gz"
        sed -e "s|@ARCH_PKGVER@|${PKGVER}|g" -e "s|@ARCH_PKGREL@|${PKGREL}|g" \
            "${SCRIPT_DIR}/arch/PKGBUILD.in" > "${PKG_DIR}/PKGBUILD"

        # makepkg refuses to run as root
        if [[ ${EUID} -eq 0 ]]; then
            id -u builder >/dev/null 2>&1 || useradd -m builder
            echo 'builder ALL=(ALL) NOPASSWD: ALL' > /etc/sudoers.d/builder
            chmod 440 /etc/sudoers.d/builder
            chmod 755 "$WORK_DIR"
            chown -R builder: "$PKG_DIR"
            su builder -c "cd '${PKG_DIR}' && makepkg --noconfirm --syncdeps"
        else
            (cd "$PKG_DIR" && makepkg --noconfirm --syncdeps)
        fi
        cp -f "${PKG_DIR}"/*.pkg.tar.zst "${OUT_DIR}/"
        ;;
    fedora)
        # Build dependencies come from the distro's own kde-connect package
        if [[ ${EUID} -eq 0 ]]; then
            dnf -y install rpm-build
            dnf -y builddep --enablerepo=fedora-source --enablerepo=updates-source kde-connect
        fi
        build_rpm
        ;;
    opensuse)
        # Build dependencies come from the distro's own kdeconnect-kde package
        if [[ ${EUID} -eq 0 ]]; then
            zypper --non-interactive modifyrepo --enable repo-source
            zypper --non-interactive --gpg-auto-import-keys install --no-recommends rpm-build
            zypper --non-interactive source-install --build-deps-only kdeconnect-kde
        fi
        build_rpm
        ;;
    *)
        echo "Unknown distro: ${DISTRO}" >&2
        exit 1
        ;;
esac

ls -lh "$OUT_DIR"
