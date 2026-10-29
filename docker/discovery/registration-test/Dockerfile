# The verified Linux alpha package was built against Ubuntu 24.04.
FROM ubuntu:24.04@sha256:008173c23f95b170204355c12626cb5a965d779a7e1283b09e9cffbb1bf33ca3
LABEL org.opencontainers.image.revision=4a92d877b45a937686e38b8c3b55aab4964c9584
LABEL org.opencontainers.image.version=1.0.0-alpha.2
ENV DEBIAN_FRONTEND=noninteractive
RUN apt-get update && apt-get install -y --no-install-recommends \
    ca-certificates curl python3 libluajit-5.1-2 \
    libboost-filesystem1.83.0 libboost-atomic1.83.0 libboost-program-options1.83.0 \
    libboost-iostreams1.83.0 libboost-system1.83.0 \
    libopenscenegraph161 libsdl2-2.0-0 libyaml-cpp0.8 libbullet3.24 \
    libsqlite3-0 libicu74 libcurl4t64 libsodium23 libprotobuf32t64 libssl3t64 \
    && dpkg-query -W > /package-versions.txt \
    && rm -rf /var/lib/apt/lists/* \
    && useradd --uid 10001 --create-home mwnet \
    && install -d -m 0700 -o 10001 -g 10001 /state
COPY package /opt/mwnet
COPY entrypoint.py verify_listing.py /opt/registration/
# Private staging may restrict source files to 0600/0700. Image code is public,
# root-owned and read-only at runtime; credentials live only in /state.
RUN printf '%s\n' 'config = "/state/xdg-config/openmw"' > /opt/mwnet/build/openmw.cfg \
    && find /opt/mwnet /opt/registration -type d -exec chmod 0755 {} + \
    && find /opt/mwnet /opt/registration -type f -exec chmod 0644 {} + \
    && chmod 0755 /opt/mwnet/build/mwnet-server /opt/mwnet/run-mwnet-server.sh \
    && ldd /opt/mwnet/build/mwnet-server > /runtime-libraries.txt \
    && cat /runtime-libraries.txt \
    && ! grep -q 'not found' /runtime-libraries.txt
USER 10001:10001
ENV MWNET_TEST_ROOT=/state MWNET_BUILD_DIR=/opt/mwnet/build
WORKDIR /state
# Fail during the build if the actual runtime user cannot access the package.
RUN test -x /opt/mwnet/build/mwnet-server \
    && test -x /opt/mwnet/run-mwnet-server.sh \
    && test -r /opt/mwnet/build/mwnet-server-default.cfg \
    && test -r /opt/mwnet/files/mwnet/core-scripts/scripts/serverCore.lua \
    && python3 -c 'import ast,pathlib; [ast.parse((pathlib.Path("/opt/registration") / n).read_text()) for n in ("entrypoint.py", "verify_listing.py")]' \
    && /opt/mwnet/run-mwnet-server.sh --prepare-only
STOPSIGNAL SIGINT
ENTRYPOINT ["python3", "/opt/registration/entrypoint.py"]
