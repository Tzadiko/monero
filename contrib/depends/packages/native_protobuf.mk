package=native_protobuf
# SECURITY / VERSION-CURRENCY NOTE (reviewed 2026-09-08) - SEC4-F18-protobuf-pin-age
# This recipe is the single source of truth for the protobuf pin: the target recipe
# (packages/protobuf.mk) takes its version, download path, file name and hash from here.
# The pin is four years old - protobuf 21.12 / protobuf-cpp 3.21.12 was released 2022-12-14 -
# and is outside upstream's supported release window.
# Advisories: no advisory raised since 21.12 affects the C++ runtime monero links. The protobuf
#   CVEs of that era land on other implementations - CVE-2024-7254 on the Java full/lite and
#   Kotlin runtimes, CVE-2025-4565 on the pure-Python backend, CVE-2026-0994 on Python
#   json_format, CVE-2026-6409 on the PHP library. The one C++ issue, CVE-2022-1941
#   (MessageSet parsing, out-of-memory), was fixed in 3.21.6, below this pin. Age, not a known
#   C++ vulnerability, is the residual issue.
# Exposure here: protobuf is linked only on the optional Trezor hardware-wallet path. Measured
#   on the built artifacts: monero-wallet-cli, monero-wallet-rpc and monero-gen-trusted-multisig
#   import 84 libprotobuf symbols each; monerod and the other nine binaries import none. The
#   protoc built here is a `build`-type host code generator and ships in no artifact, so its
#   attack surface is the build host's own .proto inputs.
# Pin frozen deliberately: AAP 0.2.2 keeps every contrib/depends recipe version as pinned,
#   because changing a recipe alters reproducible-build inputs.
# Maintainer action when the freeze lifts: move to a supported protobuf release. The library pin
#   and the protoc pin are the same pin and must move together, and $(package)_version,
#   $(package)_version_protobuf_cpp and $(package)_sha256_hash all change with it.
$(package)_version=21.12
$(package)_version_protobuf_cpp=3.21.12
$(package)_download_path=https://github.com/protocolbuffers/protobuf/releases/download/v$($(package)_version)/
$(package)_file_name=protobuf-cpp-$($(package)_version_protobuf_cpp).tar.gz
$(package)_sha256_hash=4eab9b524aa5913c6fffb20b2a8abf5ef7f95a80bc0701f3a6dbb4c607f73460

define $(package)_set_vars
  $(package)_config_opts=--disable-shared --prefix=$(build_prefix)
  $(package)_config_opts_linux=--with-pic
  $(package)_cxxflags+=-g0
endef

define $(package)_config_cmds
  $($(package)_autoconf)
endef

define $(package)_build_cmds
  $(MAKE) -C src protoc
endef

define $(package)_stage_cmds
  $(MAKE) DESTDIR=$($(package)_staging_dir) -C src install-binPROGRAMS install-nobase_dist_protoDATA
endef

define $(package)_postprocess_cmds
  rm -rf lib/
endef
