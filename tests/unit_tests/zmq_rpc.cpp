// Copyright (c) 2020-2024, The Monero Project

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

#include <boost/preprocessor/stringize.hpp>
#include <cstring>
#include <gtest/gtest.h>
#include <rapidjson/document.h>
#include <rapidjson/ostreamwrapper.h>
#include <rapidjson/prettywriter.h> 
#include <string>

#include "cryptonote_basic/account.h"
#include "cryptonote_basic/cryptonote_basic.h"
#include "cryptonote_basic/events.h"
#include "cryptonote_basic/cryptonote_format_utils.h"
#include "cryptonote_core/cryptonote_tx_utils.h"
#include "json_serialization.h"
#include "net/zmq.h"
#include "rpc/daemon_handler.h"
#include "rpc/daemon_messages.h"
#include "rpc/message.h"
#include "rpc/zmq_pub.h"
#include "rpc/zmq_restricted_methods.h"
#include "rpc/zmq_server.h"
#include "serialization/json_object.h"

#define MASSERT(...)                                                      \
  if (!(__VA_ARGS__))                                                     \
    return testing::AssertionFailure() << BOOST_PP_STRINGIZE(__VA_ARGS__)

namespace rapidjson
{
  std::ostream& operator<<(std::ostream& out, const Document& src)
  {
    OStreamWrapper buffer{out};
    PrettyWriter<OStreamWrapper> writer{buffer};
    src.Accept(writer);
    return out;
  }
}

TEST(ZmqFullMessage, InvalidRequest)
{
  EXPECT_THROW(
    (cryptonote::rpc::FullMessage{"{\"jsonrpc\":\"2.0\",\"id\":0,\"params\":[]}", true}),
    cryptonote::json::MISSING_KEY
  );
  EXPECT_THROW(
    (cryptonote::rpc::FullMessage{"{\"jsonrpc\":\"2.0\",\"id\":0,\"method\":3,\"params\":[]}", true}),
    cryptonote::json::WRONG_TYPE
  );
}

TEST(ZmqFullMessage, Request)
{
  static constexpr const char request[] = "{\"jsonrpc\":\"2.0\",\"id\":0,\"method\":\"foo\",\"params\":[]}";
  EXPECT_NO_THROW(
    (cryptonote::rpc::FullMessage{request, true})
  );

  cryptonote::rpc::FullMessage parsed{request, true};
  EXPECT_STREQ("foo", parsed.getRequestType().c_str());
}

TEST(ZmqFullMessage, RejectsUnsupportedJsonRpcVersion)
{
  // absent
  EXPECT_THROW(
    (cryptonote::rpc::FullMessage{"{\"id\":0,\"method\":\"get_height\",\"params\":[]}", true}),
    cryptonote::json::MISSING_KEY
  );
  // not a string
  EXPECT_THROW(
    (cryptonote::rpc::FullMessage{"{\"jsonrpc\":2.0,\"id\":0,\"method\":\"get_height\",\"params\":[]}", true}),
    cryptonote::json::WRONG_TYPE
  );
  // a version this daemon does not speak
  EXPECT_THROW(
    (cryptonote::rpc::FullMessage{"{\"jsonrpc\":\"1.0\",\"id\":0,\"method\":\"get_height\",\"params\":[]}", true}),
    cryptonote::json::WRONG_TYPE
  );
  // too short and too long to be "2.0"
  EXPECT_THROW(
    (cryptonote::rpc::FullMessage{"{\"jsonrpc\":\"2\",\"id\":0,\"method\":\"get_height\",\"params\":[]}", true}),
    cryptonote::json::WRONG_TYPE
  );
  EXPECT_THROW(
    (cryptonote::rpc::FullMessage{"{\"jsonrpc\":\"2.0.1\",\"id\":0,\"method\":\"get_height\",\"params\":[]}", true}),
    cryptonote::json::WRONG_TYPE
  );
  // "2.0" followed by an embedded NUL: accepted only by a comparison that stops at it
  EXPECT_THROW(
    (cryptonote::rpc::FullMessage{"{\"jsonrpc\":\"2.0\\u0000rubbish\",\"id\":0,\"method\":\"get_height\",\"params\":[]}", true}),
    cryptonote::json::WRONG_TYPE
  );

  // the same rule applies to a response envelope
  EXPECT_THROW(
    (cryptonote::rpc::FullMessage{"{\"jsonrpc\":\"1.0\",\"id\":0,\"result\":{}}", false}),
    cryptonote::json::WRONG_TYPE
  );
  EXPECT_NO_THROW(
    (cryptonote::rpc::FullMessage{"{\"jsonrpc\":\"2.0\",\"id\":0,\"result\":{}}", false})
  );
}

TEST(ZmqFullMessage, RejectsNonCanonicalMethod)
{
  /* An escaped embedded NUL must not reach method lookup: as a C string the
     name below ends at the NUL, so it would resolve to the registered
     `get_height` handler even though the client did not name that method. */
  EXPECT_THROW(
    (cryptonote::rpc::FullMessage{"{\"jsonrpc\":\"2.0\",\"id\":0,\"method\":\"get_height\\u0000suffix\",\"params\":[]}", true}),
    cryptonote::json::WRONG_TYPE
  );
  // the same for a name that would alias an entry of the restricted-mode block list
  EXPECT_THROW(
    (cryptonote::rpc::FullMessage{"{\"jsonrpc\":\"2.0\",\"id\":0,\"method\":\"set_log_level\\u0000x\",\"params\":[]}", true}),
    cryptonote::json::WRONG_TYPE
  );

  // empty
  EXPECT_THROW(
    (cryptonote::rpc::FullMessage{"{\"jsonrpc\":\"2.0\",\"id\":0,\"method\":\"\",\"params\":[]}", true}),
    cryptonote::json::WRONG_TYPE
  );
  // control characters, whitespace, quoting and escaping bytes
  EXPECT_THROW(
    (cryptonote::rpc::FullMessage{"{\"jsonrpc\":\"2.0\",\"id\":0,\"method\":\"get_height\\n\",\"params\":[]}", true}),
    cryptonote::json::WRONG_TYPE
  );
  EXPECT_THROW(
    (cryptonote::rpc::FullMessage{"{\"jsonrpc\":\"2.0\",\"id\":0,\"method\":\"get height\",\"params\":[]}", true}),
    cryptonote::json::WRONG_TYPE
  );
  EXPECT_THROW(
    (cryptonote::rpc::FullMessage{"{\"jsonrpc\":\"2.0\",\"id\":0,\"method\":\"get_height\\\\\",\"params\":[]}", true}),
    cryptonote::json::WRONG_TYPE
  );
  // non-ASCII
  EXPECT_THROW(
    (cryptonote::rpc::FullMessage{"{\"jsonrpc\":\"2.0\",\"id\":0,\"method\":\"get_h\\u00e9ight\",\"params\":[]}", true}),
    cryptonote::json::WRONG_TYPE
  );

  // one character longer than the accepted maximum
  const std::string too_long(cryptonote::rpc::FullMessage::MAX_METHOD_LENGTH + 1, 'a');
  EXPECT_THROW(
    (cryptonote::rpc::FullMessage{"{\"jsonrpc\":\"2.0\",\"id\":0,\"method\":\"" + too_long + "\",\"params\":[]}", true}),
    cryptonote::json::WRONG_TYPE
  );

  // exactly the accepted maximum, and every accepted character class
  const std::string longest(cryptonote::rpc::FullMessage::MAX_METHOD_LENGTH, 'a');
  EXPECT_NO_THROW(
    (cryptonote::rpc::FullMessage{"{\"jsonrpc\":\"2.0\",\"id\":0,\"method\":\"" + longest + "\",\"params\":[]}", true})
  );
  EXPECT_NO_THROW(
    (cryptonote::rpc::FullMessage{"{\"jsonrpc\":\"2.0\",\"id\":0,\"method\":\"Az09_.-\",\"params\":[]}", true})
  );
}

