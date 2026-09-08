// Copyright (c) 2017-2026, The Monero Project
//
// All rights reserved.
//
// Redistribution and use in source and binary forms, with or without modification, are
// permitted provided that the following conditions are met:
//
// 1. Redistributions of source code must retain the above copyright notice, this list of
//    conditions and the following disclaimer.
//
// 2. Redistributions in binary form must reproduce the above copyright notice, this list
//    of conditions and the following disclaimer in the documentation and/or other
//    materials provided with the distribution.
//
// 3. Neither the name of the copyright holder nor the names of its contributors may be
//    used to endorse or promote products derived from this software without specific
//    prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND ANY
// EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
// MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL
// THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
// SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
// PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
// INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
// STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF
// THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

#include <cstdint>
#include <gtest/gtest.h>
#include <memory>
#include <sstream>
#include <string>
#include <type_traits>

extern "C"
{
#include "crypto/crypto-ops.h"
}
#include "crypto/generators.h"
#include "cryptonote_basic/merge_mining.h"
#include "fcmp_pp/fcmp_pp_crypto.h"
#include "fcmp_pp/tower_cycle.h"
#include "ringct/rctOps.h"
#include "ringct/rctSigs.h"
#include "ringct/rctTypes.h"
#include "string_tools.h"

namespace
{
  static constexpr const std::uint8_t source[] = {
    0x8b, 0x65, 0x59, 0x70, 0x15, 0x37, 0x99, 0xaf, 0x2a, 0xea, 0xdc, 0x9f, 0xf1, 0xad, 0xd0, 0xea,
    0x6c, 0x72, 0x51, 0xd5, 0x41, 0x54, 0xcf, 0xa9, 0x2c, 0x17, 0x3a, 0x0d, 0xd3, 0x9c, 0x1f, 0x94,
    0x6c, 0x72, 0x51, 0xd5, 0x41, 0x54, 0xcf, 0xa9, 0x2c, 0x17, 0x3a, 0x0d, 0xd3, 0x9c, 0x1f, 0x94,
    0x8b, 0x65, 0x59, 0x70, 0x15, 0x37, 0x99, 0xaf, 0x2a, 0xea, 0xdc, 0x9f, 0xf1, 0xad, 0xd0, 0xea
  };

  static constexpr const char expected[] =
    "8b655970153799af2aeadc9ff1add0ea6c7251d54154cfa92c173a0dd39c1f94"
    "6c7251d54154cfa92c173a0dd39c1f948b655970153799af2aeadc9ff1add0ea";

  template<typename T>
  bool is_formatted()
  {
    T value{};

    static_assert(alignof(T) == 1, "T must have 1 byte alignment");
    static_assert(sizeof(T) <= sizeof(source), "T is too large for source");
    static_assert(sizeof(T) * 2 <= sizeof(expected), "T is too large for destination");
    static_assert(std::has_unique_object_representations_v<T>);
    std::memcpy(std::addressof(value), source, sizeof(T));

    std::stringstream out;
    out << "BEGIN" << value << "END";  
    return out.str() == "BEGIN<" + std::string{expected, sizeof(T) * 2} + ">END";
  }

  void random_fe(fe rand_fe)
  {
    unsigned char s[32];
    crypto::random32_unbiased(s);
    if (fe_frombytes_vartime(rand_fe, s) != 0)
      throw std::runtime_error("invalid random fe");
  }

  // The FCMP++ Rust FFI consumes a coordinate as 32 little-endian bytes of a canonical
  // Ed25519 field element (strictly less than p = 2^255 - 19), which is exactly the
  // layout of crypto::ec_coord that fcmp_pp::point_to_wei_x_y fills in on the
  // production path. This lets a test state such a coordinate byte by byte.
  crypto::ec_coord ec_coord_from_le_bytes(const unsigned char (&le_bytes)[32])
  {
    crypto::ec_coord coord;
    memcpy(to_bytes(coord), le_bytes, sizeof(le_bytes));
    return coord;
  }

  // fcmp++.h documents SeleneScalar as opaque to the C/C++ side (it carries a Montgomery
  // residue of the input, not the input bytes), so bit-identity of two returned values is
  // the only property a C++ test may assert about it.
  bool selene_scalars_equal(const SeleneScalar &a, const SeleneScalar &b)
  {
    return memcmp(&a, &b, sizeof(SeleneScalar)) == 0;
  }
}

TEST(Crypto, Ostream)
{
  EXPECT_TRUE(is_formatted<crypto::hash8>());
  EXPECT_TRUE(is_formatted<crypto::hash>());
  EXPECT_TRUE(is_formatted<crypto::public_key>());
  EXPECT_TRUE(is_formatted<crypto::signature>());
  EXPECT_TRUE(is_formatted<crypto::key_derivation>());
  EXPECT_TRUE(is_formatted<crypto::key_image>());
  EXPECT_TRUE(is_formatted<rct::key>());
}

TEST(Crypto, null_keys)
{
  char zero[32];
  memset(zero, 0, 32);
  ASSERT_EQ(memcmp(crypto::null_skey.data, zero, 32), 0);
  ASSERT_EQ(memcmp(crypto::null_pkey.data, zero, 32), 0);
}

