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
 *  File: ops-fpa-tap.h
 *
 *  Purpose: This file contains OpenSwitch CPU TAP interface related
 *           application code for the FPA library.
 */

#ifndef OPS_FPA_TAP_H
#define OPS_FPA_TAP_H 1

#include <net/ethernet.h>
#include "ops-fpa.h"

struct tap_info;

struct tap_info *ops_fpa_tap_init(uint32_t switchId);
int ops_fpa_tap_deinit(uint32_t switchId);

int ops_fpa_tap_if_create(uint32_t switchId, uint32_t portNum, const char *name,
                          const struct ether_addr *mac, int* tap_fd);
int ops_fpa_tap_if_delete(uint32_t switchId, int tap_fd);

#endif /* ops-fpa-tap.h */

