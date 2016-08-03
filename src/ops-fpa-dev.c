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
 *
 *  File: ops-fpa-dev.c
 *
 *  Purpose: This file contains FPA device related
 *           application code for the FPA library.
 */

#include <errno.h>
#include <ovs/util.h>
#include <unixctl.h>

#include "ops-fpa-dev.h"
#include "ops-fpa-mac-learning.h"
#include "ops-fpa-tap.h"
#include "ops-fpa-util.h"

VLOG_DEFINE_THIS_MODULE(ops_fpa_dev);

static struct ovs_mutex fpa_dev_mutex = OVS_MUTEX_INITIALIZER;

static struct fpa_dev *dev = NULL;

static char *opsFpaFlowTablesName[FPA_FLOW_TABLE_MAX] = {"CONTROL_PKT", "VLAN", "TERMINATION", "PCL0", "PCL1", "PCL2", "L2_BRIDGING", "L3_UNICAST", "EPCL"};

extern FPA_STATUS fpaGroupTableDump(void);

static void ops_fpa_dev_unixctl_init(void);


void
ops_fpa_dev_mutex_lock(void)
{
    ovs_mutex_lock(&fpa_dev_mutex);
}

void
ops_fpa_dev_mutex_unlock(void)
{
    ovs_mutex_unlock(&fpa_dev_mutex);
}

/* Returns device instance by device ID value.
 *
 * Increments device's reference counter on success. The caller
 * must free the returned fpa_dev with fpa_dev_free(). */
struct fpa_dev *
ops_fpa_dev_by_id(uint32_t switchId)
{
    /* TODO: Overwrite this function then will added support a lot of switches */
    if (dev && dev->switchId == switchId) {
        return dev;
    }
    OVS_NOT_REACHED();
}

int 
ops_fpa_dev_init(uint32_t switchId, struct fpa_dev **fdev)
{
    int err = 0;

    VLOG_INFO("FPA device (%d) init", switchId);

    ops_fpa_dev_mutex_lock();

    /* TODO: check FPA device have only one instance */
    if (dev) {
        VLOG_ERR("FPA device (%d) already exist", dev->switchId);
        dev->ref_cnt++;
        err = 0;/*TODO: ? EEXIST; */
        goto error;
    }

    dev = xzalloc(sizeof * dev);
    memset(dev, 0, sizeof * dev);
    dev->switchId = switchId;
    dev->ref_cnt = 1;

    /* Initialize TAP interface. */
    dev->tap_if_info = ops_fpa_tap_init(switchId);
    if (dev->tap_if_info)
        VLOG_INFO("FPA device (%d) initialize was successful", dev->switchId);

    err = ops_fpa_mac_learning_create(dev, &dev->ml);
    if (err) {
        VLOG_ERR("Unable to create mac learning feature");
        err = EAGAIN;
        goto error;
    }

    /* Initialize unix ctl functions */
    ops_fpa_dev_unixctl_init();

error:
    *fdev = dev ? dev : NULL;
    ops_fpa_dev_mutex_unlock();

    return err;
}

void
ops_fpa_dev_deinit(uint32_t switchId)
{
    struct fpa_dev *fdev;

    VLOG_INFO("FPA device (%d) deinit", switchId);

    ops_fpa_dev_mutex_lock();

    fdev = ops_fpa_dev_by_id(switchId);

    if (!fdev) {
        VLOG_ERR("FPA device (%d) not exist", switchId);
        ops_fpa_dev_mutex_unlock();

        return;
    }

    fdev->ref_cnt--;
    if (!fdev->ref_cnt)
    {
        ops_fpa_mac_learning_unref(fdev->ml);

        ops_fpa_tap_deinit(switchId);

        /* TODO: check FPA device have only one instance */
        free(dev);
        dev = NULL;

        VLOG_INFO("FPA device (%d) free was successful", switchId);
    }

    ops_fpa_dev_mutex_unlock();
}

/* Return TAP info by switch ID */
struct tap_info *get_tap_info_by_switch_id(uint32_t switchId)
{
    struct fpa_dev *fdev;

    fdev = ops_fpa_dev_by_id(switchId);
    if (fdev) {
        return fdev->tap_if_info;
    }

    OVS_NOT_REACHED();
}

/************************************************************************************/
/* FLOW TABLE SHOW */
/************************************************************************************/
/* TODO: remove code to another place, maybe create new file as ops-fpa-ft.c */

void ops_fpa_dev_unixctl_print_mac_address(struct ds *d_str, FPA_MAC_ADDRESS_STC mac)
{
    ds_put_format(d_str, ETH_ADDR_FMT, FPA_ETH_ADDR_ARGS(mac));
}