TEST(Crypto, verify_32)
{
  // all bytes are treated the same, so we can brute force just one byte
  unsigned char k0[32] = {0}, k1[32] = {0};
  for (unsigned int i0 = 0; i0 < 256; ++i0)
  {
    k0[0] = i0;
    for (unsigned int i1 = 0; i1 < 256; ++i1)
    {
      k1[0] = i1;
      ASSERT_EQ(!crypto_verify_32(k0, k1), i0 == i1);
    }
  }
}

TEST(Crypto, tree_branch)
{
  crypto::hash inputs[6];
  crypto::hash branch[8];
  crypto::hash branch_1[8 + 1];
  crypto::hash root, root2;
  size_t depth;
  uint32_t path, path2;

  auto hasher = [](const crypto::hash &h0, const crypto::hash &h1) -> crypto::hash
  {
    char buffer[64];
    memcpy(buffer, &h0, 32);
    memcpy(buffer + 32, &h1, 32);
    crypto::hash res;
    cn_fast_hash(buffer, 64, res);
    return res;
  };

  for (int n = 0; n < 6; ++n)
  {
    memset(&inputs[n], 0, 32);
    inputs[n].data[0] = n + 1;
  }

  // empty
  ASSERT_FALSE(crypto::tree_branch((const char(*)[32])inputs, 0, crypto::null_hash.data, (char(*)[32])branch, &depth, &path));

  // one, matching
  ASSERT_TRUE(crypto::tree_branch((const char(*)[32])inputs, 1, inputs[0].data, (char(*)[32])branch, &depth, &path));
  ASSERT_EQ(depth, 0);
  ASSERT_EQ(path, 0);
  ASSERT_TRUE(crypto::tree_path(1, 0, &path2));
  ASSERT_EQ(path, path2);
  crypto::tree_hash((const char(*)[32])inputs, 1, root.data);
  ASSERT_EQ(root, inputs[0]);
  ASSERT_TRUE(crypto::is_branch_in_tree(inputs[0].data, root.data, (const char(*)[32])branch, depth, path));
  ASSERT_FALSE(crypto::is_branch_in_tree(inputs[1].data, root.data, (const char(*)[32])branch, depth, path));
  ASSERT_FALSE(crypto::is_branch_in_tree(crypto::null_hash.data, root.data, (const char(*)[32])branch, depth, path));

  // one, not found
  ASSERT_FALSE(crypto::tree_branch((const char(*)[32])inputs, 1, inputs[1].data, (char(*)[32])branch, &depth, &path));

  // two, index 0
  ASSERT_TRUE(crypto::tree_branch((const char(*)[32])inputs, 2, inputs[0].data, (char(*)[32])branch, &depth, &path));
  ASSERT_EQ(depth, 1);
  ASSERT_EQ(path, 0);
  ASSERT_TRUE(crypto::tree_path(2, 0, &path2));
  ASSERT_EQ(path, path2);
  ASSERT_EQ(branch[0], inputs[1]);
  crypto::tree_hash((const char(*)[32])inputs, 2, root.data);
  ASSERT_EQ(root, hasher(inputs[0], inputs[1]));
  ASSERT_TRUE(crypto::is_branch_in_tree(inputs[0].data, root.data, (const char(*)[32])branch, depth, path));
  ASSERT_FALSE(crypto::is_branch_in_tree(inputs[1].data, root.data, (const char(*)[32])branch, depth, path));
  ASSERT_FALSE(crypto::is_branch_in_tree(inputs[2].data, root.data, (const char(*)[32])branch, depth, path));
  ASSERT_FALSE(crypto::is_branch_in_tree(crypto::null_hash.data, root.data, (const char(*)[32])branch, depth, path));

  // two, index 1
  ASSERT_TRUE(crypto::tree_branch((const char(*)[32])inputs, 2, inputs[1].data, (char(*)[32])branch, &depth, &path));
  ASSERT_EQ(depth, 1);
  ASSERT_EQ(path, 1);
  ASSERT_TRUE(crypto::tree_path(2, 1, &path2));
  ASSERT_EQ(path, path2);
  ASSERT_EQ(branch[0], inputs[0]);
  crypto::tree_hash((const char(*)[32])inputs, 2, root.data);
  ASSERT_EQ(root, hasher(inputs[0], inputs[1]));
  ASSERT_FALSE(crypto::is_branch_in_tree(inputs[0].data, root.data, (const char(*)[32])branch, depth, path));
  ASSERT_TRUE(crypto::is_branch_in_tree(inputs[1].data, root.data, (const char(*)[32])branch, depth, path));
  ASSERT_FALSE(crypto::is_branch_in_tree(inputs[2].data, root.data, (const char(*)[32])branch, depth, path));
  ASSERT_FALSE(crypto::is_branch_in_tree(crypto::null_hash.data, root.data, (const char(*)[32])branch, depth, path));

  // two, not found
  ASSERT_FALSE(crypto::tree_branch((const char(*)[32])inputs, 2, inputs[2].data, (char(*)[32])branch, &depth, &path));

  // a b c 0
  //  x   y
  //    z

  // three, index 0
  ASSERT_TRUE(crypto::tree_branch((const char(*)[32])inputs, 3, inputs[0].data, (char(*)[32])branch, &depth, &path));
  ASSERT_GE(depth, 1);
  ASSERT_LE(depth, 2);
  ASSERT_TRUE(crypto::tree_path(3, 0, &path2));
  ASSERT_EQ(path, path2);
  crypto::tree_hash((const char(*)[32])inputs, 3, root.data);
  ASSERT_EQ(root, hasher(inputs[0], hasher(inputs[1], inputs[2])));
  ASSERT_TRUE(crypto::is_branch_in_tree(inputs[0].data, root.data, (const char(*)[32])branch, depth, path));
  ASSERT_FALSE(crypto::is_branch_in_tree(inputs[1].data, root.data, (const char(*)[32])branch, depth, path));
  ASSERT_FALSE(crypto::is_branch_in_tree(inputs[2].data, root.data, (const char(*)[32])branch, depth, path));
  ASSERT_FALSE(crypto::is_branch_in_tree(inputs[3].data, root.data, (const char(*)[32])branch, depth, path));
  ASSERT_FALSE(crypto::is_branch_in_tree(crypto::null_hash.data, root.data, (const char(*)[32])branch, depth, path));

  // three, index 1
  ASSERT_TRUE(crypto::tree_branch((const char(*)[32])inputs, 3, inputs[1].data, (char(*)[32])branch, &depth, &path));
  ASSERT_GE(depth, 1);
  ASSERT_LE(depth, 2);
  ASSERT_TRUE(crypto::tree_path(3, 1, &path2));
  ASSERT_EQ(path, path2);
  crypto::tree_hash((const char(*)[32])inputs, 3, root.data);
  ASSERT_EQ(root, hasher(inputs[0], hasher(inputs[1], inputs[2])));
  ASSERT_FALSE(crypto::is_branch_in_tree(inputs[0].data, root.data, (const char(*)[32])branch, depth, path));
  ASSERT_TRUE(crypto::is_branch_in_tree(inputs[1].data, root.data, (const char(*)[32])branch, depth, path));
  ASSERT_FALSE(crypto::is_branch_in_tree(inputs[2].data, root.data, (const char(*)[32])branch, depth, path));
  ASSERT_FALSE(crypto::is_branch_in_tree(inputs[3].data, root.data, (const char(*)[32])branch, depth, path));
  ASSERT_FALSE(crypto::is_branch_in_tree(crypto::null_hash.data, root.data, (const char(*)[32])branch, depth, path));

  // three, index 2
  ASSERT_TRUE(crypto::tree_branch((const char(*)[32])inputs, 3, inputs[2].data, (char(*)[32])branch, &depth, &path));
  ASSERT_GE(depth, 1);
  ASSERT_LE(depth, 2);
  ASSERT_TRUE(crypto::tree_path(3, 2, &path2));
  ASSERT_EQ(path, path2);
  crypto::tree_hash((const char(*)[32])inputs, 3, root.data);
  ASSERT_FALSE(crypto::is_branch_in_tree(inputs[0].data, root.data, (const char(*)[32])branch, depth, path));
  ASSERT_FALSE(crypto::is_branch_in_tree(inputs[1].data, root.data, (const char(*)[32])branch, depth, path));
  ASSERT_TRUE(crypto::is_branch_in_tree(inputs[2].data, root.data, (const char(*)[32])branch, depth, path));
  ASSERT_FALSE(crypto::is_branch_in_tree(inputs[3].data, root.data, (const char(*)[32])branch, depth, path));
  ASSERT_FALSE(crypto::is_branch_in_tree(crypto::null_hash.data, root.data, (const char(*)[32])branch, depth, path));
  ASSERT_TRUE(crypto::tree_branch_hash(inputs[2].data, (const char(*)[32])branch, depth, path, root2.data));
  ASSERT_EQ(root, root2);

  // three, not found
  ASSERT_FALSE(crypto::tree_branch((const char(*)[32])inputs, 3, inputs[3].data, (char(*)[32])branch, &depth, &path));

  // a b c d e 0 0 0
  //    x   y
  //      z
  //    w

  // five, index 0
  ASSERT_TRUE(crypto::tree_branch((const char(*)[32])inputs, 5, inputs[0].data, (char(*)[32])branch, &depth, &path));
  ASSERT_GE(depth, 2);
  ASSERT_LE(depth, 3);
  ASSERT_TRUE(crypto::tree_path(5, 0, &path2));
  ASSERT_EQ(path, path2);
  crypto::tree_hash((const char(*)[32])inputs, 5, root.data);
  ASSERT_TRUE(crypto::is_branch_in_tree(inputs[0].data, root.data, (const char(*)[32])branch, depth, path));
  ASSERT_FALSE(crypto::is_branch_in_tree(inputs[1].data, root.data, (const char(*)[32])branch, depth, path));
  ASSERT_FALSE(crypto::is_branch_in_tree(inputs[2].data, root.data, (const char(*)[32])branch, depth, path));
  ASSERT_FALSE(crypto::is_branch_in_tree(inputs[3].data, root.data, (const char(*)[32])branch, depth, path));
  ASSERT_FALSE(crypto::is_branch_in_tree(inputs[4].data, root.data, (const char(*)[32])branch, depth, path));
  ASSERT_FALSE(crypto::is_branch_in_tree(inputs[5].data, root.data, (const char(*)[32])branch, depth, path));
  ASSERT_FALSE(crypto::is_branch_in_tree(crypto::null_hash.data, root.data, (const char(*)[32])branch, depth, path));

  // five, index 1
  ASSERT_TRUE(crypto::tree_branch((const char(*)[32])inputs, 5, inputs[1].data, (char(*)[32])branch, &depth, &path));
  ASSERT_GE(depth, 2);
  ASSERT_LE(depth, 3);
  ASSERT_TRUE(crypto::tree_path(5, 1, &path2));
  ASSERT_EQ(path, path2);
  crypto::tree_hash((const char(*)[32])inputs, 5, root.data);
  ASSERT_FALSE(crypto::is_branch_in_tree(inputs[0].data, root.data, (const char(*)[32])branch, depth, path));
  ASSERT_TRUE(crypto::is_branch_in_tree(inputs[1].data, root.data, (const char(*)[32])branch, depth, path));
  ASSERT_FALSE(crypto::is_branch_in_tree(inputs[2].data, root.data, (const char(*)[32])branch, depth, path));
  ASSERT_FALSE(crypto::is_branch_in_tree(inputs[3].data, root.data, (const char(*)[32])branch, depth, path));
  ASSERT_FALSE(crypto::is_branch_in_tree(inputs[4].data, root.data, (const char(*)[32])branch, depth, path));
  ASSERT_FALSE(crypto::is_branch_in_tree(inputs[5].data, root.data, (const char(*)[32])branch, depth, path));
  ASSERT_FALSE(crypto::is_branch_in_tree(crypto::null_hash.data, root.data, (const char(*)[32])branch, depth, path));

  // five, index 2
  ASSERT_TRUE(crypto::tree_branch((const char(*)[32])inputs, 5, inputs[2].data, (char(*)[32])branch, &depth, &path));
  ASSERT_GE(depth, 2);
  ASSERT_LE(depth, 3);
  ASSERT_TRUE(crypto::tree_path(5, 2, &path2));
  ASSERT_EQ(path, path2);
  crypto::tree_hash((const char(*)[32])inputs, 5, root.data);
  ASSERT_FALSE(crypto::is_branch_in_tree(inputs[0].data, root.data, (const char(*)[32])branch, depth, path));
  ASSERT_FALSE(crypto::is_branch_in_tree(inputs[1].data, root.data, (const char(*)[32])branch, depth, path));
  ASSERT_TRUE(crypto::is_branch_in_tree(inputs[2].data, root.data, (const char(*)[32])branch, depth, path));
  ASSERT_FALSE(crypto::is_branch_in_tree(inputs[3].data, root.data, (const char(*)[32])branch, depth, path));
  ASSERT_FALSE(crypto::is_branch_in_tree(inputs[4].data, root.data, (const char(*)[32])branch, depth, path));
  ASSERT_FALSE(crypto::is_branch_in_tree(inputs[5].data, root.data, (const char(*)[32])branch, depth, path));
  ASSERT_FALSE(crypto::is_branch_in_tree(crypto::null_hash.data, root.data, (const char(*)[32])branch, depth, path));

  // five, index 4
  ASSERT_TRUE(crypto::tree_branch((const char(*)[32])inputs, 5, inputs[4].data, (char(*)[32])branch, &depth, &path));
  ASSERT_GE(depth, 2);
  ASSERT_LE(depth, 3);
  ASSERT_TRUE(crypto::tree_path(5, 4, &path2));
  ASSERT_EQ(path, path2);
  crypto::tree_hash((const char(*)[32])inputs, 5, root.data);
  ASSERT_FALSE(crypto::is_branch_in_tree(inputs[0].data, root.data, (const char(*)[32])branch, depth, path));
  ASSERT_FALSE(crypto::is_branch_in_tree(inputs[1].data, root.data, (const char(*)[32])branch, depth, path));
  ASSERT_FALSE(crypto::is_branch_in_tree(inputs[2].data, root.data, (const char(*)[32])branch, depth, path));
  ASSERT_FALSE(crypto::is_branch_in_tree(inputs[3].data, root.data, (const char(*)[32])branch, depth, path));
  ASSERT_TRUE(crypto::is_branch_in_tree(inputs[4].data, root.data, (const char(*)[32])branch, depth, path));
  ASSERT_FALSE(crypto::is_branch_in_tree(inputs[5].data, root.data, (const char(*)[32])branch, depth, path));
  ASSERT_FALSE(crypto::is_branch_in_tree(crypto::null_hash.data, root.data, (const char(*)[32])branch, depth, path));

  // a version with an extra (dummy) hash
  memcpy(branch_1, branch, sizeof(branch));
  branch_1[depth] = crypto::null_hash;

  ASSERT_FALSE(crypto::is_branch_in_tree(inputs[4].data, root.data, (const char(*)[32])branch, depth - 1, path));
  ASSERT_FALSE(crypto::is_branch_in_tree(inputs[4].data, root.data, (const char(*)[32])branch_1, depth + 1, path));
  ASSERT_FALSE(crypto::is_branch_in_tree(inputs[4].data, root.data, (const char(*)[32])branch, depth, path ^ 1));
  ASSERT_FALSE(crypto::is_branch_in_tree(inputs[4].data, root.data, (const char(*)[32])branch, depth, path ^ 2));
  ASSERT_FALSE(crypto::is_branch_in_tree(inputs[4].data, root.data, (const char(*)[32])branch, depth, path ^ 3));
  ASSERT_FALSE(crypto::is_branch_in_tree(inputs[4].data, root.data, (const char(*)[32])(branch_1 + 1), depth, path));

  // five, not found
  ASSERT_FALSE(crypto::tree_branch((const char(*)[32])inputs, 5, crypto::null_hash.data, (char(*)[32])branch, &depth, &path));

  // depth encoding roundtrip
  for (uint32_t n_chains = 1; n_chains <= 256; ++n_chains)
  {
    for (uint32_t nonce = 0xffffffff - 512; nonce != 1025; ++nonce)
    {
      const uint64_t depth = cryptonote::encode_mm_depth(n_chains, nonce);
      uint32_t n_chains_2, nonce_2;
      ASSERT_TRUE(cryptonote::decode_mm_depth(depth, n_chains_2, nonce_2));
      ASSERT_EQ(n_chains, n_chains_2);
      ASSERT_EQ(nonce, nonce_2);
    }
  }

  // 257 chains is too much
  try { cryptonote::encode_mm_depth(257, 0); ASSERT_TRUE(false); }
  catch (...) {}
}

