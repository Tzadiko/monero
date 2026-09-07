.NOTPARALLEL :

# Pattern rule to print variables, e.g. make print-all_packages
print-%: FORCE
	@echo '$($*)'

SOURCES_PATH ?= $(BASEDIR)/sources
BASE_CACHE ?= $(BASEDIR)/built
FALLBACK_DOWNLOAD_PATH ?= https://downloads.getmonero.org/depends-sources

# The language standards the target packages and the first-party tree are built
# to. These name the standards, not the -std= spellings that reach a compiler
# command line: the C++ spelling is worked out from the host compiler below, see
# cxx_standard_flag.
C_STANDARD ?= c11
CXX_STANDARD ?= c++23

NO_WALLET ?=
NO_READLINE ?=

BUILD = $(shell ./config.guess)
HOST ?= $(BUILD)
PATCHES_PATH = $(BASEDIR)/patches
BASEDIR = $(CURDIR)
HASH_LENGTH:=11
DOWNLOAD_CONNECT_TIMEOUT:=30
DOWNLOAD_RETRIES:=5
HOST_ID_SALT ?= salt
BUILD_ID_SALT ?= salt

host:=$(BUILD)
ifneq ($(HOST),)
host:=$(HOST)
host_toolchain:=$(HOST)-
endif

ifneq ($(DEBUG),)
release_type=debug
else
release_type=release
endif

ifneq ($(TESTS),)
build_tests=ON
release_type=debug
else
build_tests=OFF
endif

ifeq ($(release_type),debug)
cmake_release_type=Debug
else
cmake_release_type=Release
endif

base_build_dir=$(BASEDIR)/work/build
base_staging_dir=$(BASEDIR)/work/staging
base_download_dir=$(BASEDIR)/work/download
canonical_host:=$(shell ./config.sub $(HOST))
build:=$(shell ./config.sub $(BUILD))

build_arch =$(firstword $(subst -, ,$(build)))
build_vendor=$(word 2,$(subst -, ,$(build)))
full_build_os:=$(subst $(build_arch)-$(build_vendor)-,,$(build))
build_os:=$(findstring linux,$(full_build_os))
build_os+=$(findstring darwin,$(full_build_os))
build_os:=$(strip $(build_os))
ifeq ($(build_os),)
build_os=$(full_build_os)
endif

host_arch=$(firstword $(subst -, ,$(canonical_host)))
host_vendor=$(word 2,$(subst -, ,$(canonical_host)))
full_host_os:=$(subst $(host_arch)-$(host_vendor)-,,$(canonical_host))
host_os:=$(findstring android,$(full_host_os))
ifeq ($(host_os),)
host_os:=$(findstring linux,$(full_host_os))
endif
host_os+=$(findstring darwin,$(full_host_os))
host_os+=$(findstring freebsd,$(full_host_os))
host_os+=$(findstring mingw32,$(full_host_os))
host_os:=$(strip $(host_os))
ifeq ($(host_os),)
host_os=$(full_host_os)
endif

$(host_arch)_$(host_os)_prefix=$(BASEDIR)/$(host)
$(host_arch)_$(host_os)_host=$(host)
host_prefix=$($(host_arch)_$(host_os)_prefix)
build_prefix=$(host_prefix)/native

AT_$(V):=
AT_:=@
AT:=$(AT_$(V))

all: install

include hosts/$(host_os).mk
include hosts/default.mk
include builders/$(build_os).mk
include builders/default.mk
include packages/packages.mk

# The -std= spelling of CXX_STANDARD that the host compiler accepts. A standard
# can have more than one spelling: Clang 13 to 16 know C++23 only by its
# provisional name, -std=c++2b, and reject -std=c++23 outright, while GCC (from
# 11) and Clang 17 or newer accept both. Clang 16 is a supported compiler (see
# docs/COMPILING_DEBUGGING_TESTING.md, "Toolchain requirements"), so emitting the
# canonical name unconditionally would fail every target C++ package - Boost,
# ZeroMQ, protobuf - before the first-party build even starts. The
# canonical name is preferred and the provisional one used only as a fallback.
# CMake's native-build spelling is version- and platform-dependent, but both
# spellings select C++23.
#
# A host compiler that cannot be probed yet, such as a cross toolchain this run
# has still to unpack, keeps the canonical name. That is the same answer the
# probe returns once the toolchain is in place for every supported compiler
# except Clang 16, so the spelling does not flip between runs and cannot churn
# the build ids it feeds.
#
# This has to come after the host and builder profiles, for two reasons:
# builders/darwin.mk replaces darwin_CXX with the xcrun compiler and the probe
# must ask the compiler the flag will actually be given to, and the host flags
# the profiles contribute are only assembled once hosts/default.mk has run.
#
# The pairs below read <canonical spelling>/<provisional spelling>.
cxx_standard_provisional := c++23/c++2b c++20/c++2a
cxx_standard_spellings := $(CXX_STANDARD) $(patsubst $(CXX_STANDARD)/%,%,$(filter $(CXX_STANDARD)/%,$(cxx_standard_provisional)))
cxx_standard_flag := $(firstword \
    $(shell for std in $(cxx_standard_spellings); do \
              $(host_CXX) -std=$$std -x c++ -fsyntax-only /dev/null >/dev/null 2>&1 && { echo $$std; break; }; \
            done) \
    $(CXX_STANDARD))