TEST(ZmqFullMessage, RequestTypeIsLengthExact)
{
  const cryptonote::rpc::FullMessage parsed{
    "{\"jsonrpc\":\"2.0\",\"id\":0,\"method\":\"get_tx_global_output_indices\",\"params\":[]}", true
  };

  const std::string request_type = parsed.getRequestType();
  EXPECT_EQ(std::string{"get_tx_global_output_indices"}, request_type);
  // no truncation: the returned length is the length of the JSON string itself
  EXPECT_EQ(std::strlen("get_tx_global_output_indices"), request_type.size());
}

namespace
{
  //! Parse a response produced by one of the rpc error builders.
  cryptonote::rpc::FullMessage parse_response(epee::byte_slice response)
  {
    return cryptonote::rpc::FullMessage{
      std::string{reinterpret_cast<const char*>(response.data()), response.size()}, false
    };
  }
}

TEST(ZmqErrorResponse, BadJsonEchoesKnownId)
{
  const rapidjson::Value id{7};

  cryptonote::rpc::FullMessage with_id = parse_response(cryptonote::rpc::BAD_JSON("bad params", id));
  EXPECT_TRUE(with_id.getID().IsNumber());
  EXPECT_EQ(7U, with_id.getID().GetUint());

  const cryptonote::rpc::error error = with_id.getError();
  EXPECT_TRUE(error.use);
  EXPECT_STREQ(cryptonote::rpc::Message::STATUS_BAD_JSON, error.error_str.c_str());
  EXPECT_STREQ("bad params", error.message.c_str());

  // the id-less overload still answers with a null id, for a request that never parsed
  cryptonote::rpc::FullMessage without_id = parse_response(cryptonote::rpc::BAD_JSON("bad envelope"));
  EXPECT_TRUE(without_id.getID().IsNull());
  EXPECT_STREQ(cryptonote::rpc::Message::STATUS_BAD_JSON, without_id.getError().error_str.c_str());
}

TEST(ZmqErrorResponse, InternalErrorDisclosesNothing)
{
  const rapidjson::Value id{"abc", 3};

  cryptonote::rpc::FullMessage parsed = parse_response(cryptonote::rpc::INTERNAL_ERROR(id));

  // the request id is echoed, so a client can correlate the failure
  EXPECT_TRUE(parsed.getID().IsString());
  EXPECT_STREQ("abc", parsed.getID().GetString());

  /* The message is fixed: the builder takes no detail argument at all, so no
     backend, standard-library or database exception text can reach a client
     through this response. */
  const cryptonote::rpc::error error = parsed.getError();
  EXPECT_TRUE(error.use);
  EXPECT_STREQ(cryptonote::rpc::Message::STATUS_FAILED, error.error_str.c_str());
  EXPECT_STREQ("Internal error while handling the request", error.message.c_str());
}

TEST(ZmqRestrictedMethods, BasicCoverage)
{
  EXPECT_TRUE(cryptonote::rpc::is_blocked_in_restricted_mode("flush_txpool"));
  EXPECT_TRUE(cryptonote::rpc::is_blocked_in_restricted_mode("get_peer_list"));
  EXPECT_TRUE(cryptonote::rpc::is_blocked_in_restricted_mode("mining_status"));
  EXPECT_TRUE(cryptonote::rpc::is_blocked_in_restricted_mode("relay_tx"));
  EXPECT_TRUE(cryptonote::rpc::is_blocked_in_restricted_mode("save_bc"));
  EXPECT_TRUE(cryptonote::rpc::is_blocked_in_restricted_mode("set_log_categories"));
  EXPECT_TRUE(cryptonote::rpc::is_blocked_in_restricted_mode("set_log_level"));
  EXPECT_TRUE(cryptonote::rpc::is_blocked_in_restricted_mode("start_mining"));
  EXPECT_TRUE(cryptonote::rpc::is_blocked_in_restricted_mode("stop_mining"));

  EXPECT_FALSE(cryptonote::rpc::is_blocked_in_restricted_mode("get_height"));
  EXPECT_FALSE(cryptonote::rpc::is_blocked_in_restricted_mode("get_info"));
  EXPECT_FALSE(cryptonote::rpc::is_blocked_in_restricted_mode("send_raw_tx"));
}

namespace
{
  //! Serialize `src` exactly as the daemon writes a ZMQ response body.
  template<typename Response>
  epee::byte_stream write_response(const Response& src)
  {
    epee::byte_stream buffer;
    {
      rapidjson::Writer<epee::byte_stream> dest{buffer};
      src.toJson(dest);
    }
    return buffer;
  }

  //! Read a serialized ZMQ response body back into a fresh `Response`.
  template<typename Response>
  Response read_response(const epee::byte_stream& buffer)
  {
    rapidjson::Document doc;
    doc.Parse(reinterpret_cast<const char*>(buffer.data()), buffer.size());
    if (doc.HasParseError())
      throw cryptonote::json::PARSE_FAIL();

    Response out{};
    out.fromJson(doc);
    return out;
  }

  //! A `crypto::hash` whose every byte is non-zero and position dependent.
  crypto::hash distinct_hash(const std::uint8_t first_byte)
  {
    crypto::hash out{};
    for (std::size_t i = 0; i < sizeof(out.data); ++i)
      out.data[i] = static_cast<char>(first_byte + i);
    return out;
  }

  /*! The `get_blocks_fast` response body as a daemon that does not know
    `top_block_hash` and `max_block_count` emits it. Built with the same
    serializers the daemon uses, so it stays a valid payload of the frozen
    DAEMON_RPC_VERSION_ZMQ 2.0 contract. */
  epee::byte_stream get_blocks_fast_response_without_newer_fields()
  {
    epee::byte_stream buffer;
    {
      rapidjson::Writer<epee::byte_stream> dest{buffer};
      dest.StartObject();
      INSERT_INTO_JSON_OBJECT(dest, rpc_version, cryptonote::rpc::DAEMON_RPC_VERSION_ZMQ);
      INSERT_INTO_JSON_OBJECT(dest, blocks, std::vector<cryptonote::rpc::block_with_transactions>{});
      INSERT_INTO_JSON_OBJECT(dest, start_height, std::uint64_t(11));
      INSERT_INTO_JSON_OBJECT(dest, current_height, std::uint64_t(22));
      INSERT_INTO_JSON_OBJECT(dest, output_indices, std::vector<cryptonote::rpc::block_output_indices>{});
      dest.EndObject();
    }
    return buffer;
  }

  cryptonote::rpc::peer make_peer(const std::uint32_t offset)
  {
    cryptonote::rpc::peer out{};
    out.id = 0x0123456789abcdefull + offset;
    out.ip = 0x0a000001u + offset;             // 10.0.0.1 and up, host byte order
    out.port = static_cast<std::uint16_t>(18080 + offset);
    out.rpc_port = static_cast<std::uint16_t>(18081 + offset);
    out.rpc_credits_per_hash = 250u + offset;
    out.last_seen = 1700000000ull + offset;
    out.pruning_seed = 384u + offset;
    return out;
  }