TEST(Crypto, generator_consistency)
{
  // crypto/generators.h
  const crypto::public_key G{crypto::get_G()};
  const crypto::public_key H{crypto::get_H()};
  const ge_p3 H_p3 = crypto::get_H_p3();

  // crypto/crypto-ops.h
  ASSERT_TRUE(memcmp(&H_p3, &ge_p3_H, sizeof(ge_p3)) == 0);

  // ringct/rctOps.h
  ASSERT_TRUE(memcmp(G.data, rct::G.bytes, 32) == 0);

  // ringct/rctTypes.h
  ASSERT_TRUE(memcmp(H.data, rct::H.bytes, 32) == 0);
}

TEST(Crypto, batch_inversion)
{
  const std::size_t MAX_TEST_ELEMS = 1000;

  std::unique_ptr<fe[]> init_elems = std::make_unique<fe[]>(MAX_TEST_ELEMS);
  std::unique_ptr<fe[]> norm_inverted = std::make_unique<fe[]>(MAX_TEST_ELEMS);

  // Init test elems and individual inversions
  for (std::size_t i = 0; i < MAX_TEST_ELEMS; ++i)
  {
    random_fe(init_elems[i]);
    fe_invert(norm_inverted[i], init_elems[i]);
  }

  // Do batch inversions and compare to individual inversions
  for (std::size_t n_elems = 1; n_elems <= MAX_TEST_ELEMS; ++n_elems)
  {
    std::unique_ptr<fe[]> batch_inverted = std::make_unique<fe[]>(n_elems);
    ASSERT_EQ(fe_batch_invert(batch_inverted.get(), init_elems.get(), n_elems), 0);
    for (std::size_t i = 0; i < n_elems; ++i)
      ASSERT_EQ(fe_equals(batch_inverted[i], norm_inverted[i]), 1);
  }
}

