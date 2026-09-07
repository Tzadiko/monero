// Copyright (c) 2014-2024, The Monero Project
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
// 
// Parts of this file are originally copyright (c) 2012-2013 The Cryptonote developers

#include <boost/asio/post.hpp>
#include <boost/chrono/chrono.hpp>
#include <boost/thread/condition_variable.hpp>
#include <boost/thread/mutex.hpp>
#include <condition_variable>
#include <mutex>
#include <thread>

#include "gtest/gtest.h"

#include "cryptonote_protocol/cryptonote_protocol_defs.h"
#include "include_base_utils.h"
#include "string_tools.h"
#include "net/abstract_tcp_server2.h"
#include "net/levin_protocol_handler_async.h"
#include "p2p/net_node.h"

// Used directly by the SSL fingerprint test below rather than relied upon
// transitively through "net/abstract_tcp_server2.h": <algorithm> for the ordering
// assertions, <atomic> for the flag that cancels the peer's handshake, <exception>
// to carry an exception out of the server thread, <functional> and <system_error>
// for the thread joiner, <ostream> to print the test's own outcome type, <utility>
// for std::move, the two OpenSSL headers for SSL_CTX_get0_certificate() and
// X509_digest(), and "net/net_ssl.h" for epee::net_utils::ssl_options_t.
#include <algorithm>
#include <atomic>
#include <exception>
#include <functional>
#include <ostream>
#include <system_error>
#include <utility>
#include <openssl/ssl.h>
#include <openssl/x509.h>
#include "net/net_ssl.h"

namespace
{
  const uint32_t test_server_port = 5626;
  const std::string test_server_host("127.0.0.1");

  struct test_connection_context : public epee::net_utils::connection_context_base
  {
  };

  struct test_protocol_handler_config
  {
    template<typename T>
    static constexpr bool after_init_connection(const std::shared_ptr<T>&) noexcept
    {
      return true;
    }
  };

  struct test_protocol_handler
  {
    typedef test_connection_context connection_context;
    typedef test_protocol_handler_config config_type;

    test_protocol_handler(epee::net_utils::i_service_endpoint* /*psnd_hndlr*/, config_type& /*config*/, connection_context& /*conn_context*/)
    {
    }

    void handle_qued_callback()
    {
    }

    bool release_protocol()
    {
      return true;
    }

    bool handle_recv(const void* /*data*/, size_t /*size*/)
    {
      return false;
    }
  };

  typedef epee::net_utils::boosted_tcp_server<test_protocol_handler> test_tcp_server;
}

TEST(boosted_tcp_server, worker_threads_are_exception_resistant)
{
  test_tcp_server srv(epee::net_utils::e_connection_type_RPC); // RPC disables network limit for unit tests
  ASSERT_TRUE(srv.init_server(test_server_port, test_server_host));

  boost::mutex mtx;
  boost::condition_variable cond;
  int counter = 0;

  auto counter_incrementer = [&counter, &cond, &mtx]()
  {
    boost::unique_lock<boost::mutex> lock(mtx);
    ++counter;
    if (4 <= counter)
    {
      cond.notify_one();
    }
  };

  // 2 threads, but 4 exceptions
  ASSERT_TRUE(srv.run_server(2, false));
  ASSERT_TRUE(srv.async_call([&counter_incrementer]() { counter_incrementer(); throw std::runtime_error("test 1"); }));
  ASSERT_TRUE(srv.async_call([&counter_incrementer]() { counter_incrementer(); throw std::string("test 2"); }));
  ASSERT_TRUE(srv.async_call([&counter_incrementer]() { counter_incrementer(); throw "test 3"; }));
  ASSERT_TRUE(srv.async_call([&counter_incrementer]() { counter_incrementer(); throw 4; }));

  {
    boost::unique_lock<boost::mutex> lock(mtx);
    ASSERT_TRUE(cond.wait_for(lock, boost::chrono::seconds(5), [&counter]{ return counter == 4; }));
  }

  // Check if threads are alive
  counter = 0;
  //auto counter_incrementer = [&counter]() { counter.fetch_add(1); epee::misc_utils::sleep_no_w(counter.load() * 10); };
  ASSERT_TRUE(srv.async_call(counter_incrementer));
  ASSERT_TRUE(srv.async_call(counter_incrementer));
  ASSERT_TRUE(srv.async_call(counter_incrementer));
  ASSERT_TRUE(srv.async_call(counter_incrementer));

  {
    boost::unique_lock<boost::mutex> lock(mtx);
    ASSERT_TRUE(cond.wait_for(lock, boost::chrono::seconds(5), [&counter]{ return counter == 4; }));
  }

  srv.send_stop_signal();
  ASSERT_TRUE(srv.timed_wait_server_stop(5 * 1000));
  ASSERT_TRUE(srv.deinit_server());
}


