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
 *  File: ops-fpa-routing.c
 *
 *  Purpose: This file contains OpenSwitch routing related
 *           application code for the FPA SDK.
 */

#include <net/ethernet.h>
#include "ops-fpa-routing.h"
#include "ops-fpa-vlan.h"

VLOG_DEFINE_THIS_MODULE(ops_fpa_routing);

#define OPS_FPA_CONTROL_PKT_COOKIE(type,vlanId) ((type <<16) | (vlanId))
#define OPS_FPA_TERMINATION_COOKIE(vlanId) (vlanId)
#define OPS_FPA_L2_BRIDGING_COOKIE(vlanId) (vlanId)

FPA_STATUS ops_fpa_vlan_set_arp_bcast(uint32_t switch_id, uint16_t vlan_id);
FPA_STATUS ops_fpa_vlan_unset_arp_bcast(uint32_t switch_id, uint16_t vlan_id);

FPA_STATUS ops_fpa_set_arp_fwd(uint32_t switch_id, uint16_t vlan_id, struct eth_addr mac);
FPA_STATUS ops_fpa_unset_arp_fwd(uint32_t switch_id, uint16_t vlan_id, struct eth_addr mac);

FPA_STATUS ops_fpa_vlan_enable_unicast_routing(uint32_t switch_id, uint16_t vlan_id, struct eth_addr mac);
FPA_STATUS ops_fpa_vlan_disable_unicast_routing(uint32_t switch_id, uint16_t vlan_id, struct eth_addr mac);

FPA_STATUS ops_fpa_vlan_set_udp_dhcp(uint32_t switch_id, uint16_t vlan_id);
FPA_STATUS ops_fpa_vlan_unset_udp_dhcp(uint32_t switch_id, uint16_t vlan_id);

FPA_STATUS
ops_fpa_vlan_set_arp_bcast(uint32_t switch_id, uint16_t vlan_id)
{
    FPA_STATUS err;
    FPA_FLOW_TABLE_ENTRY_STC flowEntry;

    /* Initiliaze Flow Entry for Ctrl Pkt table*/
    err = fpaLibFlowEntryInit(switch_id, FPA_FLOW_TABLE_TYPE_CONTROL_PKT_E, &flowEntry);
    if (err != FPA_OK) {
        VLOG_ERR("%s: failed to init CtrlPkt entry. Status: %s",
                 __func__, ops_fpa_strerr(err));
    }

    flowEntry.cookie = OPS_FPA_CONTROL_PKT_COOKIE(ETHERTYPE_ARP, vlan_id);
    flowEntry.data.control_pkt.entry_type = FPA_CONTROL_PKTS_TYPE_ARP_REQUEST_MESSAGES_E;
    /* matching broadcast MAC only */
    memset(&flowEntry.data.control_pkt.match.dstMac, 0xFF, ETH_ALEN);
    memset(&flowEntry.data.control_pkt.match.dstMacMask, 0xFF, ETH_ALEN);

    flowEntry.data.control_pkt.match.vlanId = vlan_id;
    flowEntry.data.control_pkt.match.vlanIdMask = 0xFFFF;

    flowEntry.data.control_pkt.match.etherType = ETHERTYPE_ARP;
    flowEntry.data.control_pkt.match.etherTypeMask = 0xFFFF;
    /* trapping packet to CPU */
    flowEntry.data.control_pkt.outputPort = FPA_OUTPUT_CONTROLLER;

    err = wrap_fpaLibFlowEntryAdd(switch_id, FPA_FLOW_TABLE_TYPE_CONTROL_PKT_E, &flowEntry);
    if (err != FPA_OK) {
        VLOG_ERR("%s: failed to add CtrlPkt entry. Status: %s",
                 __func__, ops_fpa_strerr(err));
    }

    return err;
}

FPA_STATUS
ops_fpa_vlan_unset_arp_bcast(uint32_t switch_id, uint16_t vlan_id)
{
    FPA_STATUS err;

    err = wrap_fpaLibFlowTableCookieDelete(switch_id,
                            FPA_FLOW_TABLE_TYPE_CONTROL_PKT_E,
                            OPS_FPA_CONTROL_PKT_COOKIE(ETHERTYPE_ARP, vlan_id));
    if (err != FPA_OK) {
        VLOG_ERR("%s: failed to delete CtrlPkt entry. Status: %s",
                __func__, ops_fpa_strerr(err));
    }

    return err;
}

