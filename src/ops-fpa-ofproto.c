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

#include <openswitch-idl.h>
#include <vswitch-idl.h>
#include <vlan-bitmap.h>
#include <ofproto/ofproto-provider.h>
#include "seq.h"
#include "connectivity.h"
#include "unixctl.h"
#include "ops-fpa.h"
#include "ops-fpa-dev.h"
#include "ops-fpa-mac-learning.h"


VLOG_DEFINE_THIS_MODULE(ops_fpa_ofproto);

struct ofproto_fpa {
    struct hmap_node node;
    struct ofproto up;

    struct sset port_names;
    struct hmap port_refs;

    struct hmap bundles;
    unsigned long vlans[512 / sizeof(unsigned long)];

    struct fpa_dev *dev;

    uint64_t change_seq;           /* Connectivity status changes. */
};

struct bundle_fpa {
    struct hmap_node node;
    void *aux;

    enum port_vlan_mode vlan_mode;
    int vlan;
    unsigned long trunks[512 / sizeof(unsigned long)]; /* 4096 bits */
    int pid; /* TODO: for LAG this needs to be a vector */
};

struct ofrule_fpa {
    struct rule up;
    FPA_FLOW_TABLE_ENTRY_STC entry;
};

struct ofport_fpa {
    struct hmap_node node;
    struct ofport up;
    //int vid;
};

struct port_ref {
    struct hmap_node node;
    struct netdev *dev;
};

struct port_iter {
    uint32_t bucket;
    uint32_t offset;
    //struct ofproto_port port;
};

static struct hmap protos = HMAP_INITIALIZER(&protos);

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

static void
ops_fpa_ofproto_init(const struct shash *iface_hints)
{
    FPA_TRACE_FN();
    ops_fpa_init();
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
        if (strcmp(type, p->up.type)) {
            sset_add(names, p->up.type);
        }
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
    if (!strcmp(port_type, OVSREC_INTERFACE_TYPE_INTERNAL) ||
        !strcmp(port_type, OVSREC_INTERFACE_TYPE_VLANSUBINT) ||
        !strcmp(port_type, OVSREC_INTERFACE_TYPE_LOOPBACK)) {
        return port_type;
    }

    return "system";
}

static int
ops_fpa_ofproto_type_run(const char *type)
{
    return 0;
}

static void
ops_fpa_ofproto_type_wait(const char *type)
{
}

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
    hmap_init(&p->port_refs);
    hmap_init(&p->bundles);
    ofproto_init_tables(up, FPA_FLOW_TABLE_MAX);

    memset(p->vlans, 0, sizeof(p->vlans));
    bitmap_set1(p->vlans, 1);

    err = ops_fpa_dev_init(FPA_DEV_SWITCH_ID_DEFAULT, &p->dev);
    if (err) {
        VLOG_ERR("ops_fpa_dev_init: %s", ops_fpa_strerr(err));
        return err;
    }

    p->change_seq = 0;

    ops_fpa_ofproto_unixctl_init();

    return 0;
}

