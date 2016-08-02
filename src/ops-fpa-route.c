/*
*  Copyright (C) 2016. Marvell International Ltd. ALL RIGHTS RESERVED.
*
*    Licensed under the Apache License, Version 2.0 (the "License"); you may
*    not use this file except in compliance with the License. You may obtain
*    a copy of the License at http://www.apache.org/licenses/LICENSE-2.0
*
*    THIS CODE IS PROVIDED ON AN  *AS IS* BASIS, WITHOUT WARRANTIES OR
*    CONDITIONS OF ANY KIND, EITHER EXPRESS OR IMPLIED, INCLUDING WITHOUT
*    LIMITATION ANY IMPLIED WARRANTIES OR CONDITIONS OF TITLE, FITNESS
*    FOR A PARTICULAR PURPOSE, MERCHANTABLITY OR NON-INFRINGEMENT.
*
*    See the Apache Version 2.0 License for specific language governing
*    permissions and limitations under the License.
*
*/
#include "ops-fpa-route.h"

#define MARVELL_FPA_ROUTE_IPv4_COOCKIE(_ipv4, _mask_len) \
	( (_ipv4<<8) | (0xff &_mask_len) )

VLOG_DEFINE_THIS_MODULE(marvell_fpa);

void
ops_fpa_route_del_group(uint32_t switchId, uint32_t group)
{
    FPA_TRACE_FN();
    wrap_fpaLibGroupTableEntryDelete(switchId, group);
}

FPA_STATUS
ops_fpa_route_add_l2_group(uint32_t switchId, uint32_t port,
                         uint16_t vlan_id, uint32_t popVlanTagAction,
                         uint32_t *l2Group_ptr)
{
    FPA_STATUS                      status = FPA_OK;
    FPA_GROUP_ENTRY_IDENTIFIER_STC  parsedGroupIdentifier;
    FPA_GROUP_TABLE_ENTRY_STC       l2GroupEntry;
    FPA_GROUP_BUCKET_ENTRY_STC      bucket;

    FPA_TRACE_FN();
    memset(&parsedGroupIdentifier, 0, sizeof(parsedGroupIdentifier));
    memset(&l2GroupEntry, 0, sizeof(l2GroupEntry));
    memset(&bucket, 0, sizeof(bucket));

    parsedGroupIdentifier.groupType = FPA_GROUP_L2_INTERFACE_E;
    parsedGroupIdentifier.portNum = port;
    parsedGroupIdentifier.vlanId = vlan_id;
    status = fpaLibGroupIdentifierBuild(&parsedGroupIdentifier, l2Group_ptr);

    if (status != FPA_OK) {
        return status;
    }

    status = fpaLibGroupTableGetEntry(switchId, *l2Group_ptr, &l2GroupEntry);

    /* Exit if L2 entry exist. */
    if (status == FPA_OK) {
        return status;
    }

    l2GroupEntry.groupIdentifier = *l2Group_ptr;
    l2GroupEntry.groupTypeSemantics = FPA_GROUP_INDIRECT;

    status = wrap_fpaLibGroupTableEntryAdd(switchId, &l2GroupEntry);

    if (status != FPA_OK) {
        return status;
    }

    bucket.groupIdentifier = *l2Group_ptr;
    bucket.index = 0;
    bucket.type = FPA_GROUP_BUCKET_L2_INTERFACE_E;
    bucket.data.l2Interface.outputPort = port;
    bucket.data.l2Interface.popVlanTagAction = popVlanTagAction;
    status = wrap_fpaLibGroupEntryBucketAdd(switchId, &bucket);

    if (status != FPA_OK) {
        wrap_fpaLibGroupTableEntryDelete(switchId, *l2Group_ptr);
        *l2Group_ptr = 0;
    }

    return status;
}