TEST(Crypto, batch_inversion_touching)
{
  // vals[0] and vals[1] are input, vals[2] and vals[3] are output
  fe vals[4];

  // Init input elems
  for (std::size_t i = 0; i < 2; ++i)
    random_fe(vals[i]);

  ASSERT_EQ(0, fe_batch_invert(vals + 2, vals, 2));

  // Do batch inversions and compare to individual inversions
  for (std::size_t i = 0; i < 2; ++i)
  {
    fe inv_simple;
    fe_invert(inv_simple, vals[i]);
    ASSERT_EQ(1, fe_equals(inv_simple, vals[2 + i]));
  }
}

TEST(Crypto, batch_invert_zero)
{
  const std::size_t TEST_ELEMS = 2;
  std::unique_ptr<fe[]> init_elems = std::make_unique<fe[]>(TEST_ELEMS);

  // Init test elems
  random_fe(init_elems[0]);
  fe_0(init_elems[1]);

  // Do the batch inversion, should fail
  std::unique_ptr<fe[]> batch_inverted = std::make_unique<fe[]>(TEST_ELEMS);
  ASSERT_EQ(fe_batch_invert(batch_inverted.get(), init_elems.get(), TEST_ELEMS), -1);
}

TEST(Crypto, fe_equals)
{
  // Test equality
  ASSERT_EQ(fe_equals(fe_d, fe_d), 1);

  // Test inequality
  fe fe_d2;
  fe_add(fe_d2, fe_d, fe_d);
  ASSERT_EQ(fe_equals(fe_d2, fe_d), 0);

  // Test different fe reprs that are actually equal
  unsigned char fe_d2_bytes[32];
  fe fe_d2_reduced;
  fe_tobytes(fe_d2_bytes, fe_d2);
  fe_frombytes_vartime(fe_d2_reduced, fe_d2_bytes);
  // We expect distinct fe_d2_reduced and fe_d2 reprs, since fe_add produces fe repr elems in a larger subdomain.
  ASSERT_NE(memcmp(fe_d2_reduced, fe_d2, sizeof(fe)), 0);
  ASSERT_EQ(fe_equals(fe_d2_reduced, fe_d2), 1);
}

