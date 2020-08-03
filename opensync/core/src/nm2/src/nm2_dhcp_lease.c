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

#include <stdlib.h>
#include <sys/wait.h>
#include <stdint.h>
#include <errno.h>
#include <limits.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <signal.h>
#include <inttypes.h>
#include <jansson.h>

#include "json_util.h"
#include "evsched.h"
#include "schema.h"
#include "log.h"
#include "nm2.h"
#include "ovsdb.h"
#include "ovsdb_sync.h"

#define WAR_ESW_3313

// Defines
#define MODULE_ID LOG_MODULE_ID_MAIN

static bool nm2_dhcp_table_update(struct schema_DHCP_leased_IP *dlip);

#if defined(WAR_ESW_3313)
#include "ds_tree.h"
#include "synclist.h"


struct dhcp_lease_node
{
    struct osn_dhcp_server_lease    dl_lease;   /* Lease data */
    bool                            dl_sync;    /* Present on synclist */
    bool                            dl_updated; /* Update pending */
    ds_tree_node_t                  dl_tnode;   /* tree node */
    synclist_node_t                 dl_snode;   /* synclist node */
};

void *dhcp_lease_synclist_data = NULL;

static synclist_fn_t osn_dhcp_server_lease_sync;
static ds_key_cmp_t osn_dhcp_server_lease_cmp;

static bool __nm2_dhcp_lease_notify(
        void *data,
        bool released,
        struct osn_dhcp_server_lease *dl);


/* ds_tree comparator */
int osn_dhcp_server_lease_cmp(void *_a, void *_b)
{
    int rc;

    struct osn_dhcp_server_lease *a = _a;
    struct osn_dhcp_server_lease *b = _b;

    rc = osn_mac_addr_cmp(&a->dl_hwaddr, &b->dl_hwaddr);
    if (rc != 0) return rc;

    rc = osn_ip_addr_cmp(&a->dl_ipaddr, &b->dl_ipaddr);
    if (rc != 0) return rc;

    return 0;
}


/* Synclist comparator */
int osn_dhcp_server_lease_sync_cmp(void *_a, void *_b)
{
    struct osn_dhcp_server_lease *a = _a;
    struct osn_dhcp_server_lease *b = _b;

    return osn_mac_addr_cmp(&a->dl_hwaddr, &b->dl_hwaddr);
}

/* Synclist handler */
void *osn_dhcp_server_lease_sync(synclist_t *sync, void *_old, void *_new)
{
    (void)sync;
    struct dhcp_lease_node *pold = _old;
    struct dhcp_lease_node *pnew = _new;

    if (_old == NULL)
    {
        /* New element */
        pnew->dl_sync = true;

        /* Call the original lease handler */
        __nm2_dhcp_lease_notify(dhcp_lease_synclist_data, false, &pnew->dl_lease);

        return _new;
    }
    else if (_new == NULL)
    {
        pold->dl_sync = false;

        /* Call the original lease handler */
        __nm2_dhcp_lease_notify(dhcp_lease_synclist_data, true, &pold->dl_lease);

        /* Element removal */
        return NULL;
    }

    /* Compare the two elements, if pnew is "fresher", replace it */
    if (pnew->dl_lease.dl_leasetime > pold->dl_lease.dl_leasetime)
    {
        return pnew;
    }

    /*
     * Comapre hostname, vendorclass and fingerprint, issue an update if they
     * differ
     */
    if (pold->dl_updated)
    {
        pold->dl_updated = false;
        __nm2_dhcp_lease_notify(dhcp_lease_synclist_data, false, &pold->dl_lease);
    }

    return pold;
}

/*
 * Global list of DHCP leases, indexed by MAC+IPADDR
 */
static ds_tree_t dhcp_lease_list = DS_TREE_INIT(osn_dhcp_server_lease_cmp, struct dhcp_lease_node, dl_tnode);

static synclist_t dhcp_lease_synclist = SYNCLIST_INIT(
        osn_dhcp_server_lease_sync_cmp,
        struct dhcp_lease_node,
        dl_snode,
        osn_dhcp_server_lease_sync);
/*
 * Hijack the nm2_dhcp_lease_notify funciton to handle ESW-3313 properly.
 */
bool nm2_dhcp_lease_notify(
        void *data,
        bool released,
        struct osn_dhcp_server_lease *dl)
{
    struct dhcp_lease_node *node;

    node = ds_tree_find(&dhcp_lease_list, dl);

    /*
     * Update the global DHCP lease list first
     */
    if (released)
    {
        if (node == NULL)
        {
            LOG(ERR, "dhcp_lease: Error removing non-existent lease: "PRI_osn_mac_addr":"PRI_osn_ip_addr,
                    FMT_osn_mac_addr(dl->dl_hwaddr),
                    FMT_osn_ip_addr(dl->dl_ipaddr));
            return true;
        }

        ds_tree_remove(&dhcp_lease_list, node);

        /* If an element exists on the synclist as well, remove it from there too */
        if (node->dl_sync)
        {
            synclist_del(&dhcp_lease_synclist, node);
        }

        free(node);
    }
    else if (node == NULL)
    {
        node = calloc(1, sizeof(struct dhcp_lease_node));
        node->dl_lease = *dl;
        ds_tree_insert(&dhcp_lease_list, node, &node->dl_lease);
    }
    else
    {
        node->dl_lease = *dl;
        node->dl_updated = true;
    }