FPA_STATUS
ops_fpa_route_add_l3_group(uint32_t switchId, uint32_t l2Group,
                         uint32_t arp_index, uint16_t vlan_id,
                         uint32_t mtu,
                         FPA_MAC_ADDRESS_STC *srcMac_ptr,
                         FPA_MAC_ADDRESS_STC *dsMac_ptr,
                         uint32_t *l3Group_ptr)
{
    FPA_STATUS                      status = FPA_OK;
    FPA_GROUP_ENTRY_IDENTIFIER_STC  parsedGroupIdentifier;
    FPA_GROUP_TABLE_ENTRY_STC       l3GroupEntry;
    FPA_GROUP_BUCKET_ENTRY_STC      bucket;

    FPA_TRACE_FN();
    memset(&parsedGroupIdentifier, 0, sizeof(parsedGroupIdentifier));
    memset(&l3GroupEntry, 0, sizeof(l3GroupEntry));
    memset(&bucket, 0, sizeof(bucket));

    parsedGroupIdentifier.groupType = FPA_GROUP_L3_UNICAST_E;
    parsedGroupIdentifier.index = arp_index;
    status = fpaLibGroupIdentifierBuild(&parsedGroupIdentifier, l3Group_ptr);
    if (status != FPA_OK) {
        return status;
    }

    status = fpaLibGroupTableGetEntry(switchId, *l3Group_ptr, &l3GroupEntry);

    /* Exit if L3 entry exist. */
    if (status == FPA_OK) {
        return status;
    }

    l3GroupEntry.groupIdentifier = *l3Group_ptr;
    l3GroupEntry.groupTypeSemantics = FPA_GROUP_INDIRECT;
    status = wrap_fpaLibGroupTableEntryAdd(switchId, &l3GroupEntry);

    if (status != FPA_OK) {
        return status;
    }

    /* new group created - create with default values */
    bucket.groupIdentifier = *l3Group_ptr;
    bucket.index = 0;
    bucket.type = FPA_GROUP_BUCKET_L3_UNICAST_E;
    /* Destination MAC in the Routing flow */
    bucket.data.l3Unicast.dstMac = *dsMac_ptr;
    /* Source MAC of L3 group is the Mac2Me dest MAC of the termination Entry */
    bucket.data.l3Unicast.srcMac = *srcMac_ptr;
    bucket.data.l3Unicast.vlanId = vlan_id;
    bucket.data.l3Unicast.mtu = mtu;
    bucket.data.l3Unicast.refGroupId = l2Group;
    status = wrap_fpaLibGroupEntryBucketAdd(switchId, &bucket);

    if (status != FPA_OK) {
        wrap_fpaLibGroupTableEntryDelete(switchId, *l3Group_ptr);
        *l3Group_ptr = 0;
    }

    return status;
}

FPA_STATUS
ops_fpa_route_add_route(uint32_t switchId, uint32_t l3Group,
                      in_addr_t dstIp4, uint32_t mask_len)
{
    FPA_STATUS status = FPA_OK;
    FPA_FLOW_TABLE_ENTRY_STC flowEntry;

    FPA_TRACE_FN();
    /* Initialize Flow Entry */
    memset(&flowEntry, 0, sizeof(FPA_FLOW_TABLE_ENTRY_STC));
    /* Fill all default fields */
    status = fpaLibFlowEntryInit(switchId, FPA_FLOW_TABLE_TYPE_L3_UNICAST_E,
                                 &flowEntry);

    flowEntry.cookie = MARVELL_FPA_ROUTE_IPv4_COOCKIE(dstIp4, mask_len);
    flowEntry.data.l3_unicast.groupId = l3Group;
    flowEntry.data.l3_unicast.match.etherType = 0x800;
    flowEntry.data.l3_unicast.outputPort = 0;

    flowEntry.data.l3_unicast.match.dstIp4 = dstIp4;
    flowEntry.data.l3_unicast.match.dstIp4Mask = 0xffffffff << (32 - mask_len);
    status = wrap_fpaLibFlowEntryAdd(switchId, FPA_FLOW_TABLE_TYPE_L3_UNICAST_E,
                                &flowEntry);
    return status;
}

FPA_STATUS
marvell_fpa_add_route_trap(uint32_t switchId, in_addr_t dstIp4,
                           uint32_t mask_len)
{
    FPA_STATUS status = FPA_OK;;
    FPA_FLOW_TABLE_ENTRY_STC flowEntry;

    FPA_TRACE_FN();
    /* Initialize Flow Entry */
    memset(&flowEntry, 0, sizeof(FPA_FLOW_TABLE_ENTRY_STC));
    /* Fill all default fields */
    status = fpaLibFlowEntryInit(switchId, FPA_FLOW_TABLE_TYPE_L3_UNICAST_E,
                                 &flowEntry);

    flowEntry.cookie = MARVELL_FPA_ROUTE_IPv4_COOCKIE(dstIp4, mask_len);
    flowEntry.data.l3_unicast.groupId = 0xffffffff;
    flowEntry.data.l3_unicast.match.etherType = 0x800;
    flowEntry.data.l3_unicast.outputPort = FPA_OUTPUT_CONTROLLER;

    flowEntry.data.l3_unicast.match.dstIp4 = dstIp4;
    flowEntry.data.l3_unicast.match.dstIp4Mask = 0xffffffff << (32 - mask_len);
    status = wrap_fpaLibFlowEntryAdd(switchId, FPA_FLOW_TABLE_TYPE_L3_UNICAST_E,
                                &flowEntry);
    return status;
}

void
ops_fpa_route_del_route(uint32_t switchId, in_addr_t dstIp4,
                      uint32_t mask_len)
{
    FPA_TRACE_FN();
    wrap_fpaLibFlowTableCookieDelete(switchId, FPA_FLOW_TABLE_TYPE_L3_UNICAST_E,
                                MARVELL_FPA_ROUTE_IPv4_COOCKIE(dstIp4, mask_len));
}