TEST(test_epee_connection, test_lifetime)
{
  struct context_t: epee::net_utils::connection_context_base {
    static constexpr size_t get_max_bytes(int) noexcept { return -1; }
    static constexpr int handshake_command() noexcept { return 1001; }
    static constexpr bool handshake_complete() noexcept { return true; }
  };

  using functional_obj_t = std::function<void ()>;
  struct command_handler_t: epee::levin::levin_commands_handler<context_t> {
    size_t delay;
    functional_obj_t on_connection_close_f;
    command_handler_t(size_t delay = 0,
      functional_obj_t on_connection_close_f = nullptr
    ):
      delay(delay),
      on_connection_close_f(on_connection_close_f)
    {}
    virtual int invoke(int, const epee::span<const uint8_t>, epee::byte_stream&, context_t&) override { epee::misc_utils::sleep_no_w(delay); return {}; }
    virtual int notify(int, const epee::span<const uint8_t>, context_t&) override { return {}; }
    virtual void callback(context_t&) override {}
    virtual void on_connection_new(context_t&) override {}
    virtual void on_connection_close(context_t&) override {
      if (on_connection_close_f)
        on_connection_close_f();
    }
    virtual ~command_handler_t() override {}
    static void destroy(epee::levin::levin_commands_handler<context_t>* ptr) { delete ptr; }
  };

  using handler_t = epee::levin::async_protocol_handler<context_t>;
  using connection_t = epee::net_utils::connection<handler_t>;
  using connection_ptr = std::shared_ptr<connection_t>;
  using shared_state_t = typename connection_t::shared_state;
  using shared_state_ptr = std::shared_ptr<shared_state_t>;
  using shared_states_t = std::vector<shared_state_ptr>;
  using tag_t = boost::uuids::uuid;
  using tags_t = std::vector<tag_t>;
  using io_context_t = boost::asio::io_context;
  using endpoint_t = boost::asio::ip::tcp::endpoint;
  using work_t = boost::asio::executor_work_guard<boost::asio::io_context::executor_type>;
  using work_ptr = std::shared_ptr<work_t>;
  using workers_t = std::vector<std::thread>;
  using server_t = epee::net_utils::boosted_tcp_server<handler_t>;
  using lock_t = std::mutex;
  using lock_guard_t = std::lock_guard<lock_t>;
  using connection_weak_ptr = std::weak_ptr<connection_t>;
  struct shared_conn_t {
    lock_t lock;
    connection_weak_ptr conn;
  };
  using shared_conn_ptr = std::shared_ptr<shared_conn_t>;

  io_context_t io_context;
  work_ptr work(std::make_shared<work_t>(io_context.get_executor()));

  workers_t workers;
  while (workers.size() < 4) {
    workers.emplace_back([&io_context]{
      io_context.run();
    });
  }

  endpoint_t endpoint(boost::asio::ip::make_address("127.0.0.1"), 5262);
  server_t server(epee::net_utils::e_connection_type_P2P);
  server.init_server(endpoint.port(),
    endpoint.address().to_string(),
    0,
    "",
    false,
    true,
    epee::net_utils::ssl_support_t::e_ssl_support_disabled
  );
  server.run_server(2, false);
  server.get_config_shared()->set_handler(new command_handler_t, &command_handler_t::destroy);

  boost::asio::post(io_context, [&io_context, &work, &endpoint, &server]{
    shared_state_ptr shared_state;
    const epee::scope_guard scope_exit_handler([&work, &shared_state]{
      work.reset();
      if (shared_state)
        shared_state->set_handler(nullptr, nullptr);
    });

    shared_state = std::make_shared<shared_state_t>();
    shared_state->set_handler(new command_handler_t, &command_handler_t::destroy);

    const auto wait_for = [](const auto& condition) {
      const auto timeout = std::chrono::steady_clock::now() + std::chrono::seconds{5};
      while (std::chrono::steady_clock::now() < timeout) {
        if (condition())
          return true;
        std::this_thread::sleep_for(std::chrono::milliseconds{1});
      }
      return condition();
    };

    auto create_connection = [&io_context, &endpoint, &shared_state] {
        connection_ptr conn(new connection_t(io_context, shared_state, {}, {}));
        conn->socket().connect(endpoint);
        EXPECT_TRUE(conn->start({}, {}));
        context_t context;
        conn->get_context(context);
        auto tag = context.m_connection_id;
        return tag;
    };

    ASSERT_TRUE(shared_state->get_connections_count() == 0);
    auto tag = create_connection();
    ASSERT_TRUE(shared_state->get_connections_count() == 1);
    bool success = shared_state->for_connection(tag, [shared_state](context_t& context){
      shared_state->close(context.m_connection_id, true);
      context.m_remote_address.get_zone();
      return true;
    });
    ASSERT_TRUE(success);

    ASSERT_TRUE(shared_state->get_connections_count() == 0);
    constexpr auto N = 8;
    tags_t tags(N);
    for(auto &t: tags)
      t = create_connection();
    ASSERT_TRUE(shared_state->get_connections_count() == N);
    size_t index = 0;
    success = shared_state->foreach_connection([&index, shared_state, &tags, &create_connection](context_t& context){
      if (!index)
        for (const auto &t: tags)
          shared_state->close(t, true);

      shared_state->close(context.m_connection_id, true);
      context.m_remote_address.get_zone();
      ++index;

      for(auto i = 0; i < N; ++i)
        create_connection();
      return true;
    });
    ASSERT_TRUE(success);
    ASSERT_TRUE(index == N);
    ASSERT_TRUE(shared_state->get_connections_count() == N * N);

    index = 0;
    success = shared_state->foreach_connection([&index, shared_state](context_t& context){
      shared_state->close(context.m_connection_id, true);
      context.m_remote_address.get_zone();
      ++index;
      return true;
    });
    ASSERT_TRUE(success);
    ASSERT_TRUE(index == N * N);
    ASSERT_TRUE(shared_state->get_connections_count() == 0);

    while (shared_state->sock_count);
    ASSERT_TRUE(shared_state->get_connections_count() == 0);
    constexpr auto DELAY = 30;
    constexpr std::chrono::milliseconds TIMEOUT{1};
    while (server.get_connections_count()) {
      server.get_config_shared()->del_in_connections(
        server.get_config_shared()->get_in_connections_count()
      );
    }
    server.get_config_shared()->set_handler(new command_handler_t(DELAY), &command_handler_t::destroy);
    for (auto i = 0; i < N; ++i) {
      tag = create_connection();
      ASSERT_TRUE(shared_state->get_connections_count() == 1);
      success = shared_state->invoke_async(1, epee::levin::message_writer{}, tag, [](int, const epee::span<const uint8_t>, context_t&){}, TIMEOUT);
      ASSERT_TRUE(success);
      while (shared_state->sock_count == 1) {
        success = shared_state->foreach_connection([&shared_state, &tag](context_t&){
          return shared_state->request_callback(tag);
        });
        ASSERT_TRUE(success);
      }
      shared_state->close(tag, true);
      ASSERT_TRUE(shared_state->get_connections_count() == 0);
    }

    while (shared_state->sock_count);
    constexpr auto ZERO_DELAY = 0;
    size_t counter = 0;
    shared_state->set_handler(new command_handler_t(ZERO_DELAY,
        [&counter]{
          ASSERT_TRUE(counter++ == 0);
        }
      ),
      &command_handler_t::destroy
    );
    connection_ptr conn(new connection_t(io_context, shared_state, {}, {}));
    conn->socket().connect(endpoint);
    conn->start({}, {});
    ASSERT_TRUE(shared_state->get_connections_count() == 1);
    shared_state->del_out_connections(1);
    ASSERT_TRUE(shared_state->get_connections_count() == 0);
    conn.reset();

    while (shared_state->sock_count);
    shared_conn_ptr shared_conn(std::make_shared<shared_conn_t>());
    shared_state->set_handler(new command_handler_t(ZERO_DELAY,
        [shared_state, shared_conn]{
          {
            connection_ptr conn;
            {
              lock_guard_t guard(shared_conn->lock);
              conn = shared_conn->conn.lock();
              shared_conn->conn.reset();
            }
            if (conn)
              conn->cancel();
          }
          const auto success = shared_state->foreach_connection([](context_t&){
            return true;
          });
          ASSERT_TRUE(success);
        }
      ),
      &command_handler_t::destroy
    );
    for (auto i = 0; i < N * N * N; ++i) {
      {
        connection_ptr conn(new connection_t(io_context, shared_state, {}, {}));
        boost::system::error_code connect_error;
        conn->socket().connect(endpoint, connect_error);
        ASSERT_FALSE(connect_error);
        conn->start({}, {});
        lock_guard_t guard(shared_conn->lock);
        shared_conn->conn = conn;
      }
      ASSERT_TRUE(shared_state->get_connections_count() == 1);
      shared_state->del_out_connections(1);
      const auto cleanup_finished = wait_for([&] {
        return shared_state->sock_count == 0
          && server.get_connections_count() == 0
          && server.get_config_shared()->get_in_connections_count() == 0;
      });
      ASSERT_TRUE(cleanup_finished);
      ASSERT_TRUE(shared_state->get_connections_count() == 0);
    }

    shared_states_t shared_states;
    while (shared_states.size() < 2) {
      shared_states.emplace_back(std::make_shared<shared_state_t>());
      shared_states.back()->set_handler(new command_handler_t(ZERO_DELAY,
          [&shared_states]{
            for (auto &s: shared_states) {
              auto success = s->foreach_connection([](context_t&){
                return true;
              });
              ASSERT_TRUE(success);
            }
          }
        ),
        &command_handler_t::destroy
      );
    }
    workers_t workers;

    for (auto &s: shared_states) {
      workers.emplace_back([&io_context, &s, &endpoint]{
        for (auto i = 0; i < N * N; ++i) {
          connection_ptr conn(new connection_t(io_context, s, {}, {}));
          conn->socket().connect(endpoint);
          conn->start({}, {});
          boost::asio::post(io_context, [conn] { conn->cancel(); });
          conn.reset();
          s->del_out_connections(1);
          while (s->sock_count);
        }
      });
    }
    for (;workers.size(); workers.pop_back())
      workers.back().join();

    for (auto &s: shared_states) {
      workers.emplace_back([&io_context, &s, &endpoint]{
        for (auto i = 0; i < N * N; ++i) {
          connection_ptr conn(new connection_t(io_context, s, {}, {}));
          conn->socket().connect(endpoint);
          conn->start({}, {});
          conn->cancel();
          while (conn.use_count() > 1);
          s->foreach_connection([&io_context, &s, &endpoint, &conn](context_t& context){
            conn.reset(new connection_t(io_context, s, {}, {}));
            conn->socket().connect(endpoint);
            conn->start({}, {});
            conn->cancel();
            while (conn.use_count() > 1);
            conn.reset();
            return true;
          });
          while (s->sock_count);
        }
      });
    }
    for (;workers.size(); workers.pop_back())
      workers.back().join();

    for (auto &s: shared_states) {
      workers.emplace_back([&io_context, &s, &endpoint]{
        for (auto i = 0; i < N; ++i) {
          connection_ptr conn(new connection_t(io_context, s, {}, {}));
          conn->socket().connect(endpoint);
          conn->start({}, {});
          context_t context;
          conn->get_context(context);
          auto tag = context.m_connection_id;
          conn->cancel();
          while (conn.use_count() > 1);
          s->for_connection(tag, [&io_context, &s, &endpoint, &conn](context_t& context){
            conn.reset(new connection_t(io_context, s, {}, {}));
            conn->socket().connect(endpoint);
            conn->start({}, {});
            conn->cancel();
            while (conn.use_count() > 1);
            conn.reset();
            return true;
          });
          while (s->sock_count);
        }
      });
    }
    for (;workers.size(); workers.pop_back())
      workers.back().join();

    for (auto &s: shared_states) {
      workers.emplace_back([&io_context, &s, &endpoint]{
        for (auto i = 0; i < N; ++i) {
          connection_ptr conn(new connection_t(io_context, s, {}, {}));
          conn->socket().connect(endpoint);
          conn->start({}, {});
          context_t context;
          conn->get_context(context);
          auto tag = context.m_connection_id;
          boost::asio::post(io_context, [conn] { conn->cancel(); });
          conn.reset();
          s->close(tag, true);
          while (s->sock_count);
        }
      });
    }
    for (;workers.size(); workers.pop_back())
      workers.back().join();
    while (server.get_connections_count()) {
      server.get_config_shared()->del_in_connections(
        server.get_config_shared()->get_in_connections_count()
      );
    }
  });

  for (auto& w: workers) {
    w.join();
  }
  server.send_stop_signal();
  server.timed_wait_server_stop(5 * 1000);
  server.deinit_server();
}

