/** @file

  A brief file description

  @section license License

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

/*
 * response-header-1.c:
 *		an example program which illustrates adding and manipulating
 *		an HTTP response MIME header:
 *
 *   Authorized possession and use of this software pursuant only
 *   to the terms of a written license agreement.
 *
 *	Usage:	response-header-1.so
 *
 *	add read_resp_header hook
 *	get http response header
 *	if 200, then
 *		add mime extension header with count of zero
 *		add mime extension header with date response was received
 *		add "Cache-Control: public" header
 *	else if 304, then
 *		retrieve cached header
 *		get old value of mime header count
 *		increment mime header count
 *		store mime header with new count
 *
 *
 *
 */

#include <time.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>

#include "ts/ts.h"
#include "ink_defs.h"

static int init_buffer_status;

static char *mimehdr1_name;
static char *mimehdr2_name;
static char *mimehdr1_value;

static TSMBuffer hdr_bufp;
static TSMLoc hdr_loc;

static TSMLoc field_loc;
static TSMLoc value_loc;

static void
modify_header(TSHttpTxn txnp)
{
  TSMBuffer resp_bufp;
  TSMBuffer cached_bufp;
  TSMLoc resp_loc;
  TSMLoc cached_loc;
  TSHttpStatus resp_status;
  TSMLoc new_field_loc;
  TSMLoc cached_field_loc;
  time_t recvd_time;

  const char *chkptr;
  int chklength;

  int num_refreshes = 0;

  if (!init_buffer_status)
    return;                     /* caller reenables */

  if (TSHttpTxnServerRespGet(txnp, &resp_bufp, &resp_loc) != TS_SUCCESS) {
    TSError("couldn't retrieve server response header\n");
    return;                     /* caller reenables */
  }

  /* TSqa06246/TSqa06144 */
  resp_status = TSHttpHdrStatusGet(resp_bufp, resp_loc);

  if (TS_HTTP_STATUS_OK == resp_status) {

    TSDebug("resphdr", "Processing 200 OK");
    TSMimeHdrFieldCreate(resp_bufp, resp_loc, &new_field_loc); /* Probably should check for errors */
    TSDebug("resphdr", "Created new resp field with loc %p", new_field_loc);

    /* copy name/values created at init
     * ( "x-num-served-from-cache" ) : ( "0"  )
     */
    TSMimeHdrFieldCopy(resp_bufp, resp_loc, new_field_loc, hdr_bufp, hdr_loc, field_loc);

        /*********** Unclear why this is needed **************/
    TSMimeHdrFieldAppend(resp_bufp, resp_loc, new_field_loc);


    /* Cache-Control: Public */
    TSMimeHdrFieldCreate(resp_bufp, resp_loc, &new_field_loc); /* Probably should check for errors */
    TSDebug("resphdr", "Created new resp field with loc %p", new_field_loc);
    TSMimeHdrFieldAppend(resp_bufp, resp_loc, new_field_loc);
    TSMimeHdrFieldNameSet(resp_bufp, resp_loc, new_field_loc,
                           TS_MIME_FIELD_CACHE_CONTROL, TS_MIME_LEN_CACHE_CONTROL);
    TSMimeHdrFieldValueStringInsert(resp_bufp, resp_loc, new_field_loc,
                                     -1, TS_HTTP_VALUE_PUBLIC, TS_HTTP_LEN_PUBLIC);

    /*
     * mimehdr2_name  = TSstrdup( "x-date-200-recvd" ) : CurrentDateTime
     */
    TSMimeHdrFieldCreate(resp_bufp, resp_loc, &new_field_loc); /* Probably should check for errors */
    TSDebug("resphdr", "Created new resp field with loc %p", new_field_loc);
    TSMimeHdrFieldAppend(resp_bufp, resp_loc, new_field_loc);
    TSMimeHdrFieldNameSet(resp_bufp, resp_loc, new_field_loc, mimehdr2_name, strlen(mimehdr2_name));
    recvd_time = time(NULL);
    TSMimeHdrFieldValueDateInsert(resp_bufp, resp_loc, new_field_loc, recvd_time);

    TSHandleMLocRelease(resp_bufp, resp_loc, new_field_loc);
    TSHandleMLocRelease(resp_bufp, TS_NULL_MLOC, resp_loc);

  } else if (TS_HTTP_STATUS_NOT_MODIFIED == resp_status) {

    TSDebug("resphdr", "Processing 304 Not Modified");

    /* N.B.: Protect writes to data (hash on URL + mutex: (ies)) */

    /* Get the cached HTTP header */
    if (TSHttpTxnCachedRespGet(txnp, &cached_bufp, &cached_loc) != TS_SUCCESS) {
      TSError("STATUS 304, TSHttpTxnCachedRespGet():");
      TSError("couldn't retrieve cached response header\n");
      TSHandleMLocRelease(resp_bufp, TS_NULL_MLOC, resp_loc);
      return;                   /* Caller reenables */
    }

    /* Get the cached MIME field name for this HTTP header */
    cached_field_loc = TSMimeHdrFieldFind(cached_bufp, cached_loc,
                                           (const char *) mimehdr1_name, strlen(mimehdr1_name));
    if (TS_NULL_MLOC == cached_field_loc) {
      TSError("Can't find header %s in cached document", mimehdr1_name);
      TSHandleMLocRelease(resp_bufp, TS_NULL_MLOC, resp_loc);
      TSHandleMLocRelease(cached_bufp, TS_NULL_MLOC, cached_loc);
      return;                   /* Caller reenables */
    }

    /* Get the cached MIME value for this name in this HTTP header */
    chkptr = TSMimeHdrFieldValueStringGet(cached_bufp, cached_loc, cached_field_loc, 0, &chklength);
    if (NULL == chkptr || !chklength) {
      TSError("Could not find value for cached MIME field name %s", mimehdr1_name);
      TSHandleMLocRelease(resp_bufp, TS_NULL_MLOC, resp_loc);
      TSHandleMLocRelease(cached_bufp, TS_NULL_MLOC, cached_loc);
      TSHandleMLocRelease(cached_bufp, cached_loc, cached_field_loc);
      return;                   /* Caller reenables */
    }
    TSDebug("resphdr", "Header field value is %s, with length %d", chkptr, chklength);


    /* Get the cached MIME value for this name in this HTTP header */
    /*
       TSMimeHdrFieldValueUintGet(cached_bufp, cached_loc, cached_field_loc, 0, &num_refreshes);
       TSDebug("resphdr",
       "Cached header shows %d refreshes so far", num_refreshes );

       num_refreshes++ ;
     */

       /* txn origin server response for this transaction stored
       * in resp_bufp, resp_loc
       *
       * Create a new MIME field/value. Cached value has been incremented.
       * Insert new MIME field/value into the server response buffer,
       * allow HTTP processing to continue. This will update
       * (indirectly invalidates) the cached HTTP headers MIME field.
       * It is apparently not necessary to update all of the MIME fields
       * in the in-process response in order to have the cached response
       * become invalid.
     */
    TSMimeHdrFieldCreate(resp_bufp, resp_loc, &new_field_loc); /* Probaby should check for errrors */

    /* mimehdr1_name : TSstrdup( "x-num-served-from-cache" ) ; */

    TSMimeHdrFieldAppend(resp_bufp, resp_loc, new_field_loc);
    TSMimeHdrFieldNameSet(resp_bufp, resp_loc, new_field_loc, mimehdr1_name, strlen(mimehdr1_name));

    TSMimeHdrFieldValueUintInsert(resp_bufp, resp_loc, new_field_loc, -1, num_refreshes);

    TSHandleMLocRelease(resp_bufp, resp_loc, new_field_loc);
    TSHandleMLocRelease(cached_bufp, cached_loc, cached_field_loc);
    TSHandleMLocRelease(cached_bufp, TS_NULL_MLOC, cached_loc);
    TSHandleMLocRelease(resp_bufp, TS_NULL_MLOC, resp_loc);

  } else {
    TSDebug("resphdr", "other response code %d", resp_status);
  }

  /*
   *  Additional 200/304 processing can go here, if so desired.
   */

  /* Caller reneables */
}


