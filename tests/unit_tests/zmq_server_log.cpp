// Copyright (c) 2016-2024, The Monero Project
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

/*! \file zmq_server_log.cpp

  Log-safety regression coverage for the ZMQ RPC transport sink in
  `src/rpc/zmq_server.cpp`.

  `ZmqServer::serve` reads request frames from an unauthenticated `ZMQ_REP`
  listener (up to `net::zmq::max_message_size`, 10 MiB) and writes reply frames
  back. Both frames are attacker-influenced, so neither may reach the log:
  embedded CR/LF forge or split log records - the daemon's own log dispatcher
  splits a message on every `\n` into a separate record - and ESC sequences
  reach an operator's terminal (CWE-117), while any retained fragment parks raw
  request, transaction and peer data in the log (CWE-532). Only the frame
  length is safe to record.

  These tests drive a real request/reply round trip through a real `ZmqServer`
  over loopback TCP, capture the log records the server actually dispatches
  from `zmq_server.cpp` with an `el::LogDispatchCallback`, and assert by exact
  string equality that the two transport records are byte counts and nothing
  else. Both server modes are covered, because the records must be identical in
  each: `restricted` must not be the difference between a safe log and an
  unsafe one. Ordering against a sentinel the stub handler emits proves the
  request record is written before dispatch and the reply record before the
  frame is sent, i.e. that the ingress and egress paths are the ones covered.
*/

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <mutex>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "byte_slice.h"
#include "misc_log_ex.h"
#include "net/zmq.h"
#include "rpc/rpc_handler.h"
#include "rpc/zmq_server.h"

namespace
{
  //! Marker planted in the request frame; must never appear in a log record.
  constexpr const char request_marker[] = "REQ_PAYLOAD_MARKER_5f3a9c";
  //! Marker planted in the reply frame; must never appear in a log record.
  constexpr const char reply_marker[] = "REPLY_PAYLOAD_MARKER_c93a5f";
  //! Pushed into the capture by the stub handler, to order the two records.
  constexpr const char handler_sentinel[] = "HANDLER_INVOKED";

  /* Frame sizes are large - so a verbatim log would be unmistakable - and
     unequal, so neither byte-count assertion can pass on the other record's
     text. Both stay well under `net::zmq::max_message_size` so the server's
     `ZMQ_MAXMSGSIZE` guard is not what this test exercises. */
  constexpr const std::size_t request_frame_size = 96 * 1024;
  constexpr const std::size_t reply_frame_size = 64 * 1024;
  static_assert(request_frame_size != reply_frame_size, "sizes must be distinguishable");
  static_assert(request_frame_size < net::zmq::max_message_size, "request must be accepted");
  static_assert(reply_frame_size < net::zmq::max_message_size, "reply must be sendable");

  //! Bounds every blocking client call so a regression fails instead of hanging.
  constexpr const int client_timeout_ms = 30000;

  //! Identifier the capture callback is installed under.
  constexpr const char sink_id[] = "unit_tests::zmq_server_log";

  //! Log category `src/rpc/zmq_server.cpp` writes under, at `DEBUG`.
  constexpr const char debug_categories[] = "net.zmq:DEBUG";

  /*! \return A `size` byte payload holding `marker` twice and a run of raw
    control bytes - CR, LF, ESC, NUL, TAB and DEL - at both the front and the
    very end, padded with printable filler. Anything that copies part of this
    frame into a log record therefore trips either the marker assertion or the
    control-byte assertion, wherever in the frame it copied from. */
  std::string build_payload(const char* const marker, const std::size_t size)
  {
    static constexpr const char control_bytes[] = {'\r', '\n', '\x1b', '[', '3', '1', 'm', '\0', '\t', '\x7f'};
    static constexpr const char filler[] = "0123456789abcdef";

    std::string out;
    out.reserve(size);
    out.append(marker);
    out.append(control_bytes, sizeof(control_bytes));
    out.append(marker);
    if (size < out.size() + sizeof(control_bytes))
      throw std::logic_error{"payload size too small for its markers"};

    while (out.size() < size - sizeof(control_bytes))
      out.push_back(filler[out.size() % (sizeof(filler) - 1)]);
    out.append(control_bytes, sizeof(control_bytes));

    if (out.size() != size)
      throw std::logic_error{"payload construction did not reach the requested size"};
    return out;
  }

