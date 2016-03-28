/*
 * The olsr.org Optimized Link-State Routing daemon(olsrd)
 * Copyright (c) 2004
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

#ifndef _OLSRD_LIB_INFO_JSON_HELPERS_H_
#define _OLSRD_LIB_INFO_JSON_HELPERS_H_

#include <stdio.h>
#include <stdbool.h>

#include "common/autobuf.h"

void abuf_json_reset_entry_number_and_depth(void);

void abuf_json_mark_output(bool open, struct autobuf *abuf);

void abuf_json_mark_object(bool open, bool array, struct autobuf *abuf, const char* header);

void abuf_json_mark_array_entry(bool open, struct autobuf *abuf);

void abuf_json_insert_comma(struct autobuf *abuf);

void abuf_json_boolean(struct autobuf *abuf, const char* key, bool value);

void abuf_json_string(struct autobuf *abuf, const char* key, const char* value);

void abuf_json_int(struct autobuf *abuf, const char* key, long long value);

void abuf_json_float(struct autobuf *abuf, const char* key, double value);

void abuf_json_ip_address(struct autobuf *abuf, const char* key, union olsr_ip_addr *ip);

void abuf_json_ip_address46(struct autobuf *abuf, const char* key, void *ip, int af);

#endif /* _OLSRD_LIB_INFO_JSON_HELPERS_H_ */