void ops_fpa_dev_unixctl_print_table_title(struct ds *d_str, uint32_t flowTableNo)
{
    ovs_assert(d_str);

    switch (flowTableNo) {
    case FPA_FLOW_TABLE_TYPE_CONTROL_PKT_E:
        ds_put_cstr(d_str, "---------------------------------------------------------------------------------------------------------------------------------------------------------\n");
        ds_put_cstr(d_str, "|      cookie     |   prio  |    time    | hard |   flags    |type| port/m | vlan/m|     destMac/m       | ethT/m |ipPrt/m |icmp/m|L4Prt/m|ctrl|clr|goto|\n");
        ds_put_cstr(d_str, "---------------------------------------------------------------------------------------------------------------------------------------------------------\n");
        break;
    case FPA_FLOW_TABLE_TYPE_VLAN_E:
        ds_put_cstr(d_str, "---------------------------------------------------------------------------------------------------------\n");
        ds_put_cstr(d_str, "|     cookie      | prio |    time    | hard |   flags    | port | vlan |  mask  | pvid  |  pcp  | goto |\n");
        ds_put_cstr(d_str, "---------------------------------------------------------------------------------------------------------\n");
        break;
    case FPA_FLOW_TABLE_TYPE_TERMINATION_E:
        ds_put_cstr(d_str, "-------------------------------------------------------------------------------------------------------------------------------------------------\n");
        ds_put_cstr(d_str, "|      cookie     | prio |    time    | hard |   flags    | port/m | vlan/m |   eth/m  |   mac address    |    mac mask      | metadat/m | goto |\n");
        ds_put_cstr(d_str, "-------------------------------------------------------------------------------------------------------------------------------------------------\n");
        break;
    case FPA_FLOW_TABLE_TYPE_L2_BRIDGING_E:
        ds_put_cstr(d_str, "-----------------------------------------------------------------------------------------------------------------------\n");
        ds_put_cstr(d_str, "|      cookie     | prio |    time    | hard |   flags    | vlan/m | mac address / mask  | cntrl| grp(HEX) |clr| goto |\n");
        ds_put_cstr(d_str, "-----------------------------------------------------------------------------------------------------------------------\n");
        break;
    case FPA_FLOW_TABLE_TYPE_PCL0_E:
    case FPA_FLOW_TABLE_TYPE_PCL1_E:
    case FPA_FLOW_TABLE_TYPE_PCL2_E:
        ds_put_cstr(d_str, "---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------\n");
        ds_put_cstr(d_str, "|     cookie      |prio |  time   |hard|  flags    |port/m|  eth/m |vlan/m |src mac addr /mask  |dst mac addr /mask  |up/m|    src ip /m    |    dst ip /m    ||actn|ctrl| grp(HEX) |metr|qu|up|dscp|clr|hwIdx |goto|\n");
        ds_put_cstr(d_str, "---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------\n");
        break;
    case FPA_FLOW_TABLE_TYPE_EPCL_E:
        ds_put_cstr(d_str, "----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------\n");
        ds_put_cstr(d_str, "|     cookie      |prio |  time   |hard|  flags    |port/m|  eth/m |vlan/m |src mac addr /mask  |dst mac addr /mask  |up/m|    src ip /m    |    dst ip /m    ||actn|metr| vlan |up|dscp|clr|hwIdx |\n");
        ds_put_cstr(d_str, "----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------\n");
        break;
    case FPA_FLOW_TABLE_TYPE_L3_UNICAST_E:
        ds_put_cstr(d_str, "-------------------------------------------------------------------------------------------------------------------\n");
        ds_put_cstr(d_str, "|     cookie      | prio |    time    | hard |   flags    | vrId |      ipAddr/m     | cntrl| grp(HEX) |clr| goto |\n");
        ds_put_cstr(d_str, "-------------------------------------------------------------------------------------------------------------------\n");
        break;
    default:
        ds_put_format(d_str, "table type %d: to output information not implemented yet\n", flowTableNo);
    }
}

