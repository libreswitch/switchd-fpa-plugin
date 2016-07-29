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
 * File: ops-fpa-mac-learning.c
 *
 * Purpose: This file contains OpenSwitch MAC learning related application code
 *          for the FPA SDK.
 */

#include <stdlib.h>
#include <unistd.h>
#include "hash.h"
#include "util.h"
#include <openvswitch/vlog.h>
#include "plugin-extensions.h"
#include "ops-fpa.h"
#include "ops-fpa-mac-learning.h"

VLOG_DEFINE_THIS_MODULE(ops_fpa_mac_learning);

/* MAC learning timer timeout in seconds. */
#define OPS_FPA_ML_TIMER_TIMEOUT 30

struct fpa_mac_learning* g_fpa_ml = NULL;
static struct vlog_rate_limit ml_rl = VLOG_RATE_LIMIT_INIT(5, 20);

static uint32_t fpa_hash_fdb_entry(const FPA_EVENT_ADDRESS_MSG_STC *fdb_entry);
static void *mac_learning_asic_events_handler(void *arg);
static void ops_fpa_mac_learning_mlearn_action_add(struct fpa_mac_learning *ml,
                                      FPA_EVENT_ADDRESS_MSG_STC *fdb_entry,
                                      uint32_t index,
                                      uint32_t reHashIndex,
                                      const mac_event event);
static void ops_fpa_mac_learning_process_mlearn(struct fpa_mac_learning *ml);

static unsigned int
normalize_idle_time(unsigned int idle_time)
{
    return (idle_time < 10 ? 10
            : idle_time > 3600 ? 3600
            : idle_time);
}

/* Creates and returns a new MAC learning table with an initial MAC aging
 * timeout of 'idle_time' seconds and an initial maximum of OPS_FPA_ML_DEFAULT_SIZE
 * entries. */
int
ops_fpa_mac_learning_create(struct fpa_dev *dev, struct fpa_mac_learning **p_ml)
{
    struct fpa_mac_learning *ml = NULL;
    FPA_STATUS status = FPA_OK;
    int idx = 0;
    struct plugin_extension_interface *extension = NULL;

    ovs_assert(dev);

    ml = xmalloc(sizeof *ml);
    hmap_init(&ml->table);
    ml->max_entries = OPS_FPA_ML_DEFAULT_SIZE;
    ml->dev = dev;
    ml->idle_time = normalize_idle_time(OPS_FPA_ML_ENTRY_DEFAULT_IDLE_TIME);

    ovs_refcount_init(&ml->ref_cnt);
    ovs_rwlock_init(&ml->rwlock);
    latch_init(&ml->exit_latch);   
    ml->plugin_interface = NULL;
    ml->curr_mlearn_table_in_use = 0;

    for (idx = 0; idx < OPS_FPA_ML_NUM_BUFFERS; idx++) {
        hmap_init(&(ml->mlearn_event_tables[idx].table));
        ml->mlearn_event_tables[idx].buffer.actual_size = 0;
        ml->mlearn_event_tables[idx].buffer.size = BUFFER_SIZE;
        hmap_reserve(&(ml->mlearn_event_tables[idx].table), BUFFER_SIZE);
    }

    if (find_plugin_extension(MAC_LEARNING_PLUGIN_INTERFACE_NAME,
                              MAC_LEARNING_PLUGIN_INTERFACE_MAJOR,
                              MAC_LEARNING_PLUGIN_INTERFACE_MINOR,
                              &extension) == 0) {
        if (extension) {
            ml->plugin_interface = extension->plugin_interface;
        }
    }

    /* Configure FDB aging time */
    status = fpaLibSwitchAgingTimeoutSet(ml->dev->switchId, ml->idle_time);
    if (status != FPA_OK) {
        VLOG_ERR("Failed to set aging time. Status: %s", ops_fpa_strerr(status));
        return EPERM;
    }

    timer_set_duration(&ml->mlearn_timer, OPS_FPA_ML_TIMER_TIMEOUT * 1000);

    ml->ml_asic_thread = ovs_thread_create("ops-fpa-ml-asic-ev",
                                      mac_learning_asic_events_handler, ml);
    VLOG_INFO("FDB events processing thread started");

    status = fpaLibSwitchSrcMacLearningSet(ml->dev->switchId, FPA_SRCMAC_LEARNING_AUTO_E);
    if (status != FPA_OK) {
        /* TODO: abort can be changed with some log if ofproto init fail 
         *shouldn't cause crash of OVS */
        VLOG_ERR("Failed to set ASIC mac learning mode. Status: %s", ops_fpa_strerr(status));
        return EPERM;
    }

    *p_ml = ml;
    return 0;
}