TEST(test_epee_connection, ssl_shutdown)
{
  struct context_t: epee::net_utils::connection_context_base {
    static constexpr size_t get_max_bytes(int) noexcept { return -1; }
    static constexpr int handshake_command() noexcept { return 1001; }
    static constexpr bool handshake_complete() noexcept { return true; }
  };

  struct command_handler_t: epee::levin::levin_commands_handler<context_t> {
    virtual int invoke(int, const epee::span<const uint8_t>, epee::byte_stream&, context_t&) override { return {}; }
    virtual int notify(int, const epee::span<const uint8_t>, context_t&) override { return {}; }
    virtual void callback(context_t&) override {}
    virtual void on_connection_new(context_t&) override {}
    virtual void on_connection_close(context_t&) override { }
    virtual ~command_handler_t() override {}
    static void destroy(epee::levin::levin_commands_handler<context_t>* ptr) { delete ptr; }
  };

  using handler_t = epee::levin::async_protocol_handler<context_t>;
  using io_context_t = boost::asio::io_context;
  using endpoint_t = boost::asio::ip::tcp::endpoint;
  using server_t = epee::net_utils::boosted_tcp_server<handler_t>;
  using socket_t = boost::asio::ip::tcp::socket;
  using ssl_socket_t = boost::asio::ssl::stream<socket_t>;
  using ssl_context_t = boost::asio::ssl::context;
  using ec_t = boost::system::error_code;

  endpoint_t endpoint(boost::asio::ip::make_address("127.0.0.1"), 5263);
  server_t server(epee::net_utils::e_connection_type_P2P);
  server.init_server(endpoint.port(),
    endpoint.address().to_string(),
    0,
    "",
    false,
    true,
    epee::net_utils::ssl_support_t::e_ssl_support_enabled
  );
  server.get_config_shared()->set_handler(new command_handler_t, &command_handler_t::destroy);
  server.run_server(2, false);

  ssl_context_t ssl_context{boost::asio::ssl::context::sslv23};
  io_context_t io_context;
  ssl_socket_t socket(io_context, ssl_context);
  ec_t ec;
  socket.next_layer().connect(endpoint, ec);
  EXPECT_EQ(ec.value(), 0);
  socket.handshake(boost::asio::ssl::stream_base::client, ec);
  EXPECT_EQ(ec.value(), 0);
  std::this_thread::sleep_for(std::chrono::milliseconds(100));
  while (server.get_config_shared()->get_connections_count() < 1);
  server.get_config_shared()->del_in_connections(1);
  while (server.get_config_shared()->get_connections_count() > 0);
  server.send_stop_signal();
  EXPECT_TRUE(server.timed_wait_server_stop(5 * 1000));
  server.deinit_server();
  socket.next_layer().shutdown(boost::asio::ip::tcp::socket::shutdown_both, ec);
  socket.next_layer().close(ec);
  socket.shutdown(ec);
}

