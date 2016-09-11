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

#include <asic-plugin.h>
#include <mac-learning-plugin.h>
#include <plugin-extensions.h>
#include "ops-fpa.h"

VLOG_DEFINE_THIS_MODULE(ops_fpa_stg);

int
ops_fpa_stg_create(int *p_stg)
{
    FPA_TRACE_FN();
    return 0;
}

int
ops_fpa_stg_delete(int stg)
{
    FPA_TRACE_FN();
    return 0;
}

int
ops_fpa_stg_add_vlan(int stg, int vid)
{
    VLOG_INFO("%s: stg=%d vid=%d", __func__, stg, vid);
    return 0;
}

int
ops_fpa_stg_remove_vlan(int stg, int vid)
{
    VLOG_INFO("%s: stg=%d vid=%d", __func__, stg, vid);
    return 0;
}

int
ops_fpa_stg_set_port_state(char *port_name, int stg, int stp_state, bool port_stp_set)
{
    VLOG_INFO("%s: port_name=%s stg=%d stp_state=%d port_stp_set=%s",
        __func__, port_name, stg, stp_state, port_stp_set ? "true" : "false"
    );
    return 0;
}

int
ops_fpa_stg_get_port_state(char *port_name, int stg, int *p_stp_state)
{
    FPA_TRACE_FN();
    return 0;
}

int
ops_fpa_stg_get_default(int *p_stg)
{
    FPA_TRACE_FN();
    return 0;
}