TEST(Crypto, ec_constants_rct_parity)
{
  ASSERT_TRUE(memcmp(&fcmp_pp::EC_I,         &rct::I,         32) == 0);
  ASSERT_TRUE(memcmp(&fcmp_pp::EC_INV_EIGHT, &rct::INV_EIGHT, 32) == 0);
}

#define CHECK_CLEARED(k, cleared) \
  crypto::ec_point cleared2; \
  const bool r = fcmp_pp::get_valid_torsion_cleared_point_vartime(rct::rct2pt(k), cleared2); \
  ASSERT_TRUE(r); \
  ASSERT_EQ(cleared, cleared2);

TEST(Crypto, torsion_check_pass_random)
{
  std::vector<rct::key> pts;
  for (int i = 0; i < 1000; ++i)
  {
    const rct::key pk = rct::pkGen();
    ge_p3 x;
    ASSERT_EQ(ge_frombytes_vartime(&x, pk.bytes), 0);
    ASSERT_TRUE(rct::isInMainSubgroup(pk));
    ASSERT_FALSE(fcmp_pp::mul8_is_identity_vartime(x));
    const crypto::ec_point cleared = fcmp_pp::clear_torsion_vartime(x);
    ASSERT_EQ(rct::rct2pt(pk), cleared);
    CHECK_CLEARED(pk, cleared);
    pts.emplace_back(pk);
  }
  ASSERT_TRUE(rct::verPointsForTorsion(pts));
}