  //! One captured log record, or the handler sentinel, in the order it occurred.
  struct capture_entry
  {
    bool is_log_record; //!< `false` for the handler sentinel.
    el::Level level;    //!< Meaningless when `is_log_record` is `false`.
    std::string text;
  };

  /*! Ordered capture shared by the log callback (which easylogging++ builds
      itself, so it cannot be handed a reference) and the stub handler. Both
      write from the server thread while the test thread reads after the join;
      the mutex makes that safe regardless, and nothing writing to it logs, so
      it cannot invert against the dispatcher's own lock. */
  class capture_state
  {
  public:
    void clear()
    {
      const std::lock_guard<std::mutex> lock{sync};
      entries.clear();
      request_seen.clear();
    }

    void add_log_record(const el::Level level, std::string text)
    {
      const std::lock_guard<std::mutex> lock{sync};
      entries.push_back(capture_entry{true, level, std::move(text)});
    }

    void add_sentinel(std::string text)
    {
      const std::lock_guard<std::mutex> lock{sync};
      entries.push_back(capture_entry{false, el::Level::Unknown, std::move(text)});
    }

    //! Records the frame the handler was dispatched, to prove it arrived intact.
    void set_request_seen(std::string request)
    {
      const std::lock_guard<std::mutex> lock{sync};
      request_seen = std::move(request);
    }

    std::vector<capture_entry> snapshot() const
    {
      const std::lock_guard<std::mutex> lock{sync};
      return entries;
    }

    std::string request_delivered() const
    {
      const std::lock_guard<std::mutex> lock{sync};
      return request_seen;
    }

  private:
    mutable std::mutex sync;
    std::vector<capture_entry> entries;
    std::string request_seen;
  };

  capture_state& capture()
  {
    static capture_state state;
    return state;
  }

  /*! \return Whether `file` - a `__FILE__` expansion, absolute or relative -
    names `zmq_server.cpp` itself. The whole basename must match, so neither
    this test file nor any other `*zmq_server.cpp` suffix is picked up. */
  bool names_transport_source(const std::string& file)
  {
    static constexpr const char expected[] = "zmq_server.cpp";
    constexpr const std::size_t width = sizeof(expected) - 1;

    if (file.size() < width || file.compare(file.size() - width, width, expected) != 0)
      return false;
    if (file.size() == width)
      return true;

    const char preceding = file[file.size() - width - 1];
    return preceding == '/' || preceding == '\\';
  }

  //! Collects every record the ZMQ transport source dispatches, at any level.
  class transport_log_sink final : public el::LogDispatchCallback
  {
  protected:
    void handle(const el::LogDispatchData* data) override
    {
      if (data == nullptr)
        return;

      const el::LogMessage* const record = data->logMessage();
      if (record == nullptr || !names_transport_source(record->file()))
        return;

      capture().add_log_record(record->level(), record->message());
    }
  };

  //! Installs the capture for one test and removes it again, come what may.
  class scoped_log_sink
  {
  public:
    scoped_log_sink()
    {
      capture().clear();
      if (!el::Helpers::installLogDispatchCallback<transport_log_sink>(sink_id))
        throw std::runtime_error{"failed to install the zmq_server.cpp log capture"};

      transport_log_sink* const sink = el::Helpers::logDispatchCallback<transport_log_sink>(sink_id);
      if (sink == nullptr)
        throw std::runtime_error{"log capture vanished after installation"};
      sink->setEnabled(true);
    }

    scoped_log_sink(const scoped_log_sink&) = delete;
    scoped_log_sink& operator=(const scoped_log_sink&) = delete;

    ~scoped_log_sink()
    {
      el::Helpers::uninstallLogDispatchCallback<transport_log_sink>(sink_id);
    }
  };

  /*! Enables `net.zmq` at `DEBUG` for one test and puts the process-wide
      category configuration back afterwards - `MDEBUG` in `zmq_server.cpp` is
      filtered out by the unit-test default, so without this the capture would
      see nothing. */
  class scoped_log_categories
  {
  public:
    explicit scoped_log_categories(const char* const categories)
      : saved(mlog_get_categories())
    {
      mlog_set_categories(categories);
    }