# The host profiles spell the flag -std=$(CXX_STANDARD); the accepted spelling
# takes its place here, once, in the assembled target flags that every target
# package recipe and the generated toolchain.cmake read. When the two spellings
# are the same - every compiler that accepts the canonical name - this
# substitutes nothing and the flags are exactly what the profile wrote. Only the
# target flags are rewritten: native packages take build_CXXFLAGS, which names
# no standard, and are built by the build compiler at its default dialect.
$(host_arch)_$(host_os)_CXXFLAGS := $(subst -std=$(CXX_STANDARD),-std=$(cxx_standard_flag),$($(host_arch)_$(host_os)_CXXFLAGS))

ifeq ($(GUIX_ENVIRONMENT),)
build_id_string:=$(BUILD_ID_SALT)
build_id_string+=$(shell $(build_CC) --version 2>/dev/null)
build_id_string+=$(shell $(build_AR) --version 2>/dev/null)
build_id_string+=$(shell $(build_CXX) --version 2>/dev/null)
build_id_string+=$(shell $(build_RANLIB) --version 2>/dev/null)
build_id_string+=$(shell $(build_STRIP) --version 2>/dev/null)

$(host_arch)_$(host_os)_id_string:=$(HOST_ID_SALT)
$(host_arch)_$(host_os)_id_string+=$(shell $(host_CC) --version 2>/dev/null)
$(host_arch)_$(host_os)_id_string+=$(shell $(host_AR) --version 2>/dev/null)
$(host_arch)_$(host_os)_id_string+=$(shell $(host_CXX) --version 2>/dev/null)
$(host_arch)_$(host_os)_id_string+=$(shell $(host_RANLIB) --version 2>/dev/null)
$(host_arch)_$(host_os)_id_string+=$(shell $(host_STRIP) --version 2>/dev/null)
else
build_id_string:=$(realpath $(GUIX_ENVIRONMENT))
$(host_arch)_$(host_os)_id_string:=$(realpath $(GUIX_ENVIRONMENT))
endif

# The dialect the host compiler is invoked with is part of the host toolchain's
# identity, on the Guix path as much as the plain one. C_STANDARD, CXX_STANDARD
# and the spelling probed above all reach host_CFLAGS/host_CXXFLAGS, and so every
# target package's compiler command line, while leaving every recipe file
# byte-identical. Without them here a run that overrides a standard computes the
# same package build ids, the same cached archive names and the same
# final_build_id as a run that does not, and silently reuses dependencies - and a
# generated share/toolchain.cmake - compiled to the other standard while exiting
# successfully. Only the host (target) id string is extended: native packages are
# built with the build compiler's default dialect, build_CXXFLAGS carrying no
# -std=, so their ids must not move.
$(host_arch)_$(host_os)_id_string += -std=$(C_STANDARD) -std=$(cxx_standard_flag)

readline_packages_$(NO_READLINE) = $(readline_packages)

wallet_packages_$(NO_WALLET) = $(wallet_packages)
wallet_native_packages_$(NO_WALLET) = $(wallet_native_packages)

packages += $($(host_arch)_$(host_os)_packages) $($(host_os)_packages) $(wallet_packages_) $(readline_packages_)
native_packages += $($(host_arch)_$(host_os)_native_packages) $($(host_os)_native_packages) $(wallet_native_packages_)

all_packages = $(packages) $(native_packages)

meta_depends = Makefile funcs.mk builders/default.mk hosts/default.mk hosts/$(host_os).mk builders/$(build_os).mk

$(host_arch)_$(host_os)_native_toolchain?=$($(host_os)_native_toolchain)

include funcs.mk

