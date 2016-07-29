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

#include <copp-asic-provider.h>
#include <plugin-extensions.h>
#include "ops-fpa.h"

VLOG_DEFINE_THIS_MODULE(ops_fpa_copp);

static int
ops_fpa_copp_stats_get(const unsigned int hw_asic_id, const enum copp_protocol_class class, struct copp_protocol_stats *const stats)
{
    FPA_TRACE_FN();
    return 0;
}

static int
ops_fpa_copp_hw_status_get(const unsigned int hw_asic_id, const enum copp_protocol_class class, struct copp_hw_status *const hw_status)
{
    FPA_TRACE_FN();
    return 0;
}

static struct copp_asic_plugin_interface ops_fpa_copp_interface = {
    .copp_stats_get     = ops_fpa_copp_stats_get,
    .copp_hw_status_get = ops_fpa_copp_hw_status_get
};

struct plugin_extension_interface ops_fpa_copp_extension = {
    COPP_ASIC_PLUGIN_INTERFACE_NAME,
    COPP_ASIC_PLUGIN_INTERFACE_MAJOR,
    COPP_ASIC_PLUGIN_INTERFACE_MINOR,
    &ops_fpa_copp_interface
};
