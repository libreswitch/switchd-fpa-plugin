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

#include "ops-fpa-ofproto.h"

#include <assert.h>
#include <netdev.h>
#include <netinet/ether.h>
#include <openswitch-idl.h>
#include <vlan-bitmap.h>
#include "connectivity.h"
#include "ops-fpa-dev.h"
#include "ops-fpa-mac-learning.h"
#include "ops-fpa-routing.h"
#include "seq.h"
#include "unixctl.h"

/* TODO: Move all functions from this header into ops-fpa-ofproto.c. */
#include "ops-fpa-route.h"

VLOG_DEFINE_THIS_MODULE(ops_fpa_ofproto);

static void *l3_index_resource = NULL;
static struct hmap host_table;

struct host_table_entry
{
    struct hmap_node node;
    in_addr_t ipv4_addr;
    uint32_t arp_index;
    uint32_t l3_group;
    uint32_t l2_group;
};

extern FPA_STATUS
marvell_fpa_add_route_trap (uint32_t switchId, in_addr_t dstIp4,
                            uint32_t mask_len);

static int delete_l3_host_entry(const struct ofproto *ofproto, void *aux,
                                bool is_ipv6_addr, char *ip_addr,
                                int *l3_egress_id);

static int add_l3_host_entry(const struct ofproto *ofproto, void *aux,
                             bool is_ipv6_addr, char *ip_addr,
                             char *next_hop_mac_addr, int *l3_egress_id);

static struct hmap protos = HMAP_INITIALIZER(&protos);
static struct hmap ports_by_pid = HMAP_INITIALIZER(&ports_by_pid);

struct ofport_fpa *ops_fpa_get_ofport_by_pid(int pid)
{
    struct hmap_node *node = hmap_first_in_bucket(&ports_by_pid, pid);
    return node ? CONTAINER_OF(node, struct ofport_fpa, node) : NULL;
}

static void ops_fpa_ofproto_unixctl_init(void);

static struct ofproto_fpa *
ops_fpa_proto_cast(const struct ofproto *proto)
{
    return CONTAINER_OF(proto, struct ofproto_fpa, up);
}

static struct ofport_fpa *
ops_fpa_port_cast(const struct ofport *port)
{
    return CONTAINER_OF(port, struct ofport_fpa, up);
}

static struct ofrule_fpa *
ops_fpa_rule_cast(const struct rule *rule)
{
    return CONTAINER_OF(rule, struct ofrule_fpa, up);
}

/*
 * Factory Functions
 */

struct ofport_fpa*
ops_fpa_get_ofproto_fpa_port(const struct ofproto *ofproto, ofp_port_t ofp_port)
{
    struct ofport *ofport = ofproto_get_port(ofproto, ofp_port);
    return ofport ? ops_fpa_port_cast(ofport) : NULL;
}

static void
ops_fpa_ofproto_init(const struct shash *iface_hints)
{
    FPA_TRACE_FN();

    /* Perform FPA initialization. */
    ops_fpa_init();

    /* Perform L3 logic initialization. */
    l3_index_resource = ops_marvell_utils_resource_init(MARVELL_FPA_NH);
    assert(l3_index_resource != NULL);
    hmap_init(&host_table);
}

static void
ops_fpa_ofproto_enumerate_types(struct sset *types)
{
    sset_add(types, "system");
    sset_add(types, "vrf");
}

static int
ops_fpa_ofproto_enumerate_names(const char *type, struct sset *names)
{
    struct ofproto_fpa *p;
    HMAP_FOR_EACH(p, node, &protos) {
        if (!STR_EQ(type, p->up.type)) {
            continue;
        }
        sset_add(names, p->up.type);
    }
    return 0;
}

static int
ops_fpa_ofproto_del(const char *type, const char *name)
{
    //VLOG_INFO("ops_fpa_ofproto_del: %s %s", type, name);
    return 0;
}

static const char *
ops_fpa_ofproto_port_open_type(const char *datapath_type, const char *port_type)
{
    //VLOG_INFO("ops_fpa_ofproto_port_open_type: datapath_type=%s port_type=%s", datapath_type, port_type);
    if (STR_EQ(port_type, OVSREC_INTERFACE_TYPE_INTERNAL) ||
        STR_EQ(port_type, OVSREC_INTERFACE_TYPE_VLANSUBINT) ||
        STR_EQ(port_type, OVSREC_INTERFACE_TYPE_LOOPBACK)) {
        return port_type;
    }

    return "system";
}

/*
 * Top-Level type Functions.
 */

static int
ops_fpa_ofproto_type_run(const char *type)
{
    return 0;
}

static void
ops_fpa_ofproto_type_wait(const char *type)
{
}

static bool
is_ofproto_vrf(const struct ofproto* ofproto)
{
    return STR_EQ(ofproto->type, "vrf");
}

/*
 * Top-Level ofproto Functions.
 */

static struct ofproto *
ops_fpa_ofproto_alloc(void)
{
    struct ofproto_fpa *p = xmalloc(sizeof *p);
    return &p->up;
}

static int
ops_fpa_ofproto_construct(struct ofproto *up)
{
    int err;
    struct ofproto_fpa *p = ops_fpa_proto_cast(up);

    VLOG_INFO("ops_fpa_ofproto_construct<%s,%s>", up->type, up->name);

    hmap_insert(&protos, &p->node, hash_string(up->name, 0));
    sset_init(&p->port_names);
    hmap_init(&p->bundles);
    ofproto_init_tables(up, FPA_FLOW_TABLE_MAX);

    memset(p->vlans, 0, sizeof(p->vlans));
/*    bitmap_set1(p->vlans, 1); */ /*OPS will later tell us that VID 1 is up */

    err = ops_fpa_dev_init(FPA_DEV_SWITCH_ID_DEFAULT, &p->dev);
    if (err) {
        VLOG_ERR("ops_fpa_dev_init: %s", ops_fpa_strerr(err));
        return err;
    }

    p->vrf_id = FPA_HAL_L3_DEFAULT_VRID;
    p->change_seq = 0;

    ops_fpa_ofproto_unixctl_init();

    return 0;
}

static void
ops_fpa_ofproto_destruct(struct ofproto *up)
{
    struct ofproto_fpa *p = ops_fpa_proto_cast(up);
    hmap_destroy(&p->bundles);
    sset_destroy(&p->port_names);
    hmap_remove(&protos, &p->node);

    ops_fpa_dev_deinit(FPA_DEV_SWITCH_ID_DEFAULT);
}

static void
ops_fpa_ofproto_dealloc(struct ofproto *up)
{
    free(ops_fpa_proto_cast(up));
}

static int
ops_fpa_ofproto_run(struct ofproto *up)
{
    struct ofproto_fpa *ofproto = ops_fpa_proto_cast(up);
    uint64_t new_seq = 0;

    new_seq = seq_read(connectivity_seq_get());
    if (ofproto->change_seq != new_seq) {
        ofproto->change_seq = new_seq;
    }

    if (STR_EQ(up->type, "system") && STR_EQ(up->name, DEFAULT_BRIDGE_NAME)) {
        if (timer_expired(&ofproto->dev->ml->mlearn_timer)) {
            ops_fpa_mac_learning_on_mlearn_timer_expired(ofproto->dev->ml);
        }
    }

    return 0;
}

static void
ops_fpa_ofproto_wait(struct ofproto *up)
{
}

static void
ops_fpa_ofproto_get_memory_usage(const struct ofproto *up, struct simap *usage)
{
}

static void
ops_fpa_ofproto_type_get_memory_usage(const char *type, struct simap *usage)
{
}

static void
ops_fpa_ofproto_flush(struct ofproto *up)
{
    FPA_TRACE_FN();
}

static void
ops_fpa_ofproto_query_tables(struct ofproto *up,
                             struct ofputil_table_features *features,
                             struct ofputil_table_stats *stats)
{
    FPA_TRACE_FN();
}

static void
ops_fpa_ofproto_set_tables_version(struct ofproto *up, cls_version_t version)
{
}

/*
 * OpenFlow Port Functions.
 */

static struct ofport *
ops_fpa_ofproto_port_alloc(void)
{
    struct ofport_fpa *p = xmalloc(sizeof *p);
    return &p->up;
}

static void
ops_fpa_ofproto_port_dealloc(struct ofport *up)
{
    FPA_TRACE_FN();
    struct ofport_fpa *p = ops_fpa_port_cast(up);
    free(p);
}

static int
ops_fpa_ofproto_port_construct(struct ofport *up)
{
    VLOG_INFO("ops_fpa_ofproto_port_construct<%s,%s>: ofp_port=%d",
              up->ofproto->type, up->ofproto->name, up->ofp_port);
    return 0;
}

static void
ops_fpa_ofproto_port_destruct(struct ofport *up)
{
    FPA_TRACE_FN();
}