TEST(Crypto, torsion_check_hardcoded)
{
  struct TorsionTestPoints { std::string point; bool torsion_free; };
  static const std::vector<TorsionTestPoints> torsion_test_points = {
      {"b10ba13e303cbe9abf7d5d44f1d417727abcc14903a74e071abd652ce1bf76dd", false},
      {"9b2e4c0281c0b02e7c53291a94d1d0cbff8883f8024f5142ee494ffbbd088071", false}, // genesis (see config::GENESIS_TX)
      {"785eda585dca4f3d27976106008ccfbca13146c8b21b8c7e4909032639a776e1", true},
      {"9a7b10563aa266032cd075f4e347f348a3841ae4f41572633351a97dd44066b4", true},
    };

  std::vector<rct::key> all_pts, torsion_free_pts, torsioned_pts, torsion_cleared_pts;
  for (const auto &point : torsion_test_points)
  {
    rct::key k;
    epee::string_tools::hex_to_pod(point.point, k);
    ge_p3 x;
    ASSERT_EQ(ge_frombytes_vartime(&x, k.bytes), 0);
    ASSERT_EQ(rct::isInMainSubgroup(k), point.torsion_free);
    ASSERT_FALSE(fcmp_pp::mul8_is_identity_vartime(x));
    const crypto::ec_point cleared = fcmp_pp::clear_torsion_vartime(x);
    if (point.torsion_free)
    {
      ASSERT_EQ(rct::rct2pt(k), cleared);
      torsion_free_pts.push_back(k);
    }
    else
    {
      ASSERT_NE(rct::rct2pt(k), cleared);
      torsioned_pts.push_back(k);
    }
    CHECK_CLEARED(k, cleared);
    all_pts.emplace_back(k);
    torsion_cleared_pts.emplace_back(rct::pt2rct(cleared));
  }

  ASSERT_TRUE(rct::verPointsForTorsion(torsion_free_pts));
  ASSERT_FALSE(rct::verPointsForTorsion(torsioned_pts));
  ASSERT_FALSE(rct::verPointsForTorsion(all_pts));
  ASSERT_TRUE(rct::verPointsForTorsion(torsion_cleared_pts));
}

TEST(Crypto, mul8_is_identity_vartime)
{
  static const std::vector<rct::key> mul8_identity_points = {
      rct::I,
      rct::Z
    };

  for (const auto &point : mul8_identity_points)
  {
    ge_p3 x;
    ASSERT_EQ(ge_frombytes_vartime(&x, point.bytes), 0);
    ASSERT_TRUE(fcmp_pp::mul8_is_identity_vartime(x));

    crypto::ec_point _;
    ASSERT_FALSE(fcmp_pp::get_valid_torsion_cleared_point_vartime(rct::rct2pt(point), _));
  }
}