TEST(test_epee_connection, ssl_handshake)
{
  using io_context_t = boost::asio::io_context;
  using work_t = boost::asio::executor_work_guard<boost::asio::io_context::executor_type>;
  using work_ptr = std::shared_ptr<work_t>;
  using workers_t = std::vector<std::thread>;
  using socket_t = boost::asio::ip::tcp::socket;
  using ssl_socket_t = boost::asio::ssl::stream<socket_t>;
  using ssl_socket_ptr = std::unique_ptr<ssl_socket_t>;
  using ssl_options_t = epee::net_utils::ssl_options_t;
  io_context_t io_context;
  work_ptr work(std::make_shared<work_t>(io_context.get_executor()));
  workers_t workers;
  auto constexpr N = 2;
  while (workers.size() < N) {
    workers.emplace_back([&io_context]{
      io_context.run();
    });
  }
  ssl_options_t ssl_options{{}};
  auto ssl_context = ssl_options.create_context();
  for (size_t i = 0; i < N * N * N; ++i) {
    ssl_socket_ptr ssl_socket(new ssl_socket_t(io_context, ssl_context));
    ssl_socket->next_layer().open(boost::asio::ip::tcp::v4());
    for (size_t i = 0; i < N; ++i) {
      boost::asio::post(
        io_context,
        [] { std::this_thread::sleep_for(std::chrono::milliseconds(50)); }
      );
    }
    EXPECT_EQ(
      ssl_options.handshake(
        io_context,
        *ssl_socket,
        ssl_socket_t::server,
        {},
        {},
        std::chrono::milliseconds(0)
      ),
      false
    );
    ssl_socket->next_layer().close();
    ssl_socket.reset();
  }
  work.reset();
  for (;workers.size(); workers.pop_back())
    workers.back().join();
}

namespace
{
  //! What one client-side handshake in `ssl_handshake_fingerprint_lookup` decided.
  enum class fingerprint_lookup_outcome
  {
    verified,     //!< The handshake completed - the peer's fingerprint was found.
    rejected,     //!< The handshake failed verification - the fingerprint was absent.
    setup_failed  //!< No verdict: the host, OpenSSL or a thread failed the test itself.
  };

  /*!
    Prints the enumerator's name, so a failed expectation reads "verified" instead
    of an integer. Found by argument-dependent lookup from this namespace, which is
    how gtest's value printer locates a stream operator.
   */
  std::ostream& operator<<(std::ostream& out, const fingerprint_lookup_outcome outcome)
  {
    switch (outcome)
    {
      case fingerprint_lookup_outcome::verified:     return out << "verified";
      case fingerprint_lookup_outcome::rejected:     return out << "rejected";
      case fingerprint_lookup_outcome::setup_failed: return out << "setup_failed";
    }
    return out << "fingerprint_lookup_outcome(" << static_cast<int>(outcome) << ")";
  }

  /*!
    Owns a `std::thread` and joins it on every way out of the scope that holds it:
    a normal return, an early return after a failed check, or an exception unwinding
    through it. A `std::thread` that is still joinable when it is destroyed calls
    std::terminate, which takes the whole test binary down and reports nothing, so
    the join cannot be left to a statement that unwinding would skip.

    `release` runs first, and is what keeps the join bounded: the thread is inside
    an operation that ends when the work it waits on is cancelled, so without
    cancelling it the join would wait out that operation's own deadline. It is
    required not to throw - the only action installed here stores to an atomic.
   */
  class scoped_thread_joiner
  {
  public:
    scoped_thread_joiner(std::thread&& thread, std::function<void()> release)
      : thread_(std::move(thread)), release_(std::move(release))
    {}

    scoped_thread_joiner(const scoped_thread_joiner&) = delete;
    scoped_thread_joiner& operator=(const scoped_thread_joiner&) = delete;

    ~scoped_thread_joiner() { join(); }

    /*!
      Releases the thread's work and joins it. Idempotent, so an explicit call and
      the destructor cannot join twice, and never throws, because it also runs from
      the destructor while an exception is in flight.
     */
    void join() noexcept
    {
      if (!thread_.joinable())
        return;

      if (release_)
        release_();

      try
      {
        thread_.join();
      }
      catch (const std::system_error& e)
      {
        // Only an unusable thread handle reaches here. Detach first, so that a
        // still-joinable thread cannot reach its destructor and end the process,
        // and report afterwards: this is a broken test environment rather than a
        // verdict about the code under test.
        thread_.detach();
        ADD_FAILURE() << "could not join the server handshake thread: " << e.what();
      }
    }

  private:
    std::thread thread_;
    std::function<void()> release_;
  };
}

