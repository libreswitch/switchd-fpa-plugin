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
 *  File: ops-fpa-wrap.h
 *
 *  Purpose: This file contains wrap functions for the FPA library.
 */

#ifndef OPS_FPA_WRAP_H
#define OPS_FPA_WRAP_H 1

#include <fpa/fpaLibApis.h>
#include <fpa/fpaLibTypes.h>

FPA_STATUS wrap_fpaLibFlowEntryAdd
(
    IN   uint32_t                       switchId,
    IN   uint32_t                       flowTableNo,
    IN   FPA_FLOW_TABLE_ENTRY_STC       *flowEntryPtr
);

FPA_STATUS wrap_fpaLibFlowEntryDelete
(
    IN   uint32_t                       switchId,
    IN   uint32_t                       flowTableNo,
    IN   FPA_FLOW_TABLE_ENTRY_STC       *flowEntryPtr,
    IN   uint32_t                       matchingMode
);

FPA_STATUS wrap_fpaLibFlowTableCookieDelete
(
    IN   uint32_t                       switchId,
    IN   uint32_t                       flowTableNo,
    IN   uint64_t                       cookie
);

FPA_STATUS wrap_fpaLibGroupTableEntryAdd
(
    IN   uint32_t                   switchId,
    IN   FPA_GROUP_TABLE_ENTRY_STC  *groupEntryPtr
);

FPA_STATUS wrap_fpaLibGroupTableEntryDelete
(
    IN   uint32_t   switchId,
    IN   uint32_t   groupIdentifier
);

FPA_STATUS wrap_fpaLibGroupEntryBucketAdd
(
    IN   uint32_t                   switchId,
    IN   FPA_GROUP_BUCKET_ENTRY_STC *bucketPtr
);

FPA_STATUS wrap_fpaLibGroupEntryBucketDelete
(
    IN   uint32_t   switchId,
    IN   uint32_t   groupIdentifier,
    IN   uint32_t   bucketIndex         /* if == -1 - delete all buckets */
);

FPA_STATUS wrap_fpaLibPortPropertiesSet
(
    IN  uint32_t                   switchId,
    IN  uint32_t                   portNum,
    IN  FPA_PORT_PROPERTIES_STC    *propertiesPtr
);

#endif /* OPS_FPA_WRAP_H */
