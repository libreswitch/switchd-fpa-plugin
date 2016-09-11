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

#ifndef OPS_FPA_STG_H
#define OPS_FPA_STG_H 1

#include "ops-fpa.h"

int ops_fpa_stg_create(int *p_stg);

int ops_fpa_stg_delete(int stg);

int ops_fpa_stg_add_vlan(int stg, int vid);

int ops_fpa_stg_remove_vlan(int stg, int vid);

int ops_fpa_stg_set_port_state(char *port_name, int stg, int stp_state, bool port_stp_set);

int ops_fpa_stg_get_port_state(char *port_name, int stg, int *p_stp_state);

int ops_fpa_stg_get_default(int *p_stg);

#endif /* OPS_FPA_STG_H */