  //! \return An IPv4 peerlist entry whose every member is distinct and non-zero.
  nodetool::peerlist_entry make_ipv4_peerlist_entry(const std::uint32_t offset)
  {
    nodetool::peerlist_entry out{};
    out.adr = epee::net_utils::ipv4_network_address{
      0x0a000001u + offset, static_cast<std::uint16_t>(18080 + offset)
    };
    out.id = 0x0123456789abcdefull + offset;
    out.last_seen = static_cast<std::int64_t>(1700000000 + offset);
    out.pruning_seed = 384u + offset;
    out.rpc_port = static_cast<std::uint16_t>(18081 + offset);
    out.rpc_credits_per_hash = 250u + offset;
    return out;
  }

  //! \return A peerlist entry on a network the IPv4-only `peer` type cannot carry.
  nodetool::peerlist_entry make_ipv6_peerlist_entry()
  {
    nodetool::peerlist_entry out{};
    out.adr = epee::net_utils::ipv6_network_address{
      boost::asio::ip::address_v6::loopback(), 18080
    };
    out.id = 0xdeadbeefdeadbeefull;
    out.last_seen = 1700000099;
    out.pruning_seed = 385;
    out.rpc_port = 18082;
    out.rpc_credits_per_hash = 251;
    return out;
  }

  //! The disposition of every host that is not banned.
  bool none_blocked(const epee::net_utils::network_address&) { return false; }

  testing::AssertionResult peers_equal(const cryptonote::rpc::peer& expected, const cryptonote::rpc::peer& actual)
  {
    MASSERT(expected.id == actual.id);
    MASSERT(expected.ip == actual.ip);
    MASSERT(expected.port == actual.port);
    MASSERT(expected.rpc_port == actual.rpc_port);
    MASSERT(expected.rpc_credits_per_hash == actual.rpc_credits_per_hash);
    MASSERT(expected.last_seen == actual.last_seen);
    MASSERT(expected.pruning_seed == actual.pruning_seed);
    return testing::AssertionSuccess();
  }
} // anonymous

TEST(ZmqDaemonResponses, GetBlocksFastRoundTripKeepsTopBlockHashAndMaxBlockCount)
{
  cryptonote::rpc::GetBlocksFast::Response src{};
  src.start_height = 1234;
  src.current_height = 5678;
  src.top_block_hash = distinct_hash(1);
  src.max_block_count = COMMAND_RPC_GET_BLOCKS_FAST_MAX_BLOCK_COUNT;

  ASSERT_NE(crypto::null_hash, src.top_block_hash);
  ASSERT_NE(0u, src.max_block_count);

  cryptonote::rpc::GetBlocksFast::Response out{};
  ASSERT_NO_THROW(out = read_response<cryptonote::rpc::GetBlocksFast::Response>(write_response(src)));

  EXPECT_EQ(src.start_height, out.start_height);
  EXPECT_EQ(src.current_height, out.current_height);
  EXPECT_EQ(src.top_block_hash, out.top_block_hash);
  EXPECT_EQ(src.max_block_count, out.max_block_count);
  EXPECT_TRUE(out.blocks.empty());
  EXPECT_TRUE(out.output_indices.empty());
}

TEST(ZmqDaemonResponses, GetBlocksFastAcceptsResponseWithoutTopBlockHashOrMaxBlockCount)
{
  const epee::byte_stream buffer = get_blocks_fast_response_without_newer_fields();
  rapidjson::Document doc;
  doc.Parse(reinterpret_cast<const char*>(buffer.data()), buffer.size());
  ASSERT_FALSE(doc.HasParseError());
  ASSERT_FALSE(doc.HasMember("top_block_hash"));
  ASSERT_FALSE(doc.HasMember("max_block_count"));

  // Deserialize into an object that already holds non-default values, so that the
  // defaults asserted below can only come from the reader clearing them and never
  // from the destination's initial value.
  cryptonote::rpc::GetBlocksFast::Response out{};
  out.top_block_hash = distinct_hash(9);
  out.max_block_count = 4321;
  ASSERT_NO_THROW(out.fromJson(doc));

  EXPECT_EQ(11u, out.start_height);
  EXPECT_EQ(22u, out.current_height);
  EXPECT_EQ(crypto::null_hash, out.top_block_hash);
  EXPECT_EQ(0u, out.max_block_count);
  EXPECT_TRUE(out.blocks.empty());
  EXPECT_TRUE(out.output_indices.empty());
}

TEST(ZmqDaemonHandler, NettypeNameCoversEveryNetwork)
{
  // The four names the HTTP get_info handler emits (core_rpc_server.cpp); the
  // ZMQ get_info response must report exactly these for the same daemon.
  EXPECT_STREQ("mainnet", cryptonote::rpc::get_nettype_name(cryptonote::MAINNET));
  EXPECT_STREQ("testnet", cryptonote::rpc::get_nettype_name(cryptonote::TESTNET));
  EXPECT_STREQ("stagenet", cryptonote::rpc::get_nettype_name(cryptonote::STAGENET));
  EXPECT_STREQ("fakechain", cryptonote::rpc::get_nettype_name(cryptonote::FAKECHAIN));

  // A locally generated chain is the only other case, and it must never be
  // reported as an empty string, which is what the response used to carry.
  EXPECT_STREQ("fakechain", cryptonote::rpc::get_nettype_name(cryptonote::UNDEFINED));
  for (const cryptonote::network_type type : {cryptonote::MAINNET, cryptonote::TESTNET, cryptonote::STAGENET, cryptonote::FAKECHAIN, cryptonote::UNDEFINED})
  {
    EXPECT_STRNE("", cryptonote::rpc::get_nettype_name(type));
  }
}

TEST(ZmqDaemonHandler, AppendPeerlistConvertsEveryFieldAndKeepsListsSeparate)
{
  const std::vector<nodetool::peerlist_entry> white{make_ipv4_peerlist_entry(0), make_ipv4_peerlist_entry(1)};
  const std::vector<nodetool::peerlist_entry> gray{make_ipv4_peerlist_entry(2)};

  // Converted once per list, exactly as the handler does it, so that the white
  // and gray lists stay separate and neither leaks into the other.
  std::vector<cryptonote::rpc::peer> white_out;
  std::vector<cryptonote::rpc::peer> gray_out;
  cryptonote::rpc::append_peerlist(none_blocked, white, white_out);
  cryptonote::rpc::append_peerlist(none_blocked, gray, gray_out);

  ASSERT_EQ(2u, white_out.size());
  ASSERT_EQ(1u, gray_out.size());
  EXPECT_TRUE(peers_equal(make_peer(0), white_out[0]));
  EXPECT_TRUE(peers_equal(make_peer(1), white_out[1]));
  EXPECT_TRUE(peers_equal(make_peer(2), gray_out[0]));
}

TEST(ZmqDaemonHandler, AppendPeerlistSkipsBlockedHosts)
{
  const std::vector<nodetool::peerlist_entry> source{
    make_ipv4_peerlist_entry(0), make_ipv4_peerlist_entry(1), make_ipv4_peerlist_entry(2)
  };
  const auto blocked_address = source[1].adr;

  std::vector<cryptonote::rpc::peer> out;
  cryptonote::rpc::append_peerlist(
    [&blocked_address](const epee::net_utils::network_address& address) { return address == blocked_address; },
    source,
    out
  );

  ASSERT_EQ(2u, out.size());
  EXPECT_TRUE(peers_equal(make_peer(0), out[0]));
  EXPECT_TRUE(peers_equal(make_peer(2), out[1]));

  // Every host banned means an empty list, never a partially converted one.
  std::vector<cryptonote::rpc::peer> none;
  cryptonote::rpc::append_peerlist(
    [](const epee::net_utils::network_address&) { return true; }, source, none
  );
  EXPECT_TRUE(none.empty());
}