/* Unreferences (and possibly destroys) MAC learning table 'ml'. */
void
ops_fpa_mac_learning_unref(struct fpa_mac_learning *ml)
{
    if (ml && ovs_refcount_unref(&ml->ref_cnt) == 1) {

        latch_set(&ml->exit_latch);
        xpthread_join(ml->ml_asic_thread, NULL);

        ops_fpa_mac_learning_flush(ml);
        hmap_destroy(&ml->table);

        latch_destroy(&ml->exit_latch);

        ovs_rwlock_destroy(&ml->rwlock);

        free(ml);
    }
}

/* Gets the MAC aging timeout from FPA */
int
ops_fpa_mac_learning_get_idle_time(struct fpa_mac_learning *ml,
                                  unsigned int *idle_time)
{
    FPA_STATUS status = FPA_OK;

    status = fpaLibSwitchAgingTimeoutGet(ml->dev->switchId, idle_time);
    if (status != FPA_OK) {
        VLOG_ERR("Failed to get aging time. Status: %s", ops_fpa_strerr(status));
        return EPERM;
    }

    return 0;
}

/* Changes the MAC aging timeout of 'ml' to 'idle_time' seconds. */
int
ops_fpa_mac_learning_set_idle_time(struct fpa_mac_learning *ml,
                                  unsigned int idle_time)
{
    FPA_STATUS status = FPA_OK;

    idle_time = normalize_idle_time(idle_time);

    status = fpaLibSwitchAgingTimeoutSet(ml->dev->switchId, idle_time);
    if (status != FPA_OK) {
        VLOG_ERR("Failed to set aging time. Status: %s", ops_fpa_strerr(status));
        return EPERM;
    }

    ml->idle_time = idle_time;

    return 0;
}

/* Sets the maximum number of entries in 'ml' to 'max_entries', adjusting it
 * to be within a reasonable range. */
void
ops_fpa_mac_learning_set_max_entries(struct fpa_mac_learning *ml,
                                    size_t max_entries)
{
    ml->max_entries = (max_entries < 10 ? 10
                       : max_entries > 1000 * 1000 ? 1000 * 1000
                       : max_entries);
}

/* Returns true if 'src_mac' may be learned on 'vlan' for 'ml'.
 * Returns false if src_mac is not valid for learning, or if 'vlan' is 
 * configured on 'ml' to flood all packets. */
bool
ops_fpa_mac_learning_may_learn(const struct fpa_mac_learning *ml,
                              const FPA_MAC_ADDRESS_STC src_mac, uint16_t vlan)
{
    bool is_learning = true;

    ovs_assert(ml);

/*    ovs_rwlock_rdlock(&ml->dev->vlan_mgr->rwlock);
    is_learning = ops_fpa_vlan_is_learning(ml->dev->vlan_mgr, vlan);
    ovs_rwlock_unlock(&ml->dev->vlan_mgr->rwlock);*/

    return is_learning; /*TODO: check if we can be called with multicast MAC*/
}

/* Inserts a new entry into mac learning table.
 * In case of fail - releases memory allocated for the entry and
 * removes correspondent entry from the hardware table. */
int
ops_fpa_mac_learning_insert(struct fpa_mac_learning *ml, struct fpa_mac_entry *e)
{
    uint32_t index;
    uint32_t reHashIndex = 0; /*TODO remove*/

    ovs_assert(ml);
    ovs_assert(e);

    if (hmap_count(&ml->table) >= ml->max_entries) {
        VLOG_WARN_RL(&ml_rl, "%s: Unable to insert entry for VLAN %d "
                             "and MAC: " FPA_ETH_ADDR_FMT
                             " to the software FDB table. The table is full\n",
                     __FUNCTION__, e->fdb_entry.vid, 
                     FPA_ETH_ADDR_ARGS(e->fdb_entry.address));
        free(e);
        return EPERM;
    }

    index = fpa_hash_fdb_entry(&e->fdb_entry);
    hmap_insert(&ml->table, &e->hmap_node, index);
    VLOG_DBG_RL(&ml_rl, "Inserted new entry into ML table: VLAN %d, "
                        "MAC: " FPA_ETH_ADDR_FMT ", Intf ID: %u, index 0x%lx\n",
                e->fdb_entry.vid,
                FPA_ETH_ADDR_ARGS(e->fdb_entry.address),
                e->fdb_entry.portNum,
                e->hmap_node.hash);

    ops_fpa_mac_learning_mlearn_action_add(ml, &e->fdb_entry,
                                          index, reHashIndex, MLEARN_ADD);

    return 0;
}

