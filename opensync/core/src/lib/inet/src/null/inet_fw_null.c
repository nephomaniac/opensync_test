/*
Copyright (c) 2015, Plume Design Inc. All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:
   1. Redistributions of source code must retain the above copyright
      notice, this list of conditions and the following disclaimer.
   2. Redistributions in binary form must reproduce the above copyright
      notice, this list of conditions and the following disclaimer in the
      documentation and/or other materials provided with the distribution.
   3. Neither the name of the Plume Design Inc. nor the
      names of its contributors may be used to endorse or promote products
      derived from this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
DISCLAIMED. IN NO EVENT SHALL Plume Design Inc. BE LIABLE FOR ANY
DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
(INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/

#include "log.h"
#include "inet_fw.h"

struct __inet_fw
{
};

inet_fw_t *inet_fw_new(const char *ifname)
{
    LOG(WARN, "inet_fw: %s: Firewall not supported on this platform.", ifname);

    return (void *)0xdeadc0de;
}

bool inet_fw_del(inet_fw_t *self)
{
    (void)self;
    return true;
}

bool inet_fw_start(inet_fw_t *self)
{
    (void)self;
    return true;
}

bool inet_fw_stop(inet_fw_t *self)
{
    (void)self;
    return true;
}

bool inet_fw_nat_set(inet_fw_t *self, bool enable)
{
    (void)self;
    (void)enable;

    return true;
}

bool inet_fw_state_get(inet_fw_t *self, bool *nat_enabled)
{
    (void)self;
    *nat_enabled = false;
    return true;
}

bool inet_fw_portforward_get(inet_fw_t *self, const struct inet_portforward *pf)
{
    (void)self;
    (void)pf;

    return true;
}

bool inet_fw_portforward_set(inet_fw_t *self, const struct inet_portforward *pf)
{
    (void)self;
    (void)pf;

    return true;
}

bool inet_fw_portforward_del(inet_fw_t *self, const struct inet_portforward *pf)
{
    (void)self;
    (void)pf;

    return true;
}
