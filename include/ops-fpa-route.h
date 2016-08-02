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
#ifndef OPS_FPA_ROUTE_H_
#define OPS_FPA_ROUTE_H_

#include "ops-fpa.h"
#include "ops-marvell-utils-resource.h"

#define MARVELL_FPA_NH  1024

FPA_STATUS ops_fpa_route_add_l2_group(uint32_t switchId, uint32_t port,
                                    uint16_t vlan_id, uint32_t popVlanTagAction,
                                    uint32_t *l2Group_ptr);

FPA_STATUS ops_fpa_route_add_l3_group(uint32_t switchId, uint32_t l2Group,
                                    uint32_t arp_index, uint16_t vlan_id,
                                    uint32_t mtu,
                                    FPA_MAC_ADDRESS_STC *srcMac_ptr,
                                    FPA_MAC_ADDRESS_STC *dsMac_ptr,
                                    uint32_t *l3Group_ptr);

FPA_STATUS ops_fpa_route_add_route(uint32_t switchId, uint32_t l3Group,
                                 in_addr_t dstIp4, uint32_t mask_len);

void ops_fpa_route_del_route(uint32_t switchId, in_addr_t dstIp4,
                           uint32_t mask_len);

void ops_fpa_route_del_group(uint32_t switchId, uint32_t group);

#endif /* OPS_FPA_ROUTE_H_ */
