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

#include "ops-fpa-vlan.h"

VLOG_DEFINE_THIS_MODULE(ops_fpa_vlan);

#define FPA_GROUP_ENTRY_PORT_MASK           0x0000FFFF
#define FPA_GROUP_ENTRY_PORT_GET_MAC(id)    (id & FPA_GROUP_ENTRY_PORT_MASK)

#define FPA_FLOW_VLAN_MASK_UNTAG 0x1000
#define FPA_FLOW_VLAN_MASK_TAG   0x1FFF

int
ops_fpa_vlan_add(uint32_t switch_id, int pid, int vid, bool tag_in, bool tag_out)
{
    uint32_t gid;
    FPA_STATUS err;

    FPA_FLOW_TABLE_ENTRY_STC flow;
    fpaLibFlowEntryInit(switch_id, FPA_FLOW_TABLE_TYPE_VLAN_E, &flow);
    flow.cookie = ops_fpa_vlan_cookie(pid, vid, tag_in);
    flow.data.vlan.inPort = pid;
    flow.data.vlan.vlanId = vid;
    flow.data.vlan.vlanIdMask = tag_in ? FPA_FLOW_VLAN_MASK_TAG : FPA_FLOW_VLAN_MASK_UNTAG;
    flow.data.vlan.newTagVid = tag_in ? -1 : vid;
    flow.data.vlan.newTagPcp = -1;
    err = wrap_fpaLibFlowEntryAdd(switch_id, FPA_FLOW_TABLE_TYPE_VLAN_E, &flow);
    if (err) {
        VLOG_ERR("fpaLibFlowEntryAdd: Status: %s\n"
        		"vlan.inPort: %d, vlan.vlanId: %d",
        		ops_fpa_strerr(err),
        		flow.data.vlan.inPort, flow.data.vlan.vlanId);
        return -1;
    }

    FPA_GROUP_ENTRY_IDENTIFIER_STC ident = {
        .groupType = FPA_GROUP_L2_INTERFACE_E,
        .portNum = pid,
        .vlanId = vid
    };
    err = fpaLibGroupIdentifierBuild(&ident, &gid);
    if (err) {
        VLOG_ERR("fpaLibGroupIdentifierBuild: Status: %s", ops_fpa_strerr(err));
        return -1;
    }

    FPA_GROUP_TABLE_ENTRY_STC group = {
        .groupIdentifier = gid,
        .groupTypeSemantics = FPA_GROUP_INDIRECT
    };
    err = wrap_fpaLibGroupTableEntryAdd(switch_id, &group);
    if (err) {
        VLOG_ERR("fpaLibGroupTableEntryAdd: Status: %s", ops_fpa_strerr(err));
        return -1;
    }

    FPA_GROUP_BUCKET_ENTRY_STC bucket = {
        .groupIdentifier = gid,
        .index = 0,
        .type = FPA_GROUP_BUCKET_L2_INTERFACE_E,
        .data.l2Interface = {
            .outputPort = pid,
            .popVlanTagAction = !tag_out
        }
    };
    err = wrap_fpaLibGroupEntryBucketAdd(switch_id, &bucket);
    if (err) {
        VLOG_ERR("fpaLibGroupEntryBucketAdd: Status: %s", ops_fpa_strerr(err));
        return -1;
    }

    return 0;
}

int
ops_fpa_vlan_rm(uint32_t switch_id, int pid, int vid, bool tag_in)
{
    FPA_STATUS err = wrap_fpaLibFlowTableCookieDelete(switch_id,
            FPA_FLOW_TABLE_TYPE_VLAN_E, ops_fpa_vlan_cookie(pid, vid, tag_in));
    if (err) {
        VLOG_ERR("fpaLibFlowTableCookieDelete: Status: %s",
                 ops_fpa_strerr(err));
    }

    FPA_GROUP_ENTRY_IDENTIFIER_STC ident = {
        .groupType = FPA_GROUP_L2_INTERFACE_E,
        .portNum = pid,
        .vlanId = vid
    };
    uint32_t gid;
    err = fpaLibGroupIdentifierBuild(&ident, &gid);
    if (err) {
        VLOG_ERR("fpaLibGroupIdentifierBuild: Status: %s", ops_fpa_strerr(err));
        return -1;
    }

    err = wrap_fpaLibGroupTableEntryDelete(switch_id, gid);
    if (err) {
        VLOG_ERR("fpaLibGroupTableEntryDelete: Status: %s", ops_fpa_strerr(err));
        return -1;
    }

    return 0;
}

int
ops_fpa_vlan_mod(bool add, uint32_t switch_id, int pid, int vid,
                 enum port_vlan_mode mode)
{
    if(pid != FPA_INVALID_INTF_ID) {
        bool tag_in  = (mode == PORT_VLAN_TRUNK);
        bool tag_out = (mode == PORT_VLAN_TRUNK)
                || (mode == PORT_VLAN_NATIVE_TAGGED);

        return add ? ops_fpa_vlan_add(switch_id, pid, vid, tag_in, tag_out)
                : ops_fpa_vlan_rm(switch_id, pid, vid, tag_in);
    }
    return 0;
}
