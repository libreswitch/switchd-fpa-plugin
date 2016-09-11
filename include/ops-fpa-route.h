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
 *
 *  File: ops-fpa-route.h
 *
 *  Purpose: This file contains OpenSwitch route related
 *           application code for the FPA SDK.
 */

#ifndef OPS_FPA_ROUTE_H
#define OPS_FPA_ROUTE_H 1

#include "ops-fpa.h"

int ops_fpa_route_add_l2_group(
    int sid, int pid, int vid, bool pop_tag, uint32_t *group
);

int ops_fpa_route_add_l3_group(
    int sid, uint32_t l2_group, int arp_index,int vid, int mtu,
    struct eth_addr *src_mac, struct eth_addr *dst_mac, uint32_t *l3_group
);

int ops_fpa_route_add_route(
    int sid, uint32_t l3_group, in_addr_t ipv4, int mask_len
);

int ops_fpa_route_add_route_trap(int sid, in_addr_t ipv4, int mask_len);

int ops_fpa_route_del_route(int sid, in_addr_t ipv4, int mask_len);

int ops_fpa_route_del_group(int sid, uint32_t group);

#endif /* OPS_FPA_ROUTE_H */