/*
The two tests below are the only code in the tree that crosses the C++-to-Rust FFI
boundary of src/fcmp_pp/fcmp_pp_rust. That library exports a single function -
selene_scalar_from_bytes, declared in the generated fcmp_pp_rust/fcmp++.h - whose only
C++ caller is the wrapper fcmp_pp::tower_cycle::selene_scalar_from_bytes, itself called
only from CurveTrees::flatten_leaves, which no shipped binary and no other registered
test reaches. With nothing referencing the wrapper, the linker keeps no member of
libfcmp_pp_rust.a in any artifact even though the archive is on fcmp_pp's link
interface, so the crossing is present in no executable and any Rust-side FFI regression
- a changed error convention, a changed struct size or alignment, a parser that ignores
its input - is invisible to the whole suite. These cases both pull the archive member
into unit_tests and give the crossing its runtime coverage.

They deliberately assert nothing about the contents of a returned SeleneScalar:
fcmp++.h documents that type as opaque to the C/C++ side (it carries a Montgomery
residue, so little-endian 1 does not come back as 1), and pinning its encoding here
would break on an internal change of the Rust bigint implementation while proving
nothing about the boundary. What is asserted is structural and
representation-independent: canonical encodings - including the Weierstrass coordinates
the production path derives - are accepted, the mapping is deterministic and injective
(which is what proves the 32 bytes are really parsed), and a non-canonical encoding is
rejected on the Rust side with its error code propagated as an exception.
*/
TEST(Crypto, selene_scalar_from_bytes_production_path)
{
  // The fixed torsion-free points (shared with torsion_check_hardcoded above) keep the
  // case reproducible; the random points widen the input space on every run.
  static const std::vector<std::string> hardcoded_torsion_free_points = {
      "785eda585dca4f3d27976106008ccfbca13146c8b21b8c7e4909032639a776e1",
      "9a7b10563aa266032cd075f4e347f348a3841ae4f41572633351a97dd44066b4",
    };
  static const std::size_t N_RANDOM_POINTS = 32;

  std::vector<rct::key> points;
  for (const std::string &point_hex : hardcoded_torsion_free_points)
  {
    rct::key k;
    ASSERT_TRUE(epee::string_tools::hex_to_pod(point_hex, k));
    points.push_back(k);
  }
  const std::size_t n_hardcoded_scalars = points.size() * 2;
  for (std::size_t i = 0; i < N_RANDOM_POINTS; ++i)
    points.push_back(rct::pkGen());

  std::vector<SeleneScalar> selene_scalars;
  selene_scalars.reserve(points.size() * 2);
  for (const rct::key &k : points)
  {
    // The same sequence CurveTrees::flatten_leaves runs: validate the point, clear
    // torsion, convert to Weierstrass coords, hand each coord to the Rust FFI.
    ge_p3 point;
    ASSERT_EQ(ge_frombytes_vartime(&point, k.bytes), 0);
    ASSERT_TRUE(rct::isInMainSubgroup(k));

    crypto::ec_point torsion_free_point;
    ASSERT_TRUE(fcmp_pp::get_valid_torsion_cleared_point_vartime(rct::rct2pt(k), torsion_free_point));

    crypto::ec_coord wei_x, wei_y;
    ASSERT_TRUE(fcmp_pp::point_to_wei_x_y(torsion_free_point, wei_x, wei_y));

    // The crossing itself: coordinates produced by the production path are canonical
    // field elements, so the FFI must return 0 and the wrapper must not throw.
    SeleneScalar selene_x{}, selene_y{};
    ASSERT_NO_THROW(selene_x = fcmp_pp::tower_cycle::selene_scalar_from_bytes(wei_x));
    ASSERT_NO_THROW(selene_y = fcmp_pp::tower_cycle::selene_scalar_from_bytes(wei_y));

    // Determinism: identical input bytes must cross to a bit-identical value.
    ASSERT_TRUE(selene_scalars_equal(selene_x, fcmp_pp::tower_cycle::selene_scalar_from_bytes(wei_x)));
    ASSERT_TRUE(selene_scalars_equal(selene_y, fcmp_pp::tower_cycle::selene_scalar_from_bytes(wei_y)));

    selene_scalars.push_back(selene_x);
    selene_scalars.push_back(selene_y);
  }
  ASSERT_EQ(selene_scalars.size(), points.size() * 2);

  // Sensitivity, asserted on the fixed vectors only so that the assertion is
  // reproducible: the four coordinates of the two hardcoded points are four distinct
  // field elements, so their Selene scalars must differ pairwise. A crossing that
  // ignored its input, or that handed back a zeroed struct on an unnoticed error,
  // would collide here.
  for (std::size_t i = 0; i < n_hardcoded_scalars; ++i)
    for (std::size_t j = i + 1; j < n_hardcoded_scalars; ++j)
      EXPECT_FALSE(selene_scalars_equal(selene_scalars[i], selene_scalars[j]));
}

