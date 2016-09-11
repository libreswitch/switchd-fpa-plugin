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

#include <plugin-extensions.h>
#include "asic-plugin.h"
#include "ops-fpa.h"
#include "ops-fpa-stg.h"
#include "ops-fpa-mac-learning.h"

VLOG_DEFINE_THIS_MODULE(ops_fpa_plugins);

extern struct plugin_extension_interface ops_fpa_copp_extension;
extern struct plugin_extension_interface ops_fpa_qos_extension;

static struct asic_plugin_interface ops_fpa_asic = {
    .create_stg            = ops_fpa_stg_create,
    .delete_stg            = ops_fpa_stg_delete,
    .add_stg_vlan          = ops_fpa_stg_add_vlan,
    .remove_stg_vlan       = ops_fpa_stg_remove_vlan,
    .set_stg_port_state    = ops_fpa_stg_set_port_state,
    .get_stg_port_state    = ops_fpa_stg_get_port_state,
    .get_stg_default       = ops_fpa_stg_get_default,
    .get_mac_learning_hmap = ops_fpa_ml_hmap_get,
    .l2_addr_flush         = NULL
};

static struct plugin_extension_interface ops_fpa_extension = {
    ASIC_PLUGIN_INTERFACE_NAME,
    ASIC_PLUGIN_INTERFACE_MAJOR,
    ASIC_PLUGIN_INTERFACE_MINOR,
    &ops_fpa_asic
};

void
init(void)
{
    FPA_TRACE_FN();
    register_plugin_extension(&ops_fpa_extension);
    /*register_plugin_extension(&ops_fpa_copp_extension);
    register_plugin_extension(&ops_fpa_qos_extension);*/
}

void
run(void)
{
}

void
wait(void)
{
}

void
destroy(void)
{
    FPA_TRACE_FN();
}
