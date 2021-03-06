/*
  Licensed to the Apache Software Foundation (ASF) under one
  or more contributor license agreements.  See the NOTICE file
  distributed with this work for additional information
  regarding copyright ownership.  The ASF licenses this file
  to you under the Apache License, Version 2.0 (the
  "License"); you may not use this file except in compliance
  with the License.  You may obtain a copy of the License at

  http://www.apache.org/licenses/LICENSE-2.0

  Unless required by applicable law or agreed to in writing, software
  distributed under the License is distributed on an "AS IS" BASIS,
  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
  See the License for the specific language governing permissions and
  limitations under the License.
*/
//////////////////////////////////////////////////////////////////////////////////////////////
// operators.cc: implementation of the operator classes
//
//
#include <arpa/inet.h>
#include <ts/ts.h>
#include <string.h>

#include "operators.h"

// OperatorRMHeader
void
OperatorRMHeader::initialize(Parser& p) {
  Operator::initialize(p);

  _header = p.get_arg();

  require_resources(RSRC_SERVER_RESPONSE_HEADERS);
  require_resources(RSRC_SERVER_REQUEST_HEADERS);
  require_resources(RSRC_CLIENT_REQUEST_HEADERS);
  require_resources(RSRC_CLIENT_RESPONSE_HEADERS);
}


void
OperatorRMHeader::exec(const Resources& res) const
{
  TSMLoc field_loc, tmp;

  if (res.bufp && res.hdr_loc) {
    TSDebug(PLUGIN_NAME, "OperatorRMHeader::exec() invoked on header %s", _header.c_str());
    field_loc = TSMimeHdrFieldFind(res.bufp, res.hdr_loc, _header.c_str(), _header.size());
    while (field_loc) {
      TSDebug(PLUGIN_NAME, "\tdeleting header %s", _header.c_str());
      tmp = TSMimeHdrFieldNextDup(res.bufp, res.hdr_loc, field_loc);
      TSMimeHdrFieldDestroy(res.bufp, res.hdr_loc, field_loc);
      TSHandleMLocRelease(res.bufp, res.hdr_loc, field_loc);
      field_loc = tmp;
    }
  }
}


// OperatorSetStatus
void
OperatorSetStatus::initialize(Parser& p) {
  Operator::initialize(p);

  _status.set_value(p.get_arg());

  if (NULL == (_reason = TSHttpHdrReasonLookup((TSHttpStatus)_status.get_int_value()))) {
    TSError("header_rewrite: unknown status %d", _status.get_int_value());
    _reason_len = 0;
  } else {
    _reason_len = strlen(_reason);
  }

  require_resources(RSRC_SERVER_RESPONSE_HEADERS);
  require_resources(RSRC_CLIENT_RESPONSE_HEADERS);
  require_resources(RSRC_RESPONSE_STATUS);
}


void
OperatorSetStatus::initialize_hooks() {
  add_allowed_hook(TS_HTTP_READ_RESPONSE_HDR_HOOK);
  add_allowed_hook(TS_HTTP_SEND_RESPONSE_HDR_HOOK);
}


void
OperatorSetStatus::exec(const Resources& res) const
{
  if (res.bufp && res.hdr_loc) {
    TSHttpHdrStatusSet(res.bufp, res.hdr_loc, (TSHttpStatus)_status.get_int_value());
    if (_reason && _reason_len > 0)
      TSHttpHdrReasonSet(res.bufp, res.hdr_loc, _reason, _reason_len);
  }
}


// OperatorSetStatusReason
void
OperatorSetStatusReason::initialize(Parser& p) {
  Operator::initialize(p);

  _reason.set_value(p.get_arg());
  require_resources(RSRC_CLIENT_RESPONSE_HEADERS);
  require_resources(RSRC_SERVER_RESPONSE_HEADERS);
}


void
OperatorSetStatusReason::initialize_hooks() {
  add_allowed_hook(TS_HTTP_READ_RESPONSE_HDR_HOOK);
  add_allowed_hook(TS_HTTP_SEND_RESPONSE_HDR_HOOK);
}

void
OperatorSetStatusReason::exec(const Resources& res) const {
  if (res.bufp && res.hdr_loc) {
    std::string reason;

    _reason.append_value(reason, res);
    if (reason.size() > 0) {
      TSDebug(PLUGIN_NAME, "Setting Status Reason to %s", reason.c_str());
      TSHttpHdrReasonSet(res.bufp, res.hdr_loc, reason.c_str(), reason.size());
    }
  }
}


// OperatorAddHeader
void
OperatorAddHeader::initialize(Parser& p) {
  Operator::initialize(p);

  _header = p.get_arg();
  _value.set_value(p.get_value());
  
  require_resources(RSRC_SERVER_RESPONSE_HEADERS);
  require_resources(RSRC_SERVER_REQUEST_HEADERS);
  require_resources(RSRC_CLIENT_REQUEST_HEADERS);
  require_resources(RSRC_CLIENT_RESPONSE_HEADERS);
}


