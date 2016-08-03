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
#ifndef OPS_FPA_H
#define OPS_FPA_H 1

#include <errno.h>
#include <openvswitch/vlog.h>
#include <ovs-thread.h>
#include <ovs/packets.h>
#include <fpa/fpaLibApis.h>
#include <fpa/fpaLibTypes.h>
#include <ofproto/ofproto.h>
#include "ops-fpa-wrap.h"

/* TODO: to be remove this include: need enum fpaFlowModFlags->FPA_SEND_FLOW_REM
 * #include <fpa/fpaLibTypesPrv.h> */
enum fpaFlowModFlags {
    FPA_SEND_FLOW_REM   = 1 << 0,  /* Send flow removed message when flow
                                    * expires or is deleted. */
    FPA_CHECK_OVERLAP   = 1 << 1,  /* Check for overlapping entries first. */
    FPA_RESET_COUNTS    = 1 << 2,  /* Reset flow packet and byte counts. */
    FPA_NO_PKT_COUNTS   = 1 << 3,  /* Don't keep track of packet count. */
    FPA_NO_BYTE_COUNTS  = 1 << 4,  /* Don't keep track of byte count. */
};

#define FPA_TRACE_FN() VLOG_INFO(__FUNCTION__)

#define STR_EQ(s1, s2) ((s1 != NULL) && (s2 != NULL) && \
                        (strlen((s1)) == strlen((s2))) && \
                        (!strncmp((s1), (s2), strlen((s2)))))

#define FPA_ETH_ADDR_FMT ETH_ADDR_FMT
#define FPA_ETH_ADDR_ARGS(mac) \
        (mac.addr)[0], (mac.addr)[1], (mac.addr)[2], (mac.addr)[3], (mac.addr)[4], (mac.addr)[5]

#define FPA_DEV_SWITCH_ID_DEFAULT         0 /* FPA plugin supports only one 
                                               switch device now */
#define FPA_VLAN_1                        1 /* Default VLAN ID */
#define FPA_VLAN_MAX_COUNT             4096 /* Maximum supported number of 
                                               VLAN IDs */

#define FPA_INVALID_SWITCH_ID        0xffff
#define FPA_INVALID_INTF_ID          0xffff 

void ops_fpa_init();
int ops_fpa_vlan_add(uint32_t switch_id, int pid, int vid, bool tag_in,
                     bool tag_out);
int ops_fpa_vlan_rm(uint32_t switch_id, int pid, int vid, bool tag_in);
int ops_fpa_vlan_mod(bool add, uint32_t switch_id, int pid, int vid,
                     enum port_vlan_mode mode);

/* return string describing FPA status code */
const char * ops_fpa_strerr(int err);
/* properly checked string to int conversion*/
int ops_fpa_str2int(const char *s, int *i);

const char * ops_fpa_str_vlan_mode(int vlan_mode);
const char * ops_fpa_str_trunks(unsigned long *trunks);
const char * ops_fpa_str_ports(ofp_port_t *ports, size_t size);

char *ops_fpa_ip2str(in_addr_t ipAddr);
uint32_t ops_fpa_ip4mask_to_prefix_len(in_addr_t ipMask);
in_addr_t ops_fpa_prefix_len_to_ip4mask(uint32_t prefix);

#define                FPA_HAL_L3_DEFAULT_VRID         0

/* Returns pointer to struct ofport_fpa for a switch interface with a given 
 * pid. */
struct ofport_fpa *ops_fpa_get_ofport_by_pid(int pid);

#endif /* ops-fpa.h */