TEST(Crypto, selene_scalar_from_bytes_canonical_bound)
{
  // Little-endian encodings that are canonical Ed25519 field elements, p = 2^255 - 19.
  static const unsigned char LE_ZERO[32] = {0x00};
  static const unsigned char LE_ONE[32] = {0x01};
  static const unsigned char LE_TWO[32] = {0x02};
  static const unsigned char LE_P_MINUS_1[32] = {
      0xec, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
      0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
      0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
      0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x7f
    };

  // Little-endian encodings of values >= p, which are not canonical field elements.
  static const unsigned char LE_P[32] = {
      0xed, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
      0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
      0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
      0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x7f
    };
  static const unsigned char LE_ALL_ONES[32] = {
      0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
      0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
      0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
      0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff
    };
  static const unsigned char LE_TWO_POW_255[32] = {
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x80
    };
  static const unsigned char LE_TWO_POW_255_PLUS_1[32] = {
      0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x80
    };

  const std::vector<crypto::ec_coord> canonical_coords = {
      ec_coord_from_le_bytes(LE_ZERO),
      ec_coord_from_le_bytes(LE_ONE),
      ec_coord_from_le_bytes(LE_TWO),
      ec_coord_from_le_bytes(LE_P_MINUS_1)
    };

  std::vector<SeleneScalar> selene_scalars;
  selene_scalars.reserve(canonical_coords.size());
  for (const crypto::ec_coord &coord : canonical_coords)
  {
    SeleneScalar selene_scalar{};
    ASSERT_NO_THROW(selene_scalar = fcmp_pp::tower_cycle::selene_scalar_from_bytes(coord));
    ASSERT_TRUE(selene_scalars_equal(selene_scalar, fcmp_pp::tower_cycle::selene_scalar_from_bytes(coord)));
    selene_scalars.push_back(selene_scalar);
  }
  ASSERT_EQ(selene_scalars.size(), canonical_coords.size());

  // Injectivity over four distinct canonical field elements, including both ends of the
  // range: distinct inputs must not collapse onto one value.
  for (std::size_t i = 0; i < selene_scalars.size(); ++i)
    for (std::size_t j = i + 1; j < selene_scalars.size(); ++j)
      EXPECT_FALSE(selene_scalars_equal(selene_scalars[i], selene_scalars[j]));

  // The canonical bound is enforced on the Rust side and the failure code must reach the
  // caller as an exception rather than as an unchecked return. Each of these crossings
  // logs one expected LOG_ERROR line naming the failing FFI call and its error code.
  const std::vector<crypto::ec_coord> non_canonical_coords = {
      ec_coord_from_le_bytes(LE_P),                  // p itself, the first non-canonical value
      ec_coord_from_le_bytes(LE_ALL_ONES),           // 2^256 - 1
      ec_coord_from_le_bytes(LE_TWO_POW_255),        // high bit set, no low bits
      ec_coord_from_le_bytes(LE_TWO_POW_255_PLUS_1)  // high bit set over a canonical low half
    };

  for (const crypto::ec_coord &coord : non_canonical_coords)
    EXPECT_THROW(fcmp_pp::tower_cycle::selene_scalar_from_bytes(coord), std::runtime_error);
}

TEST(Crypto, fe_constants)
{
  // A / 3
  fe inv_3;
  static const fe fe_3{3, 0, 0, 0, 0, 0, 0, 0, 0, 0};
  fe_invert(inv_3, fe_3);
  fe a_inv_3;
  fe_mul(a_inv_3, fe_a, inv_3);

  // c = sqrt(-(A + 2))
  // Since sqrt isn't implemented, instead showing: c^2 = -(A + 2)
  // TODO: test fe_c directly once sqrt is implemented
  fe c_sq;
  fe_sq(c_sq, fe_c);
  // -(A + 2) = -A - 2
  static const fe fe_2{2, 0, 0, 0, 0, 0, 0, 0, 0, 0};
  fe ma_sub_2;
  fe_sub(ma_sub_2, fe_ma, fe_2);
  fe_reduce_vartime(ma_sub_2, ma_sub_2);

  ASSERT_TRUE(memcmp(fe_a_inv_3, a_inv_3,  sizeof(fe)) == 0);
  ASSERT_TRUE(memcmp(c_sq,       ma_sub_2, sizeof(fe)) == 0);

  // Parity with spec
  // https://www.ietf.org/archive/id/draft-ietf-lwig-curve-representations-02.pdf E.2 Page 19
  unsigned char A_INV_3_PAPER[32] = {
      0x2a, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa,
      0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa,
      0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa,
      0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xad, 0x24, 0x51
    };

  // https://www.ietf.org/archive/id/draft-ietf-lwig-curve-representations-02.pdf E.2 Pages 19-20
  unsigned char FE_C_PAPER[32] = {
      0x70, 0xd9, 0x12, 0x0b, 0x9f, 0x5f, 0xf9, 0x44,
      0x2d, 0x84, 0xf7, 0x23, 0xfc, 0x03, 0xb0, 0x81,
      0x3a, 0x5e, 0x2c, 0x2e, 0xb4, 0x82, 0xe5, 0x7d,
      0x33, 0x91, 0xfb, 0x55, 0x00, 0xba, 0x81, 0xe7
    };

  // Reverse to have comparable endian order
  unsigned char a_inv_3_bytes[32];
  fe_tobytes(a_inv_3_bytes, fe_a_inv_3);
  std::reverse(a_inv_3_bytes, a_inv_3_bytes + 32);

  unsigned char fe_c_bytes[32];
  fe_tobytes(fe_c_bytes, fe_c);
  std::reverse(fe_c_bytes, fe_c_bytes + 32);

  ASSERT_TRUE(memcmp(a_inv_3_bytes, A_INV_3_PAPER, 32) == 0);
  ASSERT_TRUE(memcmp(fe_c_bytes,    FE_C_PAPER,    32) == 0);
}