static void
ops_fpa_ofproto_port_modified(struct ofport *up)
{
    //VLOG_INFO("ops_fpa_ofproto_port_modified<%s,%s>: ofp_port=%d", up->ofproto->type, up->ofproto->name, up->ofp_port);
}

static void
ops_fpa_ofproto_port_reconfigured(struct ofport *port,
                                  enum ofputil_port_config old_config)
{
    FPA_TRACE_FN();
}

static int
ops_fpa_ofproto_port_query_by_name(const struct ofproto *up,
                                   const char *devname,
                                   struct ofproto_port *port)
{
    //VLOG_INFO("ops_fpa_ofproto_port_query_by_name<%s,%s>: devname=%s", up->type, up->name, devname);

    struct ofproto_fpa *p = ops_fpa_proto_cast(up);
    if (!sset_contains(&p->port_names, devname)) {
        return ENODEV;
    }

    const char *type = netdev_get_type_from_name(devname);
    if (type) {
        struct ofport *ofport = shash_find_data(&up->port_by_name, devname);
        port->name = xstrdup(devname);
        port->type = xstrdup(type);
        port->ofp_port = ofport ? ofport->ofp_port : OFPP_NONE;
        return 0;
    }

    return ENODEV;
}

static int
ops_fpa_ofproto_port_add(struct ofproto *up, struct netdev *dev)
{
    const char *devname = netdev_get_name(dev);

    VLOG_INFO("ops_fpa_ofproto_port_add<%s,%s>: devname=%s", up->type, up->name,
              devname);

    struct ofproto_fpa *p = ops_fpa_proto_cast(up);
    sset_add(&p->port_names, devname);

    return 0;
}

static int
ops_fpa_ofproto_port_del(struct ofproto *up, ofp_port_t ofp_port)
{
    //VLOG_INFO("ops_fpa_ofproto_port_del: type=%s name=%s ofp_port=%d", up->type, up->name, ofp_port);
    //struct ofproto_fpa *p = ops_fpa_proto_cast(up);

    //struct sset port_names;

    //struct ofport *ofport = ofproto_get_port(&ofproto->up, ofp_port);
    //sset_find_and_delete(&ofproto->ghost_ports,netdev_get_name(ofport->up.netdev));
    //hmap_remove(&ofproto->dp_ports, &port->node);

    //p->port_names;

    return 0;
}

static int
ops_fpa_ofproto_port_get_stats(const struct ofport *port,
                               struct netdev_stats *stats)
{
    FPA_TRACE_FN();

    return netdev_get_stats(port->netdev, stats);
}

static int
ops_fpa_ofproto_port_dump_start(const struct ofproto *up, void **statep)
{
    //VLOG_INFO("ops_fpa_ofproto_port_dump_start: type=%s name=%s", up->type, up->name);
    *statep = xzalloc(sizeof(struct port_iter));
    return 0;
}

static int
ops_fpa_ofproto_port_dump_next(const struct ofproto *up, void *state,
                               struct ofproto_port *port)
{
    struct ofproto_fpa *p = ops_fpa_proto_cast(up);
    struct port_iter *iter = state;
    struct sset_node *node;

    //VLOG_INFO("ops_fpa_ofproto_port_dump_next:  type=%s name=%s bucket=%d offset=%d", up->type, up->name, iter->bucket, iter->offset);

    while ((node = sset_at_position(&p->port_names, &iter->bucket,
                                    &iter->offset))) {
        return ops_fpa_ofproto_port_query_by_name(up, node->name, port);
    }

    return EOF;
}

static int
ops_fpa_ofproto_port_dump_done(const struct ofproto *up, void *state)
{
    /*VLOG_INFO("ops_fpa_ofproto_port_dump_done:
                type=%s name=%s", up->type, up->name);*/
    free(state);
    return 0;
}

static int
ops_fpa_ofproto_port_poll(const struct ofproto *up, char **devnamep)
{
    FPA_TRACE_FN();
    return EAGAIN;
}

static void
ops_fpa_ofproto_port_poll_wait(const struct ofproto *up)
{
}

static int
ops_fpa_ofproto_port_is_lacp_current(const struct ofport *port)
{
    FPA_TRACE_FN();
    return 0;
}

static int
ops_fpa_ofproto_port_get_lacp_stats(const struct ofport *port,
                                    struct lacp_slave_stats *stats)
{
    FPA_TRACE_FN();
    return 0;
}

static enum ofperr
ops_fpa_ofproto_rule_choose_table(const struct ofproto *up,
                                  const struct match *match, uint8_t *table_idp)
{
    FPA_TRACE_FN();
    return OFPERR_OFS;
}

/*
 * OpenFlow Rule Functions
 */
#if 0
static struct rule *
ops_fpa_ofproto_rule_alloc(void)
{
    FPA_TRACE_FN();
    struct ofrule_fpa *r = xmalloc(sizeof *r);
    return &r->up;
}

static void
ops_fpa_ofproto_rule_dealloc(struct rule *up)
{
    FPA_TRACE_FN();
    free(ops_fpa_rule_cast(up));
}

static enum ofperr
ops_fpa_ofproto_rule_construct(struct rule *up)
{
    FPA_TRACE_FN();
    /*struct ofrule_fpa *r = ops_fpa_rule_cast(up);
    int err = fpaLibFlowEntryInit(0, up->table_id, &r->entry);
    if (err) {
        VLOG_ERR("fpaLibFlowEntryInit: %s", ops_fpa_strerr(err));
        return EINVAL;
    }*/

    return 0;
}

static void
ops_fpa_ofproto_rule_destruct(struct rule *rule)
{
    FPA_TRACE_FN();
}

static void
ops_fpa_ofproto_rule_insert(struct rule *rule, struct rule *old_rule,
                            bool forward_stats)
{
    FPA_TRACE_FN();
}

static void
ops_fpa_ofproto_rule_delete(struct rule *rule)
{
    FPA_TRACE_FN();
}

static void
ops_fpa_ofproto_rule_get_stats(struct rule *up, uint64_t *packet_count,
                               uint64_t *byte_count, long long int *used)
{
    FPA_TRACE_FN();
    FPA_FLOW_ENTRY_COUNTERS_STC counters;
    struct ofrule_fpa *r = ops_fpa_rule_cast(up);
    int err = fpaLibFlowEntryStatisticsGet(0, up->table_id, &r->entry,
                                           &counters);
    if (err) {
        VLOG_ERR("fpaLibFlowEntryStatisticsGet: %s", ops_fpa_strerr(err));
        return;
    }

    *packet_count = counters.packetCount;
    *byte_count = counters.byteCount;
    *used = counters.durationSec;
}

static enum ofperr
ops_fpa_ofproto_rule_execute(struct rule *rule, const struct flow *flow,
                             struct dp_packet *packet)
{
    FPA_TRACE_FN();
    return OFPERR_OFS;
}

static bool
ops_fpa_ofproto_set_frag_handling(struct ofproto *up,
                                  enum ofp_config_flags frag_handling)
{
    FPA_TRACE_FN();
    return false;
}

static enum ofperr
ops_fpa_ofproto_packet_out(struct ofproto *up, struct dp_packet *packet,
                           const struct flow *flow,
                           const struct ofpact *ofpacts, size_t ofpacts_len)
{
    FPA_TRACE_FN();
    return OFPERR_OFS;
}
#endif
/*
 * OFPP_NORMAL configuration.
 */
#if 0
static int
ops_fpa_ofproto_set_netflow(struct ofproto *up,
                            const struct netflow_options *netflow_options)
{
    FPA_TRACE_FN();
    return EOPNOTSUPP;
}

static void
ops_fpa_ofproto_get_netflow_ids(const struct ofproto *up, uint8_t *engine_type,
                                uint8_t *engine_id)
{
    FPA_TRACE_FN();
}

static int
ops_fpa_ofproto_set_sflow(struct ofproto *up,
                          const struct ofproto_sflow_options *sflow_options)
{
    return EOPNOTSUPP;
}

static int
ops_fpa_ofproto_set_ipfix(struct ofproto *up, const struct ofproto_ipfix_bridge_exporter_options *bridge_exporter_options, const struct ofproto_ipfix_flow_exporter_options *flow_exporters_options, size_t n_flow_exporters_options)
{
    FPA_TRACE_FN();
    return EOPNOTSUPP;
}

static int
ops_fpa_ofproto_set_cfm(struct ofport *port, const struct cfm_settings *s)
{
    FPA_TRACE_FN();
    return EOPNOTSUPP;
}

static bool
ops_fpa_ofproto_cfm_status_changed(struct ofport *port)
{
    FPA_TRACE_FN();
    return false;
}

static int
ops_fpa_ofproto_get_cfm_status(const struct ofport *port,
                               struct cfm_status *status)
{
    FPA_TRACE_FN();
    return EOPNOTSUPP;
}

static int
ops_fpa_ofproto_set_lldp(struct ofport *port, const struct smap *cfg)
{
    FPA_TRACE_FN();
    return EOPNOTSUPP;
}