static void
ops_fpa_ofproto_destruct(struct ofproto *up)
{
    struct ofproto_fpa *p = ops_fpa_proto_cast(up);
    hmap_destroy(&p->bundles);
    hmap_destroy(&p->port_refs);
    sset_destroy(&p->port_names);
    hmap_remove(&protos, &p->node);

    /*int err = ops_fpa_dev_deinit(FPA_DEV_SWITCH_ID_DEFAULT);
    if (err) {
        VLOG_ERR("ops_fpa_dev_deinit(): %s", ops_fpa_strerr(err));
        return;
    }*/
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

    if (!STR_EQ(up->type, "vrf")) {
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
ops_fpa_ofproto_query_tables(struct ofproto *up, struct ofputil_table_features *features, struct ofputil_table_stats *stats)
{
    FPA_TRACE_FN();
}

static void
ops_fpa_ofproto_set_tables_version(struct ofproto *up, cls_version_t version)
{
}

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
    VLOG_INFO("ops_fpa_ofproto_port_construct<%s,%s>: ofp_port=%d", up->ofproto->type, up->ofproto->name, up->ofp_port);
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
ops_fpa_ofproto_port_reconfigured(struct ofport *port, enum ofputil_port_config old_config)
{
    FPA_TRACE_FN();
}

static int
ops_fpa_ofproto_port_query_by_name(const struct ofproto *up, const char *devname, struct ofproto_port *port)
{
    //VLOG_INFO("ops_fpa_ofproto_port_query_by_name<%s,%s>: devname=%s", up->type, up->name, devname);

    struct ofproto_fpa *p = ops_fpa_proto_cast(up);
    if (!sset_contains(&p->port_names, devname)) {
        return ENODEV;
    }

    struct port_ref *ref;
    HMAP_FOR_EACH(ref, node, &p->port_refs) {
        if (!strcmp(netdev_get_name(ref->dev), devname)) {
            port->name = xstrdup(devname);
            port->type = xstrdup(netdev_get_type(ref->dev));
            port->ofp_port = ops_fpa_netdev_pid(ref->dev);

            return 0;
        }
    }

    return ENODEV;
}

static int
ops_fpa_ofproto_port_add(struct ofproto *up, struct netdev *dev)
{
    const char *devname = netdev_get_name(dev);

    VLOG_INFO("ops_fpa_ofproto_port_add<%s,%s>: devname=%s", up->type, up->name, devname);

    struct ofproto_fpa *p = ops_fpa_proto_cast(up);
    sset_add(&p->port_names, devname);

    struct port_ref *ref = xzalloc(sizeof *ref);
    ref->dev = dev;
    hmap_insert(&p->port_refs, &ref->node, ops_fpa_netdev_pid(dev));

    return 0;
}

static int
ops_fpa_ofproto_port_del(struct ofproto *up, ofp_port_t ofp_port)
{
    //VLOG_INFO("ops_fpa_ofproto_port_del: type=%s name=%s ofp_port=%d", up->type, up->name, ofp_port);
    //struct ofproto_fpa *p = ops_fpa_proto_cast(up);

    //struct sset port_names;
    //struct hmap port_refs;

    //struct ofport *ofport = ofproto_get_port(&ofproto->up, ofp_port);
    //sset_find_and_delete(&ofproto->ghost_ports,netdev_get_name(ofport->up.netdev));
    //hmap_remove(&ofproto->dp_ports, &port->node);

    //p->port_names;
    //p->port_refs;


    return 0;
}

static int
ops_fpa_ofproto_port_get_stats(const struct ofport *port, struct netdev_stats *stats)
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
ops_fpa_ofproto_port_dump_next(const struct ofproto *up, void *state, struct ofproto_port *port)
{
    struct ofproto_fpa *p = ops_fpa_proto_cast(up);
    struct port_iter *iter = state;
    struct sset_node *node;

    //VLOG_INFO("ops_fpa_ofproto_port_dump_next:  type=%s name=%s bucket=%d offset=%d", up->type, up->name, iter->bucket, iter->offset);

    while ((node = sset_at_position(&p->port_names, &iter->bucket, &iter->offset))) {
        return ops_fpa_ofproto_port_query_by_name(up, node->name, port);
    }

    return EOF;
}

static int
ops_fpa_ofproto_port_dump_done(const struct ofproto *up, void *state)
{
    //VLOG_INFO("ops_fpa_ofproto_port_dump_done:  type=%s name=%s", up->type, up->name);
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
ops_fpa_ofproto_port_get_lacp_stats(const struct ofport *port, struct lacp_slave_stats *stats)
{
    FPA_TRACE_FN();
    return 0;
}

static enum ofperr
ops_fpa_ofproto_rule_choose_table(const struct ofproto *up, const struct match *match, uint8_t *table_idp)
{
    FPA_TRACE_FN();
    return OFPERR_OFS;
}

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
ops_fpa_ofproto_rule_insert(struct rule *rule, struct rule *old_rule, bool forward_stats)
{
    FPA_TRACE_FN();
}

static void
ops_fpa_ofproto_rule_delete(struct rule *rule)
{
    FPA_TRACE_FN();
}

static void
ops_fpa_ofproto_rule_get_stats(struct rule *up, uint64_t *packet_count, uint64_t *byte_count, long long int *used)
{
    FPA_TRACE_FN();
    FPA_FLOW_ENTRY_COUNTERS_STC counters;
    struct ofrule_fpa *r = ops_fpa_rule_cast(up);
    int err = fpaLibFlowEntryStatisticsGet(0, up->table_id, &r->entry, &counters);
    if (err) {
        VLOG_ERR("fpaLibFlowEntryStatisticsGet: %s", ops_fpa_strerr(err));
        return;
    }

    *packet_count = counters.packetCount;
    *byte_count = counters.byteCount;
    *used = counters.durationSec;
}

static enum ofperr
ops_fpa_ofproto_rule_execute(struct rule *rule, const struct flow *flow, struct dp_packet *packet)
{
    FPA_TRACE_FN();
    return OFPERR_OFS;
}

static bool
ops_fpa_ofproto_set_frag_handling(struct ofproto *up, enum ofp_config_flags frag_handling)
{
    FPA_TRACE_FN();
    return false;
}

static enum ofperr
ops_fpa_ofproto_packet_out(struct ofproto *up, struct dp_packet *packet, const struct flow *flow, const struct ofpact *ofpacts, size_t ofpacts_len)
{
    FPA_TRACE_FN();
    return OFPERR_OFS;
}

static int
ops_fpa_ofproto_set_netflow(struct ofproto *up, const struct netflow_options *netflow_options)
{
    FPA_TRACE_FN();
    return EOPNOTSUPP;
}

static void
ops_fpa_ofproto_get_netflow_ids(const struct ofproto *up, uint8_t *engine_type, uint8_t *engine_id)
{
    FPA_TRACE_FN();
}

static int
ops_fpa_ofproto_set_sflow(struct ofproto *up, const struct ofproto_sflow_options *sflow_options)
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
ops_fpa_ofproto_get_cfm_status(const struct ofport *port, struct cfm_status *status)
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
ops_fpa_ofproto_get_lldp_status(const struct ofport *port, struct lldp_status *status)
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
ops_fpa_ofproto_aa_mapping_set(struct ofproto *up, void *aux, const struct aa_mapping_settings *s)
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
ops_fpa_ofproto_set_stp(struct ofproto *up, const struct ofproto_stp_settings *s)
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
ops_fpa_ofproto_set_stp_port(struct ofport *port, const struct ofproto_port_stp_settings *s)
{
    FPA_TRACE_FN();
    return EOPNOTSUPP;
}

static int
ops_fpa_ofproto_get_stp_port_status(struct ofport *port, struct ofproto_port_stp_status *s)
{
    FPA_TRACE_FN();
    return EOPNOTSUPP;
}

static int
ops_fpa_ofproto_get_stp_port_stats(struct ofport *port, struct ofproto_port_stp_stats *s)
{
    FPA_TRACE_FN();
    return EOPNOTSUPP;
}

static void
ops_fpa_ofproto_set_rstp(struct ofproto *up, const struct ofproto_rstp_settings *s)
{
    FPA_TRACE_FN();
}

static void
ops_fpa_ofproto_get_rstp_status(struct ofproto *up, struct ofproto_rstp_status *s)
{
    FPA_TRACE_FN();
}

static void
ops_fpa_ofproto_set_rstp_port(struct ofport *port, const struct ofproto_port_rstp_settings *s)
{
    FPA_TRACE_FN();
}

static void
ops_fpa_ofproto_get_rstp_port_status(struct ofport *port, struct ofproto_port_rstp_status *s)
{
    FPA_TRACE_FN();
}

static int
ops_fpa_ofproto_set_queues(struct ofport *port, const struct ofproto_port_queue *queues, size_t n_qdscp)
{
    FPA_TRACE_FN();
    return EOPNOTSUPP;
}

static int
ops_fpa_ofproto_bundle_set(struct ofproto *up, void *aux, const struct ofproto_bundle_settings *set)
{
    if (!strcmp(up->type, "vrf")) {
        return 0; /* no vrf */
    }

    VLOG_INFO("ops_fpa_ofproto_bundle_set<%s,%s>: name=%s", up->type, up->name, set ? set->name : "NULL");

    struct ofproto_fpa *this = ops_fpa_proto_cast(up);
    struct bundle_fpa *bundle = NULL;
    /* try to find bundle by aux */
    struct bundle_fpa *b;
    HMAP_FOR_EACH_IN_BUCKET(b, node, hash_pointer(aux, 0), &this->bundles) {
        if (b->aux == aux) {
            bundle = b;
            break;
        }
    }
    /* dispose bundle and return */
    if (!set) {
        if (bundle) {
            hmap_remove(&this->bundles, &bundle->node);
            free(bundle);
        }
        return 0;
    }
    /* ignore "bridge_normal" completely */
    if (!strcmp(set->name, "bridge_normal")) {
        return 0;
    }
    /* assume bundle have only one port in it */
    if (set->n_slaves != 1) {
        VLOG_ERR("unimplemented: n_slaves=%zd", set->n_slaves);
        return EINVAL;
    }
    /* create bundle if needed */
    if (!bundle) {
        bundle = xmalloc(sizeof *bundle);
        hmap_insert(&this->bundles, &bundle->node, hash_pointer(aux, 0));

        bundle->aux = aux;
        bundle->vlan = -1;
        bundle->pid = set->slaves[0];
        bundle->vlan_mode = PORT_VLAN_ACCESS;
        memset(bundle->trunks, 0x00, sizeof(bundle->trunks));
    }

    VLOG_INFO("    set: %svlan_mode=%s vlan=%d trunks[%s] slaves[%s]", set->enable ? "" : "DISABLE ",
        ops_fpa_str_vlan_mode(set->vlan_mode), set->vlan, ops_fpa_str_trunks(set->trunks), ops_fpa_str_ports(set->slaves, set->n_slaves));

    VLOG_INFO("    cur: vlan_mode=%s vlan=%d trunks[%s]",
        ops_fpa_str_vlan_mode(bundle->vlan_mode), bundle->vlan, ops_fpa_str_trunks(bundle->trunks));

    /*struct smap_node *node;
    SMAP_FOR_EACH(node, set->port_options[PORT_OPT_VLAN]) {
        VLOG_INFO("    PORT_OPT_VLAN: %s=%s", node->key, node->value);
    }
    SMAP_FOR_EACH(node, set->port_options[PORT_OPT_BOND]) {
        VLOG_INFO("    PORT_OPT_BOND: %s=%s", node->key, node->value);
    }
    SMAP_FOR_EACH(node, set->port_options[PORT_HW_CONFIG]) {
        VLOG_INFO("    PORT_HW_CONFIG: %s=%s", node->key, node->value);
    }
    SMAP_FOR_EACH(node, set->port_options[PORT_OTHER_CONFIG]) {
        VLOG_INFO("    PORT_OTHER_CONFIG: %s=%s", node->key, node->value);
    }*/

    /* TODO: check this->vlans */

    if ((bundle->vlan != set->vlan) || (bundle->vlan_mode != set->vlan_mode && set->vlan_mode != PORT_VLAN_TRUNK)) {
        /* Switching from trunk mode. */
        if (bundle->vlan == -1) {
            ops_fpa_vlan_add(bundle->pid, set->vlan, false, set->vlan_mode == PORT_VLAN_NATIVE_TAGGED);
        }
        /* Switching to trunk mode. */
        else if (set->vlan == -1) {
            ops_fpa_vlan_rm(bundle->pid, bundle->vlan, false);
        }
        /* Setting native vlan. */
        else {
            ops_fpa_vlan_rm(bundle->pid, bundle->vlan, false);
            ops_fpa_vlan_add(bundle->pid, set->vlan, false, set->vlan_mode == PORT_VLAN_NATIVE_TAGGED);
        }
    }

    unsigned long set_trunks[512 / sizeof(unsigned long)];
    switch (set->vlan_mode) {
        case PORT_VLAN_ACCESS:
            memset(set_trunks, 0x00, sizeof(set_trunks));
            break;
        case PORT_VLAN_TRUNK:
            if (set->trunks) {
                memcpy(set_trunks, set->trunks, sizeof(set_trunks));
            }
            else {
                memset(set_trunks, 0xFF, sizeof(set_trunks));
            }
            break;
        case PORT_VLAN_NATIVE_UNTAGGED:
        case PORT_VLAN_NATIVE_TAGGED:
            memset(set_trunks, 0x00, sizeof(set_trunks));
            break;
        default: break;
    }

    unsigned long diff_trunks[512 / sizeof(unsigned long)];
    for (int i = 0; i < sizeof(diff_trunks) / sizeof(diff_trunks[0]); i++) {
        diff_trunks[i] = set_trunks[i] ^ bundle->trunks[i];
    }

    int vid;
    BITMAP_FOR_EACH_1(vid, 4096, diff_trunks) {
        if (bitmap_is_set(set_trunks, vid)) {
            ops_fpa_vlan_add(bundle->pid, vid, true, true);
        }
        else {
            ops_fpa_vlan_rm(bundle->pid, vid, false);
        }
    }

    bundle->vlan_mode = set->vlan_mode;
    bundle->vlan = set->vlan;
    memcpy(bundle->trunks, set_trunks, sizeof(set_trunks));

    fpaLibFlowTableDump(0, FPA_FLOW_TABLE_TYPE_VLAN_E);

    return 0;
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
    VLOG_INFO("ops_fpa_ofproto_set_vlan<%s,%s>: %s vid %d", up->type, up->name, add ? "ENABLE" : "DISABLE", vid);

    struct ofproto_fpa *this = ops_fpa_proto_cast(up);
    bitmap_set(this->vlans, vid, add);

    struct bundle_fpa *bundle;
    HMAP_FOR_EACH(bundle, node, &this->bundles) {
        ops_fpa_vlan_mod(add, bundle->pid, vid, bundle->vlan_mode);
    }

    fpaLibFlowTableDump(0, FPA_FLOW_TABLE_TYPE_VLAN_E);

    return 0;
}

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

static int
ops_fpa_ofproto_set_realdev(struct ofport *port, ofp_port_t realdev_ofp_port, int vid)
{
    FPA_TRACE_FN();
    return 0;
}

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

static const char *
ops_fpa_ofproto_get_datapath_version(const struct ofproto *up)
{
    //FPA_TRACE_FN();
    return NULL;
}

static int
ops_fpa_ofproto_add_l3_host_entry(const struct ofproto *up, void *aux, bool is_ipv6_addr, char *ip_addr, char *next_hop_mac_addr, int *l3_egress_id)
{
    FPA_TRACE_FN();
    return 0;
}

static int
ops_fpa_ofproto_delete_l3_host_entry(const struct ofproto *up, void *aux, bool is_ipv6_addr, char *ip_addr, int *l3_egress_id)
{
    FPA_TRACE_FN();
    return 0;
}

static int
ops_fpa_ofproto_get_l3_host_hit(const struct ofproto *up, void *aux, bool is_ipv6_addr, char *ip_addr, bool *hit_bit)
{
    FPA_TRACE_FN();
    return 0;
}

static int
ops_fpa_ofproto_l3_route_action(const struct ofproto *up, enum ofproto_route_action action, struct ofproto_route *route)
{
    FPA_TRACE_FN();
    return 0;
}

static int
ops_fpa_ofproto_l3_ecmp_set(const struct ofproto *up, bool enable)
{
    FPA_TRACE_FN();
    return 0;
}

static int
ops_fpa_ofproto_l3_ecmp_hash_set(const struct ofproto *up, unsigned int hash, bool enable)
{
    FPA_TRACE_FN();
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
    .rule_alloc              = ops_fpa_ofproto_rule_alloc,
    .rule_construct          = ops_fpa_ofproto_rule_construct,
    .rule_insert             = ops_fpa_ofproto_rule_insert,
    .rule_delete             = ops_fpa_ofproto_rule_delete,
    .rule_destruct           = ops_fpa_ofproto_rule_destruct,
    .rule_dealloc            = ops_fpa_ofproto_rule_dealloc,
    .rule_get_stats          = ops_fpa_ofproto_rule_get_stats,
    .rule_execute            = ops_fpa_ofproto_rule_execute,
    .set_frag_handling       = ops_fpa_ofproto_set_frag_handling,
    .packet_out              = ops_fpa_ofproto_packet_out,
    .set_netflow             = ops_fpa_ofproto_set_netflow,
    .get_netflow_ids         = ops_fpa_ofproto_get_netflow_ids,
    .set_sflow               = ops_fpa_ofproto_set_sflow,
    .set_ipfix               = ops_fpa_ofproto_set_ipfix,
    .set_cfm                 = ops_fpa_ofproto_set_cfm,
    .cfm_status_changed      = ops_fpa_ofproto_cfm_status_changed,
    .get_cfm_status          = ops_fpa_ofproto_get_cfm_status,
    .set_lldp                = ops_fpa_ofproto_set_lldp,
    .get_lldp_status         = NULL,//ops_fpa_ofproto_get_lldp_status,
    .set_aa                  = NULL,//ops_fpa_ofproto_set_aa,
    .aa_mapping_set          = ops_fpa_ofproto_aa_mapping_set,
    .aa_mapping_unset        = ops_fpa_ofproto_aa_mapping_unset,
    .aa_vlan_get_queued      = NULL,//ops_fpa_ofproto_aa_vlan_get_queued,
    .aa_vlan_get_queue_size  = NULL,//ops_fpa_ofproto_aa_vlan_get_queue_size,
    .set_bfd                 = ops_fpa_ofproto_set_bfd,
    .bfd_status_changed      = ops_fpa_ofproto_bfd_status_changed,
    .get_bfd_status          = NULL,//ops_fpa_ofproto_get_bfd_status,
    .set_stp                 = ops_fpa_ofproto_set_stp,
    .get_stp_status          = ops_fpa_ofproto_get_stp_status,
    .set_stp_port            = ops_fpa_ofproto_set_stp_port,
    .get_stp_port_status     = ops_fpa_ofproto_get_stp_port_status,
    .get_stp_port_stats      = ops_fpa_ofproto_get_stp_port_stats,
    .set_rstp                = ops_fpa_ofproto_set_rstp,
    .get_rstp_status         = ops_fpa_ofproto_get_rstp_status,
    .set_rstp_port           = ops_fpa_ofproto_set_rstp_port,
    .get_rstp_port_status    = ops_fpa_ofproto_get_rstp_port_status,
    .set_queues              = ops_fpa_ofproto_set_queues,
    .bundle_set              = ops_fpa_ofproto_bundle_set,
    .bundle_remove           = ops_fpa_ofproto_bundle_remove,
    .bundle_get              = NULL,//ops_fpa_ofproto_bundle_get,
    .set_vlan                = ops_fpa_ofproto_set_vlan,
    .mirror_set              = ops_fpa_ofproto_mirror_set,
    .mirror_get_stats        = ops_fpa_ofproto_mirror_get_stats,
    .set_flood_vlans         = ops_fpa_ofproto_set_flood_vlans,
    .is_mirror_output_bundle = ops_fpa_ofproto_is_mirror_output_bundle,
    .forward_bpdu_changed    = ops_fpa_ofproto_forward_bpdu_changed,
    .set_mac_table_config    = NULL,//ops_fpa_ofproto_set_mac_table_config,
    .set_mcast_snooping      = ops_fpa_ofproto_set_mcast_snooping,
    .set_mcast_snooping_port = ops_fpa_ofproto_set_mcast_snooping_port,
    .set_realdev             = ops_fpa_ofproto_set_realdev,
    .meter_get_features      = NULL,//ops_fpa_ofproto_meter_get_features,
    .meter_set               = NULL,//ops_fpa_ofproto_meter_set,
    .meter_get               = NULL,//ops_fpa_ofproto_meter_get,
    .meter_del               = NULL,//ops_fpa_ofproto_meter_del,
    .group_alloc             = ops_fpa_ofproto_group_alloc,
    .group_construct         = ops_fpa_ofproto_group_construct,
    .group_destruct          = ops_fpa_ofproto_group_destruct,
    .group_dealloc           = ops_fpa_ofproto_group_dealloc,
    .group_modify            = ops_fpa_ofproto_group_modify,
    .group_get_stats         = ops_fpa_ofproto_group_get_stats,
    .get_datapath_version    = ops_fpa_ofproto_get_datapath_version,
    .add_l3_host_entry       = NULL,//ops_fpa_ofproto_add_l3_host_entry,
    .delete_l3_host_entry    = NULL,//ops_fpa_ofproto_delete_l3_host_entry,
    .get_l3_host_hit         = NULL,//ops_fpa_ofproto_get_l3_host_hit,
    .l3_route_action         = NULL,//ops_fpa_ofproto_l3_route_action,
    .l3_ecmp_set             = NULL,//ops_fpa_ofproto_l3_ecmp_set,
    .l3_ecmp_hash_set        = NULL,//ops_fpa_ofproto_l3_ecmp_hash_set
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
