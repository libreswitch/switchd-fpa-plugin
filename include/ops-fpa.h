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

#define FPA_TRACE_FN() VLOG_INFO(__FUNCTION__)

#define STR_EQ(s1, s2) ((s1 != NULL) && (s2 != NULL) && \
                        (strlen((s1)) == strlen((s2))) && \
                        (!strncmp((s1), (s2), strlen((s2)))))

#define FPA_ETH_ADDR_FMT ETH_ADDR_FMT
#define FPA_ETH_ADDR_ARGS(mac) \
        (mac.addr)[0], (mac.addr)[1], (mac.addr)[2], (mac.addr)[3], (mac.addr)[4], (mac.addr)[5]

/* TODO: This is a temporary solution. We need to come up with something more 
 * flexible. */
#define PORT_CONVERT_FPA2OPS(port)    (port+1)
#define PORT_CONVERT_OPS2FPA(port)    (port>0?(port-1):0)

void ops_fpa_init();
int ops_fpa_vlan_add(int pid, int vid, bool tag_in, bool tag_out);
int ops_fpa_vlan_rm(int pid, int vid, bool tag_in);
int ops_fpa_vlan_mod(bool add, int pid, int vid, enum port_vlan_mode mode);

/* return string describing FPA status code */
const char * ops_fpa_strerr(int err);
/* properly checked string to int conversion*/
int ops_fpa_str2int(const char *s, int *i);

const char * ops_fpa_str_vlan_mode(int vlan_mode);
const char * ops_fpa_str_trunks(unsigned long *trunks);
const char * ops_fpa_str_ports(ofp_port_t *ports, size_t size);

struct netdev;
/* get netdev port id */
int ops_fpa_netdev_pid(struct netdev *dev);

#endif /* ops-fpa.h */