    scoped_log_categories(const scoped_log_categories&) = delete;
    scoped_log_categories& operator=(const scoped_log_categories&) = delete;

    ~scoped_log_categories()
    {
      mlog_set_categories(saved.c_str());
    }

  private:
    const std::string saved;
  };

  /*! Stub handler: records the frame it was handed, marks the capture at the
      moment it runs, and replies with a large frame full of control bytes. */
  class marking_handler final : public cryptonote::rpc::RpcHandler
  {
  public:
    explicit marking_handler(std::string reply)
      : cryptonote::rpc::RpcHandler(), reply(std::move(reply))
    {}

    epee::byte_slice handle(std::string&& request) override
    {
      capture().set_request_seen(request);
      capture().add_sentinel(handler_sentinel);
      return epee::byte_slice{std::string{reply}};
    }

  private:
    const std::string reply;
  };

  /*! Owns a running `ZmqServer` and guarantees `stop()` - which joins the
      server thread - runs even when an assertion returns early, because
      `~ZmqServer` would otherwise leave the thread detached and holding a
      reference to a destroyed handler. */
  class scoped_server
  {
  public:
    scoped_server(cryptonote::rpc::RpcHandler& handler, const bool restricted)
      : server(handler, restricted)
    {}

    scoped_server(const scoped_server&) = delete;
    scoped_server& operator=(const scoped_server&) = delete;

    ~scoped_server()
    {
      server.stop();
    }

    cryptonote::rpc::ZmqServer server;
  };

  //! \return Every `is_log_record` entry of `entries` at `level`.
  std::vector<std::string> records_at(const std::vector<capture_entry>& entries, const el::Level level)
  {
    std::vector<std::string> out;
    for (const capture_entry& entry : entries)
    {
      if (entry.is_log_record && entry.level == level)
        out.push_back(entry.text);
    }
    return out;
  }

  //! \return Index of the single entry whose text is `text`, or `entries.size()`.
  std::size_t sole_index_of(const std::vector<capture_entry>& entries, const std::string& text)
  {
    std::size_t found = entries.size();
    for (std::size_t i = 0; i < entries.size(); ++i)
    {
      if (entries[i].text != text)
        continue;
      if (found != entries.size())
        return entries.size(); // more than one match is not a unique position
      found = i;
    }
    return found;
  }