TEST(ZmqDaemonHandler, AppendPeerlistSkipsAddressesThePeerTypeCannotCarry)
{
  // `cryptonote::rpc::peer` holds a 32-bit `ip`, so a non-IPv4 peer has no
  // representation in the frozen response and must be dropped rather than
  // emitted as a zero address.
  const std::vector<nodetool::peerlist_entry> source{
    make_ipv6_peerlist_entry(), make_ipv4_peerlist_entry(0), make_ipv6_peerlist_entry()
  };

  std::vector<cryptonote::rpc::peer> out;
  cryptonote::rpc::append_peerlist(none_blocked, source, out);

  ASSERT_EQ(1u, out.size());
  EXPECT_TRUE(peers_equal(make_peer(0), out[0]));
}

TEST(ZmqDaemonHandler, AppendPeerlistAppendsRatherThanReplaces)
{
  std::vector<cryptonote::rpc::peer> out{make_peer(7)};
  cryptonote::rpc::append_peerlist(none_blocked, {make_ipv4_peerlist_entry(0)}, out);

  ASSERT_EQ(2u, out.size());
  EXPECT_TRUE(peers_equal(make_peer(7), out[0]));
  EXPECT_TRUE(peers_equal(make_peer(0), out[1]));
}

namespace
{
  /*! A `DaemonHandler` over a core and a p2p server that are constructed but
    never initialised.

    Both constructors are pure member initialisation - no database is opened, no
    thread is started and no socket is bound - which is what makes a real
    dispatch test possible here. The consequence is that only handlers which do
    not reach the blockchain can be invoked: `get_peer_list` reads the p2p
    server's peer lists, and an uninitialised server simply has no network zone,
    so it reports empty lists. That is enough to prove the method is implemented
    and dispatched, which is what the stub it replaced could never do. */
  class daemon_handler_fixture
  {
  public:
    explicit daemon_handler_fixture(const bool restricted)
      : core_(nullptr)
      , protocol_(core_, nullptr, true /* offline */)
      , p2p_(protocol_)
      , handler_(core_, p2p_, restricted)
    {}

    cryptonote::rpc::DaemonHandler& handler() noexcept { return handler_; }

  private:
    cryptonote::core core_;
    cryptonote::t_cryptonote_protocol_handler<cryptonote::core> protocol_;
    nodetool::node_server<cryptonote::t_cryptonote_protocol_handler<cryptonote::core>> p2p_;
    cryptonote::rpc::DaemonHandler handler_;
  };

  //! \return The parsed response to `request` from a handler in the given mode.
  rapidjson::Document dispatch(const bool restricted, const std::string& request)
  {
    daemon_handler_fixture fixture{restricted};
    const epee::byte_slice response = fixture.handler().handle(std::string{request});

    rapidjson::Document doc;
    doc.Parse(reinterpret_cast<const char*>(response.data()), response.size());
    if (doc.HasParseError())
      throw cryptonote::json::PARSE_FAIL();
    return doc;
  }

  constexpr const char get_peer_list_request[] =
    "{\"jsonrpc\":\"2.0\",\"id\":0,\"method\":\"get_peer_list\",\"params\":{}}";
}

TEST(ZmqDaemonHandler, GetPeerListDispatchesAndSucceeds)
{
  rapidjson::Document doc;
  ASSERT_NO_THROW(doc = dispatch(false /* restricted */, get_peer_list_request));

  ASSERT_TRUE(doc.IsObject()) << doc;

  // The method used to answer every call from a stub, which the envelope renders
  // as an `error` object carrying "RPC method not yet implemented.". A success
  // now carries `result` and no `error` at all.
  ASSERT_FALSE(doc.HasMember("error")) << doc;
  ASSERT_TRUE(doc.HasMember("result")) << doc;
  const rapidjson::Value& result = doc["result"];
  ASSERT_TRUE(result.IsObject()) << doc;

  ASSERT_TRUE(result.HasMember("rpc_version")) << doc;
  EXPECT_EQ(cryptonote::rpc::DAEMON_RPC_VERSION_ZMQ, result["rpc_version"].GetUint()) << doc;

  // Both declared lists are present and are arrays. They are empty because an
  // uninitialised p2p server has no network zone and therefore no peers.
  ASSERT_TRUE(result.HasMember("white_list")) << doc;
  ASSERT_TRUE(result.HasMember("gray_list")) << doc;
  EXPECT_TRUE(result["white_list"].IsArray()) << doc;
  EXPECT_TRUE(result["gray_list"].IsArray()) << doc;
}

TEST(ZmqDaemonHandler, GetPeerListIsRefusedInRestrictedMode)
{
  rapidjson::Document doc;
  ASSERT_NO_THROW(doc = dispatch(true /* restricted */, get_peer_list_request));

  // Implementing the method must not have made it reachable without privileges:
  // the restricted check runs before dispatch, so there is no `result` at all.
  ASSERT_FALSE(doc.HasMember("result")) << doc;
  ASSERT_TRUE(doc.HasMember("error")) << doc;
  const rapidjson::Value& error = doc["error"];
  ASSERT_TRUE(error.IsObject()) << doc;

  ASSERT_TRUE(error.HasMember("error_str")) << doc;
  EXPECT_STREQ(cryptonote::rpc::Message::STATUS_FAILED, error["error_str"].GetString()) << doc;
  ASSERT_TRUE(error.HasMember("message")) << doc;
  EXPECT_STREQ(
    "\"get_peer_list\" is not available in restricted mode.",
    error["message"].GetString()
  ) << doc;
}

TEST(ZmqDaemonResponses, GetPeerListRoundTripKeepsEveryPeerField)
{
  cryptonote::rpc::GetPeerList::Response src{};
  src.white_list.push_back(make_peer(0));
  src.white_list.push_back(make_peer(1));
  src.gray_list.push_back(make_peer(2));

  cryptonote::rpc::GetPeerList::Response out{};
  ASSERT_NO_THROW(out = read_response<cryptonote::rpc::GetPeerList::Response>(write_response(src)));

  ASSERT_EQ(src.white_list.size(), out.white_list.size());
  ASSERT_EQ(src.gray_list.size(), out.gray_list.size());

  for (std::size_t i = 0; i < src.white_list.size(); ++i)
    EXPECT_TRUE(peers_equal(src.white_list[i], out.white_list[i]));
  for (std::size_t i = 0; i < src.gray_list.size(); ++i)
    EXPECT_TRUE(peers_equal(src.gray_list[i], out.gray_list[i]));
}

namespace
{
  using published_json = std::pair<std::string, rapidjson::Document>;

  constexpr const char inproc_pub[] = "inproc://dummy_pub";

  net::zmq::socket create_socket(void* ctx, const char* address)
  {
    net::zmq::socket sock{zmq_socket(ctx, ZMQ_PAIR)};
    if (!sock)
      MONERO_ZMQ_THROW("failed to create socket");
    if (zmq_bind(sock.get(), address) != 0)
      MONERO_ZMQ_THROW("socket bind failure");
    return sock;
  }

  std::vector<std::string> get_messages(void* socket, int count = -1)
  {
    std::vector<std::string> out;
    for ( ; count || count < 0; --count)
    {
      expect<std::string> next = net::zmq::receive(socket, (count < 0 ? ZMQ_DONTWAIT : 0));
      if (next == net::zmq::make_error_code(EAGAIN))
        return out;
      out.push_back(std::move(*next));
    }
    return out;
  }

  std::vector<published_json> get_published(void* socket, int count = -1)
  {
    std::vector<published_json> out;

    const auto messages = get_messages(socket, count);
    out.reserve(messages.size());

    for (const std::string& message : messages)
    {
      const char* split = std::strchr(message.c_str(), ':');
      if (!split)
        throw std::runtime_error{"Invalid ZMQ/Pub message"};

      out.emplace_back();
      out.back().first = {message.c_str(), split};
      if (out.back().second.Parse(split + 1).HasParseError())
        throw std::runtime_error{"Failed to parse ZMQ/Pub message"};
    }

    return out;
  }