/* Add unicast ARP trapping entry with L2_BRIDGING flow table */
FPA_STATUS
ops_fpa_set_arp_fwd(uint32_t switch_id, uint16_t vlan_id, struct eth_addr mac)
{
    FPA_FLOW_TABLE_ENTRY_STC flowEntry;
    FPA_STATUS err;

    /* Fill all default fields */
    err = fpaLibFlowEntryInit(switch_id, FPA_FLOW_TABLE_TYPE_L2_BRIDGING_E, &flowEntry);
    if (err != FPA_OK) {
        VLOG_ERR("%s: failed to init L2 bridging entry. Status: %s",
                   __func__, ops_fpa_strerr(err));
    }

    /* Fill FDB entry */
    flowEntry.cookie = OPS_FPA_L2_BRIDGING_COOKIE(vlan_id);
    flowEntry.data.l2_bridging.match.vlanId = vlan_id;
    flowEntry.data.l2_bridging.match.vlanIdMask = 0xFFFF;
    memcpy(flowEntry.data.l2_bridging.match.destMac.addr, mac.ea, ETH_ALEN);
    memset(flowEntry.data.l2_bridging.match.destMacMask.addr, 0xFF, ETH_ALEN);

    flowEntry.data.l2_bridging.groupId = 0xFFFFFFFF; /* don't do mirror */
    flowEntry.data.l2_bridging.outputPort = FPA_OUTPUT_CONTROLLER; /* trap to CPU */

    /* Add L2 entry to FDB*/
    err = wrap_fpaLibFlowEntryAdd(switch_id, FPA_FLOW_TABLE_TYPE_L2_BRIDGING_E, &flowEntry);
    if (err != FPA_OK) {
        VLOG_ERR("%s: failed to add L2 bridging entry. Status: %s",
                   __func__, ops_fpa_strerr(err));
    }

    return err;
}

/* Remove unicast ARP trapping entry with L2_BRIDGING flow table */
FPA_STATUS
ops_fpa_unset_arp_fwd(uint32_t switch_id, uint16_t vlan_id, struct eth_addr mac)
{
    FPA_STATUS err;

    err = wrap_fpaLibFlowTableCookieDelete(switch_id,
                                           FPA_FLOW_TABLE_TYPE_L2_BRIDGING_E,
                                           OPS_FPA_L2_BRIDGING_COOKIE(vlan_id));
    if (err != FPA_OK) {
        VLOG_ERR("%s: failed to delete L2 bridging entry. Status: %s",
                __func__, ops_fpa_strerr(err));
    }

    return err;
}

FPA_STATUS
ops_fpa_vlan_enable_unicast_routing(uint32_t switch_id, uint16_t vlan_id,
                                struct eth_addr mac)
{
    FPA_FLOW_TABLE_ENTRY_STC flowEntry;
    FPA_STATUS err;

    /* Fill all default fields */
    err = fpaLibFlowEntryInit(switch_id, FPA_FLOW_TABLE_TYPE_TERMINATION_E, &flowEntry);
    if (err != FPA_OK) {
        VLOG_ERR("%s: failed to init termination entry. Status: %s",
                   __func__, ops_fpa_strerr(err));
    }

    flowEntry.cookie = OPS_FPA_TERMINATION_COOKIE(vlan_id);
    /* Setting EtherType */
    flowEntry.data.termination.match.etherType = ETHERTYPE_IP;
    flowEntry.data.termination.match.etherTypeMask = 0xFFFF;
    /* Setting the vlan in the entry */
    flowEntry.data.termination.match.vlanId = vlan_id;
    flowEntry.data.termination.match.vlanIdMask = 0xFFFF;
    /* MAC setting */
    memcpy(&flowEntry.data.termination.match.destMac.addr, &mac.ea, ETH_ALEN);
    memset(flowEntry.data.termination.match.destMacMask.addr, 0xFF, ETH_ALEN);

    flowEntry.data.termination.metadataValue = FPA_FLOW_TABLE_METADATA_MAC2ME_BIT;
    flowEntry.data.termination.metadataMask = FPA_FLOW_TABLE_METADATA_MAC2ME_BIT;