toolchain_path=$($($(host_arch)_$(host_os)_native_toolchain)_prefixbin)
final_build_id_long+=:[sha256sum]:$(shell $(build_SHA256SUM) toolchain.cmake.in)
final_build_id+=$(shell echo -n "$(final_build_id_long)" | $(build_SHA256SUM) | cut -c-$(HASH_LENGTH))
$(host_prefix)/.stamp_$(final_build_id): $(native_packages) $(packages)
	$(AT)rm -rf $(@D)
	$(AT)mkdir -p $(@D)
	$(AT)echo copying packages: $^
	$(AT)echo to: $(@D)
	$(AT)cd $(@D); $(foreach package,$^, tar xf $($(package)_cached); )
	$(AT)touch $@

$(host_prefix)/share/toolchain.cmake : toolchain.cmake.in $(host_prefix)/.stamp_$(final_build_id)
	$(AT)@mkdir -p $(@D)
	$(AT)sed -e 's|@host@|$(host)|' \
            -e 's|@CC@|$(host_CC)|' \
            -e 's|@CXX@|$(host_CXX)|' \
            -e 's|@AR@|$(toolchain_path)$(host_AR)|' \
            -e 's|@RANLIB@|$(toolchain_path)$(host_RANLIB)|' \
            -e 's|@NM@|$(toolchain_path)$(host_NM)|' \
            -e 's|@STRIP@|$(toolchain_path)$(host_STRIP)|' \
            -e 's|@build_os@|$(build_os)|' \
            -e 's|@host_os@|$(host_os)|' \
            -e 's|@CFLAGS@|$(strip $(host_CFLAGS) $(host_$(release_type)_CFLAGS))|' \
            -e 's|@CXXFLAGS@|$(strip $(host_CXXFLAGS) $(host_$(release_type)_CXXFLAGS))|' \
            -e 's|@CPPFLAGS@|$(strip $(host_CPPFLAGS) $(host_$(release_type)_CPPFLAGS))|' \
            -e 's|@LDFLAGS@|$(strip $(host_LDFLAGS) $(host_$(release_type)_LDFLAGS))|' \
            -e 's|@allow_host_packages@|$(ALLOW_HOST_PACKAGES)|' \
            -e 's|@debug@|$(DEBUG)|' \
            -e 's|@release_type@|$(cmake_release_type)|' \
            -e 's|@build_tests@|$(build_tests)|' \
            -e 's|@cmake_system_name@|$($(host_os)_cmake_system)|' \
            -e 's|@prefix@|$($(host_arch)_$(host_os)_prefix)|'\
            -e 's|@arch@|$(host_arch)|'\
            $< > $@
	$(AT)touch $@

define check_or_remove_cached
  mkdir -p $(BASE_CACHE)/$(host)/$(package) && cd $(BASE_CACHE)/$(host)/$(package); \
  $(build_SHA256SUM) -c $($(package)_cached_checksum) >/dev/null 2>/dev/null || \
  ( rm -f $($(package)_cached_checksum); \
    if test -f "$($(package)_cached)"; then echo "Checksum mismatch for $(package). Forcing rebuild.."; rm -f $($(package)_cached_checksum) $($(package)_cached); fi )
endef

define check_or_remove_sources
  mkdir -p $($(package)_source_dir); cd $($(package)_source_dir); \
  test -f $($(package)_fetched) && ( $(build_SHA256SUM) -c $($(package)_fetched) >/dev/null 2>/dev/null || \
    ( echo "Checksum missing or mismatched for $(package) source. Forcing re-download."; \
      rm -f $($(package)_all_sources) $($(1)_fetched))) || true
endef

check-packages:
	@$(foreach package,$(all_packages),$(call check_or_remove_cached,$(package));)
check-sources:
	@$(foreach package,$(all_packages),$(call check_or_remove_sources,$(package));)

# Regression check for the target dialect. When the host compiler is runnable,
# it asserts that the -std= spelling emitted for this host is a spelling of
# CXX_STANDARD that the compiler accepts and that it selects the dialect asked
# for. It always asserts that the effective dialect is part of the build
# identity: two dialects must not share a cached archive name, a final_build_id
# or the final stamp that name them, the same dialect must reproduce all of
# those, the ids of native packages must not move with the target dialect, and
# each of those must hold on the Guix path too.
# It builds and downloads nothing. If the host compiler is unavailable, the
# compiler-acceptance check is skipped while the build-identity checks still run.
# It writes nothing - it re-reads variables and asks the host compiler about one
# flag - so it is safe to run anywhere, and it takes seconds.
#
#   make check-dialect                              this build's host
#   make check-dialect HOST=x86_64-w64-mingw32      a cross host
#   make check-dialect x86_64_linux_CXX=clang++-16  another host compiler, e.g.
#                                                   to check the spelling a
#                                                   Clang 16 is given
#
# The goals below print the cached archive names (each carries its package's
# build id), then final_build_id, then the native package ids, in that order,
# so the recipe can split the output by the counts it knows.
check_dialect_goals = $(foreach p,$(packages),print-$(p)_cached) print-final_build_id \
                      $(foreach p,$(native_packages),print-$(p)_build_id)