static uint32_t fpa_hash_fdb_entry(const FPA_EVENT_ADDRESS_MSG_STC *fdb_entry)
{
    char buf[sizeof(fdb_entry->vid)+sizeof(fdb_entry->address)];
    char *p = buf;

    memcpy(p, &fdb_entry->vid, sizeof(fdb_entry->vid));
    p += sizeof(fdb_entry->vid);
    memcpy(p, &fdb_entry->address, sizeof(fdb_entry->address));

    return hash_bytes(buf, sizeof(fdb_entry->vid)+sizeof(fdb_entry->address), 0);
}

struct fpa_mac_entry *
ops_fpa_mac_learning_lookup(const struct fpa_mac_learning *ml,
                           FPA_EVENT_ADDRESS_MSG_STC *fdb_entry)
{
    struct hmap_node *node;
    struct fpa_mac_entry *s;
    uint32_t hash = fpa_hash_fdb_entry(fdb_entry);

    ovs_assert(ml);

/*    node = hmap_first_with_hash(&ml->table, hash);
    if (node) {
        return CONTAINER_OF(node, struct fpa_mac_entry, hmap_node);
    }*/
    for (node = hmap_first_with_hash(&ml->table, hash); node;
         node = hmap_next_with_hash(node)) {
        s = CONTAINER_OF(node, struct fpa_mac_entry, hmap_node);
        if (!memcmp(&s->fdb_entry.address, &fdb_entry->address, sizeof(fdb_entry->address)) &&
            s->fdb_entry.vid == fdb_entry->vid) {
            return s;
        }    
    }    

    return NULL;
}

struct fpa_mac_entry *
ops_fpa_mac_learning_lookup_by_vlan_and_mac(const struct fpa_mac_learning *ml,
                                            uint16_t vlan_id, FPA_MAC_ADDRESS_STC macAddr)
{
    struct fpa_mac_entry *e = NULL;
    ovs_assert(ml);

    HMAP_FOR_EACH (e, hmap_node, &ml->table) {
        if (!memcmp(&e->fdb_entry.address, &macAddr, ETH_ADDR_LEN) &&
            (e->fdb_entry.vid == vlan_id)) {

            return e;
        }
    }

    return NULL;
}

/* Expires 'e' from the 'ml' hash table */
int
ops_fpa_mac_learning_expire(struct fpa_mac_learning *ml, struct fpa_mac_entry *e)
{
    ovs_assert(ml);
    ovs_assert(e);

    hmap_remove(&ml->table, &e->hmap_node);

    VLOG_DBG_RL(&ml_rl, "Expire entry in ML table: VLAN %d, "
                        "MAC: " FPA_ETH_ADDR_FMT ", Intf ID: %u, index 0x%lx\n",
                e->fdb_entry.vid,
                FPA_ETH_ADDR_ARGS(e->fdb_entry.address),
                e->fdb_entry.portNum,
                e->hmap_node.hash);

    ops_fpa_mac_learning_mlearn_action_add(ml, &e->fdb_entry,
                                          e->hmap_node.hash,
                                          e->hmap_node.hash, MLEARN_DEL);
    free(e);

    return 0;
}

/* Expires all the mac-learning entries in 'ml' */
void
ops_fpa_mac_learning_flush(struct fpa_mac_learning *ml)
{
    struct fpa_mac_entry *e = NULL;
    struct fpa_mac_entry *next = NULL;

    ovs_assert(ml);

    HMAP_FOR_EACH_SAFE (e, next, hmap_node, &ml->table) {
        ops_fpa_mac_learning_expire(ml, e);
    }
}

/* Installs entry into hardware FDB table and then in the software table. 
 * After that releases the memory allocated for the members of data. */