    err = wrap_fpaLibFlowEntryAdd(switch_id, FPA_FLOW_TABLE_TYPE_TERMINATION_E, &flowEntry);
    if (err != FPA_OK) {
        VLOG_ERR("%s: failed to add termination entry. Status: %s",
                   __func__, ops_fpa_strerr(err));
    }

    return err;
}

FPA_STATUS
ops_fpa_vlan_disable_unicast_routing(uint32_t switch_id, uint16_t vlan_id,
                                 struct eth_addr mac)
{
    FPA_STATUS err;

    err = wrap_fpaLibFlowTableCookieDelete(switch_id,
                                           FPA_FLOW_TABLE_TYPE_TERMINATION_E,
                                           OPS_FPA_TERMINATION_COOKIE(vlan_id));
    if (err != FPA_OK) {
        VLOG_ERR("%s: failed to delete Termination entry. Status: %s",
                   __func__, ops_fpa_strerr(err));
    }

    return err;
}

FPA_STATUS
ops_fpa_vlan_set_udp_dhcp(uint32_t switch_id, uint16_t vlan_id)
{
    FPA_STATUS err;
    FPA_FLOW_TABLE_ENTRY_STC flowEntry;

    /* Initiliaze Flow Entry for Ctrl Pkt table*/
    memset(&flowEntry, 0, sizeof(FPA_FLOW_TABLE_ENTRY_STC));

    /* Fill all default fields */
    err = fpaLibFlowEntryInit(switch_id, FPA_FLOW_TABLE_TYPE_CONTROL_PKT_E, &flowEntry);
    if (err != FPA_OK) {
        VLOG_ERR("%s: failed to init CtrlPkt entry. Status: %s",
                 __FUNCTION__, ops_fpa_strerr(err));
    }

    flowEntry.cookie = OPS_FPA_CONTROL_PKT_COOKIE(ETHERTYPE_IP, vlan_id);
    flowEntry.data.control_pkt.entry_type = FPA_CONTROL_PKTS_TYPE_UDP_BROADCAST_CTRL_E;
    /* matching broadcast MAC only */
    memset(&flowEntry.data.control_pkt.match.dstMac, 0xFF, ETH_ALEN);
    memset(&flowEntry.data.control_pkt.match.dstMacMask, 0xFF, ETH_ALEN);

    flowEntry.data.control_pkt.match.vlanId = vlan_id;
    flowEntry.data.control_pkt.match.vlanIdMask = 0xFFFF;

    flowEntry.data.control_pkt.match.etherType = ETHERTYPE_IP;
    flowEntry.data.control_pkt.match.etherTypeMask = 0xFFFF;

    flowEntry.data.control_pkt.match.ipProtocol = 17; /* UDP proto*/
    flowEntry.data.control_pkt.match.ipProtocolMask = 0xFFFF;

    flowEntry.data.control_pkt.match.icmpV6TypeMask = 0;

    flowEntry.data.control_pkt.match.dstL4Port = 67; /* DHCP Server port */
    flowEntry.data.control_pkt.match.dstL4PortMask = 0xFFFF;
    /* trapping packet to CPU */
    flowEntry.data.control_pkt.outputPort = FPA_OUTPUT_CONTROLLER;

    err = wrap_fpaLibFlowEntryAdd(switch_id, FPA_FLOW_TABLE_TYPE_CONTROL_PKT_E, &flowEntry);
    if (err != FPA_OK) {
        VLOG_ERR("%s: failed to add CtrlPkt entry. Status: %s",
                 __FUNCTION__, ops_fpa_strerr(err));
    }

    return err;
}

FPA_STATUS
ops_fpa_vlan_unset_udp_dhcp(uint32_t switch_id, uint16_t vlan_id)
{
    FPA_STATUS err;

    err = wrap_fpaLibFlowTableCookieDelete(switch_id,
                            FPA_FLOW_TABLE_TYPE_CONTROL_PKT_E,
                            OPS_FPA_CONTROL_PKT_COOKIE(ETHERTYPE_IP, vlan_id));
    if (err != FPA_OK) {
        VLOG_ERR("%s: failed to delete CtrlPkt entry. Status: %s",
                __FUNCTION__, ops_fpa_strerr(err));
    }

    return err;
}