    /*
     * Now traverse the global lease list and collapse it to a synclist where
     * the index is only the MAC address
     */
    dhcp_lease_synclist_data = data;

    synclist_begin(&dhcp_lease_synclist);
    ds_tree_foreach(&dhcp_lease_list, node)
    {
        synclist_add(&dhcp_lease_synclist, node);
    }
    synclist_end(&dhcp_lease_synclist);

    return true;
}

bool __nm2_dhcp_lease_notify(
        void *data,
        bool released,
        struct osn_dhcp_server_lease *dl)

#else
/*
 * This callback is called by libinet whenever a new DHCP lease is detected.
 */
bool nm2_dhcp_lease_notify(
        void *data,
        bool released,
        struct osn_dhcp_server_lease *dl)
#endif
{
    (void)data;

    bool ip_is_any = (osn_ip_addr_cmp(&dl->dl_ipaddr, &OSN_IP_ADDR_INIT) == 0);

    LOG(INFO, "dhcp_lease: %s DHCP lease: MAC:"PRI_osn_mac_addr" IP:"PRI_osn_ip_addr" Hostname:%s Time:%d%s",
            released ? "Released" : "Acquired",
            FMT_osn_mac_addr(dl->dl_hwaddr),
            FMT_osn_ip_addr(dl->dl_ipaddr),
            dl->dl_hostname,
            (int)dl->dl_leasetime,
            ip_is_any ? ", skipping" : "");

    if (ip_is_any) return true;

    /* Fill in the schema structure */
    struct schema_DHCP_leased_IP sdl;

    memset(&sdl, 0, sizeof(sdl));

    sdl.hwaddr_exists = true;
    snprintf(sdl.hwaddr, sizeof(sdl.hwaddr), PRI_osn_mac_addr, FMT_osn_mac_addr(dl->dl_hwaddr));

    sdl.inet_addr_exists = true;
    snprintf(sdl.inet_addr, sizeof(sdl.inet_addr), PRI_osn_ip_addr, FMT_osn_ip_addr(dl->dl_ipaddr));

    sdl.hostname_exists = true;
    strscpy(sdl.hostname, dl->dl_hostname, sizeof(sdl.hostname));

    sdl.fingerprint_exists = true;
    strscpy(sdl.fingerprint, dl->dl_fingerprint, sizeof(sdl.fingerprint));

    sdl.vendor_class_exists = true;
    strscpy(sdl.vendor_class, dl->dl_vendorclass, sizeof(sdl.vendor_class));

    /* A lease time of 0 indicates that this entry should be deleted */
    sdl.lease_time_exists = true;
    if (released)
    {
        sdl.lease_time = 0;
    }
    else
    {
        sdl.lease_time = (int)dl->dl_leasetime;
        /* OLPS-153: sdl.lease_time should never be 0 on first insert! */
        if (sdl.lease_time == 0) sdl.lease_time = -1;
    }

    if (!nm2_dhcp_table_update(&sdl))
    {
        LOG(WARN, "dhcp_lease: Error processing DCHP lease entry "PRI_osn_mac_addr" ("PRI_osn_ip_addr"), %s)",
                FMT_osn_mac_addr(dl->dl_hwaddr),
                FMT_osn_ip_addr(dl->dl_ipaddr),
                dl->dl_hostname);
    }

    return true;
}

bool nm2_dhcp_table_update(struct schema_DHCP_leased_IP *dlip)
{
    pjs_errmsg_t        perr;
    json_t             *where, *row, *cond;
    bool                ret;

    LOGT("dhcp_lease: Updating DHCP lease '%s'", dlip->hwaddr);

    /* OVSDB transaction where multi condition */
    where = json_array();

    cond = ovsdb_tran_cond_single("hwaddr", OFUNC_EQ, str_tolower(dlip->hwaddr));
    json_array_append_new(where, cond);

#if !defined(WAR_ESW_3313)
    /*
     * Additionally, the DHCP sniffing code may pick up random DHCP updates
     * from across the network. It is imperative that there's always only 1 MAC
     * present in the table.
     */
    cond = ovsdb_tran_cond_single("inet_addr", OFUNC_EQ, dlip->inet_addr);
    json_array_append_new(where, cond);
#endif

    if (dlip->lease_time == 0)
    {
        // Released or expired lease... remove from OVSDB
        ret = ovsdb_sync_delete_where(SCHEMA_TABLE(DHCP_leased_IP), where);
        if (!ret)
        {
            LOGE("dhcp_lease: Updating DHCP lease %s, %s (Failed to remove entry)", dlip->inet_addr, dlip->hwaddr);
            return false;
        }

        LOGN("dhcp_lease: Removed DHCP lease '%s' with '%s' '%s' '%d'",
                dlip->hwaddr,
                dlip->inet_addr,
                dlip->hostname,
                dlip->lease_time);
    }
    else
    {
        // New/active lease, upsert it into OVSDB
        row = schema_DHCP_leased_IP_to_json(dlip, perr);
        if (!ovsdb_sync_upsert_where(SCHEMA_TABLE(DHCP_leased_IP), where, row, NULL))
        {
            LOGE("Updating DHCP lease %s (Failed to insert entry)", dlip->hwaddr);
            return false;
        }

        LOGN("Updated DHCP lease '%s' with '%s' '%s' '%d'",
                dlip->hwaddr,
                dlip->inet_addr,
                dlip->hostname,
                dlip->lease_time);
    }

    return true;
}

