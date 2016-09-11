/*
 *  Copyright (C) 2016, Marvell International Ltd. ALL RIGHTS RESERVED.
 *
 *    Licensed under the Apache License, Version 2.0 (the "License"); you may
 *    not use this file except in compliance with the License. You may obtain
 *    a copy of the License at http://www.apache.org/licenses/LICENSE-2.0
 *
 *    THIS CODE IS PROVIDED ON AN *AS IS* BASIS, WITHOUT WARRANTIES OR
 *    CONDITIONS OF ANY KIND, EITHER EXPRESS OR IMPLIED, INCLUDING WITHOUT
 *    LIMITATION ANY IMPLIED WARRANTIES OR CONDITIONS OF TITLE, FITNESS
 *    FOR A PARTICULAR PURPOSE, MERCHANTABILITY OR NON-INFRINGEMENT.
 *
 *    See the Apache Version 2.0 License for specific language governing
 *    permissions and limitations under the License.
 */

#ifndef OPS_FPA_OFPROTO_H
#define OPS_FPA_OFPROTO_H 1

#include <vswitch-idl.h>
#include <ofproto/ofproto-provider.h>
#include "ops-fpa.h"
#include "ops-fpa-routing.h"
#include "ops-fpa-vlan.h"

struct fpa_net_addr {
    struct hmap_node node;
    int id;                     /* ID of host entry created on HW */
    char *address;              /* IPv4/IPv6 address */
};

struct fpa_ofproto {
    struct hmap_node node;
    struct ofproto up;

    struct sset port_names;

    struct hmap bundles;
     /* vlans in 'no shutdown' state */
    unsigned long vlans[BITMAP_N_LONGS(VLAN_BITMAP_SIZE)];

    struct fpa_dev *dev;

    bool vrf;
    size_t vrf_id;

    uint32_t switch_id;

    uint64_t change_seq;           /* Connectivity status changes. */
};

#define FPA_OFPROTO(PTR) CONTAINER_OF(PTR, struct fpa_ofproto, up)

struct fpa_bundle {
    struct hmap_node node;
    void *aux;
    char *name;

    /* TODO: for LAG this needs to be a vector */
    int intf_id;             /* Interface ID of the bundle (port, bond, etc) */

    struct fpa_l3_intf *l3_intf;

    struct fpa_net_addr *ip4addr;
    struct hmap secondary_ip4addr; /* List of secondary IP address */
};

struct fpa_ofport {
    struct hmap_node node;      /* In ports_by_pid map. */
    struct ofport up;
    unsigned long vmap[OPS_FPA_VMAP_LONGS]; /* pending vlan's, not applied to asic */
};

#define FPA_OFPORT(PTR) CONTAINER_OF(PTR, struct fpa_ofport, up)

struct port_iter {
    uint32_t bucket;
    uint32_t offset;
};

#endif /* OPS_FPA_OFPROTO_H */
