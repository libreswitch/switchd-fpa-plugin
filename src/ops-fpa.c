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

#include <stdlib.h>
#include <vlan-bitmap.h>
#include "ops-fpa.h"

#define OPS_FPA_MAX_IP_PREFIX 32

VLOG_DEFINE_THIS_MODULE(ops_fpa);

const char *
ops_fpa_strerr(int err)
{
    switch(err) {
        case FPA_ERROR                : return "Generic error";
        case FPA_OK                   : return "Operation succeeded";
        case FPA_FAIL                 : return "Operation failed";
        case FPA_BAD_VALUE            : return "Illegal value";
        case FPA_OUT_OF_RANGE         : return "Value is out of range";
        case FPA_BAD_PARAM            : return "Illegal parameter in function called";
        case FPA_BAD_PTR              : return "Illegal pointer value";
        case FPA_BAD_SIZE             : return "Illegal size";
        case FPA_BAD_STATE            : return "Illegal state of state machine";
        case FPA_SET_ERROR            : return "Set operation failed";
        case FPA_GET_ERROR            : return "Get operation failed";
        case FPA_CREATE_ERROR         : return "Fail while creating an item";
        case FPA_NOT_FOUND            : return "Item not found";
        case FPA_NO_MORE              : return "No more items found";
        case FPA_NO_SUCH              : return "No such item";
        case FPA_TIMEOUT              : return "Time out";
        case FPA_NO_CHANGE            : return "The parameter is already in this value";
        case FPA_NOT_SUPPORTED        : return "This request is not supported";
        case FPA_NOT_IMPLEMENTED      : return "This request is not implemented";
        case FPA_NOT_INITIALIZED      : return "The item is not initialized";
        case FPA_NO_RESOURCE          : return "Resource not available";
        case FPA_FULL                 : return "Item is full";
        case FPA_EMPTY                : return "Item is empty";
        case FPA_INIT_ERROR           : return "Error occured while INIT process";
        case FPA_NOT_READY            : return "The other side is not ready yet";
        case FPA_ALREADY_EXIST        : return "Tried to create existing item";
        case FPA_OUT_OF_CPU_MEM       : return "Cpu memory allocation failed";
        case FPA_ABORTED              : return "Operation has been aborted";
        case FPA_NOT_APPLICABLE_DEVICE: return "API not applicable to device, can be returned only on devNum parameter";
        case FPA_UNFIXABLE_ECC_ERROR  : return "CPSS detected memory ECC error that can't be fixed";
        case FPA_UNFIXABLE_BIST_ERROR : return "Built-in self-test detected unfixable error";
        case FPA_CHECKSUM_ERROR       : return "Checksum doesn't fits received data";
        case FPA_DSA_PARSING_ERROR    : return "DSA tag parsing error";
        default: break;
    }

    return "Unknown error";
}

const char *
ops_fpa_str_vlan_mode(int vlan_mode)
{
    switch (vlan_mode) {
        case PORT_VLAN_ACCESS: return "access";
        case PORT_VLAN_TRUNK: return "trunk";
        case PORT_VLAN_NATIVE_TAGGED: return "native_tagged";
        case PORT_VLAN_NATIVE_UNTAGGED: return "native_untagged";
        default: break;
    }
    return "invalid";
}

const char *
ops_fpa_str_trunks(unsigned long *trunks)
{
    static char str[128];
    if (trunks) {
        str[0] = 0;
        int vid = -1;
        BITMAP_FOR_EACH_1(vid, VLAN_BITMAP_SIZE, trunks) {
            sprintf(str + strlen(str), "%d,", vid);
        }
        if (strlen(str) > 0) {
            str[strlen(str) - 1] = 0;
        }
    } else {
        strcpy(str, "NULL");
    }
    return str;
}

const char *
ops_fpa_str_ports(ofp_port_t *ports, size_t size)
{
    static char str[128];
    str[0] = 0;
    if (size > 0) {
        for (int i = 0; i < size; i++) {
            sprintf(str + strlen(str), "%d,", ports[i]);
        }
        if (strlen(str) > 0) {
            str[strlen(str) - 1] = 0;
        }
    }

    return str;
}


int
ops_fpa_str2int(const char *s, int *i)
{
    char * e;

    if (!s) {
        return 1;
    }

    errno = 0;
    long int l = strtol(s, &e, 10);

    if (errno || s == e || *e != '\0' || l < INT_MIN || l > INT_MAX)
        return 1;

    *i = l;

    return 0;
}

char *
ops_fpa_ip2str(in_addr_t ipAddr)
{
    static char str[16];
    unsigned char *ip_addr_vec = (unsigned char*)&ipAddr;

    sprintf(str, "%d.%d.%d.%d", ip_addr_vec[0], ip_addr_vec[1], ip_addr_vec[2], ip_addr_vec[3]);
    return str;
}

uint32_t ops_fpa_ip4mask_to_prefix_len(in_addr_t ipMask)
{
    uint32_t prefixLen;

    prefixLen = (uint32_t)ffs(htonl((int)ipMask));
    /* if no set bit found - prefixLen remains 0 */
    if (prefixLen)
        prefixLen = OPS_FPA_MAX_IP_PREFIX + 1 - prefixLen;

    return prefixLen;
}

in_addr_t ops_fpa_prefix_len_to_ip4mask(uint32_t prefix)
{
    in_addr_t ip = 0;
    uint32_t i;

    for (i = 0; i < prefix; i++) {
        ip |= 1<<(31-i);
    }

    return ip;
}