static bool
ops_fpa_ofproto_get_lldp_status(const struct ofport *port,
                                struct lldp_status *status)
{
    FPA_TRACE_FN();
    return false;
}

static int
ops_fpa_ofproto_set_aa(struct ofproto *up, const struct aa_settings *s)
{
    FPA_TRACE_FN();
    return 0;
}

static int
ops_fpa_ofproto_aa_mapping_set(struct ofproto *up, void *aux,
                               const struct aa_mapping_settings *s)
{
    FPA_TRACE_FN();
    return 0;
}

static int
ops_fpa_ofproto_aa_mapping_unset(struct ofproto *up, void *aux)
{
    FPA_TRACE_FN();
    return 0;
}

static int
ops_fpa_ofproto_aa_vlan_get_queued(struct ofproto *up, struct ovs_list *list)
{
    FPA_TRACE_FN();
    return 0;
}

static unsigned int
ops_fpa_ofproto_aa_vlan_get_queue_size(struct ofproto *up)
{
    FPA_TRACE_FN();
    return 0;
}

static int
ops_fpa_ofproto_set_bfd(struct ofport *port, const struct smap *cfg)
{
    FPA_TRACE_FN();
    return 0;
}

static bool
ops_fpa_ofproto_bfd_status_changed(struct ofport *port)
{
    FPA_TRACE_FN();
    return false;
}

static int
ops_fpa_ofproto_get_bfd_status(struct ofport *port, struct smap *smap)
{
    FPA_TRACE_FN();
    return 0;
}

static int
ops_fpa_ofproto_set_stp(struct ofproto *up,
                        const struct ofproto_stp_settings *s)
{
    FPA_TRACE_FN();
    return EOPNOTSUPP;
}

static int
ops_fpa_ofproto_get_stp_status(struct ofproto *up, struct ofproto_stp_status *s)
{
    FPA_TRACE_FN();
    return EOPNOTSUPP;
}

static int
ops_fpa_ofproto_set_stp_port(struct ofport *port,
                             const struct ofproto_port_stp_settings *s)
{
    FPA_TRACE_FN();
    return EOPNOTSUPP;
}

static int
ops_fpa_ofproto_get_stp_port_status(struct ofport *port,
                                    struct ofproto_port_stp_status *s)
{
    FPA_TRACE_FN();
    return EOPNOTSUPP;
}

static int
ops_fpa_ofproto_get_stp_port_stats(struct ofport *port,
                                   struct ofproto_port_stp_stats *s)
{
    FPA_TRACE_FN();
    return EOPNOTSUPP;
}

static void
ops_fpa_ofproto_set_rstp(struct ofproto *up,
                         const struct ofproto_rstp_settings *s)
{
    FPA_TRACE_FN();
}

static void
ops_fpa_ofproto_get_rstp_status(struct ofproto *up,
                                struct ofproto_rstp_status *s)
{
    FPA_TRACE_FN();
}

static void
ops_fpa_ofproto_set_rstp_port(struct ofport *port,
                              const struct ofproto_port_rstp_settings *s)
{
    FPA_TRACE_FN();
}

static void
ops_fpa_ofproto_get_rstp_port_status(struct ofport *port,
                                     struct ofproto_port_rstp_status *s)
{
    FPA_TRACE_FN();
}

static int
ops_fpa_ofproto_set_queues(struct ofport *port,
                           const struct ofproto_port_queue *queues,
                           size_t n_qdscp)
{
    FPA_TRACE_FN();
    return EOPNOTSUPP;
}
#endif

static struct bundle_fpa*
ops_fpa_find_bundle_by_aux(const struct ofproto *up, void* aux)
{
    struct ofproto_fpa *this = ops_fpa_proto_cast(up);
    struct bundle_fpa* bundle;
    
    FPA_TRACE_FN();

    HMAP_FOR_EACH_IN_BUCKET(bundle, node, hash_pointer(aux, 0), 
                            &this->bundles) {
        if (bundle->aux == aux) {
            return bundle;
        }
    }

    return NULL;
}

static int
ops_fpa_rm_bundle(struct ofproto *up, struct bundle_fpa *bundle)
{
    struct ofproto_fpa *this = ops_fpa_proto_cast(up);
    FPA_TRACE_FN();
    if (NULL == bundle) {
        return EFAULT;
    }
    else {
        if (bundle->l3_intf) {
            ops_fpa_disable_routing(bundle->l3_intf);
            bundle->l3_intf = NULL;
        }

        hmap_remove(&this->bundles, &bundle->node);
        free(bundle);
    }

    return 0;
}

static int
ops_fpa_create_bundle_record(struct ofproto *up, void* aux,
                           struct bundle_fpa **bundle)
{
    struct ofproto_fpa *this = ops_fpa_proto_cast(up);

    FPA_TRACE_FN();
    *bundle = xmalloc (sizeof **bundle);
    hmap_insert(&this->bundles, &((*bundle)->node), hash_pointer (aux, 0));

    return 0;
}

static struct fpa_net_addr_t *
find_ipv4_addr_in_bundle(struct bundle_fpa *bundle, const char *address)
{
    struct fpa_net_addr_t *addr;

    HMAP_FOR_EACH_WITH_HASH (addr, addr_node, hash_string(address, 0),
                             &bundle->secondary_ip4addr) {
        if (STR_EQ(addr->address, address)) {
            return addr;
        }
    }

    return NULL;
} /* port_ip4_addr_find */

static int
bundle_ipv4_secondary_reconfigure (struct bundle_fpa *bundle,
                                 const struct ofproto_bundle_settings *set)
{
    struct shash new_ip_list;
    struct fpa_net_addr_t *addr, *next;
    struct shash_node *addr_node;
    int i;

    shash_init(&new_ip_list);

    /* Create hash of the current secondary ip's */
    for (i = 0; i < set->n_ip4_address_secondary; i++) {
       if(!shash_add_once(&new_ip_list, set->ip4_address_secondary[i],
                           set->ip4_address_secondary[i])) {
            VLOG_WARN("Duplicate address in secondary list %s",
                      set->ip4_address_secondary[i]);
        }
    }

    /* Compare current and old to delete any obsolete one's */
    HMAP_FOR_EACH_SAFE (addr, next, addr_node, &bundle->secondary_ip4addr) {
        if (!shash_find_data(&new_ip_list, addr->address)) {
            VLOG_INFO("Remove secondary IPv4 addr %s", addr->address);
            hmap_remove(&bundle->secondary_ip4addr, &addr->addr_node);
            /*TODO ops_xp_routing_delete_host_entry(ofproto, is_ipv6,
             *                                addr->address, &addr->id);*/
            free(addr->address);
            free(addr);
        }
    }
    /* Add the newly added addresses to the list */
    SHASH_FOR_EACH (addr_node, &new_ip_list) {
        struct fpa_net_addr_t *addr;
        const char *address = addr_node->data;
        if (!find_ipv4_addr_in_bundle(bundle, address)) {
            /*
             * Add the new address to the list
             */
            VLOG_INFO("Add secondary IPv4 address %s", address);
            addr = xzalloc(sizeof *addr);
            addr->address = xstrdup(address);
            hmap_insert(&bundle->secondary_ip4addr, &addr->addr_node,
                        hash_string(addr->address, 0));
            /*TODO ops_xp_routing_add_host_entry(ofproto, XPS_INTF_INVALID_ID,
                                          is_ipv6, addr->address,
                                          NULL, XPS_INTF_INVALID_ID,
                                          0, true, &addr->id);*/
        }
    }
    return 0;
}

static int
bundle_ip_reconfigure(struct ofproto_fpa *ofproto,
                      struct bundle_fpa *bundle,
                      const struct ofproto_bundle_settings *set)
{
    FPA_TRACE_FN();
    /* If primary ipv4 got added/deleted/modified */
    if (set->ip_change & PORT_PRIMARY_IPv4_CHANGED) {
        if (set->ip4_address) {
            if (bundle->ip4addr) {
                if (!STR_EQ(bundle->ip4addr->address, set->ip4_address)) {
                    VLOG_INFO("Remove primary IPv4 address=%s",
                              bundle->ip4addr->address);
                    /* If current and earlier are different, delete old */
                    delete_l3_host_entry(&ofproto->up, bundle->aux, false, 
                                         bundle->ip4addr->address,
                                         &bundle->intfId);
                    free(bundle->ip4addr->address);

                    /* Add new */
                    VLOG_INFO("Add primary IPv4 address=%s", set->ip4_address);
                    bundle->ip4addr->address = xstrdup(set->ip4_address);
                    add_l3_host_entry(&ofproto->up, bundle->aux, false,
                                      bundle->ip4addr->address, NULL,
                                      &bundle->intfId);
                } /* else no change */
            } else {
                /* Earlier primary was not there, just add new */
                VLOG_INFO("Add primary IPv4 address=%s", set->ip4_address);
                bundle->ip4addr = xzalloc(sizeof(struct fpa_net_addr_t));
                bundle->ip4addr->address = xstrdup(set->ip4_address);
                add_l3_host_entry(&ofproto->up, bundle->aux, false,
                                  bundle->ip4addr->address, NULL,
                                  &bundle->intfId);
            }
        } else {
            /* Primary got removed, earlier if it was there then remove it */
            if (bundle->ip4addr != NULL) {
                VLOG_INFO("Remove primary IPv4 address=%s",
                          bundle->ip4addr->address);
                delete_l3_host_entry(&ofproto->up, bundle->aux, false, 
                                     bundle->ip4addr->address,
                                     &bundle->intfId);
                free(bundle->ip4addr->address);
                free(bundle->ip4addr);
                bundle->ip4addr = NULL;
            }
        }
    } else if(set->ip_change & PORT_SECONDARY_IPv4_CHANGED) {
        VLOG_ERR("IPv4 secondary address is not supported yet");
        /* bundle_ipv4_secondary_reconfigure(bundle, set); */
    } else  if((set->ip_change & PORT_PRIMARY_IPv6_CHANGED)
            || (set->ip_change & PORT_SECONDARY_IPv6_CHANGED)){
        VLOG_ERR("IPv6 is not supported yet");
    }
    return 0;
}

