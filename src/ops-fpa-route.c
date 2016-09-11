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
 *  File: ops-fpa-route.c
 *
 *  Purpose: This file contains OpenSwitch route related
 *           application code for the FPA SDK.
 */

#include "ops-fpa-route.h"

#define OPS_FPA_ROUTE_IPV4_COOKIE(ipv4, mask_len) (((uint64_t)(ipv4) << 8) | (0xff & (mask_len)))

VLOG_DEFINE_THIS_MODULE(ops_fpa_route);

int
ops_fpa_route_add_l2_group(int sid, int pid, int vid, bool pop_tag, uint32_t *group)
{
    FPA_GROUP_ENTRY_IDENTIFIER_STC ident = {
        .groupType = FPA_GROUP_L2_INTERFACE_E,
        .portNum = pid,
        .vlanId = vid
    };
    int err = fpaLibGroupIdentifierBuild(&ident, group);
    if (err) {
        VLOG_ERR(
            "%s: can't build group(sid=%d pid=%d vid=%d): %s",
            __func__, sid, pid, vid, ops_fpa_strerr(err)
        );
        return 1;
    }

    FPA_GROUP_TABLE_ENTRY_STC entry = {
        .groupIdentifier = *group,
        .groupTypeSemantics = FPA_GROUP_INDIRECT
    };

    err = fpaLibGroupTableEntryAdd(sid, &entry);
    if (err) {
        VLOG_ERR(
            "%s: can't add flow(sid=%d pid=%d vid=%d): %s",
            __func__, sid, pid, vid, ops_fpa_strerr(err)
        );
        return 1;
    }

    FPA_GROUP_BUCKET_ENTRY_STC bucket = {
        .groupIdentifier = *group,
        .index = 0,
        .type = FPA_GROUP_BUCKET_L2_INTERFACE_E,
        .data.l2Interface.outputPort = pid,
        .data.l2Interface.popVlanTagAction = pop_tag
    };

    err = fpaLibGroupEntryBucketAdd(sid, &bucket);
    if (err) {
        VLOG_ERR(
            "%s: can't add bucket(sid=%d pid=%d vid=%d): %s",
            __func__, sid, pid, vid, ops_fpa_strerr(err)
        );
        err = fpaLibGroupTableEntryDelete(sid, *group);
        if (err) {
            VLOG_ERR(
                "%s: can't cleanup flow(sid=%d pid=%d vid=%d): %s",
                __func__, sid, pid, vid, ops_fpa_strerr(err)
            );
        }
        return 1;
    }

    return 0;
}

int
ops_fpa_route_add_l3_group(
    int sid, uint32_t l2_group, int arp_index, int vid, int mtu,
    struct eth_addr *src_mac, struct eth_addr *dst_mac, uint32_t *l3_group
)
{
    VLOG_INFO("%s: l2_group=%d arp_index=%d vid=%d mtu=%d",
        __func__, l2_group, arp_index, vid, mtu);

    FPA_GROUP_ENTRY_IDENTIFIER_STC ident = {
        .groupType = FPA_GROUP_L3_UNICAST_E,
        .index = arp_index
    };
    if (fpaLibGroupIdentifierBuild(&ident, l3_group)) {
        return 1;
    }

    /* Exit if L3 entry exist. */
    FPA_GROUP_TABLE_ENTRY_STC l3GroupEntry;
    if (!fpaLibGroupTableGetEntry(sid, *l3_group, &l3GroupEntry)) {
        return 1;
    }

    l3GroupEntry.groupIdentifier = *l3_group;
    l3GroupEntry.groupTypeSemantics = FPA_GROUP_INDIRECT;
    if (wrap_fpaLibGroupTableEntryAdd(sid, &l3GroupEntry)) {
        return 1;
    }

    /* new group created - create with default values */
    FPA_GROUP_BUCKET_ENTRY_STC bucket = {
        .groupIdentifier = *l3_group,
        .index = 0,
        .type = FPA_GROUP_BUCKET_L3_UNICAST_E,
        /* Destination MAC in the Routing flow */
        .data.l3Unicast.dstMac = *(FPA_MAC_ADDRESS_STC *)dst_mac,
        /* Source MAC of L3 group is the Mac2Me dest MAC of the termination Entry */
        .data.l3Unicast.srcMac = *(FPA_MAC_ADDRESS_STC *)src_mac,
        .data.l3Unicast.vlanId = vid,
        .data.l3Unicast.mtu = mtu,
        .data.l3Unicast.refGroupId = l2_group
    };
    if (wrap_fpaLibGroupEntryBucketAdd(sid, &bucket)) {
        wrap_fpaLibGroupTableEntryDelete(sid, *l3_group);
        *l3_group = 0;
        return 1;
    }

    return 0;
}

