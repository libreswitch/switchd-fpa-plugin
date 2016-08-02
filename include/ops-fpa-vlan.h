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
 *  File: ops-fpa-vlan.h
 *
 *  Purpose: This file contains OpenSwitch CPU VLAN interface related
 *           application code for the FPA library.
 */

#ifndef OPS_FPA_VALN_H
#define OPS_FPA_VALN_H 1

#include "ops-fpa.h"

/*for gcc -O0*/
inline uint64_t ops_fpa_vlan_cookie(int pid, int vid, bool tag_in) __attribute__((always_inline));

#define OPS_FPA_VLAN_COOKIE_IS_TAGGED_BIT      (1ULL << 47)

inline uint64_t
ops_fpa_vlan_cookie(int pid, int vid, bool tag_in)
{
    if(tag_in) {
        return OPS_FPA_VLAN_COOKIE_IS_TAGGED_BIT
                | ((uint64_t)vid << 32) | (pid);
    } else {
        return pid;
    }

}

#endif /* ops-fpa-vlan.h */

