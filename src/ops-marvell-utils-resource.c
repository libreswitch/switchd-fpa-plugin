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
 *  File: ops-marvell-utils-resource.c
 *
 *  Purpose: This file contains resource manager of numbers 1..n.
*/

#include "ops-marvell-utils-resource.h"
#include <assert.h>
#include "ofproto/ofproto-provider.h"


#define FREE_HEADER_INDEX 0
#define	RESOURCE_ALLOCATED NO_RESOURCE 

void *marvell_ops_utils_resource_init(size_t size)
{
    resource_id_t *arr = xmalloc((size + 1) * sizeof(resource_id_t));
    resource_id_t i;

    if (arr == NULL) {
        return arr;
    }

    for (i = 0; i < size; i++) {
        arr[i] = i + 1;
    }

    arr[i] = NO_RESOURCE;

    return arr;
}

resource_id_t marvell_ops_utils_resource_alloc(void *resource)
{
    resource_id_t *arr = resource;
    assert(resource != NULL);

    if (arr[FREE_HEADER_INDEX] == NO_RESOURCE) {
        return NO_RESOURCE;
    }

    resource_id_t i = arr[FREE_HEADER_INDEX];
    arr[FREE_HEADER_INDEX] = arr[i];
    arr[i] = RESOURCE_ALLOCATED;
    return i;
}

void marvell_ops_utils_resource_free(void *resource, resource_id_t i)
{
    resource_id_t *arr = resource;
    assert(resource != NULL);
    assert(arr[i] == RESOURCE_ALLOCATED);
    arr[i] = arr[FREE_HEADER_INDEX];
    arr[FREE_HEADER_INDEX] = i;
}
