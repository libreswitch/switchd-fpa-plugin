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
 *  File: ops-fpa-util.h
 *
 *  Purpose: This file provides public definitions for OpenSwitch utilities
 *           related application code for the FPA library.
 */

#ifndef OPS_FPA_UTIL_H
#define OPS_FPA_UTIL_H 1

#include <net/ethernet.h>
#include <unixctl.h>

int ops_fpa_system(const char * format, ...);
int ops_fpa_net_if_setup(const char *name, const struct ether_addr *mac);

void ops_fpa_start_simulation_log(struct unixctl_conn *conn,
                                  int argc OVS_UNUSED,
                                  const char *argv[], void *aux OVS_UNUSED);
void ops_fpa_stop_simulation_log(struct unixctl_conn *conn, int argc OVS_UNUSED,
                                 const char *argv[], void *aux OVS_UNUSED);

#endif /* ops-fpa-util.h */

