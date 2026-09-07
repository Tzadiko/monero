// Copyright (c) 2017-2024, The Monero Project
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

#pragma once

#include "byte_slice.h"
#include "daemon_messages.h"
#include "daemon_rpc_version.h"
#include "rpc_handler.h"
#include "cryptonote_core/cryptonote_core.h"
#include "cryptonote_protocol/cryptonote_protocol_handler.h"
#include "p2p/net_node.h"

namespace
{
  typedef nodetool::node_server<cryptonote::t_cryptonote_protocol_handler<cryptonote::core> > t_p2p;
}  // anonymous namespace

namespace cryptonote
{

namespace rpc
{

/*! \return The network name the daemon RPC surface reports for \a type.

  The four names are the ones the HTTP `get_info` handler emits
  (`core_rpc_server::on_get_info`), and the two surfaces have to agree because
  they describe one and the same daemon. `DaemonInfo::mainnet`, `::testnet` and
  `::stagenet` cannot name a locally generated chain, which is why the contract
  also carries `nettype` and why anything that is not one of the three named
  networks reports as "fakechain". */
const char* get_nettype_name(network_type type) noexcept;

/*! Append \a src to \a dest as the `peer` values of a get_peer_list response.

  Called once per list so that the white and the gray list can never drift apart
  in the filtering they apply, and appends rather than assigns so that the
  destination may already hold entries. Two entry classes are dropped, matching
  the defaults of the established HTTP `get_peer_list` handler
  (`core_rpc_server::on_get_peer_list`):

  - every entry for which \a is_blocked reports the address as banned, because
    that handler defaults `include_blocked` to false and a banned host is not a
    peer a client should be handed as reachable;
  - every address that is not IPv4, because `cryptonote::rpc::peer` carries the
    address as a 32-bit `ip` and so has no representation for anything else. The
    ZMQ response contract is frozen at DAEMON_RPC_VERSION_ZMQ 2.0, so widening
    that field is not an option.

  All seven members of `peer` are assigned. `last_seen` is a signed UNIX
  timestamp on the p2p side and an unsigned one in the response, hence the
  explicit cast.

  \param is_blocked Called with each entry's `network_address`; returns whether
    the host is currently banned. Taken as a parameter rather than read from the
    p2p server so that the conversion can be exercised on its own. */
template<typename IsBlocked>
void append_peerlist(const IsBlocked& is_blocked, const std::vector<nodetool::peerlist_entry>& src, std::vector<peer>& dest)
{
  dest.reserve(dest.size() + src.size());
  for (const nodetool::peerlist_entry& entry : src)
  {
    if (is_blocked(entry.adr))
      continue;

    if (entry.adr.get_type_id() != epee::net_utils::ipv4_network_address::get_type_id())
      continue;

    const epee::net_utils::ipv4_network_address& address = entry.adr.as<epee::net_utils::ipv4_network_address>();

    peer out{};
    out.id = entry.id;
    out.ip = address.ip();
    out.port = address.port();
    out.rpc_port = entry.rpc_port;
    out.rpc_credits_per_hash = entry.rpc_credits_per_hash;
    out.last_seen = static_cast<uint64_t>(entry.last_seen);
    out.pruning_seed = entry.pruning_seed;

    dest.push_back(out);
  }
}

class DaemonHandler : public RpcHandler
{
  public:

    DaemonHandler(cryptonote::core& c, t_p2p& p2p, bool restricted = false);

    ~DaemonHandler() { }

    void handle(const GetHeight::Request& req, GetHeight::Response& res);

    void handle(const GetBlocksFast::Request& req, GetBlocksFast::Response& res);

    void handle(const GetHashesFast::Request& req, GetHashesFast::Response& res);

    void handle(const GetTransactions::Request& req, GetTransactions::Response& res);

    void handle(const KeyImagesSpent::Request& req, KeyImagesSpent::Response& res);

    void handle(const GetTxGlobalOutputIndices::Request& req, GetTxGlobalOutputIndices::Response& res);

    void handle(const SendRawTx::Request& req, SendRawTx::Response& res);

    void handle(const SendRawTxHex::Request& req, SendRawTxHex::Response& res);

    void handle(const StartMining::Request& req, StartMining::Response& res);

    void handle(const GetInfo::Request& req, GetInfo::Response& res);

    void handle(const StopMining::Request& req, StopMining::Response& res);

    void handle(const MiningStatus::Request& req, MiningStatus::Response& res);

    void handle(const SaveBC::Request& req, SaveBC::Response& res);

    void handle(const GetBlockHash::Request& req, GetBlockHash::Response& res);

    void handle(const GetBlockTemplate::Request& req, GetBlockTemplate::Response& res);

    void handle(const SubmitBlock::Request& req, SubmitBlock::Response& res);

    void handle(const GetLastBlockHeader::Request& req, GetLastBlockHeader::Response& res);

    void handle(const GetBlockHeaderByHash::Request& req, GetBlockHeaderByHash::Response& res);

    void handle(const GetBlockHeaderByHeight::Request& req, GetBlockHeaderByHeight::Response& res);

    void handle(const GetBlockHeadersByHeight::Request& req, GetBlockHeadersByHeight::Response& res);

    void handle(const GetBlock::Request& req, GetBlock::Response& res);

    void handle(const GetPeerList::Request& req, GetPeerList::Response& res);

    void handle(const SetLogHashRate::Request& req, SetLogHashRate::Response& res);

    void handle(const SetLogLevel::Request& req, SetLogLevel::Response& res);

    void handle(const GetTransactionPool::Request& req, GetTransactionPool::Response& res);

    void handle(const GetConnections::Request& req, GetConnections::Response& res);

    void handle(const GetBlockHeadersRange::Request& req, GetBlockHeadersRange::Response& res);

    void handle(const StopDaemon::Request& req, StopDaemon::Response& res);

    void handle(const StartSaveGraph::Request& req, StartSaveGraph::Response& res);

    void handle(const StopSaveGraph::Request& req, StopSaveGraph::Response& res);

    void handle(const HardForkInfo::Request& req, HardForkInfo::Response& res);

    void handle(const GetBans::Request& req, GetBans::Response& res);

    void handle(const SetBans::Request& req, SetBans::Response& res);

    void handle(const FlushTransactionPool::Request& req, FlushTransactionPool::Response& res);

    void handle(const GetOutputHistogram::Request& req, GetOutputHistogram::Response& res);

    void handle(const GetOutputKeys::Request& req, GetOutputKeys::Response& res);

    void handle(const GetRPCVersion::Request& req, GetRPCVersion::Response& res);

    void handle(const GetFeeEstimate::Request& req, GetFeeEstimate::Response& res);

    void handle(const GetOutputDistribution::Request& req, GetOutputDistribution::Response& res);

    epee::byte_slice handle(std::string&& request) override final;

  private:

    bool getBlockHeaderByHash(const crypto::hash& hash_in, cryptonote::rpc::BlockHeaderResponse& response);

    void handleTxBlob(std::string&& tx_blob, bool relay, SendRawTx::Response& res);

    cryptonote::core& m_core;
    t_p2p& m_p2p;
    bool m_restricted;
};

}  // namespace rpc

}  // namespace cryptonote
