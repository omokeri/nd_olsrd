/*
 * The olsr.org Optimized Link-State Routing daemon (olsrd)
 *
 * (c) by the OLSR project
 *
 * See our Git repository to find out who worked on this file
 * and thus is a copyright holder on it.
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

#include <assert.h>
#include <string.h>
#include <stdio.h>

#include "common/string_handling.h"

/*
 * A somewhat safe version of strncpy and strncat. Note, that
 * BSD/Solaris strlcpy()/strlcat() differ in implementation, while
 * the BSD compiler prints out a warning if you use plain strcpy().
 */

char * strscpy(char *dest, const char *src, size_t dest_size) {
  register size_t l = 0;
#if !defined(NODEBUG) && defined(DEBUG)
  if (NULL == dest)
    fprintf(stderr, "Warning: dest is NULL in strscpy!\n");
  if (NULL == src)
    fprintf(stderr, "Warning: src is NULL in strscpy!\n");
#endif /* !defined(NODEBUG) && defined(DEBUG) */
  if (!dest || !src) {
    return NULL;
  }

  /* src does not need to be null terminated */
  if (0 < dest_size--)
    while (l < dest_size && 0 != src[l])
      l++;
  dest[l] = 0;

  return strncpy(dest, src, l);
}

char * strscat(char *dest, const char *src, size_t dest_size) {
  register size_t l = strlen(dest);
  return strscpy(dest + l, src, dest_size > l ? dest_size - l : 0);
}
