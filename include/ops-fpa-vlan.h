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

#ifndef OPS_FPA_VLAN_H
#define OPS_FPA_VLAN_H 1

#include "ops-fpa.h"

/*
    To fully describe all possible port vlan modes we introduce
    extended VLAN ID, or VIDX for short. VIDX is 14-bit value,
    which separately describes ingress and egress VID properties.

                | 1 bit | 1 bit  | 12 bits |
        --------|-------|--------|---------|
        Ingress |   0   | TAGGED |   VID   |
        --------|-------|--------|---------|
        Egress  |   1   | POPTAG |   VID   |

      TAGGED - match tagged/untagged
      POPTAG - pop/keep tag

    VIDX bitmap (VMAP) allows us to process any vlan mode changes
    in a uniform, bitwise manner.
*/

#define OPS_FPA_VMAP_BITS                 (1 << 14)
#define OPS_FPA_VMAP_LONGS                BITMAP_N_LONGS(OPS_FPA_VMAP_BITS)
#define OPS_FPA_VMAP_BYTES                bitmap_n_bytes(OPS_FPA_VMAP_BITS)
#define OPS_FPA_VIDX_INGRESS(VID, TAGGED) ((0 << 13) | ((TAGGED) << 12) | (VID))
#define OPS_FPA_VIDX_EGRESS(VID, POPTAG)  ((1 << 13) | ((POPTAG) << 12) | (VID))
#define OPS_FPA_VIDX_VID(VIDX)            ((VIDX) & ((1 << 12) - 1))
#define OPS_FPA_VIDX_IS_EGRESS(VIDX)      ((VIDX) & (1 << 13))
#define OPS_FPA_VIDX_ARG(VIDX)            ((VIDX) & (1 << 12))

/* fetch l2 state of FPA port 'pid' into 'vmap' bitmap */
void ops_fpa_vlan_fetch(int sid, int pid, unsigned long *vmap);
/* add vidx encoded flow to FPA */
int ops_fpa_vlan_add(int sid, int pid, int vidx);
/* delete vidx encoded flow form FPA */
int ops_fpa_vlan_del(int sid, int pid, int vidx);
/* return true if 'vid' is internal VLAN ID, false otherwise */
bool ops_fpa_vlan_internal(int vid);
/* add flows for port 'pid' on switch 'sid' with internal VLAN ID 'vid' */
int ops_fpa_vlan_add_internal(int sid, int pid, int vid);
/* delete flows with VLAN ID 'vid' for port 'pid' on switch 'sid' */
int ops_fpa_vlan_del_internal(int sid, int pid, int vid);

#endif /* OPS_FPA_VLAN_H */