/*Try to add VLAN member in ASIC checking is vlan enabled in ofproto*/
static int
ops_fpa_vlan_member_add(struct ofproto_fpa* ofproto, int pid, int vid,
                        bool tag_in, bool tag_out)
{
    if(bitmap_is_set(ofproto->vlans, vid)) {
        VLOG_INFO("Add member %d to ENABLED VLAN %d", pid, vid);
        return ops_fpa_vlan_add(ofproto->switch_id, pid, vid, tag_in, tag_out);
    }
    return 0;
}

/*Try to remove member from VLAN in ASIC checking is vlan enabled in ofproto*/
static int
ops_fpa_vlan_member_rm(struct ofproto_fpa* ofproto, int pid, int vid,
                       bool tag_in)
{
    if (bitmap_is_set(ofproto->vlans, vid)) {
        VLOG_INFO("Remove member %d from ENABLED VLAN %d", pid, vid);
        return ops_fpa_vlan_rm(ofproto->switch_id, pid, vid, tag_in);
    }
    return 0;
}
static int
ops_fpa_ofproto_bundle_set(struct ofproto *up, void *aux,
                           const struct ofproto_bundle_settings *set)
{
    int err_no = 0;
    struct bundle_fpa *bundle = NULL;
    struct ofproto_fpa *ofproto = ops_fpa_proto_cast(up);
    int vlan_id = 0;
    VLOG_INFO("ops_fpa_ofproto_bundle_set<%s,%s>: name=%s", up->type, up->name,
              set ? set->name : "NULL");

    bundle = ops_fpa_find_bundle_by_aux(up, aux);

    /* remove bundle if needed */
    if (set == NULL) {
        VLOG_INFO("ops_fpa_ofproto_bundle_set: REMOVE bundle");
        return ops_fpa_rm_bundle(up, bundle);
    }

    /* assume bundle have only one port in it */
    if (set->n_slaves != 1) {
        VLOG_ERR("unimplemented: n_slaves=%zd", set->n_slaves);
        return EINVAL;
    }
    /* create bundle if needed */
    if (bundle == NULL) {
        VLOG_INFO("ops_fpa_ofproto_bundle_set: CREATE bundle");
        ops_fpa_create_bundle_record(up, aux, &bundle);
        bundle->name = NULL;

        bundle->aux = aux;

        bundle->vlan = -1;
        bundle->vlan_mode = PORT_VLAN_ACCESS;

        memset(bundle->trunks, 0, sizeof(bundle->trunks));

        struct ofport *port = ofproto_get_port(up, set->slaves[0]);
        bundle->intfId = port ? netdev_get_ifindex(port->netdev) 
                              : FPA_INVALID_INTF_ID;
        bundle->ofp_port = set->slaves[0];

        /* If the first port in the bundle has a pid, add it to ports_by_pid. */
        if (bundle->intfId != FPA_INVALID_INTF_ID) {
            struct ofport_fpa *port_fpa = ops_fpa_port_cast(port);
            hmap_insert(&ports_by_pid, &port_fpa->node, bundle->intfId);
        }

        bundle->l3_intf = NULL;

        bundle->ip4addr = NULL;
        hmap_init(&bundle->secondary_ip4addr);
    }
    /* Set bundle name. */
    if (!bundle->name || !STR_EQ(set->name, bundle->name)) {
        free(bundle->name);
        bundle->name = xstrdup(set->name);
    }
    VLOG_INFO("%s: bundle->name=%s\n "
             "n_slaves=%zu, slaves[0]=%d, vlan_mode=%d"
             "vlan=%d intfId=%X ",
             __FUNCTION__, bundle->name, set->n_slaves,
             set->slaves[0], (int)set->vlan_mode,
             set->vlan, bundle->intfId
             );

    /* TODO: check this->vlans */
    /* for now, we don't support L3 on top of LAG */

    if (is_ofproto_vrf(&ofproto->up)) {
        struct ofport_fpa *port;
        const char* type;
        VLOG_INFO("Configure vrf interface");
        port = ops_fpa_get_ofproto_fpa_port(&ofproto->up, set->slaves[0]);
        if (port == NULL) {
            VLOG_ERR("Slave is not in the ports");
            return 0;
        }

        type = netdev_get_type(port->up.netdev);
        VLOG_INFO("netdev type: %s", type);

        if (STR_EQ(type, OVSREC_INTERFACE_TYPE_SYSTEM)) {
            vlan_id = smap_get_int (set->port_options[PORT_HW_CONFIG],
                                    "internal_vlan_id", 0);
        }
        else if (STR_EQ(type, OVSREC_INTERFACE_TYPE_INTERNAL)) {
            vlan_id = set->vlan;
            VLOG_INFO("%s get interface vlan internal vlan = %d", __FUNCTION__,
                     vlan_id);
        }
        else if (STR_EQ(type, OVSREC_INTERFACE_TYPE_VLANSUBINT)) {
            VLOG_INFO("%s get subinterface vlan", __FUNCTION__);
            /*bcm call copy-paste*/
            /*netdev_bcmsdk_get_subintf_vlan (port->up.netdev, &vlan_id);*/
            VLOG_INFO("%s subinterface vlan = %d", __FUNCTION__, vlan_id);
            VLOG_WARN("VLAN subintf not supported yet");
            goto done;
        }
        else if (STR_EQ(type, OVSREC_INTERFACE_TYPE_LOOPBACK)) {
            /* For l3-loopback interfaces, just configure ips */
            VLOG_WARN("%s Done with l3 loopback configurations", __FUNCTION__);
            goto done;
        }
        else {
            VLOG_WARN("%s is not supported yet", type);
        }
        VLOG_INFO("VLAN_ID=%d",vlan_id);

        if (bundle->l3_intf) {
            /* if reserved vlan changed/removed or if port status is disabled */
            if (!set->enable) {
                /*ops_xp_routing_disable_l3_interface(ofproto, bundle->l3_intf);*/
                VLOG_INFO("Deleting ROUTING");
                ops_fpa_disable_routing(bundle->l3_intf);
                bundle->l3_intf = NULL;
            }
        }
        /* Create L3 structure if needed. */
        if (vlan_id && !bundle->l3_intf && set->enable) {
            struct eth_addr mac;
            netdev_get_etheraddr(port->up.netdev, &mac);

            VLOG_INFO("%s: NETDEV %s, MAC "ETH_ADDR_FMT" VLAN %u",
                      __FUNCTION__, netdev_get_name(port->up.netdev),
                      ETH_ADDR_ARGS(mac), vlan_id);

            if (STR_EQ(type, OVSREC_INTERFACE_TYPE_SYSTEM)) {
                VLOG_INFO("Enabling ROUTING on SYSTEM interface");
                bundle->l3_intf = ops_fpa_enable_routing_interface(
                        ofproto->switch_id, bundle->intfId, vlan_id, mac);
                goto done;
            }
            else if (STR_EQ(type, OVSREC_INTERFACE_TYPE_VLANSUBINT)) {
                VLOG_INFO("Enabling ROUTING on VLAN SUBINTERFACE");
                VLOG_WARN("Subinterface is not supported yet");
                goto done;
            }
            else if (STR_EQ(type, OVSREC_INTERFACE_TYPE_INTERNAL)) {
                VLOG_INFO("Enabling ROUTING on internal VLAN interface");
                bundle->l3_intf = ops_fpa_enable_routing_vlan(
                        ofproto->switch_id, vlan_id, mac);
                goto done;
            }
            else {
                VLOG_ERR("%s: unknown interface type: %s", __FUNCTION__, type);
            }
        }

        bundle_ip_reconfigure(ofproto, bundle, set);
    }

    /* If ofproto's name is equal to bundle name => interface internal. */
    if (STR_EQ(ofproto->up.name, bundle->name)) {
        VLOG_INFO("INTERNAL INTERFACE");
        /* Nothing to do for internal port. */
        return 0;
    }
    /*L2 Operations*/
    unsigned long set_trunks[BITMAP_N_LONGS(FPA_VLAN_MAX_COUNT)];
    memset(set_trunks, 0, sizeof(set_trunks));

    /* Init set_trunks variable */
    switch (set->vlan_mode) {
        case PORT_VLAN_ACCESS:
            break;
        case PORT_VLAN_TRUNK:
        case PORT_VLAN_NATIVE_UNTAGGED:
        case PORT_VLAN_NATIVE_TAGGED:
            if (set->trunks) {
                memcpy(set_trunks, set->trunks, sizeof(set_trunks));
            }
            else {
                memset(set_trunks, 0xFF, sizeof(set_trunks));
            }
            break;
        default:
            break;
    }

    if(!is_ofproto_vrf(&ofproto->up)
            && (bundle->intfId != FPA_INVALID_INTF_ID)) {
        VLOG_INFO("Configuring L2 interface VLAN's");

        /* Initial VLAN configuration when bundle is just created. */
        if((bundle->vlan == -1) && bundle->vlan_mode == PORT_VLAN_ACCESS) {

            if(set->vlan != -1) {
                ops_fpa_vlan_member_add(
                        ofproto, bundle->intfId, set->vlan, false,
                        set->vlan_mode == PORT_VLAN_NATIVE_TAGGED);
            }
        }
        /* Change VLAN configuration. */
        else if ((bundle->vlan != set->vlan)
                     || (bundle->vlan_mode != set->vlan_mode
                         && set->vlan_mode != PORT_VLAN_TRUNK)) {
            /* Switching from trunk mode - adding pvid. */
            if (bundle->vlan == -1) {
                /* We need to assign port to vlan. */
                VLOG_DBG("VLAN ADD vlan_id %d intf %d", bundle->vlan,
                         bundle->intfId);
                ops_fpa_vlan_member_add(
                        ofproto, bundle->intfId, set->vlan, false,
                        set->vlan_mode == PORT_VLAN_NATIVE_TAGGED);
            }
            /* Switching to trunk mode - removing pvid. */
            else if (set->vlan == -1) {
                VLOG_DBG("VLAN RM vlan_id %d intf %d", bundle->vlan,
                         bundle->intfId);
                ops_fpa_vlan_member_rm(ofproto, bundle->intfId, bundle->vlan,
                                       false);
            }
            /* Updating mode to native VLAN. */
            else {
                VLOG_DBG("VLAN NATIVE");
                VLOG_DBG("VLAN ADD vlan_id %d intf %d", bundle->vlan,
                         bundle->intfId);
                ops_fpa_vlan_member_rm(ofproto, bundle->intfId, bundle->vlan,
                                        false);
                VLOG_DBG("VLAN RM vlan_id %d intf %d", set->vlan,
                         bundle->intfId);
                ops_fpa_vlan_member_add(
                        ofproto, bundle->intfId, set->vlan, false,
                        set->vlan_mode == PORT_VLAN_NATIVE_TAGGED);
            }
        }

        /* Update trunk configuration. */
        unsigned long diff_trunks[BITMAP_N_LONGS(FPA_VLAN_MAX_COUNT)];
        for (int i = 0; i < sizeof(diff_trunks) / sizeof(diff_trunks[0]); i++) {
            diff_trunks[i] = set_trunks[i] ^ bundle->trunks[i];
        }

        int vid;
        BITMAP_FOR_EACH_1(vid, FPA_VLAN_MAX_COUNT, diff_trunks)
        {
            if (bitmap_is_set(set_trunks, vid)) {
                ops_fpa_vlan_member_add(ofproto, bundle->intfId, vid, true,
                                        true);
            } else {
                ops_fpa_vlan_member_rm(ofproto, bundle->intfId, vid, true);
            }
        }

        goto done;
    }

done:
    VLOG_INFO("Bundle_set DONE");
    memcpy (bundle->trunks, set_trunks, sizeof(set_trunks));
    bundle->vlan_mode = set->vlan_mode;
    bundle->vlan = set->vlan;

    return err_no;
}