int
ops_fpa_mac_learning_learn(struct fpa_mac_learning *ml,
                          FPA_EVENT_ADDRESS_MSG_STC *data)
{
    struct fpa_mac_entry *e = xmalloc(sizeof(*e));

    ovs_assert(ml);
    ovs_assert(data);

    if (!ops_fpa_mac_learning_may_learn(ml, data->address,
                                       data->vid)) {
        VLOG_WARN_RL(&ml_rl, "%s: Either learning is disabled on the VLAN %u"
                             " or wrong MAC: "FPA_ETH_ADDR_FMT" has to be learned."
                             " Skipping.",
                     __FUNCTION__, data->vid,
                     FPA_ETH_ADDR_ARGS(data->address));
        return EPERM;
    }

    /* Add new entry to software FDB */
    memcpy(&e->fdb_entry, data,
           sizeof(e->fdb_entry));
    e->port.p = NULL;

    return ops_fpa_mac_learning_insert(ml, e);

}

/* Removes entry from the software and hardware FDB tables using its index. */
int
ops_fpa_mac_learning_age_by_entry(struct fpa_mac_learning *ml, 
                                        FPA_EVENT_ADDRESS_MSG_STC *fdb_entry)
{
    struct fpa_mac_entry *e = NULL;

    ovs_assert(ml);

    e = ops_fpa_mac_learning_lookup(ml, fdb_entry);

    if (e) {
        return ops_fpa_mac_learning_expire(ml, e);
    }

    return 0;
}

    OVS_REQ_WRLOCK(ml->rwlock);
/* Removes entry from the software and hardware FDB tables using
 * VLAN and MAC. */
int
ops_fpa_mac_learning_age_by_vlan_and_mac(struct fpa_mac_learning *ml,
                                        uint16_t vlan, FPA_MAC_ADDRESS_STC macAddr)
{
    struct fpa_mac_entry *e = NULL;

    ovs_assert(ml);

    e = ops_fpa_mac_learning_lookup_by_vlan_and_mac(ml, vlan, macAddr);

    if (e) {
        return ops_fpa_mac_learning_expire(ml, e);
    } else {
        VLOG_WARN_RL(&ml_rl, "%s: No entry with VLAN %d "
                             "and MAC: " FPA_ETH_ADDR_FMT
                             " found in FDB\n",
                     __FUNCTION__, vlan,
                     FPA_ETH_ADDR_ARGS(macAddr));
        return EPERM;
    }

    return 0;
}

/* This handler thread receives incoming learning events
 * of different types and handles them correspondingly. */
static void *
mac_learning_asic_events_handler(void *arg)
{
    struct fpa_mac_learning *ml = arg;
    FPA_STATUS err;
    FPA_EVENT_ADDRESS_MSG_STC msg;

    ovs_assert(ml);


    /* Receiving and processing events loop. */
/*TODO test
    while (!latch_is_set(&ml->exit_latch)) {*/
    while (1) {

        memset(&msg, 0x0, sizeof(FPA_EVENT_ADDRESS_MSG_STC)); /*TODO: check if need */
        err = fpaLibBridgingAuMsgGet(ml->dev->switchId, false, &msg);
        if ((err != FPA_OK) && (err != FPA_NO_MORE)) {
            VLOG_ERR_RL(&ml_rl, "%s: %s", __FUNCTION__, ops_fpa_strerr(err)); 
        }
        else if (err == FPA_OK) {
            /*struct fpa_ml_learning_data learning_data;*/
            
            VLOG_INFO("AuMsg: type:%d, port:%d, vid:%d, MAC "FPA_ETH_ADDR_FMT, msg.type, msg.portNum, msg.vid, 
                      FPA_ETH_ADDR_ARGS(msg.address)); 

            switch (msg.type) {
            case FPA_EVENT_ADDRESS_UPDATE_NEW_E:
                ovs_rwlock_wrlock(&ml->rwlock);
                ops_fpa_mac_learning_learn(ml, &msg);
                ovs_rwlock_unlock(&ml->rwlock);
                break;

            case FPA_EVENT_ADDRESS_UPDATE_AGED_E:
                VLOG_INFO_RL(&ml_rl, "TODO aging message reseived");
                ops_fpa_mac_learning_age_by_entry(ml, &msg);
                break;

            default:
                VLOG_ERR_RL(&ml_rl, "%s: illegal msg.type: %d", __FUNCTION__, msg.type);
                break;
            }
        }
        else {
            VLOG_DBG_RL(&ml_rl, "%s: empty AuMsg queue", __FUNCTION__);
        }

        /* TODO: another thread "event_thread" to be created for servicing all ML events: NA, aging,
         * flushing interfaces/VLAN, static MAC.
         * Current thread will just send events to event_thread */

    }

    VLOG_INFO("FDB events processing thread finished");

    return NULL;
}