/*!
  Proves that a certificate fingerprint supplied in an unsorted list is found by
  `epee::net_utils::ssl_options_t`, and that an absent one is rejected.

  The fingerprint list is a private member sorted by the `ssl_options_t`
  constructor and searched with a binary search by `ssl_options_t::has_fingerprint`.
  The two operations must agree on one ordering: if they disagree, the search
  silently stops finding valid certificates - a peer that should be trusted is
  dropped, or worse, the failure hides until a particular list order occurs. The
  ordering is not reachable from a test directly (the list is private and the
  comparator has internal linkage), so it is exercised the only way a caller can:
  by handing an out-of-order list to the public constructor and running a real
  handshake against a peer whose certificate is, and then is not, in that list.

  Because the second half of that pair expects a handshake to fail, a failure of
  the test's own scaffolding must not be able to look like it. Every step that can
  fail without saying anything about the code under test therefore reports its own
  `fingerprint_lookup_outcome::setup_failed`, which matches neither expectation.
 */
TEST(test_epee_connection, ssl_handshake_fingerprint_lookup)
{
  using io_context_t = boost::asio::io_context;
  using socket_t = boost::asio::ip::tcp::socket;
  using acceptor_t = boost::asio::ip::tcp::acceptor;
  using endpoint_t = boost::asio::ip::tcp::endpoint;
  using ssl_socket_t = boost::asio::ssl::stream<socket_t>;
  using ssl_options_t = epee::net_utils::ssl_options_t;
  using fingerprint_t = std::vector<std::uint8_t>;
  using ec_t = boost::system::error_code;

  // SSL must be enabled explicitly: `create_context()` returns a bare context
  // without any certificate when the configuration is disabled, and the
  // `ssl_options_t{{}}` spelling used by the ssl_handshake test above is a
  // disabled configuration. With no private key path configured the context gets
  // a self-signed RSA certificate generated in memory, which is exactly the peer
  // certificate a fingerprint whitelist is meant to pin.
  ssl_options_t server_options{epee::net_utils::ssl_support_t::e_ssl_support_enabled};
  auto server_context = server_options.create_context();

  // "get0" hands back a pointer the context still owns - it must not be freed here.
  X509* const server_certificate = SSL_CTX_get0_certificate(server_context.native_handle());
  ASSERT_NE(server_certificate, nullptr);

  // Computed exactly as ssl_options_t::has_fingerprint() computes the value it
  // searches for - SHA-256 into an EVP_MAX_MD_SIZE buffer, then shrunk to the
  // digest length - so that this is the very fingerprint the lookup will look for.
  // The human-readable helpers in net_ssl.h are deliberately not used: they return
  // a colon-separated string, not these raw bytes.
  fingerprint_t expected(EVP_MAX_MD_SIZE);
  unsigned int digest_size = 0;
  ASSERT_EQ(X509_digest(server_certificate, EVP_sha256(), expected.data(), &digest_size), 1);
  expected.resize(digest_size);
  ASSERT_EQ(expected.size(), static_cast<std::size_t>(SSL_FINGERPRINT_SIZE));

  // The same strict weak ordering the fingerprint comparator in net_ssl.cpp
  // defines, so that the expectations below are stated in terms of the order
  // actually under test instead of std::vector's relational operators.
  const auto fingerprint_less = [](const fingerprint_t& lhs, const fingerprint_t& rhs) {
    return std::lexicographical_compare(lhs.begin(), lhs.end(), rhs.begin(), rhs.end());
  };

  // Two decoy fingerprints derived from `expected` deterministically, one on each
  // side of it. Moving the first byte that is not already at its bound decides the
  // order on its own - every earlier byte is identical to `expected` - so the decoy
  // is strictly greater or strictly smaller, stays exactly SSL_FINGERPRINT_SIZE
  // bytes long, and can never be a duplicate of `expected` itself.
  const auto derive_decoy = [&expected](const bool greater_than_expected) {
    fingerprint_t decoy(expected);
    const std::uint8_t bound = greater_than_expected ? 0xff : 0x00;
    for (std::size_t i = 0; i < decoy.size(); ++i)
    {
      if (decoy[i] == bound)
        continue;
      decoy[i] = static_cast<std::uint8_t>(greater_than_expected ? decoy[i] + 1 : decoy[i] - 1);
      break;
    }
    return decoy;
  };

  const fingerprint_t greater = derive_decoy(true);
  const fingerprint_t smaller = derive_decoy(false);

  // A digest of nothing but 0xff (or 0x00) bytes would defeat that derivation. It
  // cannot occur for a real certificate, and these assertions prove it did not.
  ASSERT_TRUE(fingerprint_less(expected, greater));
  ASSERT_TRUE(fingerprint_less(smaller, expected));

  // The flow both paths share, so that they cannot drift apart. Returns what the
  // fingerprint lookup decided, or `setup_failed` if it never got that far.
  const auto run_client_handshake =
    [&server_options, &server_context](const std::vector<fingerprint_t>& fingerprints)
  {
    using outcome_t = fingerprint_lookup_outcome;

    // Everything the attempt does runs inside this handler. `ssl_options_t::handshake`
    // is not noexcept - its first act, through configure(), is a throwing
    // set_option() - and the Asio constructors below can throw too, so an escaping
    // exception becomes a reported failure here instead of unwinding out of the test
    // body. That matters most once the server thread exists: unwinding a scope that
    // holds a joinable std::thread calls std::terminate, which would end the whole
    // test binary. Everything created below releases itself whichever way this scope
    // is left - the sockets and the acceptor close in their destructors, and the
    // joiner, declared after everything the thread touches, is destroyed first and
    // joins the thread before any of it goes away.
    try
    {
      // Generous: a loopback handshake completes in milliseconds and this deadline is
      // only reached if a peer stops answering. Zero would fire immediately, which is
      // how the ssl_handshake test above forces a failure.
      const std::chrono::milliseconds timeout(30 * 1000);

      // Held in a named local that outlives the handshake below: configure() installs
      // a verification callback that captures these options by reference and is
      // invoked from within the handshake. The empty ca_path is required - it leaves
      // the user-certificate check with no certificate authority to fall back on, so
      // verification always reaches the fingerprint lookup.
      ssl_options_t client_options{fingerprints, ""};
      auto client_context = client_options.create_context();

      // One io_context per side. handshake() drives the context it is given itself
      // (restarting it and polling it in its own loop), so no worker thread and no
      // work guard is needed here; two independent contexts keep the two
      // self-pumping loops from stealing each other's handlers.
      io_context_t server_io;
      io_context_t client_io;
      ec_t ec;

      // Port 0 lets the OS assign a free loopback port, so this test never collides
      // with another test - or another copy of itself - over a fixed port number.
      acceptor_t acceptor(server_io);
      const endpoint_t bind_endpoint(boost::asio::ip::address_v4::loopback(), 0);

      // Each step below is a precondition of the next one, and none of them says
      // anything about the code under test - they are the calls that fail when the
      // host runs out of descriptors or ephemeral ports. So a failure ends the
      // attempt here, with no verdict, instead of being recorded and carried into
      // the next call: accept() in particular is synchronous with no deadline and no
      // cancellation, and reaching it without a connection already queued would
      // block this test for as long as the run lasts. Returning early needs no
      // teardown of its own - the acceptor and both sockets close in their
      // destructors.
      acceptor.open(bind_endpoint.protocol(), ec);
      if (ec)
      {
        ADD_FAILURE() << "could not open the loopback acceptor: " << ec.message();
        return outcome_t::setup_failed;
      }
      acceptor.bind(bind_endpoint, ec);
      if (ec)
      {
        ADD_FAILURE() << "could not bind the loopback acceptor: " << ec.message();
        return outcome_t::setup_failed;
      }
      acceptor.listen(boost::asio::socket_base::max_listen_connections, ec);
      if (ec)
      {
        ADD_FAILURE() << "could not listen on the loopback acceptor: " << ec.message();
        return outcome_t::setup_failed;
      }
      const endpoint_t server_endpoint = acceptor.local_endpoint(ec);
      if (ec)
      {
        ADD_FAILURE() << "could not read the acceptor's assigned port: " << ec.message();
        return outcome_t::setup_failed;
      }

      // Connect first and accept second: the blocking connect() completes through the
      // listen backlog, so this ordering cannot deadlock on a single thread. And
      // accept() is reached only once connect() has succeeded, which is what
      // guarantees it has a connection waiting for it rather than a wait with no end.
      ssl_socket_t client_ssl(client_io, client_context);
      client_ssl.next_layer().connect(server_endpoint, ec);
      if (ec)
      {
        ADD_FAILURE() << "could not connect to the loopback acceptor: " << ec.message();
        return outcome_t::setup_failed;
      }
      ssl_socket_t server_ssl(server_io, server_context);
      acceptor.accept(server_ssl.next_layer(), ec);
      if (ec)
      {
        ADD_FAILURE() << "could not accept the loopback connection: " << ec.message();
        return outcome_t::setup_failed;
      }

      // Set from this thread and polled by handshake() from inside the server
      // thread's own loop, which then closes the socket itself: this is the
      // cancellation hook the production interface provides, and the only way to end
      // the server's wait without operating on a socket that another thread is using.
      std::atomic<bool> server_aborted{false};

      // An exception must not leave a thread function - that calls std::terminate -
      // so the server's is carried back here and reported once the thread is joined.
      std::exception_ptr server_exception;

      // Set only from the handler below, and only when the server threw: a handshake
      // that threw cannot go on to answer the client, so the client would otherwise
      // sit out its whole deadline waiting for a peer that has already gone. Read
      // through the client's own cancellation hook, it ends that wait at once. On
      // every healthy path this stays false and the client behaves exactly as it
      // would with no hook at all.
      std::atomic<bool> server_threw{false};

      // The thread is handed to the guard that owns it in the same statement that
      // creates it, so nothing can sit between a running thread and the object
      // responsible for joining it.
      scoped_thread_joiner server_joiner(
        std::thread(
          [&] {
            try
            {
              // The server's verdict is deliberately not asserted: once the client
              // rejects the certificate, the server legitimately fails with a TLS
              // alert.
              server_options.handshake(
                server_io,
                server_ssl,
                ssl_socket_t::server,
                {},
                {},
                timeout,
                [&server_aborted] { return server_aborted.load(std::memory_order_relaxed); }
              );
            }
            catch (...)
            {
              server_exception = std::current_exception();
              server_threw.store(true, std::memory_order_release);
            }
          }
        ),
        [&server_aborted] { server_aborted.store(true, std::memory_order_relaxed); }
      );

      const bool verified = client_options.handshake(
        client_io,
        client_ssl,
        ssl_socket_t::client,
        {},
        {},
        timeout,
        [&server_threw] { return server_threw.load(std::memory_order_acquire); }
      );

      // Joined here rather than left to the guard, because the join is what makes
      // the thread's write to server_exception visible to this thread - and the
      // client handshake that has just finished, or given up, is the only thing the
      // server was waiting for. The guard remains the join of last resort for the
      // paths that never reach this line.
      server_joiner.join();
      if (server_exception)
      {
        try
        {
          std::rethrow_exception(server_exception);
        }
        catch (const std::exception& e)
        {
          ADD_FAILURE() << "the server handshake threw: " << e.what();
        }
        catch (...)
        {
          ADD_FAILURE() << "the server handshake threw a non-standard exception";
        }
        return outcome_t::setup_failed;
      }

      // handshake() already closes the socket on failure, so repeating it here is
      // harmless; the error code is ignored, as it is elsewhere in this file. The
      // destructors would do this too - it is spelled out for the path that reaches
      // a verdict, which is why no other path needs a teardown of its own.
      client_ssl.next_layer().shutdown(socket_t::shutdown_both, ec);
      client_ssl.next_layer().close(ec);
      server_ssl.next_layer().shutdown(socket_t::shutdown_both, ec);
      server_ssl.next_layer().close(ec);
      acceptor.close(ec);
      return verified ? outcome_t::verified : outcome_t::rejected;
    }
    catch (const std::exception& e)
    {
      ADD_FAILURE() << "the fingerprint handshake threw: " << e.what();
      return outcome_t::setup_failed;
    }
    catch (...)
    {
      ADD_FAILURE() << "the fingerprint handshake threw a non-standard exception";
      return outcome_t::setup_failed;
    }
  };

  // Deliberately out of order around `expected`, so the constructor's sort has real
  // work to do and the subsequent binary search can only find the entry if both use
  // the same ordering.
  const std::vector<fingerprint_t> matching{greater, expected, smaller};
  ASSERT_FALSE(std::is_sorted(matching.begin(), matching.end(), fingerprint_less));
  EXPECT_EQ(run_client_handshake(matching), fingerprint_lookup_outcome::verified);

  // The same flow with the server's fingerprint removed. The list stays non-empty
  // and out of order, so the lookup still runs its search - rather than returning
  // early on an empty list - and the handshake must fail, because SSL support is
  // enabled rather than autodetected. An error is logged on this path by design.
  const std::vector<fingerprint_t> mismatching{greater, smaller};
  ASSERT_FALSE(std::is_sorted(mismatching.begin(), mismatching.end(), fingerprint_less));
  EXPECT_EQ(run_client_handshake(mismatching), fingerprint_lookup_outcome::rejected);
}