void ops_fpa_dev_unixctl_print_table_entry(struct ds *d_str, FPA_FLOW_TABLE_ENTRY_STC *flowEntry)
{
    ovs_assert(d_str);
    ovs_assert(flowEntry);

    switch (flowEntry->entryType) {
    case FPA_FLOW_TABLE_TYPE_CONTROL_PKT_E:
        ds_put_format(d_str, "|%16llx |%8x | %10d | %4d | 0x%08x | %2d | %4d/%d | %4d/%d |",
            (unsigned long long int)flowEntry->cookie,
            flowEntry->priority,
            flowEntry->timeoutIdleTime,
            flowEntry->timeoutHardTime,
            flowEntry->flowModFlags,
            flowEntry->entryType,
            flowEntry->data.control_pkt.match.inPort,
            (flowEntry->data.control_pkt.match.inPortMask==0)? 0:1,
            flowEntry->data.control_pkt.match.vlanId,
            (flowEntry->data.control_pkt.match.vlanIdMask==0)? 0:1);
        ops_fpa_dev_unixctl_print_mac_address(d_str, flowEntry->data.control_pkt.match.dstMac);
        ds_put_format(d_str, "/%d | %4x/%d |%4x/%d |%4x/%d |%4x/%d |  %s |%2d |%2d |\n",
            flowEntry->data.control_pkt.match.dstMacMask.addr[0]?1:0,
            flowEntry->data.control_pkt.match.etherType,
            (flowEntry->data.control_pkt.match.etherTypeMask==0)? 0:1,
            flowEntry->data.control_pkt.match.ipProtocol,
            (flowEntry->data.control_pkt.match.ipProtocolMask==0)? 0:1,
            flowEntry->data.control_pkt.match.icmpV6Type,
            (flowEntry->data.control_pkt.match.icmpV6TypeMask==0)? 0:1,
            flowEntry->data.control_pkt.match.dstL4Port,
            (flowEntry->data.control_pkt.match.dstL4PortMask==0)? 0:1,
            (flowEntry->data.control_pkt.outputPort == FPA_OUTPUT_CONTROLLER)?"+":" ",
            flowEntry->data.control_pkt.clearActions,
            flowEntry->data.control_pkt.gotoTableNo);
        break;
    case FPA_FLOW_TABLE_TYPE_VLAN_E:
        ds_put_format(d_str, "|%16llx | %4d | %10d | %4d | 0x%08x | %4d | %4d | 0x%04x | %5d | %5d | %4d |\n",
            (unsigned long long int)flowEntry->cookie,
            flowEntry->priority,
            flowEntry->timeoutIdleTime,
            flowEntry->timeoutHardTime,
            flowEntry->flowModFlags,
            flowEntry->data.vlan.inPort,
            flowEntry->data.vlan.vlanId,
            flowEntry->data.vlan.vlanIdMask,
            flowEntry->data.vlan.newTagVid,
            flowEntry->data.vlan.newTagPcp,
            flowEntry->data.vlan.gotoTableNo);
        break;
    case FPA_FLOW_TABLE_TYPE_TERMINATION_E:
        ds_put_format(d_str, "|%16llx | %4d | %10d | %4d | 0x%08x | %4d/%d | %4d/%d | 0x%04x/%d |",
            (unsigned long long int)flowEntry->cookie,
            flowEntry->priority,
            flowEntry->timeoutIdleTime,
            flowEntry->timeoutHardTime,
            flowEntry->flowModFlags,
            flowEntry->data.termination.match.inPort,
            flowEntry->data.termination.match.inPortMask?1:0,
            flowEntry->data.termination.match.vlanId,
            flowEntry->data.termination.match.vlanIdMask?1:0,
            flowEntry->data.termination.match.etherType,
            flowEntry->data.termination.match.etherTypeMask?1:0);
        ops_fpa_dev_unixctl_print_mac_address(d_str, flowEntry->data.termination.match.destMac);
        ds_put_cstr(d_str, " |");
        ops_fpa_dev_unixctl_print_mac_address(d_str, flowEntry->data.termination.match.destMacMask);
        ds_put_format(d_str, " | %4lld/%4lld | %4d |\n",
            (unsigned long long int)flowEntry->data.termination.metadataValue,
            (unsigned long long int)flowEntry->data.termination.metadataMask,
            flowEntry->data.termination.gotoTableNo);
        break;
    case FPA_FLOW_TABLE_TYPE_L2_BRIDGING_E:
        ds_put_format(d_str, "|%16llx | %4d | %10d | %4d | 0x%08x | %4d/%d | ",
            (unsigned long long int)flowEntry->cookie,
            flowEntry->priority,
            flowEntry->timeoutIdleTime,
            flowEntry->timeoutHardTime,
            flowEntry->flowModFlags,
            flowEntry->data.l2_bridging.match.vlanId,
            flowEntry->data.l2_bridging.match.vlanIdMask?1:0);
        ops_fpa_dev_unixctl_print_mac_address(d_str, flowEntry->data.l2_bridging.match.destMac);
        ds_put_format(d_str, "/%d |   %s  | %8x | %d | %4d |\n",
            flowEntry->data.l2_bridging.match.destMacMask.addr[0]?1:0,
            (flowEntry->data.l2_bridging.outputPort == FPA_OUTPUT_CONTROLLER)?"+":" ",
            flowEntry->data.l2_bridging.groupId,
            flowEntry->data.l2_bridging.clearActions,
            flowEntry->data.l2_bridging.gotoTableNo);
        break;
/*  case FPA_FLOW_TABLE_TYPE_PCL0_E:
    case FPA_FLOW_TABLE_TYPE_PCL1_E:
    case FPA_FLOW_TABLE_TYPE_PCL2_E:
        fpaLibFlowTableIpclDump(flowTableNo);
        break;
    case FPA_FLOW_TABLE_TYPE_EPCL_E:
        fpaLibFlowTableEpclDump();
        break;*/
    case FPA_FLOW_TABLE_TYPE_L3_UNICAST_E:
        ds_put_format(d_str, "|%16llx | %4d | %10d | %4d | 0x%08x | %4d |%15s/%2d |  %s   | %8x | %d | %4d |\n",
            (unsigned long long int)flowEntry->cookie,
            flowEntry->priority,
            flowEntry->timeoutIdleTime,
            flowEntry->timeoutHardTime,
            flowEntry->flowModFlags,
            flowEntry->data.l3_unicast.match.vrfId,
            ops_fpa_ip2str(flowEntry->data.l3_unicast.match.dstIp4),
            ops_fpa_ip4mask_to_prefix_len(flowEntry->data.l3_unicast.match.dstIp4Mask),
            (flowEntry->data.l3_unicast.outputPort == FPA_OUTPUT_CONTROLLER)?"+":" ",
            flowEntry->data.l3_unicast.groupId,
            flowEntry->data.l3_unicast.clearActions,
            flowEntry->data.l3_unicast.gotoTableNo);
        break;
    default:
        ds_put_format(d_str, "entry type %d: to output information not implemented yet\n", flowEntry->entryType);
    }
}