void
ops_fpa_mac_learning_dump_table(struct fpa_mac_learning *ml, struct ds *d_str)
{
    const struct fpa_mac_entry *e = NULL;

    ovs_assert(ml);
    ovs_assert(d_str);

    ds_put_cstr(d_str, " port    VLAN  MAC                index\n");

    HMAP_FOR_EACH(e, hmap_node, &ml->table) {
        if (e) {
            char iface_name[PORT_NAME_SIZE];

            snprintf(iface_name, PORT_NAME_SIZE, "%u", e->fdb_entry.portNum); /*TODO check if extra conversion needed */

            ds_put_format(d_str, "%-8s %4d  "FPA_ETH_ADDR_FMT"  0x%lx\n",
                          iface_name,
                          e->fdb_entry.vid, 
                          FPA_ETH_ADDR_ARGS(e->fdb_entry.address),
                          e->hmap_node.hash);
        }
    }
}

/* Checks if the hmap has reached it's capacity or not. */
static bool
ops_fpa_mac_learning_mlearn_table_is_full(const struct mlearn_hmap *mhmap)
{
    return (mhmap->buffer.actual_size == mhmap->buffer.size);
}

/* Clears the hmap and the buffer for storing the hmap nodes. */
static void
ops_fpa_mac_learning_clear_mlearn_hmap(struct mlearn_hmap *mhmap)
{
    if (mhmap) {
        memset(&(mhmap->buffer), 0, sizeof(mhmap->buffer));
        mhmap->buffer.size = BUFFER_SIZE;
        hmap_clear(&(mhmap->table));
    }
}

/* Fills mlearn_hmap_node fields. */
static void
ops_fpa_mac_learning_mlearn_entry_fill_data(struct fpa_dev *dev,
                                           struct mlearn_hmap_node *entry,
                                           FPA_EVENT_ADDRESS_MSG_STC *fdb_entry,
                                           const mac_event event)
{
    ovs_assert(dev);
    ovs_assert(entry);
    ovs_assert(fdb_entry);

    /*ops_fpa_mac_copy_and_reverse(entry->mac.ea, fdb_entry->address.addr);*/
    memcpy(entry->mac.ea, fdb_entry->address.addr, ETH_ADDR_LEN);
    entry->port = PORT_CONVERT_FPA2OPS(fdb_entry->portNum); /*TODO: convert*/
    entry->vlan = fdb_entry->vid;
    entry->hw_unit = dev->switchId;
    entry->oper = event;

    snprintf(entry->port_name, PORT_NAME_SIZE, "%u", entry->port);
}

/* Adds the action entry in the mlearn_event_tables hmap.
 *
 * If the entry is already present, it is modified or else it's created.
 */
