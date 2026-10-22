# The WebKitGTK cross build: Debian 12 (the box's release) on the laptop, with the armhf libraries
# WebKit links against installed next to the amd64 tools (multiarch), and clang-16, which Debian
# itself builds WebKit with on armhf. The native gcc is for the code generators, which run the
# preprocessor on JavaScript and IDL files. See build.sh.
FROM debian:bookworm

RUN sed -i 's/^Types: deb$/Types: deb deb-src/' /etc/apt/sources.list.d/debian.sources && \
    dpkg --add-architecture armhf && apt-get update && \
    DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends \
        crossbuild-essential-armhf build-essential clang-16 lld-16 cmake ninja-build pkg-config \
        ruby perl python3 gperf bison flex unifdef xz-utils ca-certificates dpkg-dev quilt patch && \
    DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends \
        libglib2.0-dev:armhf libharfbuzz-dev:armhf libcairo2-dev:armhf libdrm-dev:armhf \
        libfontconfig-dev:armhf libfreetype-dev:armhf libicu-dev:armhf libgcrypt20-dev:armhf \
        libhyphen-dev:armhf liblcms2-dev:armhf libmanette-0.2-dev:armhf libxslt1-dev:armhf \
        libxml2-dev:armhf libgtk-3-dev:armhf libsqlite3-dev:armhf \
        libsystemd-dev:armhf libgudev-1.0-dev:armhf libwoff-dev:armhf libavif-dev:armhf \
        libjpeg-dev:armhf libopenjp2-7-dev:armhf libpng-dev:armhf libtasn1-6-dev:armhf \
        libwebp-dev:armhf libxt-dev:armhf libgstreamer1.0-dev:armhf \
        libgstreamer-plugins-base1.0-dev:armhf libenchant-2-dev:armhf libsecret-1-dev:armhf \
        libegl-dev:armhf libgl-dev:armhf libgles-dev:armhf libseccomp-dev:armhf flite1-dev:armhf \
        libepoxy-dev:armhf \
        gir1.2-soup-3.0:armhf libbrotli-dev:armhf libnghttp2-dev:armhf libpsl-dev:armhf \
        libsoup-3.0-0:armhf zlib1g-dev:armhf && \
    # libsoup-3.0-dev needs libsysprof-capture-4-dev, which only libsysprof-4-dev provides, and
    # that pulls in gobject-introspection:armhf, which can't sit next to the amd64 tools. WebKit
    # is built without sysprof (as Debian does): only the headers and .pc files are needed.
    cd /tmp && apt-get download libsoup-3.0-dev:armhf libsysprof-4-dev:armhf && \
    dpkg -i --force-depends ./*.deb && rm ./*.deb && \
    rm -rf /var/lib/apt/lists/*
