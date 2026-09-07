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

#include "message.h"

#include <cstring>

#include "daemon_rpc_version.h"
#include "serialization/json_object.h"

namespace cryptonote
{

namespace rpc
{

const char* Message::STATUS_OK = "OK";
const char* Message::STATUS_RETRY = "Retry";
const char* Message::STATUS_FAILED = "Failed";
const char* Message::STATUS_BAD_REQUEST = "Invalid request type";
const char* Message::STATUS_BAD_JSON = "Malformed json";
const char* Message::STATUS_REQUEST_TOO_LARGE = "Request too large";

namespace
{
constexpr const char error_field[] = "error";
constexpr const char id_field[] = "id";
constexpr const char jsonrpc_field[] = "jsonrpc";
constexpr const char method_field[] = "method";
constexpr const char params_field[] = "params";
constexpr const char result_field[] = "result";

//! Length of the one JSON-RPC protocol version this daemon speaks.
constexpr std::size_t jsonrpc_version_length = sizeof(FullMessage::JSONRPC_VERSION) - 1;

/*! Public error text for a failure whose cause must not be disclosed. The
  exception that caused it is logged by the caller instead of being copied into
  the response (CWE-209). */
constexpr const char internal_error_details[] = "Internal error while handling the request";

/*! \return True if `value` may appear in a JSON-RPC method name.

  Method names are identifiers, so the accepted set is ASCII alphanumerics plus
  `_`, `.` and `-`. Everything else is rejected, which excludes NUL - the byte
  that would otherwise truncate a name and let a non-canonical request dispatch
  as a shorter registered method - along with every other control character,
  whitespace, quoting and escaping byte, and all non-ASCII input. Bytes are
  examined as `unsigned char` so the classification does not depend on whether
  `char` is signed, and it is done here rather than with `<cctype>` so that it
  is immune to the process locale. */
bool is_canonical_method_char(const char value) noexcept
{
  const unsigned char byte = static_cast<unsigned char>(value);
  return (byte >= 'a' && byte <= 'z') ||
         (byte >= 'A' && byte <= 'Z') ||
         (byte >= '0' && byte <= '9') ||
         byte == '_' || byte == '.' || byte == '-';
}

/*! \return The validated `method` member of `src`.

  \throw cryptonote::json::MISSING_KEY if `method` is absent.
  \throw cryptonote::json::WRONG_TYPE if `method` is not a string, is empty, is
    longer than `FullMessage::MAX_METHOD_LENGTH`, or contains a byte that is not
    canonical. The length is taken from the DOM instead of from a C string, so
    an embedded NUL is seen and rejected rather than silently ending the name. */
const rapidjson::Value& get_method_field(const rapidjson::Value& src)
{
  const auto member = src.FindMember(method_field);
  if (member == src.MemberEnd())
    throw cryptonote::json::MISSING_KEY{method_field};
  if (!member->value.IsString())
    throw cryptonote::json::WRONG_TYPE{"Expected string"};

  const char* const method = member->value.GetString();
  const std::size_t length = member->value.GetStringLength();
  static_assert(FullMessage::MAX_METHOD_LENGTH == 64, "the error text below names this bound");
  if (length == 0 || length > FullMessage::MAX_METHOD_LENGTH)
    throw cryptonote::json::WRONG_TYPE{"non-empty method name of at most 64 characters"};

  for (std::size_t i = 0; i < length; ++i)
  {
    if (!is_canonical_method_char(method[i]))
      throw cryptonote::json::WRONG_TYPE{"method name of characters [A-Za-z0-9_.-] only"};
  }

  return member->value;
}

/*! Verify the JSON-RPC protocol version of `src`.

  Presence alone is not enough: an envelope that names an unsupported version,
  or that carries a non-string there, is not a request this daemon can honour
  and must be rejected before dispatch rather than interpreted as 2.0 (CWE-20).

  \throw cryptonote::json::MISSING_KEY if `jsonrpc` is absent.
  \throw cryptonote::json::WRONG_TYPE if `jsonrpc` is not the string "2.0". The
    comparison is length-checked, so a value that merely starts with `2.0` -
    including one whose remainder is hidden behind an embedded NUL - is
    rejected. */
void validate_jsonrpc_field(const rapidjson::Value& src)
{
  const auto member = src.FindMember(jsonrpc_field);
  if (member == src.MemberEnd())
    throw cryptonote::json::MISSING_KEY{jsonrpc_field};
  if (!member->value.IsString())
    throw cryptonote::json::WRONG_TYPE{"string"};

  if (member->value.GetStringLength() != jsonrpc_version_length ||
      std::memcmp(member->value.GetString(), FullMessage::JSONRPC_VERSION, jsonrpc_version_length) != 0)
  {
    throw cryptonote::json::WRONG_TYPE{"jsonrpc version \"2.0\""};
  }
}
}

void validate_id_field(const rapidjson::Value& src)
{
  const auto member = src.FindMember(id_field);
  if (member == src.MemberEnd())
    return;

  // If present, JSON-RPC 2.0 request ids must be String, Number, or Null.
  if (!member->value.IsString() && !member->value.IsNumber() && !member->value.IsNull())
    throw cryptonote::json::WRONG_TYPE{"Expected string, number or null"};
}

void Message::toJson(rapidjson::Writer<epee::byte_stream>& dest) const
{
  dest.StartObject();
  INSERT_INTO_JSON_OBJECT(dest, rpc_version, DAEMON_RPC_VERSION_ZMQ);
  doToJson(dest);
  dest.EndObject();
}

void Message::fromJson(const rapidjson::Value& val)
{
  GET_FROM_JSON_OBJECT(val, rpc_version, rpc_version);
}

FullMessage::FullMessage(std::string&& json_string, bool request)
  : contents(std::move(json_string)), doc()
{
  /* Insitu parsing does not copy data from `contents` to DOM,
     accelerating string heavy content. */
  doc.ParseInsitu<rapidjson::kParseIterativeFlag>(std::addressof(contents[0]));
  if (doc.HasParseError() || !doc.IsObject())
  {
    throw cryptonote::json::PARSE_FAIL();
  }

  validate_jsonrpc_field(doc); // throws unless the version is exactly "2.0"

  if (request)
  {
    get_method_field(doc); // throws on errors, including a non-canonical method name
    OBJECT_HAS_MEMBER_OR_THROW(doc, params_field)
    validate_id_field(doc);
  }
  else
  {
    if (!doc.HasMember(result_field) && !doc.HasMember(error_field))
    {
      throw cryptonote::json::MISSING_KEY("error/result");
    }
  }
}

std::string FullMessage::getRequestType() const
{
  const rapidjson::Value& method = get_method_field(doc);
  /* Constructed from pointer plus length: a `const char*` would end the name at
     the first NUL, so a method such as `get_height\0suffix` would be handed to
     method lookup as `get_height` and dispatch as that registered method. The
     validation in `get_method_field` already rejects such a name; taking the
     length from the DOM as well keeps this accessor exact on its own. */
  return std::string{method.GetString(), method.GetStringLength()};
}

const rapidjson::Value& FullMessage::getMessage() const
{
  if (doc.HasMember(params_field))
  {
    return doc[params_field];
  }
  else if (doc.HasMember(result_field))
  {
    return doc[result_field];
  }

  //else
  OBJECT_HAS_MEMBER_OR_THROW(doc, error_field)
  return doc[error_field];

}

rapidjson::Value FullMessage::getMessageCopy()
{
  return rapidjson::Value(getMessage(), doc.GetAllocator());
}

const rapidjson::Value& FullMessage::getID() const
{
  OBJECT_HAS_MEMBER_OR_THROW(doc, id_field)
  return doc[id_field];
}

cryptonote::rpc::error FullMessage::getError()
{
  cryptonote::rpc::error err;
  err.use = false;
  if (doc.HasMember(error_field))
  {
    GET_FROM_JSON_OBJECT(doc, err, error);
    err.use = true;
  }

  return err;
}

epee::byte_slice FullMessage::getRequest(const std::string& request, const Message& message, const unsigned id)
{
  epee::byte_stream buffer;
  {
    rapidjson::Writer<epee::byte_stream> dest{buffer};

    dest.StartObject();
    INSERT_INTO_JSON_OBJECT(dest, jsonrpc, (boost::string_ref{"2.0", 3}));

    dest.Key(id_field);
    json::toJsonValue(dest, id);

    dest.Key(method_field);
    json::toJsonValue(dest, request);

    dest.Key(params_field);
    message.toJson(dest);

    dest.EndObject();

    if (!dest.IsComplete())
      throw std::logic_error{"Invalid JSON tree generated"};
  }
  return epee::byte_slice{std::move(buffer)};
}


epee::byte_slice FullMessage::getResponse(const Message& message, const rapidjson::Value& id)
{
  epee::byte_stream buffer;
  {
    rapidjson::Writer<epee::byte_stream> dest{buffer};

    dest.StartObject();
    INSERT_INTO_JSON_OBJECT(dest, jsonrpc, (boost::string_ref{"2.0", 3}));

    dest.Key(id_field);
    json::toJsonValue(dest, id);

    if (message.status == Message::STATUS_OK)
    {
      dest.Key(result_field);
      message.toJson(dest);
    }
    else
    {
      cryptonote::rpc::error err;

      err.error_str = message.status;
      err.message = message.error_details;

      INSERT_INTO_JSON_OBJECT(dest, error, err);
    }
    dest.EndObject();

    if (!dest.IsComplete())
      throw std::logic_error{"Invalid JSON tree generated"};
  }
  return epee::byte_slice{std::move(buffer)};
}

// convenience functions for bad input
epee::byte_slice BAD_REQUEST(const std::string& request)
{
  rapidjson::Value invalid;
  return BAD_REQUEST(request, invalid);
}

epee::byte_slice BAD_REQUEST(const std::string& request, const rapidjson::Value& id)
{
  Message fail;
  fail.status = Message::STATUS_BAD_REQUEST;
  fail.error_details = std::string("\"") + request + "\" is not a valid request.";
  return FullMessage::getResponse(fail, id);
}

epee::byte_slice BAD_JSON(const std::string& error_details)
{
  rapidjson::Value invalid;
  return BAD_JSON(error_details, invalid);
}

epee::byte_slice BAD_JSON(const std::string& error_details, const rapidjson::Value& id)
{
  Message fail;
  fail.status = Message::STATUS_BAD_JSON;
  fail.error_details = error_details;
  return FullMessage::getResponse(fail, id);
}

epee::byte_slice INTERNAL_ERROR(const rapidjson::Value& id)
{
  Message fail;
  fail.status = Message::STATUS_FAILED;
  fail.error_details = internal_error_details;
  return FullMessage::getResponse(fail, id);
}

epee::byte_slice REQUEST_TOO_LARGE()
{
  rapidjson::Value invalid;
  Message fail;
  fail.status = Message::STATUS_REQUEST_TOO_LARGE;
  fail.error_details = "Request exceeds maximum message size.";
  return FullMessage::getResponse(fail, invalid);
}


}  // namespace rpc

}  // namespace cryptonote