  /*! Runs one full round trip against a server in `restricted` mode listening
      on `port`, then asserts the log records it produced. Every failure is
      fatal and loud: a bind, connect, send or receive that does not happen is
      a failed test, never a skipped one. */
  void run_transport_log_case(const bool restricted, const char* const port)
  {
    const std::string request = build_payload(request_marker, request_frame_size);
    const std::string reply = build_payload(reply_marker, reply_frame_size);
    ASSERT_EQ(request_frame_size, request.size());
    ASSERT_EQ(reply_frame_size, reply.size());

    const scoped_log_categories categories{debug_categories};
    const scoped_log_sink sink{};

    marking_handler handler{reply};
    scoped_server owner{handler, restricted};

    ASSERT_NE(nullptr, owner.server.init_rpc("127.0.0.1", port))
      << "failed to bind the ZMQ RPC listener on 127.0.0.1:" << port;
    owner.server.run();

    /* The client gets its own context so that terminating it cannot block on
       the server's sockets, and vice versa. */
    const net::zmq::context client_context{zmq_ctx_new()};
    ASSERT_NE(nullptr, client_context.get());

    net::zmq::socket client{zmq_socket(client_context.get(), ZMQ_REQ)};
    ASSERT_NE(nullptr, client.get());

    static constexpr const int no_linger = 0;
    ASSERT_EQ(0, zmq_setsockopt(client.get(), ZMQ_LINGER, std::addressof(no_linger), sizeof(no_linger)));
    ASSERT_EQ(0, zmq_setsockopt(client.get(), ZMQ_RCVTIMEO, std::addressof(client_timeout_ms), sizeof(client_timeout_ms)));
    ASSERT_EQ(0, zmq_setsockopt(client.get(), ZMQ_SNDTIMEO, std::addressof(client_timeout_ms), sizeof(client_timeout_ms)));

    const std::string endpoint = std::string{"tcp://127.0.0.1:"} + port;
    ASSERT_EQ(0, zmq_connect(client.get(), endpoint.c_str())) << "failed to connect to " << endpoint;

    ASSERT_EQ(int(request.size()), zmq_send(client.get(), request.data(), request.size(), 0))
      << "failed to send the request frame";

    const expect<std::string> received = net::zmq::receive(client.get());
    ASSERT_TRUE(bool(received)) << "no reply frame: " << received.error().message();
    EXPECT_EQ(reply, *received) << "the reply frame was altered on the wire";
    EXPECT_EQ(request, capture().request_delivered()) << "the request frame was altered before dispatch";

    client.reset();
    owner.server.stop(); // joins the server thread: the capture is complete after this

    const std::vector<capture_entry> entries = capture().snapshot();
    const std::vector<std::string> transport_records = records_at(entries, el::Level::Debug);

    /* Exactly two - no more, no fewer. Fewer means the capture matched nothing
       and every assertion below would be vacuous; more means the transport
       gained a log record this test has not vetted. */
    ASSERT_EQ(2u, transport_records.size())
      << "expected exactly the two transport records from zmq_server.cpp";

    /* Exact equality is the point: it admits the byte count and rejects any
       payload excerpt, escaped or not, and it pins the count to the true frame
       length rather than to whatever the server chose to measure. */
    EXPECT_EQ("Received RPC request (" + std::to_string(request.size()) + " bytes)", transport_records.front());
    EXPECT_EQ("Sending RPC reply (" + std::to_string(reply.size()) + " bytes)", transport_records.back());

    /* Every record from that source at any level - the two transport records
       and the listener's own lifecycle records - must be free of payload and
       of anything that can forge a record or drive a terminal. */
    std::size_t inspected = 0;
    for (const capture_entry& entry : entries)
    {
      if (!entry.is_log_record)
        continue;
      ++inspected;

      const char* const level = el::LevelHelper::convertToString(entry.level);
      EXPECT_EQ(std::string::npos, entry.text.find(request_marker))
        << level << " record leaks request payload: " << entry.text;
      EXPECT_EQ(std::string::npos, entry.text.find(reply_marker))
        << level << " record leaks reply payload: " << entry.text;

      for (std::size_t i = 0; i < entry.text.size(); ++i)
      {
        const unsigned char byte = static_cast<unsigned char>(entry.text[i]);
        EXPECT_TRUE(0x20 <= byte && byte != 0x7f)
          << level << " record carries control byte 0x" << std::hex << unsigned(byte) << std::dec
          << " at offset " << i;
      }
    }
    EXPECT_LE(2u, inspected) << "no log record from zmq_server.cpp was inspected";

    /* Ingress before dispatch, egress before the send: the handler ran between
       the two records, so the request record cannot have been written after
       the frame was parsed and the reply record cannot have been written after
       the frame left. */
    const std::size_t request_at = sole_index_of(entries, transport_records.front());
    const std::size_t sentinel_at = sole_index_of(entries, handler_sentinel);
    const std::size_t reply_at = sole_index_of(entries, transport_records.back());
    ASSERT_NE(entries.size(), request_at) << "request record not uniquely located";
    ASSERT_NE(entries.size(), sentinel_at) << "handler ran zero or several times";
    ASSERT_NE(entries.size(), reply_at) << "reply record not uniquely located";
    EXPECT_LT(request_at, sentinel_at) << "the request record must precede dispatch";
    EXPECT_LT(sentinel_at, reply_at) << "the reply record must precede the send";
  }
} // anonymous

/* One port per test: `ZMQ_LINGER` on the server socket is two seconds, so
   reusing a single port across both tests would be flaky. Both ports are
   otherwise unused in tests/unit_tests. */

TEST(zmq_server_logging, RestrictedLogsFrameCountsOnly)
{
  run_transport_log_case(true, "19093");
}

TEST(zmq_server_logging, UnrestrictedLogsFrameCountsOnly)
{
  run_transport_log_case(false, "19094");
}
