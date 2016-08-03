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
*    File: ops-marvell-utils-resource.c
*
*    Purpose: Resource allocation for L3 FPA functions.
*/

#include "ops-marvell-utils-resource.h"
#include <assert.h>
#include "ofproto/ofproto-provider.h"

#define OPS_FPA_RESOURCE_FREE_HEADER_ 0
#define	OPS_FPA_RESOURCE_RESOURCE_ OPS_FPA_NO_RESOURCE 

void *ops_marvell_utils_resource_init(size_t size)
{
    ops_fpa_resource_id_t *arr = xmalloc((size + 1) * sizeof(ops_fpa_resource_id_t));
    ops_fpa_resource_id_t i;

    if (arr == NULL) {
        return arr;
    }

    for (i = 0; i < size; i++) {
        arr[i] = i + 1;
    }

    arr[i] = OPS_FPA_NO_RESOURCE;

    return arr;
}

ops_fpa_resource_id_t ops_marvell_utils_resource_alloc(void *resource)
{
    ops_fpa_resource_id_t *arr = resource;
    assert(resource != NULL);

    if (arr[OPS_FPA_RESOURCE_FREE_HEADER_] == OPS_FPA_NO_RESOURCE) {
        return OPS_FPA_NO_RESOURCE;
    }

    ops_fpa_resource_id_t i = arr[OPS_FPA_RESOURCE_FREE_HEADER_];
    arr[OPS_FPA_RESOURCE_FREE_HEADER_] = arr[i];
    arr[i] = OPS_FPA_RESOURCE_RESOURCE_;
    return i;
}

void ops_marvell_utils_resource_free(void *resource, ops_fpa_resource_id_t i)
{
    ops_fpa_resource_id_t *arr = resource;
    assert(resource != NULL);
    assert(arr[i] == OPS_FPA_RESOURCE_RESOURCE_);
    arr[i] = arr[OPS_FPA_RESOURCE_FREE_HEADER_];
    arr[OPS_FPA_RESOURCE_FREE_HEADER_] = i;
}