namespace
{
  struct config_t {
    using condition_t = std::condition_variable_any;
    using lock_guard_t = std::lock_guard<std::mutex>;
    void notify_success()
    {
      lock_guard_t guard(lock);
      success = true;
      condition.notify_all();
    }

    template<typename T>
    static bool after_init_connection(const std::shared_ptr<T>& conn)
    {
      if (!conn)
        return false;
      conn->m_protocol_handler.after_init_connection();
      return true;
    }

    std::mutex lock;
    condition_t condition;
    bool success;
  };
}

TEST(boosted_tcp_server, strand_deadlock)
{
  using context_t = epee::net_utils::connection_context_base;
  using lock_t = std::mutex;
  using unique_lock_t = std::unique_lock<lock_t>;

  struct handler_t {
    using config_type = config_t;
    using connection_context = context_t;
    using byte_slice_t = epee::byte_slice;
    using socket_t = epee::net_utils::i_service_endpoint;

    handler_t(socket_t *socket, config_t &config, context_t &context):
      socket(socket),
      config(config),
      context(context)
    {}
    void after_init_connection()
    {
      unique_lock_t guard(lock);
      if (!context.m_is_income) {
        guard.unlock();
        socket->do_send(byte_slice_t{"."});
      }
    }
    void handle_qued_callback()
    {
    }
    bool handle_recv(const char *data, size_t bytes_transferred)
    {
      unique_lock_t guard(lock);
      if (!context.m_is_income) {
        if (context.m_recv_cnt == 1024) {
          guard.unlock();
          socket->do_send(byte_slice_t{"."});
        }
      }
      else {
        if (context.m_recv_cnt == 1) {
          for(size_t i = 0; i < 1024; ++i) {
            guard.unlock();
            socket->do_send(byte_slice_t{"."});
            guard.lock();
          }
        }
        else if(context.m_recv_cnt == 2) {
          guard.unlock();
          socket->close(false);
        }
      }
      return true;
    }
    void release_protocol()
    {
      unique_lock_t guard(lock);
      if(!context.m_is_income
        && context.m_recv_cnt == 1024
        && context.m_send_cnt == 2
      ) {
        guard.unlock();
        config.notify_success();
      }
    }

    lock_t lock;
    socket_t *socket;
    config_t &config;
    context_t &context;
  };

  using server_t = epee::net_utils::boosted_tcp_server<handler_t>;
  using endpoint_t = boost::asio::ip::tcp::endpoint;

  endpoint_t endpoint(boost::asio::ip::make_address("127.0.0.1"), 5262);
  server_t server(epee::net_utils::e_connection_type_RPC);
  server.init_server(
    endpoint.port(),
    endpoint.address().to_string(),
    {},
    {},
    {},
    true,
    epee::net_utils::ssl_support_t::e_ssl_support_disabled
  );
  server.run_server(2, {});
  server.async_call(
    [&]{
      context_t context;
      ASSERT_TRUE(
        server.connect(
          endpoint.address().to_string(),
          std::to_string(endpoint.port()),
          5,
          context,
          "0.0.0.0",
          epee::net_utils::ssl_support_t::e_ssl_support_disabled
        )
      );
    }
  );
  {
    unique_lock_t guard(server.get_config_object().lock);
    EXPECT_TRUE(
      server.get_config_object().condition.wait_for(
        guard,
        std::chrono::seconds(5),
        [&] { return server.get_config_object().success; }
      )
    );
  }

  server.send_stop_signal();
  server.timed_wait_server_stop(5 * 1000);
  server.deinit_server();
}

