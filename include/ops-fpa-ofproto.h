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

#ifndef OPS_FPA_OFPROTO_H_
#define OPS_FPA_OFPROTO_H_

#include <vswitch-idl.h>
#include <ofproto/ofproto-provider.h>
#include "ops-fpa.h"
#include "ops-fpa-routing.h"

struct fpa_net_addr_t {
    struct hmap_node addr_node;
    int id;                     /* ID of host entry created on HW */
    char *address;              /* IPv4/IPv6 address */
};

struct fpa_l3_intf_t {
    size_t l3_vrf;
    bool vlan_intf;
    int vlan_id;
};

struct ofproto_fpa {
    struct hmap_node node;
    struct ofproto up;

    struct sset port_names;

    struct hmap bundles;
    unsigned long vlans[BITMAP_N_LONGS(FPA_VLAN_MAX_COUNT)];

    struct fpa_dev *dev;

    bool vrf;
    size_t vrf_id;

    uint32_t switch_id;

    uint64_t change_seq;           /* Connectivity status changes. */
};

struct bundle_fpa {
    struct hmap_node node;
    void *aux;

    char *name;
    enum port_vlan_mode vlan_mode;
    int vlan;
    unsigned long trunks[BITMAP_N_LONGS(FPA_VLAN_MAX_COUNT)];

    /* TODO: for LAG this needs to be a vector */
    int intfId;             /* Interface ID of the bundle (port, bond, etc) */
    ofp_port_t ofp_port;    /* OpenSwitch port ID */

    /*bool is_lag;*/ //LAG not supported in M3
    struct fpa_l3_intf *l3_intf;

    struct fpa_net_addr_t *ip4addr;
    struct hmap secondary_ip4addr; /* List of secondary IP address */
};

struct ofrule_fpa {
    struct rule up;
    FPA_FLOW_TABLE_ENTRY_STC entry;
};

struct ofport_fpa {
    struct hmap_node node;      /* In ports_by_pid map. */
    struct ofport up;
    //int vid;
};

struct port_iter {
    uint32_t bucket;
    uint32_t offset;
    //struct ofproto_port port;
};

#endif /* OPS_FPA_OFPROTO_H_ */

