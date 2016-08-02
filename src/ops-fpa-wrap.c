/*
 *  Copyright (C) 2016, Marvell International Ltd. ALL RIGHTS RESERVED.
 *
 *    Licensed under the Apache License, Version 2.0 (the "License"); you may
 *    not use this file except in compliance with the License. You may obtain
 *    a copy of the License at http://www.apache.org/licenses/LICENSE-2.0
 *
 *    THIS CODE IS PROVIDED ON AN  *AS IS* BASIS, WITHOUT WARRANTIES OR
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

VLOG_DEFINE_THIS_MODULE(ops_fpa_wrap);

/* static struct vlog_rate_limit rl = VLOG_RATE_LIMIT_INIT(5, 20); */

extern FPA_STATUS fpaGroupTableDump(void);

FPA_STATUS wrap_fpaLibFlowEntryAdd
(
    IN   uint32_t                       switchId,
    IN   uint32_t                       flowTableNo,
    IN   FPA_FLOW_TABLE_ENTRY_STC       *flowEntryPtr
)
{
    FPA_STATUS status;

    status = fpaLibFlowEntryAdd(switchId, flowTableNo, flowEntryPtr);

    fpaLibFlowTableDump(switchId, flowTableNo);
    
    return status;
}

FPA_STATUS wrap_fpaLibFlowEntryDelete
(
    IN   uint32_t                       switchId,
    IN   uint32_t                       flowTableNo,
    IN   FPA_FLOW_TABLE_ENTRY_STC       *flowEntryPtr,
    IN   uint32_t                       matchingMode
)
{
    FPA_STATUS status;
    
    status = fpaLibFlowEntryDelete(switchId, flowTableNo, flowEntryPtr, matchingMode);

    fpaLibFlowTableDump(switchId, flowTableNo);
    
    return status;
}

FPA_STATUS wrap_fpaLibFlowTableCookieDelete
(
    IN   uint32_t                       switchId,
    IN   uint32_t                       flowTableNo,
    IN   uint64_t                       cookie
)
{
    FPA_STATUS status;
    
    status = fpaLibFlowTableCookieDelete(switchId, flowTableNo, cookie);

    fpaLibFlowTableDump(switchId, flowTableNo);
    
    return status;
}

FPA_STATUS wrap_fpaLibGroupTableEntryAdd
(
    IN   uint32_t                   switchId,
    IN   FPA_GROUP_TABLE_ENTRY_STC  *groupEntryPtr
)
{
    FPA_STATUS status;
    
    status = fpaLibGroupTableEntryAdd(switchId, groupEntryPtr);
    
    /* fpaGroupTableDump(); */
    
    return status;
}

FPA_STATUS wrap_fpaLibGroupTableEntryDelete
(
    IN   uint32_t   switchId,
    IN   uint32_t   groupIdentifier
)
{
    FPA_STATUS status;
    
    status = fpaLibGroupTableEntryDelete(switchId, groupIdentifier);
    
    fpaGroupTableDump();
    
    return status;
}

FPA_STATUS wrap_fpaLibGroupEntryBucketAdd
(
    IN   uint32_t                   switchId,
    IN   FPA_GROUP_BUCKET_ENTRY_STC *bucketPtr
)
{
    FPA_STATUS status;
    
    status = fpaLibGroupEntryBucketAdd(switchId, bucketPtr);
    
    fpaGroupTableDump();
    
    return status;
}

FPA_STATUS wrap_fpaLibGroupEntryBucketDelete
(
    IN   uint32_t   switchId,
    IN   uint32_t   groupIdentifier,
    IN   uint32_t   bucketIndex         /* if == -1 - delete all buckets */
)
{
    FPA_STATUS status;
    
    status = fpaLibGroupEntryBucketDelete(switchId, groupIdentifier, bucketIndex);
    
    fpaGroupTableDump();
    
    return status;
}