  testing::AssertionResult compare_json(const std::string expected_json, const rapidjson::Document& published)
  {
    rapidjson::Document expected;
    expected.Parse(expected_json.c_str());
    MASSERT(!expected.HasParseError());
    if (expected != published)
      return testing::AssertionFailure() << expected << " != " << published;
    return testing::AssertionSuccess();
  }

  testing::AssertionResult compare_full_txpool(epee::span<const cryptonote::txpool_event> events, const published_json& pub)
  {
    MASSERT(pub.first == "json-full-txpool_add");
    MASSERT(pub.second.IsArray());
    MASSERT(pub.second.Size() <= events.size());

    std::size_t i = 0;
    for (const cryptonote::txpool_event& event : events)
    {
      MASSERT(i <= pub.second.Size());
      if (!event.res)
        continue;

      cryptonote::transaction tx{};
      cryptonote::json::fromJsonValue(pub.second[i], tx);

      crypto::hash id{};
      MASSERT(cryptonote::get_transaction_hash(event.tx, id));
      MASSERT(cryptonote::get_transaction_hash(tx, id));
      MASSERT(event.tx.hash == tx.hash);
      ++i;
    }
    return testing::AssertionSuccess();
  }

  testing::AssertionResult compare_minimal_txpool(epee::span<const cryptonote::txpool_event> events, const published_json& pub)
  {
    MASSERT(pub.first == "json-minimal-txpool_add");
    MASSERT(pub.second.IsArray());
    MASSERT(pub.second.Size() <= events.size());

    std::size_t i = 0;
    for (const cryptonote::txpool_event& event : events)
    {
      MASSERT(i <= pub.second.Size());
      if (!event.res)
        continue;

      std::size_t actual_size = 0;
      crypto::hash actual_id{};

      MASSERT(pub.second[i].IsObject());
      GET_FROM_JSON_OBJECT(pub.second[i], actual_id, id);
      GET_FROM_JSON_OBJECT(pub.second[i], actual_size, blob_size);

      std::size_t expected_size = 0;
      crypto::hash expected_id{};
      MASSERT(cryptonote::get_transaction_hash(event.tx, expected_id, expected_size));
      MASSERT(expected_size == actual_size);
      MASSERT(expected_id == actual_id);
      ++i;
    }
    return testing::AssertionSuccess();
  }

  testing::AssertionResult compare_miner_data(const std::string expected, const published_json& pub)
  {
    MASSERT(pub.first == "json-full-miner_data");
    return compare_json(expected, pub.second);
  }

  testing::AssertionResult compare_full_block(const epee::span<const cryptonote::block> expected, const published_json& pub)
  {
    MASSERT(pub.first == "json-full-chain_main");
    MASSERT(pub.second.IsArray());

    std::vector<cryptonote::block> actual;
    cryptonote::json::fromJsonValue(pub.second, actual);

    MASSERT(expected.size() == actual.size());

    for (std::size_t i = 0; i < expected.size(); ++i)
    {
      crypto::hash id;
      MASSERT(cryptonote::get_block_hash(expected[i], id));
      MASSERT(cryptonote::get_block_hash(actual[i], id));
      MASSERT(expected[i].hash == actual[i].hash);
    }

    return testing::AssertionSuccess();
  }

  testing::AssertionResult compare_minimal_block(std::size_t height, const epee::span<const cryptonote::block> expected, const published_json& pub)
  {
    MASSERT(pub.first == "json-minimal-chain_main");
    MASSERT(pub.second.IsObject());
    MASSERT(!expected.empty());

    std::size_t actual_height = 0;
    crypto::hash actual_prev_id{};
    std::vector<crypto::hash> actual_ids{};
    GET_FROM_JSON_OBJECT(pub.second, actual_height, first_height);
    GET_FROM_JSON_OBJECT(pub.second, actual_prev_id, first_prev_id);
    GET_FROM_JSON_OBJECT(pub.second, actual_ids, ids);

    MASSERT(height == actual_height);
    MASSERT(expected[0].prev_id == actual_prev_id);
    MASSERT(expected.size() == actual_ids.size());

    for (std::size_t i = 0; i < expected.size(); ++i)
    {
      crypto::hash id;
      MASSERT(cryptonote::get_block_hash(expected[i], id));
      MASSERT(id == actual_ids[i]);
    }

    return testing::AssertionSuccess();
  }

  struct zmq_base : public testing::Test
  {
    cryptonote::account_base acct;

    zmq_base()
      : testing::Test(), acct()
    {
      acct.generate();
    }

    cryptonote::transaction make_miner_transaction()
    {
      return test::make_miner_transaction(acct.get_keys().m_account_address);
    }

    cryptonote::transaction make_transaction(const std::vector<cryptonote::account_public_address>& destinations)
    {
      return test::make_transaction(acct.get_keys(), {make_miner_transaction()}, destinations, true, true);
    }

    cryptonote::transaction make_transaction()
    {
      cryptonote::account_base temp_account;
      temp_account.generate();
      return make_transaction({temp_account.get_keys().m_account_address});
    }

    cryptonote::block make_block()
    {
      cryptonote::block block{};
      block.major_version = 1;
      block.minor_version = 3;
      block.timestamp = 100;
      block.prev_id = crypto::rand<crypto::hash>();
      block.nonce = 100;
      block.miner_tx = make_miner_transaction();
      return block;
    }
  };

  struct zmq_pub : public zmq_base
  {
    net::zmq::context ctx;
    net::zmq::socket relay;
    net::zmq::socket dummy_pub;
    net::zmq::socket dummy_client;
    std::shared_ptr<cryptonote::listener::zmq_pub> pub;

    zmq_pub()
      : zmq_base(),
        ctx(zmq_init(1)),
        relay(create_socket(ctx.get(), cryptonote::listener::zmq_pub::relay_endpoint())),
        dummy_pub(create_socket(ctx.get(), inproc_pub)),
        dummy_client(zmq_socket(ctx.get(), ZMQ_PAIR)),
        pub(std::make_shared<cryptonote::listener::zmq_pub>(ctx.get()))
    {
      if (!dummy_client)
        MONERO_ZMQ_THROW("failed to create socket");
      if (zmq_connect(dummy_client.get(), inproc_pub) != 0)
        MONERO_ZMQ_THROW("failed to connect to dummy pub");
    }

    virtual void TearDown() override final
    {
      EXPECT_EQ(0u, get_messages(relay.get()).size());
      EXPECT_EQ(0u, get_messages(dummy_client.get()).size());
    }

    template<std::size_t N>
    bool sub_request(const char (&topic)[N])
    {
      return pub->sub_request({topic, N - 1});
    }
  };

  struct dummy_handler final : cryptonote::rpc::RpcHandler
  {
    dummy_handler()
      : cryptonote::rpc::RpcHandler()
    {}

    virtual epee::byte_slice handle(std::string&& request) override final
    {
      throw std::logic_error{"not implemented"};
    }
  };

  struct zmq_server : public zmq_base
  {
    dummy_handler handler;
    cryptonote::rpc::ZmqServer server;
    std::shared_ptr<cryptonote::listener::zmq_pub> pub;
    net::zmq::socket sub;

