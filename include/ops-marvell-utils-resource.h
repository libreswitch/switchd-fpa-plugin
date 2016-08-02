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

#ifndef _marvell_ops_utils_resource_
#define _marvell_ops_utils_resource_

#include <stdlib.h>

#define	NO_RESOURCE 0

typedef int resource_id_t;

void *marvell_ops_utils_resource_init(size_t size);

resource_id_t marvell_ops_utils_resource_alloc(void *resource);

void marvell_ops_utils_resource_free(void * resource, resource_id_t i);


#endif
