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

#include <bufmon-provider.h>
#include "ops-fpa.h"

VLOG_DEFINE_THIS_MODULE(ops_fpa_bufmon);

static int
ops_fpa_bufmon_init(void)
{
    FPA_TRACE_FN();
    return 0;
}

static void
ops_fpa_bufmon_system_config(const bufmon_system_config_t *cfg)
{
    FPA_TRACE_FN();
}

static void
ops_fpa_bufmon_counter_config(bufmon_counter_info_t *counter)
{
    FPA_TRACE_FN();
}

static void
ops_fpa_bufmon_counter_stats_get(bufmon_counter_info_t *counter_list, int num_counter)
{
    FPA_TRACE_FN();
}

static void
ops_fpa_bufmon_trigger_register(bool enable)
{
    FPA_TRACE_FN();
}

static struct bufmon_class ops_fpa_bufmon_class = {
    .init                     = ops_fpa_bufmon_init,
    .bufmon_system_config     = ops_fpa_bufmon_system_config,
    .bufmon_counter_config    = ops_fpa_bufmon_counter_config,
    .bufmon_counter_stats_get = ops_fpa_bufmon_counter_stats_get,
    .bufmon_trigger_register  = ops_fpa_bufmon_trigger_register
};

void
bufmon_register(void)
{
    bufmon_class_register(&ops_fpa_bufmon_class);
}
