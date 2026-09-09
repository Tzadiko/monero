# Reproducible release image for the Monero daemon: a StageX builder cross-builds
# monerod against contrib/depends for musl, and the runtime stage ships that one
# static binary from scratch.
#
# StageX is a full-source-bootstrapped, multi-signed, reproducible container
# toolchain. It is not hosted on GitHub, so the provenance of the pins below is
# recorded here rather than left to be guessed:
#
#   upstream repository    https://codeberg.org/stagex/stagex
#   read-only mirror       https://github.com/stagex-mirror/stagex
#   package index / docs   https://stagex.tools
#   artefact signatures    https://sigs.stagex.tools
#   images                 docker.io/stagex/<core|pallet|user>-<package>:<tag>
#
# Every image is pinned to a release tag *and* to the digest that tag resolved to.
# The digest is the trust anchor -- it is what Docker fetches and what makes the
# build reproducible -- while the tag is what makes the pin auditable: to tell
# whether sx2026.06.0 is still current, compare it with the release tags at
# https://codeberg.org/stagex/stagex/tags (mirrored on GitHub, and listed per
# repository on Docker Hub). Bump a pin by changing its tag and its digest
# together, never one without the other.
FROM stagex/pallet-gcc-gnu-gnu:sx2026.06.0@sha256:c06b4e5e490d7fdc77951510b5256f889a8bd51c6fa9d6c41c7f6c4cea88f35c AS builder
COPY --from=stagex/core-curl:sx2026.06.0@sha256:7a95abfe88eea0a7afd614d219e0b0f11fd77ce257046489baa0fbbf2fc6c088 . /
COPY --from=stagex/core-openssl:sx2026.06.0@sha256:5fbecea19913b8c9bb2e5976b03833d44f99c98290b17ee5ef9b469470b7bbff . /
COPY --from=stagex/core-ca-certificates:sx2026.06.0@sha256:ea7076d1bb83693fa4766c9cbd8132e59ac0981799fd8b58961c245cd360b66d . /
COPY --from=stagex/user-patch:sx2026.06.0@sha256:1d4428893f0ea9abfabc1fb5e365c5593fe10c6ed8ffc592d6528157a4299942 . /
COPY --from=stagex/core-cmake:sx2026.06.0@sha256:626a3fdf157efacd00c3ceb0529ae80dde1072d64fa1925aafe9819bebc92047 . /
COPY --from=stagex/core-ncurses:sx2026.06.0@sha256:90cc5d029c5073405f9db39c88b9509b8959bbd8f19d8cd02c20e9350cc40254 . /
COPY --from=stagex/pallet-rust:sx2026.06.0@sha256:59d4d0c9e232a05ecb99348f7216b521af1b914a430059dbdb9130018f2afde1 . /

ENV TARGET="x86_64-pc-linux-musl"
WORKDIR /monero
# .dockerignore keeps the host's build trees, contrib/depends outputs and logs out
# of this copy: a build/CMakeCache.txt from the host records absolute host paths
# and makes the cmake step below abort. Git history is copied in on purpose --
# cmake/GitVersion.cmake stamps the daemon's version string from it.
COPY . .

RUN make -C contrib/depends -j$(nproc) download-linux NO_WALLET=1 NO_READLINE=1

RUN make -C contrib/depends -j$(nproc) HOST="${TARGET}" NO_WALLET=1 NO_READLINE=1

RUN cmake --toolchain "contrib/depends/${TARGET}/share/toolchain.cmake" -S . -B build \
        -DSTACK_TRACE=OFF \
        -DUSE_READLINE=OFF \
        -DUSE_DEVICE_TREZOR=OFF \
        -DSTATIC_FLAGS="-static-pie" && \
    cmake --build build --target daemon --parallel $(nproc)

# The runtime stage is `FROM scratch`: no shell, no mkdir, no chown. Prepare the
# daemon's data directory here, with the ownership and mode it needs at runtime,
# and copy it in below -- copying the parent preserves the directory's own
# metadata, which copying an empty directory by name would not.
RUN mkdir -p /rootfs/data && \
    chmod 0700 /rootfs/data && \
    chown 65534:65534 /rootfs/data

FROM scratch

COPY --from=builder /monero/build/bin/monerod /
COPY --from=builder --chown=65534:65534 /rootfs/ /

EXPOSE 18080
EXPOSE 18081

# Run unprivileged. `scratch` ships no /etc/passwd, so the identity must be
# numeric; 65534:65534 is the conventional nobody:nogroup pair. Nothing here needs
# privilege: both ports above are unprivileged, and /data is owned by this user.
# A process running as a non-root uid holds no capabilities, and deployments
# should still add `--cap-drop=ALL --read-only`.
USER 65534:65534

# The only path the daemon writes (blockchain, p2p state, RPC SSL material, log).
# Declaring it a volume is what lets the image run with `--read-only`, and Docker
# seeds an anonymous volume from the directory in the image, so the volume
# inherits its 65534:65534 ownership.
VOLUME /data

# monerod derives its default data directory from $HOME (tools::get_default_data_dir,
# src/common/util.cpp), which is unset in a scratch image and would resolve to
# /.bitmonero on the read-only root filesystem. Point it at the volume so that an
# overridden CMD without --data-dir still writes somewhere writable.
ENV HOME=/data

# Liveness probe. The image holds one binary and no shell, so the probe is monerod
# itself in daemon-client mode: it performs an RPC round trip against the endpoint
# the default CMD binds and exits non-zero when the daemon does not answer.
# `print_pl_stats` is used rather than `status` because `status` reports success
# even when its request fails (src/daemon/rpc_command_executor.cpp), so it cannot
# tell a live daemon from a dead one. `--log-file=/dev/null` keeps the probe's own
# start-up lines out of the daemon's log file.
#
# The probe tracks the default CMD, so override it (`docker run --health-cmd`,
# compose `healthcheck.test`, or `--no-healthcheck`) when a deployment moves the
# RPC endpoint -- --testnet and --stagenet bind 28081 and 38081 -- or passes
# --restricted-rpc, which unregisters the /get_peer_list handler the probe calls
# (src/rpc/core_rpc_server.h). Under --rpc-login, set the same credentials in the
# container's RPC_LOGIN environment variable and the probe authenticates with them.
HEALTHCHECK --start-period=90s --interval=60s --timeout=15s --retries=3 \
    CMD ["/monerod", "--data-dir=/data", "--log-file=/dev/null", "--rpc-bind-ip=127.0.0.1", "--rpc-bind-port=18081", "print_pl_stats"]

ENTRYPOINT ["/monerod"]
CMD ["--data-dir=/data", "--p2p-bind-ip=0.0.0.0", "--p2p-bind-port=18080", "--rpc-bind-ip=0.0.0.0", "--rpc-bind-port=18081", "--non-interactive", "--confirm-external-bind"]