    zmq_server()
      : zmq_base(),
        handler(),
        server(handler, false),
        pub(),
        sub()
    {
      void* ctx = server.init_rpc({}, {});
      if (!ctx)
        throw std::runtime_error{"init_rpc failure"};

      const std::string endpoint = inproc_pub;
      pub = server.init_pub({std::addressof(endpoint), 1});
      if (!pub)
        throw std::runtime_error{"failed to initialize zmq/pub"};

      sub.reset(zmq_socket(ctx, ZMQ_SUB));
      if (!sub)
        MONERO_ZMQ_THROW("failed to create socket");
      if (zmq_connect(sub.get(), inproc_pub) != 0)
        MONERO_ZMQ_THROW("failed to connect to dummy pub");

      server.run();
    }

    virtual void TearDown() override final
    {
      EXPECT_EQ(0u, get_messages(sub.get()).size());
      sub.reset();
      pub.reset();
      server.stop();
    }

    template<std::size_t N>
    void subscribe(const char (&topic)[N])
    {
      if (zmq_setsockopt(sub.get(), ZMQ_SUBSCRIBE, topic, N - 1) != 0)
        MONERO_ZMQ_THROW("failed to subscribe");
    }
  };
}

TEST_F(zmq_pub, InvalidContext)
{
  EXPECT_THROW(cryptonote::listener::zmq_pub{nullptr}, std::logic_error);
}

TEST_F(zmq_pub, NoBlocking)
{
  EXPECT_FALSE(pub->relay_to_pub(relay.get(), dummy_pub.get()));
}

TEST_F(zmq_pub, DefaultDrop)
{
  EXPECT_EQ(0u, pub->send_txpool_add({{make_transaction(), {}, true}}));

  const cryptonote::block bl = make_block();
  EXPECT_EQ(0u,pub->send_chain_main(5, {std::addressof(bl), 1}));
  EXPECT_NO_THROW(cryptonote::listener::zmq_pub::chain_main{pub}(5, {std::addressof(bl), 1}));
}

TEST_F(zmq_pub, JsonFullTxpool)
{
  static constexpr const char topic[] = "\1json-full-txpool_add";

  ASSERT_TRUE(sub_request(topic));

  std::vector<cryptonote::txpool_event> events
  {
   {make_transaction(), {}, true}, {make_transaction(), {}, true}
  };

  EXPECT_NO_THROW(pub->send_txpool_add(events));
  EXPECT_TRUE(pub->relay_to_pub(relay.get(), dummy_pub.get()));

  auto pubs = get_published(dummy_client.get());
  EXPECT_EQ(1u, pubs.size());
  ASSERT_LE(1u, pubs.size());
  EXPECT_TRUE(compare_full_txpool(epee::to_span(events), pubs.front()));

  EXPECT_NO_THROW(cryptonote::listener::zmq_pub::txpool_add{pub}(events));
  EXPECT_TRUE(pub->relay_to_pub(relay.get(), dummy_pub.get()));

  pubs = get_published(dummy_client.get());
  EXPECT_EQ(1u, pubs.size());
  ASSERT_LE(1u, pubs.size());
  EXPECT_TRUE(compare_full_txpool(epee::to_span(events), pubs.front()));

  events.at(0).res = false;
  EXPECT_EQ(1u, pub->send_txpool_add(events));
  EXPECT_TRUE(pub->relay_to_pub(relay.get(), dummy_pub.get()));

  pubs = get_published(dummy_client.get());
  EXPECT_EQ(1u, pubs.size());
  ASSERT_LE(1u, pubs.size());
  EXPECT_TRUE(compare_full_txpool(epee::to_span(events), pubs.front()));

  events.at(0).res = false;
  EXPECT_NO_THROW(cryptonote::listener::zmq_pub::txpool_add{pub}(events));
  EXPECT_TRUE(pub->relay_to_pub(relay.get(), dummy_pub.get()));

  pubs = get_published(dummy_client.get());
  EXPECT_EQ(1u, pubs.size());
  ASSERT_LE(1u, pubs.size());
  EXPECT_TRUE(compare_full_txpool(epee::to_span(events), pubs.front()));
}

TEST_F(zmq_pub, JsonMinimalTxpool)
{
  static constexpr const char topic[] = "\1json-minimal-txpool_add";

  ASSERT_TRUE(sub_request(topic));

  std::vector<cryptonote::txpool_event> events
  {
   {make_transaction(), {}, true}, {make_transaction(), {}, true}
  };

  EXPECT_NO_THROW(pub->send_txpool_add(events));
  EXPECT_TRUE(pub->relay_to_pub(relay.get(), dummy_pub.get()));

  auto pubs = get_published(dummy_client.get());
  EXPECT_EQ(1u, pubs.size());
  ASSERT_LE(1u, pubs.size());
  EXPECT_TRUE(compare_minimal_txpool(epee::to_span(events), pubs.front()));

  EXPECT_NO_THROW(cryptonote::listener::zmq_pub::txpool_add{pub}(events));
  EXPECT_TRUE(pub->relay_to_pub(relay.get(), dummy_pub.get()));

  pubs = get_published(dummy_client.get());
  EXPECT_EQ(1u, pubs.size());
  ASSERT_LE(1u, pubs.size());
  EXPECT_TRUE(compare_minimal_txpool(epee::to_span(events), pubs.front()));

  events.at(0).res = false;
  EXPECT_EQ(1u, pub->send_txpool_add(events));
  EXPECT_TRUE(pub->relay_to_pub(relay.get(), dummy_pub.get()));

  pubs = get_published(dummy_client.get());
  EXPECT_EQ(1u, pubs.size());
  ASSERT_LE(1u, pubs.size());
  EXPECT_TRUE(compare_minimal_txpool(epee::to_span(events), pubs.front()));

  events.at(0).res = false;
  EXPECT_NO_THROW(cryptonote::listener::zmq_pub::txpool_add{pub}(events));
  EXPECT_TRUE(pub->relay_to_pub(relay.get(), dummy_pub.get()));

  pubs = get_published(dummy_client.get());
  EXPECT_EQ(1u, pubs.size());
  ASSERT_LE(1u, pubs.size());
  EXPECT_TRUE(compare_minimal_txpool(epee::to_span(events), pubs.front()));
}

TEST_F(zmq_pub, JsonFullChain)
{
  static constexpr const char topic[] = "\1json-full-chain_main";

  ASSERT_TRUE(sub_request(topic));

  const std::array<cryptonote::block, 2> blocks{{make_block(), make_block()}};

  EXPECT_EQ(1u, pub->send_chain_main(100, epee::to_span(blocks)));
  EXPECT_TRUE(pub->relay_to_pub(relay.get(), dummy_pub.get()));

  auto pubs = get_published(dummy_client.get());
  EXPECT_EQ(1u, pubs.size());
  ASSERT_LE(1u, pubs.size());
  EXPECT_TRUE(compare_full_block(epee::to_span(blocks), pubs.front()));

  EXPECT_NO_THROW(cryptonote::listener::zmq_pub::chain_main{pub}(533, epee::to_span(blocks)));
  EXPECT_TRUE(pub->relay_to_pub(relay.get(), dummy_pub.get()));

  pubs = get_published(dummy_client.get());
  EXPECT_EQ(1u, pubs.size());
  ASSERT_LE(1u, pubs.size());
  EXPECT_TRUE(compare_full_block(epee::to_span(blocks), pubs.front()));
}