struct fpa_l3_intf *
ops_fpa_enable_routing_interface(uint32_t switch_id, uint32_t port_num,
                                 uint16_t vlan_id, struct eth_addr mac)
{
    if (ops_fpa_vlan_add_internal(switch_id, port_num, vlan_id)) {
        return NULL;
    }

    struct fpa_l3_intf *l3_intf = ops_fpa_enable_routing_vlan(switch_id, vlan_id, mac);
    if (!l3_intf) {
        ops_fpa_vlan_del_internal(switch_id, port_num, vlan_id);
        return NULL;
    }

    l3_intf->intf_id = port_num;
    l3_intf->vlan_intf = true;

    return l3_intf;
}

struct fpa_l3_intf *
ops_fpa_enable_routing_vlan(uint32_t switch_id, uint16_t vlan_id,
                            struct eth_addr mac)
{
    FPA_STATUS err;
    struct fpa_l3_intf *l3_intf = NULL;

    err = ops_fpa_vlan_set_arp_bcast(switch_id, vlan_id);
    if (err != FPA_OK) {
        VLOG_ERR("%s: ops_fpa_vlan_set_arp_bcast(): %s",
                 __func__, ops_fpa_strerr(err));
        return NULL;
    }

    err = ops_fpa_set_arp_fwd(switch_id, vlan_id, mac);
    if (err != FPA_OK) {
        VLOG_ERR("%s: ops_fpa_set_arp_fwd(): %s",
                 __func__, ops_fpa_strerr(err));
        goto fail_set_arp_fwd;
    }

    err = ops_fpa_vlan_set_udp_dhcp(switch_id, vlan_id);
    if (err != FPA_OK) {
        VLOG_ERR("%s: ops_fpa_vlan_set_udp_dhcp() %s",
                 __func__, ops_fpa_strerr(err));
        goto fail_set_dhcp;
    }

    err = ops_fpa_vlan_enable_unicast_routing(switch_id, vlan_id, mac);
    if (err != FPA_OK)
    {
        VLOG_ERR("%s: ops_fpa_vlan_enable_unicast_routing(): %s",
                 __func__, ops_fpa_strerr(err));
        goto fail_vlan_enable_unicast_routing;
    }

    l3_intf = xzalloc(sizeof(struct fpa_l3_intf));
    l3_intf->switchId = switch_id;
    l3_intf->vlan_id = vlan_id;
    l3_intf->mac = mac;

    return l3_intf;

fail_vlan_enable_unicast_routing:
    ops_fpa_vlan_unset_udp_dhcp(switch_id, vlan_id);
fail_set_dhcp:
    ops_fpa_unset_arp_fwd(switch_id, vlan_id, mac);
fail_set_arp_fwd:
    ops_fpa_vlan_unset_arp_bcast(switch_id, vlan_id);

    return NULL;
}

void
ops_fpa_disable_routing(struct fpa_l3_intf *l3_intf)
{
    FPA_STATUS err;

    ovs_assert(l3_intf);

    if (l3_intf->vlan_intf) {
        ops_fpa_vlan_del_internal(l3_intf->switchId, l3_intf->intf_id, l3_intf->vlan_id);
    }

    err = ops_fpa_vlan_unset_arp_bcast(l3_intf->switchId, l3_intf->vlan_id);
    if (err != FPA_OK) {
        VLOG_ERR("%s: ops_fpa_vlan_unset_arp_bcast(): %s",
                 __func__, ops_fpa_strerr(err));
        goto done;
    }

    err = ops_fpa_unset_arp_fwd(l3_intf->switchId, l3_intf->vlan_id,
                                l3_intf->mac);
    if (err != FPA_OK) {
        VLOG_ERR("%s: ops_fpa_unset_arp_fwd(): %s",
                 __func__, ops_fpa_strerr(err));
        goto done;
    }

    err = ops_fpa_vlan_unset_udp_dhcp(l3_intf->switchId, l3_intf->vlan_id);
    if (err != FPA_OK) {
        VLOG_ERR("%s: ops_fpa_vlan_unset_udp_dhcp(): %s",
                 __func__, ops_fpa_strerr(err));
        goto done;
    }

    err = ops_fpa_vlan_disable_unicast_routing(
        l3_intf->switchId, l3_intf->vlan_id, l3_intf->mac
    );
    if (err != FPA_OK) {
        VLOG_ERR("%s: ops_fpa_vlan_disable_unicast_routing(): %s",
                 __func__, ops_fpa_strerr(err));
        goto done;
    }

done:
    free(l3_intf);
}
