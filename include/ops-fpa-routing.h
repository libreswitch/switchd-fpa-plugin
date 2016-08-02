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

#ifndef OPS_FPA_ROUTING_H_
#define OPS_FPA_ROUTING_H_

#include <vswitch-idl.h>
#include <ofproto/ofproto-provider.h>
#include "ops-fpa.h"

struct fpa_l3_intf {
    uint32_t switchId;
    uint32_t intf_id;       /* Valid if (!vlan_intf) */
    uint16_t vlan_id;
    struct eth_addr mac;
    bool vlan_intf;
};

FPA_STATUS fpa_vlan_set_arp_bcast(uint32_t switchId, uint16_t vlanId);
FPA_STATUS fpa_vlan_unset_arp_bcast(uint32_t switchId, uint16_t vlanId);

FPA_STATUS fpa_set_arp_fwd(uint32_t switchId, uint16_t vlanId, struct eth_addr mac);
FPA_STATUS fpa_unset_arp_fwd(uint32_t switchId, uint16_t vlanId, struct eth_addr mac);

FPA_STATUS fpa_vlan_enable_unicast_routing(uint32_t switchId, uint16_t vlanId, struct eth_addr mac);
FPA_STATUS fpa_vlan_disable_unicast_routing(uint32_t switchId, uint16_t vlanId, struct eth_addr mac);

struct fpa_l3_intf *ops_fpa_enable_routing_interface(uint32_t switchId, uint32_t portNum, uint16_t vlanId, struct eth_addr mac);
struct fpa_l3_intf *ops_fpa_enable_routing_vlan(uint32_t switchId, uint16_t vlanId, struct eth_addr mac);

void ops_fpa_disable_routing(struct fpa_l3_intf *l3_intf);

#endif /* OPS_FPA_ROUTING_H_ */

