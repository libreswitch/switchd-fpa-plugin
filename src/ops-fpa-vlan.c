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
#include "ops-fpa-route.h"

VLOG_DEFINE_THIS_MODULE(ops_fpa_vlan);

#define OPS_FPA_VLAN_COOKIE(pid, vid, tagged) ((tagged) ? ((uint64_t)(vid) << 32) | (pid) : (pid))
#define OPS_FPA_GID_VLAN(id) (((id) & 0x0FFF0000) >> 16)
#define OPS_FPA_GID_PORT(id) (((id) & 0x0000FFFF) >>  0)

void
ops_fpa_vlan_fetch(int sid, int pid, unsigned long *vmap)
{
    memset(vmap, 0, OPS_FPA_VMAP_BYTES);

    FPA_FLOW_TABLE_ENTRY_STC flow;
    for (int i = 1; !fpaLibFlowTableGetNext(sid, FPA_FLOW_TABLE_TYPE_VLAN_E, i, &flow); i++) {
        if (pid == flow.data.vlan.inPort) {
            int vid = flow.data.vlan.vlanId;
            bool tagged = flow.data.vlan.vlanIdMask == FPA_FLOW_VLAN_MASK_TAG;
            bitmap_set1(vmap, OPS_FPA_VIDX_INGRESS(vid, tagged));
        }
    }

    FPA_GROUP_TABLE_ENTRY_STC group;
    for (uint32_t gid = 0; !fpaLibGroupTableGetNext(sid, gid, &group); gid = group.groupIdentifier) {
        if (pid == OPS_FPA_GID_PORT(group.groupIdentifier)) {
            FPA_GROUP_BUCKET_ENTRY_STC bucket;
            int err = fpaLibGroupEntryBucketGet(sid, group.groupIdentifier, 0, &bucket);
            if (err) {
                VLOG_ERR("%s: sid=%d pid=%d fpaLibGroupEntryBucketGet: %s", __func__, sid, pid, ops_fpa_strerr(err));
                continue;
            }

            bool pop = bucket.data.l2Interface.popVlanTagAction;
            int vid = OPS_FPA_GID_VLAN(group.groupIdentifier);

            bitmap_set1(vmap, OPS_FPA_VIDX_EGRESS(vid, pop));
        }
    }
}

int
ops_fpa_vlan_add(int sid, int pid, int vidx)
{
    int vid = OPS_FPA_VIDX_VID(vidx);
    /* for egress vidx -> create group table entry */
    if (OPS_FPA_VIDX_IS_EGRESS(vidx)) {
        uint32_t dummy;
        return ops_fpa_route_add_l2_group(sid, pid, vid, OPS_FPA_VIDX_ARG(vidx), &dummy);
    }
    /* for ingress vidx -> create VLAN table entry */
    bool match_tagged = OPS_FPA_VIDX_ARG(vidx);

    FPA_FLOW_TABLE_ENTRY_STC flow;
    fpaLibFlowEntryInit(sid, FPA_FLOW_TABLE_TYPE_VLAN_E, &flow);

    flow.cookie = OPS_FPA_VLAN_COOKIE(pid, vid, match_tagged);
    flow.data.vlan.inPort = pid;
    flow.data.vlan.vlanId = vid;
    flow.data.vlan.vlanIdMask = match_tagged ? FPA_FLOW_VLAN_MASK_TAG : FPA_FLOW_VLAN_MASK_UNTAG;
    flow.data.vlan.newTagVid = match_tagged ? FPA_FLOW_VLAN_IGNORE_VAL : vid;
    flow.data.vlan.newTagPcp = FPA_FLOW_VLAN_IGNORE_VAL;

    return fpaLibFlowEntryAdd(sid, FPA_FLOW_TABLE_TYPE_VLAN_E, &flow);
}

int
ops_fpa_vlan_del(int sid, int pid, int vidx)
{
    int vid = OPS_FPA_VIDX_VID(vidx);
    /* for egress vidx - delete group table entry */
    if (OPS_FPA_VIDX_IS_EGRESS(vidx)) {
        FPA_GROUP_ENTRY_IDENTIFIER_STC ident = {
            .groupType = FPA_GROUP_L2_INTERFACE_E,
            .portNum = pid,
            .vlanId = vid
        };
        uint32_t gid;
        fpaLibGroupIdentifierBuild(&ident, &gid);
        return fpaLibGroupTableEntryDelete(sid, gid);
    }
    /* for ingress vidx -> delete VLAN table entry */
    bool match_tagged = OPS_FPA_VIDX_ARG(vidx);
    return fpaLibFlowTableCookieDelete(sid, FPA_FLOW_TABLE_TYPE_VLAN_E,
        OPS_FPA_VLAN_COOKIE(pid, vid, match_tagged)
    );
}

/* global internal vlans bitmap */
static unsigned long internal_vlans[BITMAP_N_LONGS(VLAN_BITMAP_SIZE)] = {0};

bool
ops_fpa_vlan_internal(int vid)
{
    return bitmap_is_set(internal_vlans, vid);
}

int
ops_fpa_vlan_add_internal(int sid, int pid, int vid)
{
    if (ops_fpa_vlan_add(sid, pid, OPS_FPA_VIDX_INGRESS(vid, false))) {
        VLOG_ERR("%s: can't add ingress flow: sid=%d pid=%d vid=%d", __func__, sid, pid, vid);
        return 1;
    }

    if (ops_fpa_vlan_add(sid, pid, OPS_FPA_VIDX_EGRESS(vid, false))) {
        VLOG_ERR("%s: can't add egress flow: sid=%d pid=%d vid=%d", __func__, sid, pid, vid);
        if (ops_fpa_vlan_del(sid, pid, OPS_FPA_VIDX_INGRESS(vid, false))) {
            VLOG_ERR("%s: can't cleanup ingress flow: sid=%d pid=%d vid=%d", __func__, sid, pid, vid);
        }
        return 1;
    }

    if(ops_fpa_vlan_internal(vid)) {
        VLOG_ERR("%s: vid=%d is already internal", __func__, vid);
        return 1;
    }

    bitmap_set1(internal_vlans, vid);

    return 0;
}

int
ops_fpa_vlan_del_internal(int sid, int pid, int vid)
{
    int err = 0;

    if (ops_fpa_vlan_del(sid, pid, OPS_FPA_VIDX_INGRESS(vid, false))) {
        VLOG_ERR("%s: can't remove ingress flow: sid=%d pid=%d vid=%d", __func__, sid, pid, vid);
        err = 1;
    }
    if (ops_fpa_vlan_del(sid, pid, OPS_FPA_VIDX_EGRESS(vid, false))) {
        VLOG_ERR("%s: can't remove egress flow: sid=%d pid=%d vid=%d", __func__, sid, pid, vid);
        err = 1;
    }
    if(!ops_fpa_vlan_internal(vid)) {
        VLOG_ERR("%s: vid=%d were not internal", __func__, vid);
        err = 1;
    }

    bitmap_set0(internal_vlans, vid);

    return err;
}