TEST_F(zmq_pub, JsonFullMinerData)
{
/*  uint8_t major_version;
    uint64_t height;
    const crypto::hash& prev_id;
    const crypto::hash& seed_hash;
    cryptonote::difficulty_type diff;
    uint64_t median_weight;
    uint64_t already_generated_coins;
    const std::vector<cryptonote::tx_block_template_backlog_entry>& tx_backlog ; */

  static constexpr const char topic[] = "\1json-full-miner_data";
  ASSERT_TRUE(sub_request(topic));

    //std::size_t send_miner_data(uint8_t major_version, uint64_t height, const crypto::hash& prev_id, const crypto::hash& seed_hash, difficulty_type diff, uint64_t median_weight, uint64_t already_generated_coins, const std::vector<tx_block_template_backlog_entry>& tx_backlog);
  
  const auto hash = crypto::rand<crypto::hash>();
  const auto seed = crypto::rand<crypto::hash>();
  const cryptonote::difficulty_type difficulty = 500;
  const std::vector<cryptonote::tx_block_template_backlog_entry> txs = {
    {crypto::rand<crypto::hash>(), 7545, 455},
    {crypto::rand<crypto::hash>(), 755, 34545}
  };
  const std::string expected =
    R"({
      "major_version": 100,
      "height": 200,
      "prev_id": ")" + epee::to_hex::string(epee::as_byte_span(hash)) + R"(",
      "seed_hash": ")" + epee::to_hex::string(epee::as_byte_span(seed)) + R"(",
      "difficulty": ")" + cryptonote::hex(difficulty) + R"(",
      "median_weight": 400,
      "already_generated_coins": 10000,
      "tx_backlog": [
        {
          "id": ")" + epee::to_hex::string(epee::as_byte_span(txs.at(0).id)) + R"(",
          "weight": 7545,
          "fee": 455
        },{
          "id": ")" + epee::to_hex::string(epee::as_byte_span(txs.at(1).id)) + R"(",
          "weight": 755,
          "fee": 34545
        }
      ]
    })";
  EXPECT_EQ(1u, pub->send_miner_data(100, 200, hash, seed, difficulty, 400, 10000, txs));
  EXPECT_TRUE(pub->relay_to_pub(relay.get(), dummy_pub.get()));

  auto pubs = get_published(dummy_client.get());
  ASSERT_EQ(1u, pubs.size());
  EXPECT_TRUE(compare_miner_data(expected, pubs.front()));

  EXPECT_NO_THROW(cryptonote::listener::zmq_pub::miner_data{pub}(100, 200, hash, seed, difficulty, 400, 10000, txs));
  EXPECT_TRUE(pub->relay_to_pub(relay.get(), dummy_pub.get()));

  pubs = get_published(dummy_client.get());
  ASSERT_EQ(1u, pubs.size());
  EXPECT_TRUE(compare_miner_data(expected, pubs.front()));
}


TEST_F(zmq_pub, JsonMinimalChain)
{
  static constexpr const char topic[] = "\1json-minimal-chain_main";

  ASSERT_TRUE(sub_request(topic));

  const std::array<cryptonote::block, 2> blocks{{make_block(), make_block()}};

  EXPECT_EQ(1u, pub->send_chain_main(100, epee::to_span(blocks)));
  EXPECT_TRUE(pub->relay_to_pub(relay.get(), dummy_pub.get()));

  auto pubs = get_published(dummy_client.get());
  EXPECT_EQ(1u, pubs.size());
  ASSERT_LE(1u, pubs.size());
  EXPECT_TRUE(compare_minimal_block(100, epee::to_span(blocks), pubs.front()));

  EXPECT_NO_THROW(cryptonote::listener::zmq_pub::chain_main{pub}(533, epee::to_span(blocks)));
  EXPECT_TRUE(pub->relay_to_pub(relay.get(), dummy_pub.get()));

  pubs = get_published(dummy_client.get());
  EXPECT_EQ(1u, pubs.size());
  ASSERT_LE(1u, pubs.size());
  EXPECT_TRUE(compare_minimal_block(533, epee::to_span(blocks), pubs.front()));
}

TEST_F(zmq_pub, JsonFullAll)
{
  static constexpr const char topic[] = "\1json-full";

  ASSERT_TRUE(sub_request(topic));
  {
    std::vector<cryptonote::txpool_event> events
    {
     {make_transaction(), {}, true}, {make_transaction(), {}, true}
    };

    EXPECT_EQ(1u, pub->send_txpool_add(events));
    EXPECT_TRUE(pub->relay_to_pub(relay.get(), dummy_pub.get()));

    auto pubs = get_published(dummy_client.get());
    EXPECT_EQ(1u, pubs.size());
    ASSERT_LE(1u, pubs.size());
    EXPECT_TRUE(compare_full_txpool(epee::to_span(events), pubs.front()));

    EXPECT_NO_THROW(cryptonote::listener::zmq_pub::txpool_add{pub}(events));
    EXPECT_TRUE(pub->relay_to_pub(relay.get(), dummy_pub.get()));

    pubs = get_published(dummy_client.get());
    EXPECT_EQ(1u, pubs.size());
    ASSERT_LE(1u, pubs.size());
    EXPECT_TRUE(compare_full_txpool(epee::to_span(events), pubs.front()));

    events.at(0).res = false;
    EXPECT_NO_THROW(pub->send_txpool_add(events));
    EXPECT_TRUE(pub->relay_to_pub(relay.get(), dummy_pub.get()));

    pubs = get_published(dummy_client.get());
    EXPECT_EQ(1u, pubs.size());
    ASSERT_LE(1u, pubs.size());
    EXPECT_TRUE(compare_full_txpool(epee::to_span(events), pubs.front()));

    events.at(0).res = false;
    EXPECT_NO_THROW(cryptonote::listener::zmq_pub::txpool_add{pub}(events));
    EXPECT_TRUE(pub->relay_to_pub(relay.get(), dummy_pub.get()));

    pubs = get_published(dummy_client.get());
    EXPECT_EQ(1u, pubs.size());
    ASSERT_LE(1u, pubs.size());
    EXPECT_TRUE(compare_full_txpool(epee::to_span(events), pubs.front()));
  }
  {
    const std::array<cryptonote::block, 2> blocks{{make_block(), make_block()}};

    EXPECT_EQ(1u, pub->send_chain_main(100, epee::to_span(blocks)));
    EXPECT_TRUE(pub->relay_to_pub(relay.get(), dummy_pub.get()));

    auto pubs = get_published(dummy_client.get());
    EXPECT_EQ(1u, pubs.size());
    ASSERT_LE(1u, pubs.size());
    EXPECT_TRUE(compare_full_block(epee::to_span(blocks), pubs.front()));

    EXPECT_NO_THROW(cryptonote::listener::zmq_pub::chain_main{pub}(533, epee::to_span(blocks)));
    EXPECT_TRUE(pub->relay_to_pub(relay.get(), dummy_pub.get()));

    pubs = get_published(dummy_client.get());
    EXPECT_EQ(1u, pubs.size());
    ASSERT_LE(1u, pubs.size());
    EXPECT_TRUE(compare_full_block(epee::to_span(blocks), pubs.front()));
  }
}

