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

#pragma once

#include <cstddef>
#include <rapidjson/document.h>
#include <rapidjson/writer.h>
#include <string>

#include "byte_slice.h"
#include "byte_stream.h"
#include "rpc/message_data_structs.h"

namespace cryptonote
{

namespace rpc
{

  class Message
  {
      virtual void doToJson(rapidjson::Writer<epee::byte_stream>& dest) const
      {}

    public:
      static const char* STATUS_OK;
      static const char* STATUS_RETRY;
      static const char* STATUS_FAILED;
      static const char* STATUS_BAD_REQUEST;
      static const char* STATUS_BAD_JSON;
      static const char* STATUS_REQUEST_TOO_LARGE;

      Message() : status(STATUS_OK), rpc_version(0) { }

      virtual ~Message() { }

      void toJson(rapidjson::Writer<epee::byte_stream>& dest) const;

      virtual void fromJson(const rapidjson::Value& val);

      std::string status;
      std::string error_details;
      uint32_t rpc_version;
  };

  class FullMessage
  {
    public:
      //! Only JSON-RPC 2.0 envelopes are accepted; the `jsonrpc` member must equal this exactly.
      static constexpr char JSONRPC_VERSION[] = "2.0";

      /*! Longest accepted `method` value. The longest method this daemon
        registers is 28 characters (`get_tx_global_output_indices`); the bound
        exists so that an unbounded attacker-supplied name can never be copied
        into an error response or a log line. */
      static constexpr std::size_t MAX_METHOD_LENGTH = 64;

      ~FullMessage() { }

      /*! Parse and validate a JSON-RPC envelope.

        Throws a `cryptonote::json::JSON_ERROR` unless the document is an object
        whose `jsonrpc` member is the string `JSONRPC_VERSION`. When `request`
        is set, `method` must additionally be a canonical method name - see
        `getRequestType()` - `params` must be present, and `id`, if present,
        must be a string, a number or null. Otherwise the envelope must carry a
        `result` or an `error` member.

        \throw cryptonote::json::PARSE_FAIL if the text is not a JSON object.
        \throw cryptonote::json::MISSING_KEY if a required member is absent.
        \throw cryptonote::json::WRONG_TYPE if a member has the wrong type, the
          protocol version is not `JSONRPC_VERSION`, or `method` is not
          canonical. */
      FullMessage(std::string&& json_string, bool request=false);

      /*! \return The `method` member, exactly as many bytes long as the JSON
        string it came from.

        The value is validated at construction: it is non-empty, at most
        `MAX_METHOD_LENGTH` bytes, and composed only of `[A-Za-z0-9_.-]`. An
        embedded NUL - which would otherwise truncate the name and let a
        non-canonical request dispatch as a shorter registered method - is
        therefore rejected before this value can reach method lookup, as are
        control characters and any byte outside that set. */
      std::string getRequestType() const;

      const rapidjson::Value& getMessage() const;

      rapidjson::Value getMessageCopy();

      const rapidjson::Value& getID() const;

      cryptonote::rpc::error getError();

      static epee::byte_slice getRequest(const std::string& request, const Message& message, unsigned id);
      static epee::byte_slice getResponse(const Message& message, const rapidjson::Value& id);
    private:

      FullMessage() = default;
      FullMessage(const FullMessage&) = delete;
      FullMessage& operator=(const FullMessage&) = delete;

      FullMessage(const std::string& request, Message* message);
      FullMessage(Message* message);

      std::string contents;
      rapidjson::Document doc;
  };


  // convenience functions for bad input
  epee::byte_slice BAD_REQUEST(const std::string& request);
  epee::byte_slice BAD_REQUEST(const std::string& request, const rapidjson::Value& id);

  //! \return A `Malformed json` response with a null id, for a request whose envelope did not parse.
  epee::byte_slice BAD_JSON(const std::string& error_details);

  /*! \return A `Malformed json` response for `id`, for a request whose envelope
    parsed but whose parameters did not. Echoing the id back is required by
    JSON-RPC 2.0 whenever the id is known, and it is what lets a client
    correlate the failure with the request it sent. */
  epee::byte_slice BAD_JSON(const std::string& error_details, const rapidjson::Value& id);

  /*! \return A failure response for `id` carrying a fixed, non-disclosing
    message.

    Used when a request was well-formed and mapped to a handler but the handler
    or the response serializer threw. The exception text is deliberately not
    forwarded: it originates in the storage backend, the standard library or a
    third-party library and would disclose internal function names, type names
    and filesystem paths to any client, including one restricted to public
    methods (CWE-209). Callers log the exception text instead. */
  epee::byte_slice INTERNAL_ERROR(const rapidjson::Value& id);

  epee::byte_slice REQUEST_TOO_LARGE();


}  // namespace rpc

}  // namespace cryptonote