namespace
{
  struct shutdown_handler_t;
  struct shutdown_context_t: epee::net_utils::connection_context_base {
    static constexpr size_t get_max_bytes(int) noexcept { return -1; }
    static constexpr int handshake_command() noexcept { return 1001; }
    static constexpr bool handshake_complete() noexcept { return true; }
  };
}

namespace epee { namespace levin
{
  template<>
  struct get_handler<shutdown_context_t> {
    using type = shutdown_handler_t;
  };
}}

namespace
{
  struct shutdown_config_t : epee::levin::async_protocol_handler_config<shutdown_context_t> {
    void received_handshake() { handshake_received.raise(); }
    epee::simple_event handshake_received;
  };

  struct shutdown_command_handler_t: epee::levin::levin_commands_handler<shutdown_context_t> {
    using context_t = shutdown_context_t;
    virtual int invoke(int, const epee::span<const uint8_t>, epee::byte_stream&, context_t&) override { return {}; }
    virtual int notify(int, const epee::span<const uint8_t>, context_t&) override { return {}; }
    virtual void callback(context_t&) override {}
    virtual void on_connection_new(context_t&) override {}
    virtual void on_connection_close(context_t&) override { }
    virtual ~shutdown_command_handler_t() override {}
    static void destroy(epee::levin::levin_commands_handler<context_t>* ptr) { delete ptr; }
  };

  struct shutdown_handler_t : epee::levin::async_protocol_handler<shutdown_context_t> {
    using config_type = shutdown_config_t;
    using connection_context = shutdown_context_t;
    using epee::levin::async_protocol_handler<connection_context>::async_protocol_handler;

    bool handle_recv(const void *data, size_t bytes_transferred) override
    {
      // We don't respond to the handshake (the async_invoke_remote_command2 is waiting for a response)
      MINFO("handle_recv just came in");
      config_type* config = dynamic_cast<config_type*>(&m_config);
      if (config == nullptr)
        throw std::runtime_error("m_config must be of type config_t");
      config->received_handshake();
      return true;
    }
  };
}


