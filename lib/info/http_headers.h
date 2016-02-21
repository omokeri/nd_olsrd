/*
 * The olsr.org Optimized Link-State Routing daemon(olsrd)
 * Copyright (c) 2015
 *
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * * Redistributions of source code must retain the above copyright
 *   notice, this list of conditions and the following disclaimer.
 * * Redistributions in binary form must reproduce the above copyright
 *   notice, this list of conditions and the following disclaimer in
 *   the documentation and/or other materials provided with the
 *   distribution.
 * * Neither the name of olsr.org, olsrd nor the names of its
 *   contributors may be used to endorse or promote products derived
 *   from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
 * COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
 * CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN
 * ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 *
 * Visit http://www.olsr.org for more information.
 *
 * If you find this software useful feel free to make a donation
 * to the project. For more information see the website or contact
 * the copyright holders.
 *
 */

#ifndef _OLSRD_LIB_INFO_HTTP_HEADERS_H_
#define _OLSRD_LIB_INFO_HTTP_HEADERS_H_

#include "common/autobuf.h"

#define INFO_HTTP_VERSION "HTTP/1.1"

/* Response types */
#define INFO_HTTP_OK             (200)
#define INFO_HTTP_NOTFOUND       (404)

void http_header_build(const char * plugin_name, unsigned int status, const char *mime, struct autobuf *abuf, int *contentLengthIndex);

void http_header_adjust_content_length(struct autobuf *abuf, int contentLengthIndex, int contentLength);

static INLINE const char * httpStatusToReply(unsigned int status) {
  switch (status) {
    case INFO_HTTP_NOTFOUND:
      return INFO_HTTP_VERSION " 404 Not Found";

    case INFO_HTTP_OK:
    default:
      return INFO_HTTP_VERSION " 200 OK";
  }
}

#endif /* _OLSRD_LIB_INFO_HTTP_HEADERS_H_ */
