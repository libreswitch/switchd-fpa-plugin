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

#include <qos-asic-provider.h>
#include <plugin-extensions.h>
#include "ops-fpa.h"

VLOG_DEFINE_THIS_MODULE(ops_fpa_qos);

static int
ops_fpa_qos_set_port_qos_cfg(struct ofproto *proto, void *aux, const struct qos_port_settings *settings)
{
    FPA_TRACE_FN();
    return 0;
}

static int
ops_fpa_qos_set_cos_map(struct ofproto *proto, void *aux, const struct cos_map_settings *settings)
{
    FPA_TRACE_FN();
    return 0;
}

static int
ops_fpa_qos_set_dscp_map(struct ofproto *proto, void *aux, const struct dscp_map_settings *settings)
{
    FPA_TRACE_FN();
    return 0;
}

static int
ops_fpa_qos_apply_qos_profile(struct ofproto *proto, void *aux, const struct schedule_profile_settings *s_settings, const struct queue_profile_settings *q_settings)
{
    FPA_TRACE_FN();
    return 0;
}

static struct qos_asic_plugin_interface ops_fpa_qos_interface = {
    .set_port_qos_cfg  = ops_fpa_qos_set_port_qos_cfg,
    .set_cos_map       = ops_fpa_qos_set_cos_map,
    .set_dscp_map      = ops_fpa_qos_set_dscp_map,
    .apply_qos_profile = ops_fpa_qos_apply_qos_profile
};

struct plugin_extension_interface ops_fpa_qos_extension = {
    QOS_ASIC_PLUGIN_INTERFACE_NAME,
    QOS_ASIC_PLUGIN_INTERFACE_MAJOR,
    QOS_ASIC_PLUGIN_INTERFACE_MINOR,
    &ops_fpa_qos_interface
};
