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

#include <net/ethernet.h>
#include "ops-fpa-routing.h"

VLOG_DEFINE_THIS_MODULE(ops_fpa_routing);

/*for gcc -O0*/
inline uint64_t ops_fpa_arp_cookie(uint16_t vlanId) __attribute__((always_inline));
inline uint64_t ops_fpa_mac_cookie(uint16_t vlanId, struct eth_addr mac) __attribute__((always_inline));

inline uint64_t
ops_fpa_arp_cookie(uint16_t vlanId)
{
    return (ETHERTYPE_ARP<<16) | vlanId;
}

inline uint64_t
ops_fpa_mac_cookie(uint16_t vlanId, struct eth_addr mac)
{
    uint64_t cookie;

    memcpy((void *)&cookie, (void *)mac.ea, ETH_ADDR_LEN);

    return cookie | ((uint64_t)vlanId) << 48;
}

FPA_STATUS
fpa_vlan_set_arp_bcast(uint32_t switchId, uint16_t vlanId)
{
    FPA_STATUS status;
    FPA_FLOW_TABLE_ENTRY_STC flowEntry;

    /* Initiliaze Flow Entry for Ctrl Pkt table*/
    memset(&flowEntry, 0, sizeof(FPA_FLOW_TABLE_ENTRY_STC));

    /* Fill all default fields */
    status = fpaLibFlowEntryInit(switchId, FPA_FLOW_TABLE_TYPE_CONTROL_PKT_E, &flowEntry);
    if (status != FPA_OK) {
        VLOG_ERR("%s: failed to init CtrlPkt entry. Status: %s",
                 __FUNCTION__, ops_fpa_strerr(status));
    }

    flowEntry.cookie = ops_fpa_arp_cookie(vlanId);
    flowEntry.data.control_pkt.entry_type = FPA_CONTROL_PKTS_TYPE_ARP_REQUEST_MESSAGES_E;
    /* matching broadcast MAC only */
    memset(&flowEntry.data.control_pkt.match.dstMac, 0xFF, ETH_ALEN);
    memset(&flowEntry.data.control_pkt.match.dstMacMask, 0xFF, ETH_ALEN);

    flowEntry.data.control_pkt.match.vlanId = vlanId;
    flowEntry.data.control_pkt.match.vlanIdMask = 0xFFFF;

    flowEntry.data.control_pkt.match.etherType = ETHERTYPE_ARP;
    flowEntry.data.control_pkt.match.etherTypeMask = 0xFFFF;
    /* trapping packet to CPU */
    flowEntry.data.control_pkt.outputPort = FPA_OUTPUT_CONTROLLER;

    status = wrap_fpaLibFlowEntryAdd(switchId, FPA_FLOW_TABLE_TYPE_CONTROL_PKT_E, &flowEntry);
    if (status != FPA_OK) {
        VLOG_ERR("%s: failed to add CtrlPkt entry. Status: %s",
                 __FUNCTION__, ops_fpa_strerr(status));
    }

    return status;
}

FPA_STATUS
fpa_vlan_unset_arp_bcast(uint32_t switchId, uint16_t vlanId)
{
    FPA_STATUS status;

    status = wrap_fpaLibFlowTableCookieDelete(switchId, FPA_FLOW_TABLE_TYPE_CONTROL_PKT_E, ops_fpa_arp_cookie(vlanId));
    if (status != FPA_OK) {
        VLOG_ERR("%s: failed to delete CtrlPkt entry. Status: %s",
                __FUNCTION__, ops_fpa_strerr(status));
    }

    return status;
}

/* Add unicast ARP trapping entry with L2_BRIDGING flow table */
FPA_STATUS
fpa_set_arp_fwd(uint32_t switchId, uint16_t vlanId, struct eth_addr mac)
{
    FPA_FLOW_TABLE_ENTRY_STC flowEntry;
    FPA_STATUS status;

    /* Fill all default fields */
    status = fpaLibFlowEntryInit(switchId, FPA_FLOW_TABLE_TYPE_L2_BRIDGING_E, &flowEntry);
    if (status != FPA_OK) {
        VLOG_ERR("%s: failed to init L2 bridging entry. Status: %s",
                   __FUNCTION__, ops_fpa_strerr(status));
    }

    /* Fill FDB entry */
    flowEntry.cookie = ops_fpa_mac_cookie(vlanId, mac);
    flowEntry.flowModFlags = FPA_SEND_FLOW_REM;
    flowEntry.data.l2_bridging.match.vlanId = vlanId;
    flowEntry.data.l2_bridging.match.vlanIdMask = 0xFFFF;
    memcpy(flowEntry.data.l2_bridging.match.destMac.addr, mac.ea, ETH_ALEN);
    memset(flowEntry.data.l2_bridging.match.destMacMask.addr, 0xFF, ETH_ALEN);

    flowEntry.data.l2_bridging.groupId = 0xFFFFFFFF; /* don't do mirror */
    flowEntry.data.l2_bridging.outputPort = FPA_OUTPUT_CONTROLLER; /* trap to CPU */
    flowEntry.data.l2_bridging.gotoTableNo = 0;

    /* Add L2 entry to FDB*/
    status = wrap_fpaLibFlowEntryAdd(switchId, FPA_FLOW_TABLE_TYPE_L2_BRIDGING_E, &flowEntry);
    if (status != FPA_OK) {
        VLOG_ERR("%s: failed to add L2 bridging entry. Status: %s",
                   __FUNCTION__, ops_fpa_strerr(status));
    }

    return status;
}