static void
ops_fpa_ofproto_bundle_remove(struct ofport *up)
{
    VLOG_INFO("ops_fpa_ofproto_bundle_remove<%s,%s>: ofp_port=%d", up->ofproto->type, up->ofproto->name, up->ofp_port);
}

static int
ops_fpa_ofproto_bundle_get(struct ofproto *up, void *aux, int *bundle_handle)
{
    FPA_TRACE_FN();
    return 0;
}

static int
ops_fpa_ofproto_set_vlan(struct ofproto *up, int vid, bool add)
{
    VLOG_INFO("ops_fpa_ofproto_set_vlan<%s,%s>: %s vid %d",
              up->type, up->name, add ? "ENABLE" : "DISABLE", vid);

    struct ofproto_fpa *this = ops_fpa_proto_cast(up);
    bitmap_set(this->vlans, vid, add);

    struct bundle_fpa *bundle;
    HMAP_FOR_EACH(bundle, node, &this->bundles) {
        if(vid == bundle->vlan) {
            ops_fpa_vlan_mod(add, this->switch_id, bundle->intfId, vid,
                             bundle->vlan_mode);
        }
        if ((NULL != bundle->trunks) && (vid != bundle->vlan)
                && bitmap_is_set(bundle->trunks, vid)
                && (bundle->vlan_mode == PORT_VLAN_TRUNK
                       || bundle->vlan_mode == PORT_VLAN_NATIVE_UNTAGGED
                       || bundle->vlan_mode == PORT_VLAN_NATIVE_TAGGED))
        {
            /*Port is trunk*/
            ops_fpa_vlan_mod(add, this->switch_id, bundle->intfId, vid,
                             PORT_VLAN_TRUNK);
        }
    }

    return 0;
}

#if 0
static int
ops_fpa_ofproto_mirror_set(struct ofproto *up, void *aux, const struct ofproto_mirror_settings *s)
{
    FPA_TRACE_FN();
    return 0;
}

static int
ops_fpa_ofproto_mirror_get_stats(struct ofproto *up, void *aux, uint64_t *packets, uint64_t *bytes)
{
    FPA_TRACE_FN();
    return EOPNOTSUPP;
}

static int
ops_fpa_ofproto_set_flood_vlans(struct ofproto *up, unsigned long *flood_vlans)
{
    FPA_TRACE_FN();
    return EOPNOTSUPP;
}

static bool
ops_fpa_ofproto_is_mirror_output_bundle(const struct ofproto *up, void *aux)
{
    FPA_TRACE_FN();
    return false;
}

static void
ops_fpa_ofproto_forward_bpdu_changed(struct ofproto *up)
{
    FPA_TRACE_FN();
}

static void
ops_fpa_ofproto_set_mac_table_config(struct ofproto *up, unsigned int idle_time, size_t max_entries)
{
    FPA_TRACE_FN();
}

static int
ops_fpa_ofproto_set_mcast_snooping(struct ofproto *up, const struct ofproto_mcast_snooping_settings *s)
{
    FPA_TRACE_FN();
    return EOPNOTSUPP;
}

static int
ops_fpa_ofproto_set_mcast_snooping_port(struct ofproto *up, void *aux, const struct ofproto_mcast_snooping_port_settings *s)
{
    FPA_TRACE_FN();
    return EOPNOTSUPP;
}
#endif
/*
 * OpenFlow meter functions.
 */
#if 0
static void
ops_fpa_ofproto_meter_get_features(const struct ofproto *up, struct ofputil_meter_features *features)
{
    FPA_TRACE_FN();
}

static enum ofperr
ops_fpa_ofproto_meter_set(struct ofproto *up, ofproto_meter_id *id, const struct ofputil_meter_config *config)
{
    FPA_TRACE_FN();
    return OFPERR_OFS;
}

static enum ofperr
ops_fpa_ofproto_meter_get(const struct ofproto *up, ofproto_meter_id id, struct ofputil_meter_stats *stats)
{
    FPA_TRACE_FN();
    return OFPERR_OFS;
}

static void
ops_fpa_ofproto_meter_del(struct ofproto *up, ofproto_meter_id meter_id)
{
    FPA_TRACE_FN();
}
#endif

/*
 * OpenFlow 1.1+ groups.
 */
#if 0
static struct ofgroup *
ops_fpa_ofproto_group_alloc(void)
{
    FPA_TRACE_FN();
    return NULL;
}

static enum ofperr
ops_fpa_ofproto_group_construct(struct ofgroup *group)
{
    FPA_TRACE_FN();
    return OFPERR_OFS;
}

static void
ops_fpa_ofproto_group_destruct(struct ofgroup *group)
{
    FPA_TRACE_FN();
}