/* TODO: reHashIndex usage to be revised */
static void
ops_fpa_mac_learning_mlearn_action_add(struct fpa_mac_learning *ml,
                                      FPA_EVENT_ADDRESS_MSG_STC *fdb_entry,
                                      uint32_t index,
                                      uint32_t reHashIndex,
                                      const mac_event event)
    OVS_REQ_WRLOCK(ml->rwlock)
{
    struct mlearn_hmap_node *e;
    struct hmap_node *node;
    struct mlearn_hmap *mhmap;
    int actual_size = 0;

    ovs_assert(ml);
    ovs_assert(fdb_entry);

    mhmap = &ml->mlearn_event_tables[ml->curr_mlearn_table_in_use];
    actual_size = mhmap->buffer.actual_size;

    node = hmap_first_with_hash(&mhmap->table, index);
    if (node) {
        /* Entry already exists - just fill it with new data. */
        e = CONTAINER_OF(node, struct mlearn_hmap_node, hmap_node);
#if 0 /*TODO delete */
        if (index != reHashIndex) {
            /* Rehasing occured - move an old entry to a new place. */
            if (actual_size < mhmap->buffer.size) {
                struct mlearn_hmap_node *new_e =
                                    &(mhmap->buffer.nodes[actual_size]);

                memcpy(new_e, e, sizeof(*new_e));
                hmap_insert(&mhmap->table, &(new_e->hmap_node), reHashIndex);
                mhmap->buffer.actual_size++;
            }  else {
                VLOG_ERR("Not able to insert elements in hmap, size is: %u\n",
                         mhmap->buffer.actual_size);
            }
        }
#endif
        ops_fpa_mac_learning_mlearn_entry_fill_data(ml->dev, e,
                                                   fdb_entry, event);
    } else {

        /* Entry doesn't exist - add a new one. */
        if (actual_size < mhmap->buffer.size) {
            e = &(mhmap->buffer.nodes[actual_size]);
            ops_fpa_mac_learning_mlearn_entry_fill_data(ml->dev, e,
                                                       fdb_entry, event);
            hmap_insert(&mhmap->table, &(e->hmap_node), index);
            mhmap->buffer.actual_size++;
        } else {
            VLOG_ERR("Not able to insert elements in hmap, size is: %u\n",
                      mhmap->buffer.actual_size);
        }
    }

    /* Notify vswitchd */
    if (ops_fpa_mac_learning_mlearn_table_is_full(mhmap)) {
        ops_fpa_mac_learning_process_mlearn(ml);
    }
}

/* Main processing function for OPS mlearn tables.
 *
 * This function will be invoked when either of the two conditions
 * are satisfied:
 * 1. current in use hmap for storing all macs learnt is full
 * 2. timer thread times out
 *
 * This function will check if there is any new MACs learnt, if yes,
 * then it triggers callback from bridge.
 * Also it changes the current hmap in use.
 *
 * current_hmap_in_use = current_hmap_in_use ^ 1 is used to toggle
 * the current hmap in use as the buffers are 2.
 */
static void
ops_fpa_mac_learning_process_mlearn(struct fpa_mac_learning *ml)
    OVS_REQ_WRLOCK(ml->rwlock)
{
    if (ml && ml->plugin_interface) {
        if (hmap_count(&(ml->mlearn_event_tables[ml->curr_mlearn_table_in_use].table))) {
            ml->plugin_interface->mac_learning_trigger_callback();
            ml->curr_mlearn_table_in_use = ml->curr_mlearn_table_in_use ^ 1;
            ops_fpa_mac_learning_clear_mlearn_hmap(&ml->mlearn_event_tables[ml->curr_mlearn_table_in_use]);
        }
    } else {
        VLOG_ERR("%s: Unable to find mac learning plugin interface",
                 __FUNCTION__);
    }
}

/* Mlearn timer expiration handler. */
void
ops_fpa_mac_learning_on_mlearn_timer_expired(struct fpa_mac_learning *ml)
{
    if (ml) {
        ovs_rwlock_wrlock(&ml->rwlock);
        ops_fpa_mac_learning_process_mlearn(ml);
        timer_set_duration(&ml->mlearn_timer, OPS_FPA_ML_TIMER_TIMEOUT * 1000);
        ovs_rwlock_unlock(&ml->rwlock);
    }
}

int
ops_fpa_ml_hmap_get(struct mlearn_hmap **mhmap)
{
    struct fpa_dev *dev = ops_fpa_dev_by_id(FPA_DEV_SWITCH_ID_DEFAULT);
    struct fpa_mac_learning *ml = dev->ml;

    ovs_assert(ml);

    if (!mhmap) {
        VLOG_ERR("%s: Invalid argument", __FUNCTION__);
        /*TODO enable when be ready 
        ops_fpa_dev_free(dev);*/
        return EINVAL;
    }

    ovs_rwlock_rdlock(&ml->rwlock);
    if (hmap_count(&(ml->mlearn_event_tables[ml->curr_mlearn_table_in_use ^ 1].table))) {
        *mhmap = &ml->mlearn_event_tables[ml->curr_mlearn_table_in_use ^ 1];
    } else {
        *mhmap = NULL;
    }
    ovs_rwlock_unlock(&ml->rwlock);

    /*TODO enable when be ready 
    ops_fpa_dev_free(dev);*/

    return 0;
}
