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
 * File: ops-fpa-mac-learning.h
 *
 * Purpose: This file provides public definitions for OpenSwitch MAC learning
 *          related application code for the FPA SDK.
 */

#ifndef OPS_FPA_MAC_LEARNING_H
#define OPS_FPA_MAC_LEARNING_H 1

#include "hmap.h"
#include "latch.h"
#include "timer.h"
#include "mac-learning.h"
#include "mac-learning-plugin.h"

#include "ops-fpa-dev.h"

/* Default maximum size of a MAC learning table, in entries. */
#define OPS_FPA_ML_DEFAULT_SIZE  MAC_DEFAULT_MAX

/* Time, in seconds, before expiring a MAC entry due to inactivity. */
#define OPS_FPA_ML_ENTRY_DEFAULT_IDLE_TIME  MAC_ENTRY_DEFAULT_IDLE_TIME

/* The buffers are defined as 2 in order to allow simultaneous read access to
 * bridge.c and ops-fpa-mac-learning.c code from different threads.
 */
#define OPS_FPA_ML_NUM_BUFFERS   2

/* A MAC learning table entry.
 * Guarded by owning 'fpa_mac_learning''s rwlock */
struct fpa_mac_entry {
    struct hmap_node hmap_node; /* Node in a fpa_mac_learning hmap. */

    FPA_EVENT_ADDRESS_MSG_STC fdb_entry;
    /* The following are marked guarded to prevent users from iterating over or
     * accessing a fpa_mac_entry without holding the parent fpa_mac_learning rwlock. */
    /* Learned port. */
    union {
        void *p;
        ofp_port_t ofp_port;
    } port OVS_GUARDED;
};

/* MAC learning table. */
struct fpa_mac_learning {
    struct hmap table;              /* Learning table. */
    unsigned int idle_time;         /* Max age before deleting an entry. */
    size_t max_entries;             /* Max number of learned MACs. */
    struct ovs_refcount ref_cnt;
    struct ovs_rwlock rwlock;
    pthread_t ml_asic_thread;       /* ML Thread ID. */
    struct latch exit_latch;     /* Tells child threads to exit. */
    struct fpa_dev *dev;
    /* Tables which store mac learning events destined for main
     * processing in OPS mac-learning-plugin. */
    struct mlearn_hmap mlearn_event_tables[OPS_FPA_ML_NUM_BUFFERS];
    /* Index of a mlearn table which is currently in use. */
    int curr_mlearn_table_in_use;
    struct timer mlearn_timer;
    struct mac_learning_plugin_interface *plugin_interface;
};

typedef enum {
    OPS_FPA_ML_LEARNING_EVENT,
    OPS_FPA_ML_AGING_EVENT,
    OPS_FPA_ML_PORT_DOWN_EVENT,
    OPS_FPA_ML_VLAN_REMOVED_EVENT
} fpa_ml_event_type;

struct fpa_ml_event {
    fpa_ml_event_type type;
    union fpa_ml_event_data {
        /*struct fpa_ml_learning_data learning_data;*/
        FPA_EVENT_ADDRESS_MSG_STC msg;
        uint32_t index;
        uint32_t portNum;
        uint16_t vid;
    } data;
};

int ops_fpa_mac_learning_create(struct fpa_dev *dev, struct fpa_mac_learning **p_ml);

void ops_fpa_mac_learning_unref(struct fpa_mac_learning *);

int ops_fpa_mac_learning_get_idle_time(struct fpa_mac_learning *ml,
                                      unsigned int *idle_time);
int ops_fpa_mac_learning_set_idle_time(struct fpa_mac_learning *ml,
                                      unsigned int idle_time)
    OVS_REQ_WRLOCK(ml->rwlock);

void ops_fpa_mac_learning_flush(struct fpa_mac_learning *ml)
    OVS_REQ_WRLOCK(ml->rwlock);

void ops_fpa_mac_learning_dump_table(struct fpa_mac_learning *ml,
                                    struct ds *d_str);

void ops_fpa_mac_learning_on_mlearn_timer_expired(struct fpa_mac_learning *ml);

int ops_fpa_ml_hmap_get(struct mlearn_hmap **mhmap);

struct fpa_mac_entry *
ops_fpa_mac_learning_lookup(const struct fpa_mac_learning *ml,
                            FPA_EVENT_ADDRESS_MSG_STC *fdb_entry)
    OVS_REQ_RDLOCK(ml->rwlock);

struct fpa_mac_entry *
ops_fpa_mac_learning_lookup_by_vlan_and_mac(const struct fpa_mac_learning *ml,
                                            uint16_t vlan_id,
                                            FPA_MAC_ADDRESS_STC macAddr)
    OVS_REQ_RDLOCK(ml->rwlock);

#endif /* OPS_FPA_MAC_LEARNING_H */