static void
ops_fpa_ofproto_group_dealloc(struct ofgroup *group)
{
    FPA_TRACE_FN();
}

static enum ofperr
ops_fpa_ofproto_group_modify(struct ofgroup *group)
{
    FPA_TRACE_FN();
    return OFPERR_OFS;
}

static enum ofperr
ops_fpa_ofproto_group_get_stats(const struct ofgroup *group, struct ofputil_group_stats *stats)
{
    FPA_TRACE_FN();

    FPA_GROUP_COUNTERS_STC counters;
    int err = fpaLibGroupEntryStatisticsGet(0, group->group_id, &counters);
    if (err) {
        VLOG_ERR("fpaLibGroupEntryStatisticsGet: %s", ops_fpa_strerr(err));
        if (err == FPA_NOT_FOUND) {
            return OFPERR_OFPBRC_BAD_TABLE_ID;
        }

        return EINVAL;
    }

    /* set only packet and byte counts, ovs promise to fill the rest */
    stats->packet_count = counters.packetCount;
    stats->byte_count = counters.byteCount;

    return 0;
}

/*
 * Datapath information
 */

static const char *
ops_fpa_ofproto_get_datapath_version(const struct ofproto *up)
{
    //FPA_TRACE_FN();
    return NULL;
}
#endif

static int
string_to_ipv4_addr(char *ip_address, in_addr_t *addr, int *prefixlen) 
{
    char tmp_ip_addr[strlen(ip_address) + 1];
    const int maxlen = 32;

    *prefixlen = maxlen;
    strcpy(tmp_ip_addr, ip_address);

    char *p;
    if ((p = strchr(tmp_ip_addr, '/'))) {
        *p++ = '\0';
        *prefixlen = atoi(p);
    }

    if (*prefixlen > maxlen) {
        return EINVAL;
    }

    /* ipv4 address in network order */
    *addr = inet_addr(tmp_ip_addr);
    if (*addr == -1) {
        return EINVAL;
    }

    return 0;
}

uint32_t
host_table_key(in_addr_t ipv4_addr)
{
    char buf[sizeof(ipv4_addr)];
    FPA_TRACE_FN();
    memcpy(buf, &ipv4_addr, sizeof(ipv4_addr));
    return hash_bytes(buf, sizeof(ipv4_addr), 0);
}

static void
host_table_add(in_addr_t ipv4_addr, uint32_t arp_index, uint32_t l3_group, 
               uint32_t l2_group)
{
    struct host_table_entry *entry = xmalloc(sizeof *entry);

    FPA_TRACE_FN();

    memset(entry, 0, sizeof *entry);
    entry->ipv4_addr = ipv4_addr;
    entry->arp_index = arp_index;
    entry->l3_group = l3_group;
    entry->l2_group = l2_group;

    hmap_insert(&host_table, &entry->node, host_table_key(ipv4_addr));
}

void 
host_table_delete(struct host_table_entry *entry)
{
    FPA_TRACE_FN();

    hmap_remove(&host_table, &entry->node);
    free(entry);
}

struct host_table_entry *
host_table_find(in_addr_t ipv4_addr)
{
    uint32_t index = host_table_key(ipv4_addr);;

    FPA_TRACE_FN();

    struct host_table_entry *entry;
    HMAP_FOR_EACH_WITH_HASH(entry, node, index, &host_table) {
        if (entry->ipv4_addr == ipv4_addr) {
            return entry;
        }
    }

    return NULL;
}

static int
add_l3_host_entry(const struct ofproto *ofproto, void *aux,
                  bool is_ipv6_addr, char *ip_addr,
                  char *next_hop_mac_addr, int *l3_egress_id)
{
    int ret = 0;
    FPA_STATUS status = FPA_OK;

    FPA_TRACE_FN();
    VLOG_INFO("    ip_addr = %s", ip_addr);
    VLOG_INFO("    next_hop_mac_addr = %s", next_hop_mac_addr);

    if (is_ipv6_addr) {
        VLOG_ERR("%s: IPv6 is not supported yet.", __func__);
        return EINVAL;
    }

    if (!next_hop_mac_addr) {
        /* TODO: If the next hop is NULL, we should configure FPA to trap 
         * packets on CPU. */
        return EINVAL;
    }

    /* Get target bundle and port. */
    struct bundle_fpa *bundle = ops_fpa_find_bundle_by_aux(ofproto, aux);
    struct ofport_fpa *port = ops_fpa_get_ofproto_fpa_port(ofproto,
                                                           bundle->ofp_port);

    uint32_t switch_id = bundle->l3_intf->switchId;
    *l3_egress_id = bundle->l3_intf->intf_id;
    uint32_t vlan_id = bundle->l3_intf->vlan_id;

    VLOG_INFO("    bundle->name = %s", bundle->name);
    VLOG_INFO("    switch_id = %d", switch_id);
    VLOG_INFO("    vlan_id = %d", vlan_id);
    VLOG_INFO("    l3_egress_id = %d", *l3_egress_id);

    struct eth_addr src_mac_addr;
    netdev_get_etheraddr(port->up.netdev, &src_mac_addr);

    struct ether_addr dst_mac_addr;
    if (ether_aton_r(next_hop_mac_addr, &dst_mac_addr) == NULL) {
        VLOG_ERR("%s: Bad nexthop address %s.", __func__, next_hop_mac_addr);
        return EINVAL;
    }

    /* Parse host's IPv4 address. */
    int mask_len;
    in_addr_t ipv4_addr;
    ret = string_to_ipv4_addr(ip_addr, &ipv4_addr, &mask_len);
    if (ret != 0 || mask_len != 32) {
        VLOG_ERR("%s: Bad IPv4 address %s.", __func__, ip_addr);
        return EINVAL;
    }

    /* Add L2 interface group for the port. */
    uint32_t l2Group;
    status = ops_fpa_route_add_l2_group(switch_id, *l3_egress_id, vlan_id, 
                                      false, &l2Group);
    if (status != FPA_OK) {
        return EINVAL;
    }

    /* Allocate ARP index. */
    uint32_t arp_index = ops_marvell_utils_resource_alloc(l3_index_resource);
    if (arp_index == OPS_FPA_NO_RESOURCE) {
        VLOG_ERR("%s: Can't allocate index resource.", __func__);
        return EINVAL;
    }

    /* Add L3 unicast group. */
    uint32_t l3Group;
    status = ops_fpa_route_add_l3_group(switch_id, l2Group, arp_index, 
                                      vlan_id, port->up.mtu,
                                      (FPA_MAC_ADDRESS_STC *)&src_mac_addr, 
                                      (FPA_MAC_ADDRESS_STC *)&dst_mac_addr,
                                      &l3Group);
    if (status != FPA_OK) {
        return EINVAL;
    }

    /* Add entry about host's IP into the unicast routing flow table. */
    status = ops_fpa_route_add_route(switch_id, l3Group, ipv4_addr, mask_len);
    if (status != FPA_OK) {
        return EINVAL;
    }

    /* Add record into the host table. */
    host_table_add(ipv4_addr, arp_index, l3Group, l2Group);

    return 0;
}

static int
delete_l3_host_entry(const struct ofproto *ofproto, void *aux,
                     bool is_ipv6_addr, char *ip_addr,
                     int *l3_egress_id)
{
    int ret = 0;

    FPA_TRACE_FN();
    VLOG_INFO("    ip_addr = %s", ip_addr);
    VLOG_INFO("    l3_egress_id = %d", *l3_egress_id);

    if (is_ipv6_addr) {
        VLOG_ERR("%s: IPv6 is not supported yet.", __func__);
    	return EINVAL;
    }

    /* TODO: Get the switch ID from port's netdev. */
    uint32_t switchid = 0; 

    /* Parse host's IPv4 address. */
    int mask_len;
    in_addr_t ipv4_addr;
    ret = string_to_ipv4_addr(ip_addr, &ipv4_addr, &mask_len);
    if (ret != 0 || mask_len != 32) {
        VLOG_ERR("%s: Bad IPv4 address %s.", __func__, ip_addr);
        return EINVAL;
    }

    struct host_table_entry *entry = host_table_find(ipv4_addr);
    if (entry == NULL){
        VLOG_ERR("%s: Can't find entry for %s.", __func__, ip_addr);
        return EINVAL;
    }

    /* Delete routes */
    ops_fpa_route_del_route(switchid, ipv4_addr, mask_len);
    ops_fpa_route_del_group(switchid, entry->l3_group);
    ops_fpa_route_del_group(switchid, entry->l2_group);
    ops_marvell_utils_resource_free(l3_index_resource, entry->arp_index);
    host_table_delete(entry);

    return 0;
}

