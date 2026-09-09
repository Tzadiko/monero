package=protobuf
# The version, download path, file name and sha256 hash below are inherited from
# packages/native_protobuf.mk, which is the single source of truth for the protobuf pin.
# The SEC4-F18-protobuf-pin-age version-currency note lives there; do not duplicate it here.
$(package)_version=$(native_$(package)_version)
$(package)_version_protobuf_cpp=$(native_$(package)_version_protobuf_cpp)
$(package)_download_path=$(native_$(package)_download_path)
$(package)_file_name=$(native_$(package)_file_name)
$(package)_sha256_hash=$(native_$(package)_sha256_hash)
$(package)_dependencies=native_$(package)

define $(package)_set_vars
  $(package)_config_opts=--disable-shared --with-protoc=$(build_prefix)/bin/protoc
  $(package)_config_opts_linux=--with-pic
endef

define $(package)_config_cmds
  $($(package)_autoconf) AR_FLAGS=$($(package)_arflags)
endef

define $(package)_build_cmds
  $(MAKE) -C src libprotobuf.la
endef

define $(package)_stage_cmds
  $(MAKE) DESTDIR=$($(package)_staging_dir) -C src install-nobase_includeHEADERS &&\
  $(MAKE) DESTDIR=$($(package)_staging_dir) install-pkgconfigDATA &&\
  cp src/.libs/libprotobuf.a $($(package)_staging_prefix_dir)/lib/
endef
