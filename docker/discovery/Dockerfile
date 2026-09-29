# Base image and GameNetworkingSockets revision are pinned. Package versions are
# recorded in the image manifest alongside the source revision supplied by CI.
FROM ubuntu:24.04@sha256:008173c23f95b170204355c12626cb5a965d779a7e1283b09e9cffbb1bf33ca3 AS build
RUN apt-get update && apt-get install -y --no-install-recommends \
    build-essential cmake ninja-build git ca-certificates libboost-system-dev \
    libsodium-dev libssl-dev libprotobuf-dev protobuf-compiler libcurl4-openssl-dev libsqlite3-dev \
    && rm -rf /var/lib/apt/lists/*
WORKDIR /source
COPY CMakeLists.txt ./
COPY cmake cmake
COPY components components
COPY apps apps
RUN cmake -S . -B /build -G Ninja -DCMAKE_BUILD_TYPE=Release \
      -DMWNET_DIRECTORY_ONLY=ON -DMWNET_FETCH_DEPS=ON \
    && cmake --build /build --target mwnet-directory mwnet-discovery-tests -j2 \
    && ctest --test-dir /build -R '^mwnet-discovery$' --output-on-failure

FROM ubuntu:24.04@sha256:008173c23f95b170204355c12626cb5a965d779a7e1283b09e9cffbb1bf33ca3 AS runtime
ARG SOURCE_REVISION=unknown
LABEL org.opencontainers.image.revision=$SOURCE_REVISION
RUN apt-get update && apt-get install -y --no-install-recommends \
    ca-certificates curl libboost-system1.83.0 libsodium23 libssl3t64 libprotobuf32t64 libcurl4t64 libsqlite3-0 \
    && dpkg-query -W > /package-versions.txt && rm -rf /var/lib/apt/lists/* \
    && useradd --uid 10001 --create-home directory && mkdir /data && chown directory:directory /data
COPY --from=build /build/apps/mwnet-directory/mwnet-directory /usr/local/bin/
USER 10001:10001
WORKDIR /data
EXPOSE 8080
HEALTHCHECK --interval=30s --timeout=3s --retries=3 CMD curl --fail --silent http://127.0.0.1:8080/healthz || exit 1
ENTRYPOINT ["/usr/local/bin/mwnet-directory"]

FROM runtime AS test
USER root
RUN apt-get update && apt-get install -y --no-install-recommends python3 && rm -rf /var/lib/apt/lists/*
USER 10001:10001
COPY CI/run_mwnet_discovery_soak.py /usr/local/bin/run_mwnet_discovery_soak.py
COPY --from=build /build/apps/mwnet_tests/mwnet-discovery-tests /usr/local/bin/
ENTRYPOINT ["/usr/local/bin/mwnet-discovery-tests"]