/* Remove unicast ARP trapping entry with L2_BRIDGING flow table */
FPA_STATUS
fpa_unset_arp_fwd(uint32_t switchId, uint16_t vlanId, struct eth_addr mac)
{
    FPA_STATUS status;

    status = wrap_fpaLibFlowTableCookieDelete(switchId, FPA_FLOW_TABLE_TYPE_L2_BRIDGING_E, ops_fpa_mac_cookie(vlanId, mac));
    if (status != FPA_OK) {
        VLOG_ERR("%s: failed to delete L2 bridging entry. Status: %s",
                __FUNCTION__, ops_fpa_strerr(status));
    }

    return status;
}

FPA_STATUS
fpa_vlan_enable_unicast_routing(uint32_t switchId, uint16_t vlanId, struct eth_addr mac)
{
    FPA_FLOW_TABLE_ENTRY_STC flowEntry;
    FPA_STATUS status;

    /* Fill all default fields */
    status = fpaLibFlowEntryInit(switchId, FPA_FLOW_TABLE_TYPE_TERMINATION_E, &flowEntry);
    if (status != FPA_OK) {
        VLOG_ERR("%s: failed to init termination entry. Status: %s",
                   __FUNCTION__, ops_fpa_strerr(status));
    }

    flowEntry.cookie = ops_fpa_mac_cookie(vlanId, mac);
    flowEntry.flowModFlags = FPA_SEND_FLOW_REM;
    /* Setting EtherType */
    flowEntry.data.termination.match.etherType = ETHERTYPE_IP;
    flowEntry.data.termination.match.etherTypeMask = 0xFFFF;
    /* Setting the vlan in the entry */
    flowEntry.data.termination.match.vlanId = vlanId;
    flowEntry.data.termination.match.vlanIdMask = 0xFFFF;
    /* MAC setting */
    memcpy(&flowEntry.data.termination.match.destMac.addr, &mac.ea, ETH_ALEN);
    memset(flowEntry.data.termination.match.destMacMask.addr, 0xFF, ETH_ALEN);

    flowEntry.data.termination.metadataValue = FPA_FLOW_TABLE_METADATA_MAC2ME_BIT;
    flowEntry.data.termination.metadataMask = FPA_FLOW_TABLE_METADATA_MAC2ME_BIT;

    status = wrap_fpaLibFlowEntryAdd(switchId, FPA_FLOW_TABLE_TYPE_TERMINATION_E, &flowEntry);
    if (status != FPA_OK) {
        VLOG_ERR("%s: failed to add termination entry. Status: %s",
                   __FUNCTION__, ops_fpa_strerr(status));
    }

    return status;
}

FPA_STATUS
fpa_vlan_disable_unicast_routing(uint32_t switchId, uint16_t vlanId, struct eth_addr mac)
{
    FPA_STATUS status;

    status = wrap_fpaLibFlowTableCookieDelete(switchId, FPA_FLOW_TABLE_TYPE_TERMINATION_E, ops_fpa_mac_cookie(vlanId, mac));
    if (status != FPA_OK) {
        VLOG_ERR("%s: failed to delete Termination entry. Status: %s",
                   __FUNCTION__, ops_fpa_strerr(status));
    }

    return status;
}

struct fpa_l3_intf *
ops_fpa_enable_routing_interface(uint32_t switchId, uint32_t portNum, uint16_t vlanId, struct eth_addr mac)
{
    struct fpa_l3_intf * l3_intf = NULL;

    if (!ops_fpa_vlan_add(switchId, portNum, vlanId, false, false)) {
         l3_intf = ops_fpa_enable_routing_vlan(switchId, vlanId, mac);
         if (l3_intf)
         {
            l3_intf->intf_id = portNum;
            l3_intf->vlan_intf = true;
         }
    }

    return l3_intf;
}

struct fpa_l3_intf *
ops_fpa_enable_routing_vlan(uint32_t switchId, uint16_t vlanId, struct eth_addr mac)
{
    FPA_STATUS status;
    struct fpa_l3_intf * l3_intf = NULL;

    status = fpa_vlan_set_arp_bcast(switchId, vlanId);
    if (status != FPA_OK) {
        return NULL;
    }

    status = fpa_set_arp_fwd(switchId, vlanId, mac);
    if (status != FPA_OK) {
        return NULL;
    }

    status = fpa_vlan_enable_unicast_routing(switchId, vlanId, mac);
    if (status == FPA_OK)
    {
        l3_intf = xzalloc(sizeof(struct fpa_l3_intf));
        l3_intf->switchId = switchId;
        l3_intf->vlan_id = vlanId;
        l3_intf->mac = mac;
    }

    return l3_intf;
}

void
ops_fpa_disable_routing(struct fpa_l3_intf *l3_intf)
{
    FPA_STATUS status;

    ovs_assert(l3_intf);

    if (l3_intf->vlan_intf)
        ops_fpa_vlan_rm(l3_intf->switchId, l3_intf->intf_id, l3_intf->vlan_id,
                        false);

    status = fpa_vlan_unset_arp_bcast(l3_intf->switchId, l3_intf->vlan_id);
    if (status == FPA_OK)
        status = fpa_vlan_disable_unicast_routing(l3_intf->switchId, l3_intf->vlan_id, l3_intf->mac);

    if (status != FPA_OK)
        VLOG_ERR("%s: failed to disable routing. Status: %s",
                __FUNCTION__, ops_fpa_strerr(status));

    free(l3_intf);
}

