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

#include "ops-fpa.h"

VLOG_DEFINE_THIS_MODULE(ops_fpa_vlan);

#define FPA_GROUP_ENTRY_PORT_MASK           0x0000FFFF
#define FPA_GROUP_ENTRY_PORT_GET_MAC(id)    (id & FPA_GROUP_ENTRY_PORT_MASK)

#define FPA_FLOW_VLAN_MASK_UNTAG 0x1000
#define FPA_FLOW_VLAN_MASK_TAG   0x1FFF

inline uint64_t
ops_fpa_vlan_cookie(int pid, int vid, bool tag_in)
{
    return (pid << 13) | (tag_in ? 1 << 12 : 0) | (vid);
}

int
ops_fpa_vlan_add(int pid, int vid, bool tag_in, bool tag_out)
{
    uint32_t gid;
    int err;

    FPA_FLOW_TABLE_ENTRY_STC flow;
    fpaLibFlowEntryInit(0, FPA_FLOW_TABLE_TYPE_VLAN_E, &flow);
    flow.cookie = ops_fpa_vlan_cookie(pid, vid, tag_in);
    flow.data.vlan.inPort = pid - 1;
    flow.data.vlan.vlanId = vid;
    flow.data.vlan.vlanIdMask = tag_in ? FPA_FLOW_VLAN_MASK_TAG : FPA_FLOW_VLAN_MASK_UNTAG;
    flow.data.vlan.newTagVid = tag_in ? -1 : vid;
    flow.data.vlan.newTagPcp = -1;
    err = fpaLibFlowEntryAdd(0, FPA_FLOW_TABLE_TYPE_VLAN_E, &flow);
    if (err) {
        VLOG_ERR("fpaLibFlowEntryAdd: %s", ops_fpa_strerr(err));
        return -1;
    }

    FPA_GROUP_ENTRY_IDENTIFIER_STC ident = {
        .groupType = FPA_GROUP_L2_INTERFACE_E,
        .portNum = pid - 1,
        .vlanId = vid
    };
    err = fpaLibGroupIdentifierBuild(&ident, &gid);
    if (err) {
        VLOG_ERR("fpaLibGroupIdentifierBuild: %s", ops_fpa_strerr(err));
        return -1;
    }

    FPA_GROUP_TABLE_ENTRY_STC group = {
        .groupIdentifier = gid,
        .groupTypeSemantics = FPA_GROUP_INDIRECT
    };
    err = fpaLibGroupTableEntryAdd(0, &group);
    if (err) {
        VLOG_ERR("fpaLibGroupTableEntryAdd: %s", ops_fpa_strerr(err));
        return -1;
    }

    FPA_GROUP_BUCKET_ENTRY_STC bucket = {
        .groupIdentifier = gid,
        .index = 0,
        .type = FPA_GROUP_BUCKET_L2_INTERFACE_E,
        .data.l2Interface = {
            .outputPort = FPA_GROUP_ENTRY_PORT_GET_MAC(gid),
            .popVlanTagAction = !tag_out
        }
    };
    err = fpaLibGroupEntryBucketAdd(0, &bucket);
    if (err) {
        VLOG_ERR("fpaLibGroupEntryBucketAdd: %s", ops_fpa_strerr(err));
        return -1;
    }

    return 0;
}


int
ops_fpa_vlan_rm(int pid, int vid, bool tag_in)
{
    int err = fpaLibFlowTableCookieDelete(0, FPA_FLOW_TABLE_TYPE_VLAN_E, ops_fpa_vlan_cookie(pid, vid, tag_in));
    if (err) {
        VLOG_ERR("fpaLibFlowTableCookieDelete: %s :(", ops_fpa_strerr(err));
    }

    FPA_GROUP_ENTRY_IDENTIFIER_STC ident = {
        .groupType = FPA_GROUP_L2_INTERFACE_E,
        .portNum = pid - 1,
        .vlanId = vid
    };
    uint32_t gid;
    err = fpaLibGroupIdentifierBuild(&ident, &gid);
    if (err) {
        VLOG_ERR("fpaLibGroupIdentifierBuild: %s :(", ops_fpa_strerr(err));
        return -1;
    }

    err = fpaLibGroupTableEntryDelete(0, gid);
    if (err) {
        VLOG_ERR("fpaLibGroupTableEntryDelete: %s :(", ops_fpa_strerr(err));
        return -1;
    }

    return 0;
}

int
ops_fpa_vlan_mod(bool add, int pid, int vid, enum port_vlan_mode mode)
{
    bool tag_in  = (mode == PORT_VLAN_TRUNK);
    bool tag_out = (mode == PORT_VLAN_TRUNK) || (mode == PORT_VLAN_NATIVE_TAGGED);

    return add ? ops_fpa_vlan_add(pid, vid, tag_in, tag_out) : ops_fpa_vlan_rm(pid, vid, tag_in);
}