static int
get_l3_host_hit(const struct ofproto *ofproto, void *aux,
                bool is_ipv6_addr, char *ip_addr, bool *hit_bit)
{

    FPA_TRACE_FN();
    VLOG_INFO("    ip_addr = %s", ip_addr);

    if (is_ipv6_addr) {
        VLOG_ERR("%s: IPv6 is not supported yet.", __func__);
        return EINVAL;
    }

    /* TODO: Get the switch ID from port's netdev. */
    uint32_t switchid = 0; 

    /* Parse host's IPv4 address. */
    int mask_len;
    in_addr_t ipv4_addr;
    int ret = string_to_ipv4_addr(ip_addr, &ipv4_addr, &mask_len);
    if (ret != 0 || mask_len != 32) {
        VLOG_ERR("%s: Bad IPv4 address %s.", __func__, ip_addr);
        return EINVAL;
    }

    struct host_table_entry *host_entry = host_table_find(ipv4_addr);
    if (host_entry == NULL) { 
        VLOG_ERR("%s: Can't find entry for %s.", __func__, ip_addr);
        return EINVAL;
    }

    FPA_GROUP_COUNTERS_STC counters_entry;
    FPA_STATUS status = fpaLibGroupEntryStatisticsGet(switchid, 
                                                      host_entry->l3_group,
                                                      &counters_entry);
    if (status != FPA_OK) {
        return EINVAL;
    }

    *hit_bit = (counters_entry.referenceCount > 1);
    return 0;
}

static int
l3_route_action(const struct ofproto *ofproto,
                enum ofproto_route_action action,
                struct ofproto_route *route)
{
    int ret = 0;
    in_addr_t route_ipv4_address;
    int route_mask_len;

    FPA_TRACE_FN();
    VLOG_INFO("   action = %s", action == OFPROTO_ROUTE_ADD ? "add" :
                                action == OFPROTO_ROUTE_DELETE ? "delete" : 
                                "delete nexthop");
    VLOG_INFO("   route->prefix = %s", route->prefix);
    VLOG_INFO("   route->nexthop = %s", route->nexthops[0].id);

    switch (route->family) {
    case OFPROTO_ROUTE_IPV4:
        ret = string_to_ipv4_addr(route->prefix, &route_ipv4_address, 
                                  &route_mask_len);
        if (ret != 0) {
            VLOG_ERR("%s: Bad IPv4 address %s.", __func__, route->prefix);
            return EINVAL;
        }

        break;

    default:
        VLOG_ERR("%s: IPv6 is not supported yet.", __func__);
        return EINVAL;
    }

    FPA_STATUS status = FPA_OK;
    in_addr_t host_ipv4_address;
    int host_mask_len;
    struct host_table_entry *host_entry = NULL;

    /* TODO: Get the switch ID from port's netdev. */
    uint32_t switchid = 0; 

    switch (action) {
    case OFPROTO_ROUTE_ADD:
        ops_fpa_route_del_route(switchid, route_ipv4_address, route_mask_len); 

        /* No ecmp - only 1 nexhhop. */
        if (route->n_nexthops != 1) {
            VLOG_ERR("%s: ECMP is not supported yet.", __func__);
            return EINVAL;
        }

        if (route->nexthops[0].state == OFPROTO_NH_RESOLVED) {
            ret = string_to_ipv4_addr(route->nexthops[0].id, 
                                      &host_ipv4_address,
                                      &host_mask_len);
            if (ret != 0) {
                VLOG_ERR("%s: Bad IPv4 address %s.", __func__, route->prefix);
                return EINVAL;
            }

            host_entry = host_table_find(host_ipv4_address);
            if (host_entry == NULL) { 
                VLOG_ERR("%s: Can't find entry for %s.", __func__, 
                         route->nexthops[0].id);
                return EINVAL;
            }

            status = ops_fpa_route_add_route(switchid, host_entry->l3_group,
                                           route_ipv4_address, route_mask_len);
        } else { 
            /* Unresolved - trap to cpu. */
            status = marvell_fpa_add_route_trap(switchid, route_ipv4_address, 
                                                route_mask_len);
        }

        if (status != FPA_OK) {
            return EINVAL;
        }

        break;

    case OFPROTO_ROUTE_DELETE:
        ops_fpa_route_del_route(switchid, route_ipv4_address, route_mask_len);
        break;

    case OFPROTO_ROUTE_DELETE_NH:
        VLOG_ERR("%s: Action OFPROTO_ROUTE_DELETE_NH is not supported yet.", 
                 __func__);
        return EINVAL;
    default:
        VLOG_ERR("%s: Unsupported action %d.", __func__, action);
        return EINVAL;
    }

    return 0;
}

static struct ofproto_class ops_fpa_ofproto_class = {
    .init                    = ops_fpa_ofproto_init,
    .enumerate_types         = ops_fpa_ofproto_enumerate_types,
    .enumerate_names         = ops_fpa_ofproto_enumerate_names,
    .del                     = ops_fpa_ofproto_del,
    .port_open_type          = ops_fpa_ofproto_port_open_type,
    .type_run                = ops_fpa_ofproto_type_run,
    .type_wait               = ops_fpa_ofproto_type_wait,
    .alloc                   = ops_fpa_ofproto_alloc,
    .construct               = ops_fpa_ofproto_construct,
    .destruct                = ops_fpa_ofproto_destruct,
    .dealloc                 = ops_fpa_ofproto_dealloc,
    .run                     = ops_fpa_ofproto_run,
    .wait                    = ops_fpa_ofproto_wait,
    .get_memory_usage        = ops_fpa_ofproto_get_memory_usage,
    .type_get_memory_usage   = ops_fpa_ofproto_type_get_memory_usage,
    .flush                   = ops_fpa_ofproto_flush,
    .query_tables            = ops_fpa_ofproto_query_tables,
    .set_tables_version      = ops_fpa_ofproto_set_tables_version,
    .port_alloc              = ops_fpa_ofproto_port_alloc,
    .port_construct          = ops_fpa_ofproto_port_construct,
    .port_destruct           = ops_fpa_ofproto_port_destruct,
    .port_dealloc            = ops_fpa_ofproto_port_dealloc,
    .port_modified           = ops_fpa_ofproto_port_modified,
    .port_reconfigured       = ops_fpa_ofproto_port_reconfigured,
    .port_query_by_name      = ops_fpa_ofproto_port_query_by_name,
    .port_add                = ops_fpa_ofproto_port_add,
    .port_del                = ops_fpa_ofproto_port_del,
    .port_get_stats          = ops_fpa_ofproto_port_get_stats,
    .port_dump_start         = ops_fpa_ofproto_port_dump_start,
    .port_dump_next          = ops_fpa_ofproto_port_dump_next,
    .port_dump_done          = ops_fpa_ofproto_port_dump_done,
    .port_poll               = NULL,//ops_fpa_ofproto_port_poll,
    .port_poll_wait          = ops_fpa_ofproto_port_poll_wait,
    .port_is_lacp_current    = ops_fpa_ofproto_port_is_lacp_current,
    .port_get_lacp_stats     = ops_fpa_ofproto_port_get_lacp_stats,
    .rule_choose_table       = ops_fpa_ofproto_rule_choose_table,
    .rule_alloc              = NULL, //ops_fpa_ofproto_rule_alloc,
    .rule_construct          = NULL, //ops_fpa_ofproto_rule_construct,
    .rule_insert             = NULL, //ops_fpa_ofproto_rule_insert,
    .rule_delete             = NULL, //ops_fpa_ofproto_rule_delete,
    .rule_destruct           = NULL, //ops_fpa_ofproto_rule_destruct,
    .rule_dealloc            = NULL, //ops_fpa_ofproto_rule_dealloc,
    .rule_get_stats          = NULL, //ops_fpa_ofproto_rule_get_stats,
    .rule_execute            = NULL, //ops_fpa_ofproto_rule_execute,
    .set_frag_handling       = NULL, //ops_fpa_ofproto_set_frag_handling,
    .packet_out              = NULL, //ops_fpa_ofproto_packet_out,
    .set_netflow             = NULL, //ops_fpa_ofproto_set_netflow,
    .get_netflow_ids         = NULL, //ops_fpa_ofproto_get_netflow_ids,
    .set_sflow               = NULL, //ops_fpa_ofproto_set_sflow,
    .set_ipfix               = NULL, //ops_fpa_ofproto_set_ipfix,
    .set_cfm                 = NULL, //ops_fpa_ofproto_set_cfm,
    .cfm_status_changed      = NULL, //ops_fpa_ofproto_cfm_status_changed,
    .get_cfm_status          = NULL, //ops_fpa_ofproto_get_cfm_status,
    .set_lldp                = NULL, //ops_fpa_ofproto_set_lldp,
    .get_lldp_status         = NULL, //ops_fpa_ofproto_get_lldp_status,
    .set_aa                  = NULL, //ops_fpa_ofproto_set_aa,
    .aa_mapping_set          = NULL, //ops_fpa_ofproto_aa_mapping_set,
    .aa_mapping_unset        = NULL, //ops_fpa_ofproto_aa_mapping_unset,
    .aa_vlan_get_queued      = NULL, //ops_fpa_ofproto_aa_vlan_get_queued,
    .aa_vlan_get_queue_size  = NULL, //ops_fpa_ofproto_aa_vlan_get_queue_size,
    .set_bfd                 = NULL, //ops_fpa_ofproto_set_bfd,
    .bfd_status_changed      = NULL, //ops_fpa_ofproto_bfd_status_changed,
    .get_bfd_status          = NULL, //ops_fpa_ofproto_get_bfd_status,
    .set_stp                 = NULL, //ops_fpa_ofproto_set_stp,
    .get_stp_status          = NULL, //ops_fpa_ofproto_get_stp_status,
    .set_stp_port            = NULL, //ops_fpa_ofproto_set_stp_port,
    .get_stp_port_status     = NULL, //ops_fpa_ofproto_get_stp_port_status,
    .get_stp_port_stats      = NULL, //ops_fpa_ofproto_get_stp_port_stats,
    .set_rstp                = NULL, //ops_fpa_ofproto_set_rstp,
    .get_rstp_status         = NULL, //ops_fpa_ofproto_get_rstp_status,
    .set_rstp_port           = NULL, //ops_fpa_ofproto_set_rstp_port,
    .get_rstp_port_status    = NULL, //ops_fpa_ofproto_get_rstp_port_status,
    .set_queues              = NULL, //ops_fpa_ofproto_set_queues,
    .bundle_set              = ops_fpa_ofproto_bundle_set,
    .bundle_remove           = ops_fpa_ofproto_bundle_remove,
    .bundle_get              = NULL, //ops_fpa_ofproto_bundle_get,
    .set_vlan                = ops_fpa_ofproto_set_vlan,
    .mirror_set              = NULL, //ops_fpa_ofproto_mirror_set,
    .mirror_get_stats        = NULL, //ops_fpa_ofproto_mirror_get_stats,
    .set_flood_vlans         = NULL, //ops_fpa_ofproto_set_flood_vlans,
    .is_mirror_output_bundle = NULL, //ops_fpa_ofproto_is_mirror_output_bundle,
    .forward_bpdu_changed    = NULL, //ops_fpa_ofproto_forward_bpdu_changed,
    .set_mac_table_config    = NULL, //ops_fpa_ofproto_set_mac_table_config,
    .set_mcast_snooping      = NULL, //ops_fpa_ofproto_set_mcast_snooping,
    .set_mcast_snooping_port = NULL, //ops_fpa_ofproto_set_mcast_snooping_port,
    .set_realdev             = NULL, /* set_realdev, is unused */
    .meter_get_features      = NULL, //ops_fpa_ofproto_meter_get_features,
    .meter_set               = NULL, //ops_fpa_ofproto_meter_set,
    .meter_get               = NULL, //ops_fpa_ofproto_meter_get,
    .meter_del               = NULL, //ops_fpa_ofproto_meter_del,
    .group_alloc             = NULL, //ops_fpa_ofproto_group_alloc,
    .group_construct         = NULL, //ops_fpa_ofproto_group_construct,
    .group_destruct          = NULL, //ops_fpa_ofproto_group_destruct,
    .group_dealloc           = NULL, //ops_fpa_ofproto_group_dealloc,
    .group_modify            = NULL, //ops_fpa_ofproto_group_modify,
    .group_get_stats         = NULL, //ops_fpa_ofproto_group_get_stats,
    .get_datapath_version    = NULL, //ops_fpa_ofproto_get_datapath_version,
    .add_l3_host_entry       = add_l3_host_entry,
    .delete_l3_host_entry    = delete_l3_host_entry,
    .get_l3_host_hit         = get_l3_host_hit,
    .l3_route_action         = l3_route_action,
    .l3_ecmp_set             = NULL,
    .l3_ecmp_hash_set        = NULL,
};