TEST(boosted_tcp_server, shutdown)
{
  using context_t = shutdown_context_t;
  using command_handler_t = shutdown_command_handler_t;
  using handler_t = shutdown_handler_t;

  boost::asio::ip::tcp::endpoint endpoint(boost::asio::ip::make_address("127.0.0.1"), 5262);
  epee::net_utils::boosted_tcp_server<handler_t> server(epee::net_utils::e_connection_type_P2P);
  server.init_server(
    endpoint.port(),
    endpoint.address().to_string(),
    {},
    {},
    {},
    true,
    epee::net_utils::ssl_support_t::e_ssl_support_disabled
  );
  server.get_config_shared()->set_handler(new command_handler_t, &command_handler_t::destroy);

  // Run the server in a thread and wait for it to start
  MINFO("Starting the server");
  std::thread running_server([&]{server.run_server(2, true/*wait*/);} );

  // Have the server connect to itself
  MINFO("Connecting the server to itself");
  context_t context;
  {
    epee::simple_event connected;
    server.async_call(
      [&]{
        ASSERT_TRUE(
          server.connect(
            endpoint.address().to_string(),
            std::to_string(endpoint.port()),
            5,
            context,
            "0.0.0.0",
            epee::net_utils::ssl_support_t::e_ssl_support_disabled
          )
        );
        connected.raise();
      }
    );
    connected.wait();
  }

  // Invoke handshake to the connection, and wait for cb cancel in a separate thread
  MINFO("Invoking handshake");
  epee::simple_event ev;
  {
    using COMMAND_HANDSHAKE = nodetool::COMMAND_HANDSHAKE_T<cryptonote::CORE_SYNC_DATA>;
    COMMAND_HANDSHAKE::request arg;
    bool r = epee::net_utils::async_invoke_remote_command2<COMMAND_HANDSHAKE::response>(context, COMMAND_HANDSHAKE::ID, arg, server.get_config_object(),
      [&ev](int code, const COMMAND_HANDSHAKE::response&, context_t&)
    {
      ASSERT_EQ(code, LEVIN_ERROR_CONNECTION_DESTROYED);
      ev.raise();
    }, std::chrono::milliseconds{P2P_DEFAULT_HANDSHAKE_INVOKE_TIMEOUT});
    ASSERT_TRUE(r);

    MINFO("Waiting for handshake invocation to be received");
    server.get_config_object().handshake_received.wait();
  }

  MINFO("Stopping the server");
  server.mark_stop_signal_sent();
  server.close_server_connections();
  server.get_config_object().close(context.m_connection_id, true/*wait_for_shutdown*/);
  server.stop_io_context();
  running_server.join();

  MINFO("Waiting for handshake to cancel");
  ev.wait();
}

TEST(boosted_tcp_server, write_failure)
{
  using context_t = epee::net_utils::connection_context_base;

  struct config_t {
    static constexpr bool after_init_connection(const std::shared_ptr<epee::net_utils::connection_basic>&) noexcept
    {
      return true;
    }
  };

  struct handler_t {
    using config_type = config_t;
    using connection_context = context_t;
    using socket_t = epee::net_utils::i_service_endpoint;

    handler_t(socket_t *socket, config_t &config, context_t &):
      config(config)
    {}
    
    void handle_qued_callback()
    {}

    bool handle_recv(const char *data, size_t bytes_transferred)
    {
      throw std::runtime_error{"UNEXPECTED!"};
    }

    void release_protocol()
    {}

    config_t &config;
  };


  using byte_slice_t = epee::byte_slice;
  using connection_t = epee::net_utils::connection<handler_t>;
  using shared_t = connection_t::shared_state;
  using tcp_t = boost::asio::ip::tcp;
  using endpoint_t = tcp_t::endpoint;
  using socket_t = tcp_t::socket;
  using acceptor_t = tcp_t::acceptor;

  const endpoint_t endpoint{boost::asio::ip::make_address("127.0.0.1"), 5262};
  boost::asio::io_context context{};
  acceptor_t acceptor{context};
  acceptor.open(endpoint.protocol());
#if !defined(_WIN32)
  acceptor.set_option(boost::asio::ip::tcp::acceptor::reuse_address(true));
#endif
  acceptor.bind(endpoint);
  acceptor.listen();

  socket_t in_socket{context};

  std::shared_ptr<connection_t> out_connection;
  const auto shared = std::make_shared<shared_t>();
  const auto make_connection = [&] {
    in_socket = socket_t{context};
    acceptor.async_accept(in_socket, [] (auto error) { EXPECT_TRUE(!error); });

    socket_t out_socket{context};
    out_socket.async_connect(endpoint, [] (auto error) { EXPECT_TRUE(!error); });

    context.restart();
    ASSERT_EQ(2u, context.run()); // connect and accept

    out_connection = std::make_shared<connection_t>(
      context,
      std::move(out_socket),
      shared,
      epee::net_utils::e_connection_type_P2P,
      epee::net_utils::ssl_support_t::e_ssl_support_disabled
    );
    EXPECT_TRUE(out_connection->start(false, true));
  };

  make_connection();
  {
    const byte_slice_t payload{"."};
    epee::net_utils::i_service_endpoint& out{*out_connection};
    static_assert(ABSTRACT_SERVER_SEND_QUE_MAX_COUNT < std::numeric_limits<std::size_t>::max(), "");
    for (std::size_t i = 0; i <= ABSTRACT_SERVER_SEND_QUE_MAX_COUNT; ++i)
      EXPECT_TRUE(out.do_send(payload.clone()));
    EXPECT_FALSE(out.do_send(payload.clone()));
  }
  context.restart();
  EXPECT_LE(1u, context.run());
  EXPECT_EQ(connection_t::WASTED, out_connection->get_status());

  make_connection();
  {
    const byte_slice_t spayload{"."};
    const byte_slice_t lpayload{std::string(std::size_t(3 * 128 * 1024), '.')};
    epee::net_utils::i_service_endpoint& out{*out_connection};
    for (std::size_t i = 0; i < ABSTRACT_SERVER_SEND_QUE_MAX_COUNT; ++i)
      EXPECT_TRUE(out.do_send(spayload.clone()));
    EXPECT_FALSE(out.do_send(lpayload.clone()));
  }
  context.restart();
  EXPECT_LE(1u, context.run());
  EXPECT_EQ(connection_t::WASTED, out_connection->get_status());
}