static int
modify_response_header_plugin(TSCont contp ATS_UNUSED, TSEvent event, void *edata)
{
  TSHttpTxn txnp = (TSHttpTxn) edata;

  switch (event) {
  case TS_EVENT_HTTP_READ_RESPONSE_HDR:
    TSDebug("resphdr", "Called back with TS_EVENT_HTTP_READ_RESPONSE_HDR");
    modify_header(txnp);
    TSHttpTxnReenable(txnp, TS_EVENT_HTTP_CONTINUE);
    /*  fall through  */

  default:
    break;
  }
  return 0;
}

int
check_ts_version()
{

  const char *ts_version = TSTrafficServerVersionGet();
  int result = 0;

  if (ts_version) {
    int major_ts_version = 0;
    int minor_ts_version = 0;
    int patch_ts_version = 0;

    if (sscanf(ts_version, "%d.%d.%d", &major_ts_version, &minor_ts_version, &patch_ts_version) != 3) {
      return 0;
    }

    /* Need at least TS 2.0 */
    if (major_ts_version >= 2) {
      result = 1;
    }

  }

  return result;
}

void
TSPluginInit(int argc, const char *argv[])
{
  TSMLoc chk_field_loc;

  TSPluginRegistrationInfo info;

  info.plugin_name = "response-header-1";
  info.vendor_name = "MyCompany";
  info.support_email = "ts-api-support@MyCompany.com";

  if (TSPluginRegister(TS_SDK_VERSION_3_0, &info) != TS_SUCCESS) {
    TSError("Plugin registration failed.\n");
  }

  if (!check_ts_version()) {
    TSError("Plugin requires Traffic Server 3.0 or later\n");
    return;
  }

  init_buffer_status = 0;
  if (argc > 1) {
    TSError("usage: %s \n", argv[0]);
    TSError("warning: too many args %d\n", argc);
    TSError("warning: ignoring unused arguments beginning with %s\n", argv[1]);
  }

  /*
   *  The following code sets up an "init buffer" containing an extension header
   *  and its initial value.  This will be the same for all requests, so we try
   *  to be efficient and do all of the work here rather than on a per-transaction
   *  basis.
   */


  hdr_bufp = TSMBufferCreate();
  TSMimeHdrCreate(hdr_bufp, &hdr_loc);

  mimehdr1_name = TSstrdup("x-num-served-from-cache");
  mimehdr1_value = TSstrdup("0");

  /* Create name here and set DateTime value when o.s.
   * response 200 is received
   */
  mimehdr2_name = TSstrdup("x-date-200-recvd");

  TSDebug("resphdr", "Inserting header %s with value %s into init buffer", mimehdr1_name, mimehdr1_value);

  TSMimeHdrFieldCreate(hdr_bufp, hdr_loc, &field_loc); /* Probably should check for errors */
  TSMimeHdrFieldAppend(hdr_bufp, hdr_loc, field_loc);
  TSMimeHdrFieldNameSet(hdr_bufp, hdr_loc, field_loc, mimehdr1_name, strlen(mimehdr1_name));
  TSMimeHdrFieldValueStringInsert(hdr_bufp, hdr_loc, field_loc, -1, mimehdr1_value, strlen(mimehdr1_value));
  TSDebug("resphdr", "init buffer hdr, field and value locs are %p, %p and %p", hdr_loc, field_loc, value_loc);
  init_buffer_status = 1;


  TSHttpHookAdd(TS_HTTP_READ_RESPONSE_HDR_HOOK, TSContCreate(modify_response_header_plugin, NULL));

  /*
   *  The following code demonstrates how to extract the field_loc from the header.
   *  In this plugin, the init buffer and thus field_loc never changes.  Code
   *  similar to this may be used to extract header fields from any buffer.
   */

  if (TS_NULL_MLOC == (chk_field_loc = TSMimeHdrFieldGet(hdr_bufp, hdr_loc, 0))) {
    TSError("couldn't retrieve header field from init buffer");
    TSError("marking init buffer as corrupt; no more plugin processing");
    init_buffer_status = 0;
    /* bail out here and reenable transaction */
  } else {
    if (field_loc != chk_field_loc)
      TSError("retrieved buffer field loc is %p when it should be %p", chk_field_loc, field_loc);
  }
}