void
ofproto_register(void)
{
    ofproto_class_register(&ops_fpa_ofproto_class);
}

static struct ofproto_fpa *
ops_fpa_ofproto_lookup(const char *name)
{
    struct ofproto_fpa *ofproto;

    ovs_assert(name);

    HMAP_FOR_EACH_WITH_HASH (ofproto, node,
                             hash_string(name, 0), &protos) {
        if (STR_EQ(ofproto->up.name, name)) {
            return ofproto;
        }
    }
    return NULL;
}

static void
fpa_unixctl_fdb_flush(struct unixctl_conn *conn, int argc OVS_UNUSED,
                     const char *argv[] OVS_UNUSED, void *aux OVS_UNUSED)
{
    struct ofproto_fpa *ofproto;

    if (argc > 1) {
        ofproto = ops_fpa_ofproto_lookup(argv[1]);
        if (!ofproto) {
            unixctl_command_reply_error(conn, "no such bridge");
            return;
        }
        ovs_rwlock_wrlock(&ofproto->dev->ml->rwlock);
        ops_fpa_mac_learning_flush(ofproto->dev->ml);
        ovs_rwlock_unlock(&ofproto->dev->ml->rwlock);
    } else {
        HMAP_FOR_EACH (ofproto, node, &protos) {
            ovs_rwlock_wrlock(&ofproto->dev->ml->rwlock);
            ops_fpa_mac_learning_flush(ofproto->dev->ml);
            ovs_rwlock_unlock(&ofproto->dev->ml->rwlock);
        }
    }

    unixctl_command_reply(conn, "table successfully flushed");
}

static void
fpa_unixctl_fdb_show(struct unixctl_conn *conn, int argc OVS_UNUSED,
                    const char *argv[] OVS_UNUSED, void *aux OVS_UNUSED)
{
    struct ds d_str = DS_EMPTY_INITIALIZER;
    const struct ofproto_fpa *ofproto = NULL;

    ofproto = ops_fpa_ofproto_lookup(argv[1]);
    if (!ofproto) {
        unixctl_command_reply_error(conn, "no such bridge");
        return;
    }

    ovs_assert(ofproto->dev);
    ovs_assert(ofproto->dev->ml);

    ovs_rwlock_rdlock(&ofproto->dev->ml->rwlock);
    ops_fpa_mac_learning_dump_table(ofproto->dev->ml, &d_str);
    ovs_rwlock_unlock(&ofproto->dev->ml->rwlock);

    unixctl_command_reply(conn, ds_cstr(&d_str));
    ds_destroy(&d_str);
}

static void
fpa_unixctl_fdb_get_aging(struct unixctl_conn *conn, int argc,
                               const char *argv[], void *aux OVS_UNUSED)
{
    struct ds d_str = DS_EMPTY_INITIALIZER;
    const struct ofproto_fpa *ofproto = NULL;
    unsigned int idle_time;
    int status;

    /* Get bridge */
    ofproto = ops_fpa_ofproto_lookup(argv[1]);
    if (!ofproto) {
        unixctl_command_reply_error(conn, "no such bridge");
        return;
    }

    status = ops_fpa_mac_learning_get_idle_time(ofproto->dev->ml, &idle_time);
    if (status) {
        unixctl_command_reply_error(conn, "failed to get aging time");
        return;
    }

    ds_put_format(&d_str, "FDB aging time is %d", idle_time);
    unixctl_command_reply(conn, ds_cstr(&d_str));
    ds_destroy(&d_str);
}

static void
fpa_unixctl_fdb_configure_aging(struct unixctl_conn *conn, int argc,
                               const char *argv[], void *aux OVS_UNUSED)
{
    const struct ofproto_fpa *ofproto = NULL;
    unsigned int idle_time;
    int status;

    /* Get bridge */
    ofproto = ops_fpa_ofproto_lookup(argv[1]);
    if (!ofproto) {
        unixctl_command_reply_error(conn, "no such bridge");
        return;
    }

    /* Get Age time. */
    status = ovs_scan(argv[2], "%"SCNu32, &idle_time);
    if (!status) {
        unixctl_command_reply_error(conn, "invalid aging time");
        return;
    }

    status = ops_fpa_mac_learning_set_idle_time(ofproto->dev->ml, idle_time);
    if (status) {
        unixctl_command_reply_error(conn, "failed to set aging time");
        return;
    }

    unixctl_command_reply(conn, "FDB aging time been update successfully");
}

static void
ops_fpa_ofproto_unixctl_init(void)
{
    static bool registered;
    if (registered) {
        return;
    }
    registered = true;

    unixctl_command_register("fpa/fdb/flush", "[bridge]", 0, 1,
                            fpa_unixctl_fdb_flush, NULL);
    unixctl_command_register("fpa/fdb/show", "bridge", 1, 1,
                            fpa_unixctl_fdb_show, NULL);
    unixctl_command_register("fpa/fdb/get-age", "bridge",
                             1, 1, fpa_unixctl_fdb_get_aging, NULL);
    unixctl_command_register("fpa/fdb/set-age", "bridge aging_time",
                             2, 2, fpa_unixctl_fdb_configure_aging, NULL);
}