static void
ops_fpa_dev_unixctl_flow_table_list(struct unixctl_conn *conn, int argc OVS_UNUSED,
                                    const char *argv[], void *aux OVS_UNUSED)
{
    uint32_t table;
    struct ds d_str = DS_EMPTY_INITIALIZER;

    ds_put_cstr(&d_str, "Flow table list:\n");
    for (table = 0; table < FPA_FLOW_TABLE_MAX; table++) {
        ds_put_format(&d_str, "  flowTableNo: %d; name: %s\n", table, opsFpaFlowTablesName[table]);
    }

    unixctl_command_reply(conn, ds_cstr(&d_str));
    ds_destroy(&d_str);
}

static void
ops_fpa_dev_unixctl_flow_table_show(struct unixctl_conn *conn, int argc OVS_UNUSED,
                                    const char *argv[], void *aux OVS_UNUSED)
{
    uint32_t switchId, flowTableNo;
    struct ds d_str = DS_EMPTY_INITIALIZER;
    FPA_STATUS status;
    FPA_FLOW_TABLE_ENTRY_STC flowEntry;

    switchId = atoi(argv[1]);
    flowTableNo = atoi(argv[2]);

    if (flowTableNo >= FPA_FLOW_TABLE_MAX) {
        unixctl_command_reply_error(conn, "flowTableNo invalid");
        return;
    }

    ops_fpa_dev_unixctl_print_table_title(&d_str, flowTableNo);

    status = fpaLibFlowTableGetNext(switchId, flowTableNo, 1, &flowEntry);
    while (status == FPA_OK) {
        ops_fpa_dev_unixctl_print_table_entry(&d_str, &flowEntry);
        status = fpaLibFlowTableGetNext(switchId, flowTableNo, 0, &flowEntry);
    }

    unixctl_command_reply(conn, ds_cstr(&d_str));
    ds_destroy(&d_str);
}

static void
ops_fpa_dev_unixctl_group_table_show(struct unixctl_conn *conn, int argc OVS_UNUSED,
                                     const char *argv[], void *aux OVS_UNUSED)
{
    struct ds d_str = DS_EMPTY_INITIALIZER;

    ds_put_cstr(&d_str, "See group tables in switchd log file\n");
    fpaGroupTableDump();

    unixctl_command_reply(conn, ds_cstr(&d_str));
    ds_destroy(&d_str);
}

static void
ops_fpa_dev_unixctl_init(void)
{
    unixctl_command_register("fpa/dev/ft-list", "", 0, 0,
                             ops_fpa_dev_unixctl_flow_table_list, NULL);

    unixctl_command_register("fpa/dev/ft-show", "switchId flowTableNo", 2, 2,
                             ops_fpa_dev_unixctl_flow_table_show, NULL);

    unixctl_command_register("fpa/dev/gt-show", "", 0, 0,
                             ops_fpa_dev_unixctl_group_table_show, NULL);
}