int
ops_fpa_route_add_route(int sid, uint32_t l3_group, in_addr_t ipv4, int mask_len)
{
    VLOG_INFO("%s: l3_group %d, ip "IP_FMT", mask %d",
        __func__, l3_group, IP_ARGS(ipv4), mask_len);

    FPA_FLOW_TABLE_ENTRY_STC entry;
    if (fpaLibFlowEntryInit(sid, FPA_FLOW_TABLE_TYPE_L3_UNICAST_E, &entry)) {
        return 1;
    }

    entry.cookie = OPS_FPA_ROUTE_IPV4_COOKIE(ipv4, mask_len);
    entry.data.l3_unicast.groupId = l3_group;
    entry.data.l3_unicast.match.etherType = 0x800;
    entry.data.l3_unicast.outputPort = 0;
    entry.data.l3_unicast.match.dstIp4 = ipv4;
    entry.data.l3_unicast.match.dstIp4Mask = 0xffffffff >> (32 - mask_len);

    return wrap_fpaLibFlowEntryAdd(sid, FPA_FLOW_TABLE_TYPE_L3_UNICAST_E, &entry);
}

int
ops_fpa_route_add_route_trap(int sid, in_addr_t ipv4, int mask_len)
{
    FPA_FLOW_TABLE_ENTRY_STC entry;
    if (fpaLibFlowEntryInit(sid, FPA_FLOW_TABLE_TYPE_L3_UNICAST_E, &entry)) {
        return 1;
    }

    entry.cookie = OPS_FPA_ROUTE_IPV4_COOKIE(ipv4, mask_len);
    entry.data.l3_unicast.groupId = 0xffffffff;
    entry.data.l3_unicast.match.etherType = 0x800;
    entry.data.l3_unicast.outputPort = FPA_OUTPUT_CONTROLLER;

    entry.data.l3_unicast.match.dstIp4 = ipv4;
    entry.data.l3_unicast.match.dstIp4Mask = 0xffffffff >> (32 - mask_len);

    return wrap_fpaLibFlowEntryAdd(sid, FPA_FLOW_TABLE_TYPE_L3_UNICAST_E, &entry);
}

int
ops_fpa_route_del_route(int sid, in_addr_t ipv4, int mask_len)
{
    VLOG_INFO("%s: ipv4 "IP_FMT", mask %d", __func__, IP_ARGS(ipv4), mask_len);

    int err = wrap_fpaLibFlowTableCookieDelete(sid,
        FPA_FLOW_TABLE_TYPE_L3_UNICAST_E,
        OPS_FPA_ROUTE_IPV4_COOKIE(ipv4, mask_len)
    );

    if (err) {
        VLOG_ERR("%s: failed to delete route %s: %s", __func__,
            ops_fpa_ip2str(ipv4), ops_fpa_strerr(err)
        );
        return 1;
    }

    return 0;
}

int
ops_fpa_route_del_group(int sid, uint32_t group)
{
    VLOG_INFO("%s: group %d", __func__, group);

    int err = wrap_fpaLibGroupTableEntryDelete(sid, group);
    if (err) {
        VLOG_ERR("%s: failed to delete group %u: %s",
                  __func__, group, ops_fpa_strerr(err));
        return 1;
    }

    return 0;
}