void
OperatorAddHeader::exec(const Resources& res) const
{
//  int IP = TSHttpTxnServerIPGet(res.txnp);
//  inet_ntop(AF_INET, &IP, buf, sizeof(buf));
  std::string value;

  _value.append_value(value, res);

  // Never set an empty header (I don't think that ever makes sense?)
  if (value.empty()) {
    TSDebug(PLUGIN_NAME, "Would set header %s to an empty value, skipping", _header.c_str());
    return;
  }
  
  if (res.bufp && res.hdr_loc) {
    TSDebug(PLUGIN_NAME, "OperatorAddHeader::exec() invoked on header %s: %s", _header.c_str(), value.c_str());
    TSMLoc field_loc;
    
    if (TS_SUCCESS == TSMimeHdrFieldCreateNamed(res.bufp, res.hdr_loc, _header.c_str(), _header.size(), &field_loc)) {
      if (TS_SUCCESS == TSMimeHdrFieldValueStringInsert(res.bufp, res.hdr_loc, field_loc, -1, value.c_str(), value.size())) {
        TSDebug(PLUGIN_NAME, "   adding header %s", _header.c_str());
        //INKHttpHdrPrint(res.bufp, res.hdr_loc, reqBuff);
        TSMimeHdrFieldAppend(res.bufp, res.hdr_loc, field_loc);
      }
      TSHandleMLocRelease(res.bufp, res.hdr_loc, field_loc);
    }
      
  }
}


/// TODO and XXX: These currently only support when running as remap plugin.
// OperatorSetDestination
void
OperatorSetDestination::initialize(Parser& p) {
  Operator::initialize(p);

  _url_qual = parse_url_qualifier(p.get_arg());
  _value.set_value(p.get_value());
  // TODO: What resources would we require here?
}


void
OperatorSetDestination::exec(const Resources& res) const
{
  if (res._rri) {
    std::string value;

    // Never set an empty destination value (I don't think that ever makes sense?)
    switch (_url_qual) {

    case URL_QUAL_HOST:
      _value.append_value(value, res);
      if (value.empty()) {
        TSDebug(PLUGIN_NAME, "Would set destination HOST to an empty value, skipping");
      } else {
        const_cast<Resources&>(res).changed_url = true;
        TSUrlHostSet(res._rri->requestBufp, res._rri->requestUrl, value.c_str(), value.size());
        TSDebug(PLUGIN_NAME, "OperatorSetHost::exec() invoked with HOST: %s", value.c_str());
      }
      break;

    case URL_QUAL_PATH:
      _value.append_value(value, res);
      if (value.empty()) {
        TSDebug(PLUGIN_NAME, "Would set destination PATH to an empty value, skipping");
      } else {
        const_cast<Resources&>(res).changed_url = true;
        TSUrlHostSet(res._rri->requestBufp, res._rri->requestUrl, value.c_str(), value.size());
        TSDebug(PLUGIN_NAME, "OperatorSetHost::exec() invoked with PATH: %s", value.c_str());
      }
      break;

    case URL_QUAL_QUERY:
      _value.append_value(value, res);
      if (value.empty()) {
        TSDebug(PLUGIN_NAME, "Would set destination QUERY to an empty value, skipping");
      } else {
        //1.6.4--Support for preserving QSA in case of set-destination
        if (get_oper_modifiers() & OPER_QSA) {
          int query_len = 0;
          const char* query = TSUrlHttpQueryGet(res._rri->requestBufp, res._rri->requestUrl, &query_len);
          TSDebug(PLUGIN_NAME, "QSA mode, append original query string: %.*s", query_len, query);
          //std::string connector = (value.find("?") == std::string::npos)? "?" : "&";
          value.append("&");
          value.append(query, query_len);
        }

        const_cast<Resources&>(res).changed_url = true;
        TSUrlHttpQuerySet(res._rri->requestBufp, res._rri->requestUrl, value.c_str(), value.size());
        TSDebug(PLUGIN_NAME, "OperatorSetHost::exec() invoked with QUERY: %s", value.c_str());
      }
      break;

    case URL_QUAL_PORT:
      if (_value.get_int_value() <= 0) {
        TSDebug(PLUGIN_NAME, "Would set destination PORT to an invalid range, skipping");
      } else {
        const_cast<Resources&>(res).changed_url = true;
        TSUrlPortSet(res._rri->requestBufp, res._rri->requestUrl, _value.get_int_value());
        TSDebug(PLUGIN_NAME, "OperatorSetHost::exec() invoked with PORT: %d", _value.get_int_value());
      }
      break;
    case URL_QUAL_URL:
      // TODO: Implement URL parser.
      break;
    default:
      break;
    }
  } else {
    // TODO: Handle the non-remap case here (InkAPI hooks)
  }
}


