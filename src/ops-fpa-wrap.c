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
 *  File: ops-fpa-wrap.c
 *
 *  Purpose: This file contains wrap functions for the FPA library.
 */

#include <openvswitch/vlog.h>

#include "ops-fpa-wrap.h"

static struct ovs_mutex ops_fpa_wrap_mutex = OVS_MUTEX_INITIALIZER;

VLOG_DEFINE_THIS_MODULE(ops_fpa_wrap);


FPA_STATUS wrap_fpaLibFlowEntryAdd
(
    IN   uint32_t                       switchId,
    IN   uint32_t                       flowTableNo,
    IN   FPA_FLOW_TABLE_ENTRY_STC       *flowEntryPtr
)
{
    FPA_STATUS err;

    err = fpaLibFlowEntryAdd(switchId, flowTableNo, flowEntryPtr);

    fpaLibFlowTableDump(switchId, flowTableNo);

    return err;
}

FPA_STATUS wrap_fpaLibFlowEntryDelete
(
    IN   uint32_t                       switchId,
    IN   uint32_t                       flowTableNo,
    IN   FPA_FLOW_TABLE_ENTRY_STC       *flowEntryPtr,
    IN   uint32_t                       matchingMode
)
{
    FPA_STATUS err;

    err = fpaLibFlowEntryDelete(switchId, flowTableNo, flowEntryPtr, matchingMode);

    fpaLibFlowTableDump(switchId, flowTableNo);

    return err;
}

FPA_STATUS wrap_fpaLibFlowTableCookieDelete
(
    IN   uint32_t                       switchId,
    IN   uint32_t                       flowTableNo,
    IN   uint64_t                       cookie
)
{
    FPA_STATUS err;

    err = fpaLibFlowTableCookieDelete(switchId, flowTableNo, cookie);

    fpaLibFlowTableDump(switchId, flowTableNo);

    return err;
}

FPA_STATUS wrap_fpaLibGroupTableEntryAdd
(
    IN   uint32_t                   switchId,
    IN   FPA_GROUP_TABLE_ENTRY_STC  *groupEntryPtr
)
{
    FPA_STATUS err;

    err = fpaLibGroupTableEntryAdd(switchId, groupEntryPtr);


    return err;
}

FPA_STATUS wrap_fpaLibGroupTableEntryDelete
(
    IN   uint32_t   switchId,
    IN   uint32_t   groupIdentifier
)
{
    FPA_STATUS err;

    err = fpaLibGroupTableEntryDelete(switchId, groupIdentifier);


    return err;
}

FPA_STATUS wrap_fpaLibGroupEntryBucketAdd
(
    IN   uint32_t                   switchId,
    IN   FPA_GROUP_BUCKET_ENTRY_STC *bucketPtr
)
{
    FPA_STATUS err;

    err = fpaLibGroupEntryBucketAdd(switchId, bucketPtr);


    return err;
}

FPA_STATUS wrap_fpaLibGroupEntryBucketDelete
(
    IN   uint32_t   switchId,
    IN   uint32_t   groupIdentifier,
    IN   uint32_t   bucketIndex         /* if == -1 - delete all buckets */
)
{
    FPA_STATUS err;

    err = fpaLibGroupEntryBucketDelete(switchId, groupIdentifier, bucketIndex);


    return err;
}

FPA_STATUS wrap_fpaLibPortPropertiesSet
(
    IN  uint32_t                   switchId,
    IN  uint32_t                   portNum,
    IN  FPA_PORT_PROPERTIES_STC    *propertiesPtr
)
{
    FPA_STATUS err;

    ovs_mutex_lock(&ops_fpa_wrap_mutex);
    err = fpaLibPortPropertiesSet(switchId, portNum, propertiesPtr);
    ovs_mutex_unlock(&ops_fpa_wrap_mutex);

    return err;
}