check-dialect: FORCE
	@set -eu; \
	fail() { echo "check-dialect: FAIL: $$1" >&2; exit 1; }; \
	target_lines=$$(( $(words $(packages)) + 1 )); native_lines=$(words $(native_packages)); \
	echo "check-dialect: $(host): CXX_STANDARD=$(CXX_STANDARD) emitted as -std=$(cxx_standard_flag)"; \
	case " $(cxx_standard_spellings) " in \
	  *" $(cxx_standard_flag) "*) ;; \
	  *) fail "-std=$(cxx_standard_flag) is not a spelling of $(CXX_STANDARD)";; \
	esac; \
	case "$(host_CXXFLAGS)" in \
	  *-std=$(cxx_standard_flag)*) ;; \
	  *) fail "host_CXXFLAGS does not carry -std=$(cxx_standard_flag): $(host_CXXFLAGS)";; \
	esac; \
	case '$(CXX_STANDARD)' in \
	  c++23) probe='static_assert(__cplusplus > 202002L, "older than C++23");';; \
	  *) probe='';; \
	esac; \
	if : | $(host_CXX) -x c++ -fsyntax-only - >/dev/null 2>&1; then \
	  out=$$(printf '%s\n' "$$probe" | $(host_CXX) -std=$(cxx_standard_flag) -x c++ -fsyntax-only - 2>&1) \
	    || fail "the host compiler rejects -std=$(cxx_standard_flag) or does not select $(CXX_STANDARD): $$out"; \
	  echo "check-dialect: the host compiler accepts -std=$(cxx_standard_flag) and selects $(CXX_STANDARD)"; \
	else \
	  echo "check-dialect: host compiler not runnable yet, compiler check skipped"; \
	fi; \
	for guix in "" "$(BASEDIR)"; do \
	  a=$$(GUIX_ENVIRONMENT="$$guix" $(MAKE) -s --no-print-directory CXX_STANDARD=c++23 $(check_dialect_goals)); \
	  b=$$(GUIX_ENVIRONMENT="$$guix" $(MAKE) -s --no-print-directory CXX_STANDARD=c++20 $(check_dialect_goals)); \
	  c=$$(GUIX_ENVIRONMENT="$$guix" $(MAKE) -s --no-print-directory CXX_STANDARD=c++23 $(check_dialect_goals)); \
	  where=$${guix:+Guix }; where=$${where:-plain }; \
	  test "$$a" = "$$c" || fail "$${where}path: two identical runs computed different ids"; \
	  test "$$(printf '%s\n' "$$a" | head -n $$target_lines)" \
	    != "$$(printf '%s\n' "$$b" | head -n $$target_lines)" \
	    || fail "$${where}path: c++23 and c++20 share their archive names and final_build_id"; \
	  test "$$(printf '%s\n' "$$a" | tail -n $$native_lines)" \
	     = "$$(printf '%s\n' "$$b" | tail -n $$native_lines)" \
	    || fail "$${where}path: native package ids moved with the target dialect"; \
	  echo "check-dialect: $${where}path: dialect reaches the target identity, native ids unmoved, runs reproducible"; \
	done; \
	echo "check-dialect: PASS"

$(host_prefix)/share/toolchain.cmake: check-packages

check-packages: check-sources

install: check-packages $(host_prefix)/share/toolchain.cmake

download-one: check-sources $(all_sources)

download-osx:
	@$(MAKE) -s HOST=x86_64-apple-darwin download-one
download-linux:
	@$(MAKE) -s HOST=x86_64-unknown-linux-gnu download-one
download-win:
	@$(MAKE) -s HOST=x86_64-w64-mingw32 download-one
download-freebsd:
	@$(MAKE) -s HOST=x86_64-unknown-freebsd download-one
download-android:
	@$(MAKE) -s HOST=aarch64-linux-android download-one
download: download-osx download-linux download-win download-freebsd download-android

 $(foreach package,$(all_packages),$(eval $(call ext_add_stages,$(package))))

.PHONY: install cached download-one download-osx download-linux download-win download-freebsd download-android download check-packages check-sources check-dialect
.PHONY: FORCE