/// TODO and XXX: These currently only support when running as remap plugin.
// OperatorSetRedirect
void
OperatorSetRedirect::initialize(Parser& p) {
  Operator::initialize(p);

  _status.set_value(p.get_arg());
  _location.set_value(p.get_value());

  if ((_status.get_int_value() != (int)TS_HTTP_STATUS_MOVED_PERMANENTLY) &&
      (_status.get_int_value() != (int)TS_HTTP_STATUS_MOVED_TEMPORARILY)) {
    TSError("header_rewrite: unsupported redirect status %d", _status.get_int_value());
  }

  require_resources(RSRC_SERVER_RESPONSE_HEADERS);
  require_resources(RSRC_CLIENT_RESPONSE_HEADERS);
  require_resources(RSRC_RESPONSE_STATUS);
}


void
OperatorSetRedirect::exec(const Resources& res) const
{
  if (res._rri) {
    if (res.bufp && res.hdr_loc) {
      std::string value;

      _location.append_value(value, res);
         
      // Replace %{PATH} to original path
      size_t pos_path = 0;
      
      if ((pos_path = value.find("%{PATH}")) != std::string::npos) {
          value.erase(pos_path, 7); // erase %{PATH} from the rewritten to url
          int path_len = 0;
          const char *path = TSUrlPathGet(res._rri->requestBufp, res._rri->requestUrl, &path_len);
          if (path_len > 0) {
            TSDebug(PLUGIN_NAME, "Find %%{PATH} in redirect url, replace it with: %.*s", path_len, path);
            value.insert(pos_path, path, path_len);
          }
      }
       
      // Append the original query string
      int query_len = 0;
      const char *query = TSUrlHttpQueryGet(res._rri->requestBufp, res._rri->requestUrl, &query_len);
      if ((get_oper_modifiers() & OPER_QSA) && (query_len > 0)) {
          TSDebug(PLUGIN_NAME, "QSA mode, append original query string: %.*s", query_len, query);
          std::string connector = (value.find("?") == std::string::npos)? "?" : "&";
          value.append(connector);
          value.append(query, query_len);
      }

      TSHttpTxnSetHttpRetStatus(res.txnp,(TSHttpStatus)_status.get_int_value());
      //TSHttpHdrStatusSet(res.bufp, res.hdr_loc, (TSHttpStatus)_status.get_int_value());
      const char *start = value.c_str();
      const char *end = value.size() + start;
      TSUrlParse(res._rri->requestBufp, res._rri->requestUrl, &start, end);
      TSDebug(PLUGIN_NAME, "OperatorSetRedirect::exec() invoked with destination=%s and status code=%d", 
              value.c_str(), _status.get_int_value());
    }
    
  } else {
    // TODO: Handle the non-remap case here (InkAPI hooks)
  }
}


// OperatorSetTimeoutOut
void
OperatorSetTimeoutOut::initialize(Parser& p) {
  Operator::initialize(p);

  if (p.get_arg() == "active") {
    _type = TO_OUT_ACTIVE;
  } else if (p.get_arg() == "inactive") {
    _type = TO_OUT_INACTIVE;
  } else if (p.get_arg() == "connect") {
    _type = TO_OUT_CONNECT;
  } else if (p.get_arg() == "dns") {
    _type = TO_OUT_DNS;
  } else {
    _type = TO_OUT_UNDEFINED;
    TSError("header_rewrite: unsupported timeout qualifier: %s", p.get_arg().c_str());
  }

  _timeout.set_value(p.get_value());
}


void
OperatorSetTimeoutOut::exec(const Resources& res) const
{
  switch (_type) {
  case TO_OUT_ACTIVE:
    TSDebug(PLUGIN_NAME, "OperatorSetTimeoutOut::exec(active, %d)", _timeout.get_int_value());
    TSHttpTxnActiveTimeoutSet(res.txnp, _timeout.get_int_value());
    break;

  case TO_OUT_INACTIVE:
    TSDebug(PLUGIN_NAME, "OperatorSetTimeoutOut::exec(inactive, %d)", _timeout.get_int_value());
    TSHttpTxnNoActivityTimeoutSet(res.txnp, _timeout.get_int_value());
    break;

  case TO_OUT_CONNECT:
    TSDebug(PLUGIN_NAME, "OperatorSetTimeoutOut::exec(connect, %d)", _timeout.get_int_value());
    TSHttpTxnConnectTimeoutSet(res.txnp, _timeout.get_int_value());
    break;

  case TO_OUT_DNS:
    TSDebug(PLUGIN_NAME, "OperatorSetTimeoutOut::exec(dns, %d)", _timeout.get_int_value());
    TSHttpTxnDNSTimeoutSet(res.txnp, _timeout.get_int_value());
    break;
  default:
    TSError("header_rewrite: unsupported timeout");
    break;
  }
}