TEST_F(zmq_pub, JsonMinimalAll)
{
  static constexpr const char topic[] = "\1json-minimal";

  ASSERT_TRUE(sub_request(topic));

  {
    std::vector<cryptonote::txpool_event> events
    {
     {make_transaction(), {}, true}, {make_transaction(), {}, true}
    };

    EXPECT_EQ(1u, pub->send_txpool_add(events));
    EXPECT_TRUE(pub->relay_to_pub(relay.get(), dummy_pub.get()));

    auto pubs = get_published(dummy_client.get());
    EXPECT_EQ(1u, pubs.size());
    ASSERT_LE(1u, pubs.size());
    EXPECT_TRUE(compare_minimal_txpool(epee::to_span(events), pubs.front()));

    EXPECT_NO_THROW(cryptonote::listener::zmq_pub::txpool_add{pub}(events));
    EXPECT_TRUE(pub->relay_to_pub(relay.get(), dummy_pub.get()));

    pubs = get_published(dummy_client.get());
    EXPECT_EQ(1u, pubs.size());
    ASSERT_LE(1u, pubs.size());
    EXPECT_TRUE(compare_minimal_txpool(epee::to_span(events), pubs.front()));

    events.at(0).res = false;
    EXPECT_NO_THROW(pub->send_txpool_add(events));
    EXPECT_TRUE(pub->relay_to_pub(relay.get(), dummy_pub.get()));

    pubs = get_published(dummy_client.get());
    EXPECT_EQ(1u, pubs.size());
    ASSERT_LE(1u, pubs.size());
    EXPECT_TRUE(compare_minimal_txpool(epee::to_span(events), pubs.front()));

    events.at(0).res = false;
    EXPECT_NO_THROW(cryptonote::listener::zmq_pub::txpool_add{pub}(events));
    EXPECT_TRUE(pub->relay_to_pub(relay.get(), dummy_pub.get()));

    pubs = get_published(dummy_client.get());
    EXPECT_EQ(1u, pubs.size());
    ASSERT_LE(1u, pubs.size());
    EXPECT_TRUE(compare_minimal_txpool(epee::to_span(events), pubs.front()));
  }
  {
    const std::array<cryptonote::block, 2> blocks{{make_block(), make_block()}};

    EXPECT_EQ(1u, pub->send_chain_main(100, epee::to_span(blocks)));
    EXPECT_TRUE(pub->relay_to_pub(relay.get(), dummy_pub.get()));

    auto pubs = get_published(dummy_client.get());
    EXPECT_EQ(1u, pubs.size());
    ASSERT_LE(1u, pubs.size());
    EXPECT_TRUE(compare_minimal_block(100, epee::to_span(blocks), pubs.front()));

    EXPECT_NO_THROW(cryptonote::listener::zmq_pub::chain_main{pub}(533, epee::to_span(blocks)));
    EXPECT_TRUE(pub->relay_to_pub(relay.get(), dummy_pub.get()));

    pubs = get_published(dummy_client.get());
    EXPECT_EQ(1u, pubs.size());
    ASSERT_LE(1u, pubs.size());
    EXPECT_TRUE(compare_minimal_block(533, epee::to_span(blocks), pubs.front()));
  }
}

TEST_F(zmq_pub, JsonAll)
{
  static constexpr const char topic[] = "\1json";

  ASSERT_TRUE(sub_request(topic));

  {
    std::vector<cryptonote::txpool_event> events
    {
     {make_transaction(), {}, true}, {make_transaction(), {}, true}
    };

    EXPECT_EQ(1u, pub->send_txpool_add(events));
    EXPECT_TRUE(pub->relay_to_pub(relay.get(), dummy_pub.get()));

    auto pubs = get_published(dummy_client.get());
    EXPECT_EQ(2u, pubs.size());
    ASSERT_LE(2u, pubs.size());
    EXPECT_TRUE(compare_full_txpool(epee::to_span(events), pubs.front()));
    EXPECT_TRUE(compare_minimal_txpool(epee::to_span(events), pubs.back()));

    EXPECT_NO_THROW(cryptonote::listener::zmq_pub::txpool_add{pub}(events));
    EXPECT_TRUE(pub->relay_to_pub(relay.get(), dummy_pub.get()));

    pubs = get_published(dummy_client.get());
    EXPECT_EQ(2u, pubs.size());
    ASSERT_LE(2u, pubs.size());
    EXPECT_TRUE(compare_full_txpool(epee::to_span(events), pubs.front()));
    EXPECT_TRUE(compare_minimal_txpool(epee::to_span(events), pubs.back()));

    events.at(0).res = false;
    EXPECT_EQ(1u, pub->send_txpool_add(events));
    EXPECT_TRUE(pub->relay_to_pub(relay.get(), dummy_pub.get()));

    pubs = get_published(dummy_client.get());
    EXPECT_EQ(2u, pubs.size());
    ASSERT_LE(2u, pubs.size());
    EXPECT_TRUE(compare_full_txpool(epee::to_span(events), pubs.front()));
    EXPECT_TRUE(compare_minimal_txpool(epee::to_span(events), pubs.back()));

    events.at(0).res = false;
    EXPECT_NO_THROW(cryptonote::listener::zmq_pub::txpool_add{pub}(events));
    EXPECT_TRUE(pub->relay_to_pub(relay.get(), dummy_pub.get()));

    pubs = get_published(dummy_client.get());
    EXPECT_EQ(2u, pubs.size());
    ASSERT_LE(2u, pubs.size());
    EXPECT_TRUE(compare_full_txpool(epee::to_span(events), pubs.front()));
    EXPECT_TRUE(compare_minimal_txpool(epee::to_span(events), pubs.back()));
  }
  {
    const std::array<cryptonote::block, 1> blocks{{make_block()}};

    EXPECT_EQ(2u, pub->send_chain_main(100, epee::to_span(blocks)));
    EXPECT_TRUE(pub->relay_to_pub(relay.get(), dummy_pub.get()));
    EXPECT_TRUE(pub->relay_to_pub(relay.get(), dummy_pub.get()));

    auto pubs = get_published(dummy_client.get());
    EXPECT_EQ(2u, pubs.size());
    ASSERT_LE(2u, pubs.size());
    EXPECT_TRUE(compare_full_block(epee::to_span(blocks), pubs.front()));
    EXPECT_TRUE(compare_minimal_block(100, epee::to_span(blocks), pubs.back()));

    EXPECT_NO_THROW(cryptonote::listener::zmq_pub::chain_main{pub}(533, epee::to_span(blocks)));
    EXPECT_TRUE(pub->relay_to_pub(relay.get(), dummy_pub.get()));
    EXPECT_TRUE(pub->relay_to_pub(relay.get(), dummy_pub.get()));

    pubs = get_published(dummy_client.get());
    EXPECT_EQ(2u, pubs.size());
    ASSERT_LE(2u, pubs.size());
    EXPECT_TRUE(compare_full_block(epee::to_span(blocks), pubs.front()));
    EXPECT_TRUE(compare_minimal_block(533, epee::to_span(blocks), pubs.back()));
  }
}

TEST_F(zmq_pub, JsonChainWeakPtrSkip)
{
  static constexpr const char topic[] = "\1json";

  ASSERT_TRUE(sub_request(topic));

  const std::array<cryptonote::block, 1> blocks{{make_block()}};

  pub.reset();
  EXPECT_NO_THROW(cryptonote::listener::zmq_pub::chain_main{pub}(533, epee::to_span(blocks)));
}

TEST_F(zmq_pub, JsonTxpoolWeakPtrSkip)
{
  static constexpr const char topic[] = "\1json";

  ASSERT_TRUE(sub_request(topic));

  std::vector<cryptonote::txpool_event> events
  {
    {make_transaction(), {}, true}, {make_transaction(), {}, true}
  };

  pub.reset();
  EXPECT_NO_THROW(cryptonote::listener::zmq_pub::txpool_add{pub}(std::move(events)));
}

TEST_F(zmq_server, pub)
{
  subscribe("json-minimal");

  std::vector<cryptonote::txpool_event> events
  {
   {make_transaction(), {}, true}, {make_transaction(), {}, true}
  };

  const std::array<cryptonote::block, 1> blocks{{make_block()}};

  ASSERT_EQ(1u, pub->send_txpool_add(events));
  ASSERT_EQ(1u, pub->send_chain_main(200, epee::to_span(blocks)));

  auto pubs = get_published(sub.get(), 2);
  EXPECT_EQ(2u, pubs.size());
  ASSERT_LE(2u, pubs.size());
  EXPECT_TRUE(compare_minimal_txpool(epee::to_span(events), pubs.front()));
  EXPECT_TRUE(compare_minimal_block(200, epee::to_span(blocks), pubs.back()));
}
