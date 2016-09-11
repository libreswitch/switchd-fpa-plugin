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

#include <netdev.h>
#include <netinet/ether.h>
#include <openswitch-idl.h>
#include "connectivity.h"
#include "seq.h"
#include "unixctl.h"

#include "ops-fpa-mac-learning.h"
#include "ops-fpa-routing.h"
#include "ops-fpa-route.h"
#include "ops-fpa-tap.h"

VLOG_DEFINE_THIS_MODULE(ops_fpa_ofproto);


struct host_table_entry
{
    struct hmap_node node;
    in_addr_t ipv4_addr;
    uint32_t arp_index;
    uint32_t l3_group;
    uint32_t l2_group;
};

/* max number of nexthop entries supported */
#define ARP_INDICES_SIZE 1024
static int arp_indices[ARP_INDICES_SIZE + 1];

static struct hmap host_table;
static struct hmap protos = HMAP_INITIALIZER(&protos);
static struct hmap ports_by_pid = HMAP_INITIALIZER(&ports_by_pid);

static int delete_l3_host_entry(const struct ofproto *up, void *aux,
                                bool is_ipv6_addr, char *ip_addr,
                                int *l3_egress_id);

static int add_l3_host_entry(const struct ofproto *up, void *aux,
                             bool is_ipv6_addr, char *ip_addr,
                             char *next_hop_mac_addr, int *l3_egress_id);

struct fpa_ofport *ops_fpa_get_ofport_by_pid(int pid)
{
    struct hmap_node *node = hmap_first_in_bucket(&ports_by_pid, pid);
    return node ? CONTAINER_OF(node, struct fpa_ofport, node) : NULL;
}

static void ops_fpa_ofproto_unixctl_init(void);

/*
 * Factory Functions
 */

struct fpa_ofport *
ops_fpa_ofport(const struct ofproto *up, ofp_port_t ofp_port)
{
    struct ofport *ofport = ofproto_get_port(up, ofp_port);
    return ofport ? FPA_OFPORT(ofport) : NULL;
}

static void
ops_fpa_ofproto_init(const struct shash *iface_hints)
{
    FPA_TRACE_FN();

    /* Perform FPA initialization. */
    ops_fpa_init();

    /* Perform L3 logic initialization. */
    for (int i = 0; i < ARP_INDICES_SIZE; i++) {
        arp_indices[i] = i + 1;
    }
    arp_indices[ARP_INDICES_SIZE] = 0;

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
    struct fpa_ofproto *p;
    HMAP_FOR_EACH(p, node, &protos) {
        if (STR_EQ(type, p->up.type)) {
            sset_add(names, p->up.type);
        }
    }
    return 0;
}

static int
ops_fpa_ofproto_del(const char *type, const char *name)
{
    VLOG_DBG("%s<%s,%s>:", __func__, type, name);
    return 0;
}

static const char *
ops_fpa_ofproto_port_open_type(const char *datapath_type, const char *port_type)
{
    VLOG_DBG("%s: datapath_type=%s port_type=%s", __func__, datapath_type, port_type);
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

/*
 * Top-Level ofproto Functions.
 */

static struct ofproto *
ops_fpa_ofproto_alloc(void)
{
    struct fpa_ofproto *p = xmalloc(sizeof *p);
    return &p->up;
}

static int
ops_fpa_ofproto_construct(struct ofproto *up)
{
    struct fpa_ofproto *this = FPA_OFPROTO(up);

    VLOG_INFO("%s<%s,%s>", __func__, up->type, up->name);

    hmap_insert(&protos, &this->node, hash_string(up->name, 0));
    sset_init(&this->port_names);
    hmap_init(&this->bundles);
    ofproto_init_tables(up, FPA_FLOW_TABLE_MAX);

    memset(this->vlans, 0, sizeof(this->vlans));

    this->switch_id = FPA_DEV_SWITCH_ID_DEFAULT;
    int err = ops_fpa_dev_init(this->switch_id, &this->dev);
    if (err) {
        VLOG_ERR("ops_fpa_dev_init failed");
        return err;
    }
    this->vrf_id = FPA_HAL_L3_DEFAULT_VRID;
    this->change_seq = 0;

    ops_fpa_ofproto_unixctl_init();

    return 0;
}

static void
ops_fpa_ofproto_destruct(struct ofproto *up)
{
    struct fpa_ofproto *this = FPA_OFPROTO(up);
    hmap_destroy(&this->bundles);
    sset_destroy(&this->port_names);
    hmap_remove(&protos, &this->node);

    ops_fpa_dev_deinit(FPA_DEV_SWITCH_ID_DEFAULT);
}

static void
ops_fpa_ofproto_dealloc(struct ofproto *up)
{
    free(FPA_OFPROTO(up));
}

static int
ops_fpa_ofproto_run(struct ofproto *up)
{
    struct fpa_ofproto *this = FPA_OFPROTO(up);
    uint64_t new_seq = 0;

    new_seq = seq_read(connectivity_seq_get());
    if (this->change_seq != new_seq) {
        this->change_seq = new_seq;
    }

    if (STR_EQ(up->type, "system") && STR_EQ(up->name, DEFAULT_BRIDGE_NAME)) {
        if (timer_expired(&this->dev->ml->mlearn_timer)) {
            ops_fpa_mac_learning_on_mlearn_timer_expired(this->dev->ml);
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
    struct fpa_ofport *p = xmalloc(sizeof *p);
    return &p->up;
}

static void
ops_fpa_ofproto_port_dealloc(struct ofport *up)
{
    FPA_TRACE_FN();
    struct fpa_ofport *p = FPA_OFPORT(up);
    free(p);
}

static int
ops_fpa_ofproto_port_construct(struct ofport *up)
{
    VLOG_INFO("%s<%s,%s>: ofp_port=%d", __func__,
              up->ofproto->type, up->ofproto->name, up->ofp_port);
    struct fpa_ofport *p = FPA_OFPORT(up);
    memset(p->vmap, 0, OPS_FPA_VMAP_BYTES);
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
    VLOG_INFO("%s<%s,%s>: ofp_port=%d", __func__,
        up->ofproto->type, up->ofproto->name, up->ofp_port);
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
    VLOG_DBG("%s<%s,%s>: devname=%s", __func__, up->type, up->name, devname);

    struct fpa_ofproto *this = FPA_OFPROTO(up);
    if (!sset_contains(&this->port_names, devname)) {
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

    VLOG_INFO("%s<%s,%s>: devname=%s", __func__, up->type, up->name, devname);

    struct fpa_ofproto *this = FPA_OFPROTO(up);
    sset_add(&this->port_names, devname);

    return 0;
}

static int
ops_fpa_ofproto_port_del(struct ofproto *up, ofp_port_t ofp_port)
{
    VLOG_INFO("%s<%s,%s>: ofp_port=%d", __func__, up->type, up->name, ofp_port);
#if 0
    struct fpa_ofproto *p = FPA_OFPROTO(up);

    struct sset port_names;

    struct ofport *ofport = ofproto_get_port(up, ofp_port);
    sset_find_and_delete(&ofproto->ghost_ports,netdev_get_name(ofport->up.netdev));
    hmap_remove(&ofproto->dp_ports, &port->node);

    p->port_names;
#endif
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
    VLOG_DBG("%s<%s,%s>:", __func__, up->type, up->name);

    *statep = xzalloc(sizeof(struct port_iter));
    return 0;
}

static int
ops_fpa_ofproto_port_dump_next(const struct ofproto *up, void *state,
                               struct ofproto_port *port)
{
    struct fpa_ofproto *this = FPA_OFPROTO(up);
    struct port_iter *iter = state;
    struct sset_node *node;

    VLOG_DBG("%s<%s,%s>: bucket=%d offset=%d", __func__, up->type, up->name,
        iter->bucket, iter->offset
    );

    while ((node = sset_at_position(&this->port_names, &iter->bucket,
                                    &iter->offset))) {
        return ops_fpa_ofproto_port_query_by_name(up, node->name, port);
    }

    return EOF;
}

static int
ops_fpa_ofproto_port_dump_done(const struct ofproto *up, void *state)
{
    VLOG_DBG("%s<%s,%s>:", __func__, up->type, up->name);

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

static struct rule *
ops_fpa_rule_alloc()
{
    VLOG_DBG("%s", __func__);
    return NULL;
}

static struct fpa_bundle *
ops_fpa_find_bundle(const struct ofproto *up, void *aux)
{
    struct fpa_ofproto *this = FPA_OFPROTO(up);
    struct fpa_bundle *bundle;

    HMAP_FOR_EACH_IN_BUCKET(bundle, node, hash_pointer(aux, 0),
                            &this->bundles) {
        if (bundle->aux == aux) {
            return bundle;
        }
    }

    return NULL;
}

static int
ops_fpa_rm_bundle(struct ofproto *up, struct fpa_bundle *bundle)
{
    struct fpa_ofproto *this = FPA_OFPROTO(up);
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
ops_fpa_create_bundle_record(struct ofproto *up, void *aux,
                           struct fpa_bundle **bundle)
{
    struct fpa_ofproto *this = FPA_OFPROTO(up);

    FPA_TRACE_FN();
    *bundle = xmalloc(sizeof **bundle);
    hmap_insert(&this->bundles, &((*bundle)->node), hash_pointer (aux, 0));

    return 0;
}

static struct fpa_net_addr *
find_ipv4_addr_in_bundle(struct fpa_bundle *bundle, const char *address)
{
    struct fpa_net_addr *addr;

    HMAP_FOR_EACH_WITH_HASH(addr, node, hash_string(address, 0),
                             &bundle->secondary_ip4addr) {
        if (STR_EQ(addr->address, address)) {
            return addr;
        }
    }

    return NULL;
}

static int
bundle_ipv4_secondary_reconfigure(struct ofproto *ofproto,
                                   struct fpa_bundle *bundle,
                                   const struct ofproto_bundle_settings *set)
{
    struct shash new_ip_list;

    shash_init(&new_ip_list);

    /* Create hash of the current secondary ip's. */
    for (int i = 0; i < set->n_ip4_address_secondary; i++) {
       if(!shash_add_once(&new_ip_list, set->ip4_address_secondary[i],
                           set->ip4_address_secondary[i])) {
            VLOG_WARN("Duplicate address in secondary list %s",
                      set->ip4_address_secondary[i]);
        }
    }

    /* Compare current and old to delete any obsolete one's */
    struct fpa_net_addr *addr, *next;
    HMAP_FOR_EACH_SAFE(addr, next, node, &bundle->secondary_ip4addr) {
        if (!shash_find_data(&new_ip_list, addr->address)) {
            VLOG_INFO("Remove secondary IPv4 addr %s", addr->address);
            hmap_remove(&bundle->secondary_ip4addr, &addr->node);
            delete_l3_host_entry(ofproto, bundle->aux, false,
                                 bundle->ip4addr->address,
                                 &bundle->intf_id);
            free(addr->address);
            free(addr);
        }
    }
    /* Add the newly added addresses to the list. */
    struct shash_node *addr_node;
    SHASH_FOR_EACH(addr_node, &new_ip_list) {
        struct fpa_net_addr *addr;
        const char *address = addr_node->data;
        if (!find_ipv4_addr_in_bundle(bundle, address)) {
            /* Add the new address to the list */
            VLOG_INFO("Add secondary IPv4 address %s", address);
            addr = xzalloc(sizeof *addr);
            addr->address = xstrdup(address);
            hmap_insert(&bundle->secondary_ip4addr, &addr->node,
                        hash_string(addr->address, 0));
            add_l3_host_entry(ofproto, bundle->aux, false,
                              bundle->ip4addr->address, NULL,
                              &bundle->intf_id);
        }
    }

    return 0;
}

static int
bundle_ip_reconfigure(struct ofproto *ofproto,
                      struct fpa_bundle *bundle,
                      const struct ofproto_bundle_settings *set)
{
    FPA_TRACE_FN();
    /* If primary ipv4 got added/deleted/modified. */
    if (set->ip_change & PORT_PRIMARY_IPv4_CHANGED) {
        if (set->ip4_address) {
            if (bundle->ip4addr) {
                if (!STR_EQ(bundle->ip4addr->address, set->ip4_address)) {
                    VLOG_INFO("Remove primary IPv4 address=%s",
                              bundle->ip4addr->address);
                    /* If current and earlier are different, delete old. */
                    delete_l3_host_entry(ofproto, bundle->aux, false,
                                         bundle->ip4addr->address,
                                         &bundle->intf_id);
                    free(bundle->ip4addr->address);

                    /* Add new. */
                    VLOG_INFO("Add primary IPv4 address=%s", set->ip4_address);
                    bundle->ip4addr->address = xstrdup(set->ip4_address);
                    add_l3_host_entry(ofproto, bundle->aux, false,
                                      bundle->ip4addr->address, NULL,
                                      &bundle->intf_id);
                } /* else no change */
            } else {
                /* Earlier primary was not there, just add new. */
                VLOG_INFO("Add primary IPv4 address=%s", set->ip4_address);
                bundle->ip4addr = xzalloc(sizeof(struct fpa_net_addr));
                bundle->ip4addr->address = xstrdup(set->ip4_address);
                add_l3_host_entry(ofproto, bundle->aux, false,
                                  bundle->ip4addr->address, NULL,
                                  &bundle->intf_id);
            }
        } else {
            /* Primary got removed, earlier if it was there then remove it. */
            if (bundle->ip4addr != NULL) {
                VLOG_INFO("Remove primary IPv4 address=%s",
                          bundle->ip4addr->address);
                delete_l3_host_entry(ofproto, bundle->aux, false,
                                     bundle->ip4addr->address,
                                     &bundle->intf_id);
                free(bundle->ip4addr->address);
                free(bundle->ip4addr);
                bundle->ip4addr = NULL;
            }
        }
    } else if(set->ip_change & PORT_SECONDARY_IPv4_CHANGED) {
        VLOG_ERR("IPv4 secondary address is not supported yet");
        bundle_ipv4_secondary_reconfigure(ofproto, bundle, set);
    } else  if((set->ip_change & PORT_PRIMARY_IPv6_CHANGED)
            || (set->ip_change & PORT_SECONDARY_IPv6_CHANGED)){
        VLOG_ERR("IPv6 is not supported yet");
    }

    return 0;
}

static int
ops_fpa_ofproto_bundle_set(struct ofproto *up, void *aux,
                           const struct ofproto_bundle_settings *set)
{
    int err_no = 0;
    struct fpa_ofproto *this = FPA_OFPROTO(up);
    struct fpa_bundle *bundle = ops_fpa_find_bundle(up, aux);

    /* Remove bundle if needed. */
    if (set == NULL) {
        return ops_fpa_rm_bundle(up, bundle);
    }

    /* Assume bundle have only one port in it. */
    if (set->n_slaves != 1) {
        VLOG_ERR("%s<%s,%s>: n_slaves=%zd not implemented",
            __func__, up->type, up->name, set->n_slaves);
        return EINVAL;
    }

    /* Create bundle if needed. */
    if (bundle == NULL) {
        ops_fpa_create_bundle_record(up, aux, &bundle);
        bundle->name = NULL;

        bundle->aux = aux;

        struct ofport *port = ofproto_get_port(up, set->slaves[0]);
        bundle->intf_id = port ? netdev_get_ifindex(port->netdev)
                              : FPA_INVALID_INTF_ID;

        /* If the first port in the bundle has a pid, add it to ports_by_pid. */
        if (bundle->intf_id != FPA_INVALID_INTF_ID) {
            struct fpa_ofport *port_fpa = FPA_OFPORT(port);
            hmap_insert(&ports_by_pid, &port_fpa->node, bundle->intf_id);
        }

        bundle->l3_intf = NULL;
        bundle->ip4addr = NULL;
        hmap_init(&bundle->secondary_ip4addr);

        /* managing TAP to bridge membership */
        if (STR_EQ(up->type, "system")) {
            ops_fpa_bridge_port_add(set->name);
        }
        else if (STR_EQ(up->type, "vrf")) {
            ops_fpa_bridge_port_rm(set->name);
        }
    }

    /* Set bundle name. */
    if (!bundle->name || !STR_EQ(set->name, bundle->name)) {
        free(bundle->name);
        bundle->name = xstrdup(set->name);
    }

    VLOG_INFO("%s<%s,%s>: name=%s slaves[0]=%d mode=%s vlan=%d intf_id=%X",
        __func__, up->type, up->name, bundle->name, set->slaves[0],
        ops_fpa_str_vlan_mode(set->vlan_mode), set->vlan, bundle->intf_id
    );

    /* For now, we don't support L3 on top of LAG. */
    if (STR_EQ(up->type, "vrf")) {
        VLOG_INFO("Configure vrf interface");

        int vlan_id = set->vlan;

        struct fpa_ofport *port = ops_fpa_ofport(up, set->slaves[0]);
        if (port == NULL) {
            VLOG_ERR("Slave is not in the ports");
            return 0;
        }

        const char *type = netdev_get_type(port->up.netdev);
        VLOG_INFO("netdev type: %s", type);

        if (STR_EQ(type, OVSREC_INTERFACE_TYPE_SYSTEM)) {
            vlan_id = smap_get_int(set->port_options[PORT_HW_CONFIG],
                                    "internal_vlan_id", 0);
            VLOG_INFO("%s: system interface vlan = %d", __func__, vlan_id);
        }
        else if (STR_EQ(type, OVSREC_INTERFACE_TYPE_INTERNAL)) {
            VLOG_INFO("%s: internal interface vlan = %d", __func__, vlan_id);
        }
        else if (STR_EQ(type, OVSREC_INTERFACE_TYPE_LOOPBACK)) {
            /* For l3-loopback interfaces, just configure ips. */
            VLOG_WARN("%s Done with l3 loopback configurations", __func__);
            goto done;
        }
        else {
            VLOG_WARN("%s is not supported yet", type);
            err_no = ENOSYS;
            goto done;
        }

        VLOG_INFO("VLAN_ID=%d",vlan_id);

        /* If reserved vlan changed/removed or if port status is disabled. */
        if (bundle->l3_intf) {
            /* Disable routing only if there is no active routes. Otherwise it
             * will be disabled after all routes are deleted. */
            if (!set->enable && bundle->l3_intf->routes_count == 0) {
                VLOG_INFO("Disable ROUTING");
                ops_fpa_disable_routing(bundle->l3_intf);
                bundle->l3_intf = NULL;
            }
        }

        /* Create L3 structure if needed. */
        if (vlan_id && !bundle->l3_intf && set->enable) {
            struct eth_addr mac;
            netdev_get_etheraddr(port->up.netdev, &mac);

            VLOG_INFO("%s: NETDEV %s, MAC "ETH_ADDR_FMT" VLAN %u",
                      __func__, netdev_get_name(port->up.netdev),
                      ETH_ADDR_ARGS(mac), vlan_id);

            if (STR_EQ(type, OVSREC_INTERFACE_TYPE_SYSTEM)) {
                VLOG_INFO("Enabling ROUTING on SYSTEM interface");
                bundle->l3_intf = ops_fpa_enable_routing_interface(
                        this->switch_id, bundle->intf_id, vlan_id, mac);
                if(!bundle->l3_intf) {
                    VLOG_ERR("Can't enable routing on interface %d",
                             bundle->intf_id);
                    err_no = EINVAL;
                    goto done;
                }
            }
            else if (STR_EQ(type, OVSREC_INTERFACE_TYPE_VLANSUBINT)) {
                VLOG_INFO("Enabling ROUTING on VLAN SUBINTERFACE");
                VLOG_WARN("Subinterface is not supported yet");
                err_no = ENOSYS;
                goto done;
            }
            else if (STR_EQ(type, OVSREC_INTERFACE_TYPE_INTERNAL)) {
                VLOG_INFO("Enabling ROUTING on internal VLAN interface");
                bundle->l3_intf = ops_fpa_enable_routing_vlan(
                        this->switch_id, vlan_id, mac);
            }
            else {
                VLOG_ERR("%s: unknown interface type: %s", __func__, type);
                err_no = EINVAL;
                goto done;
            }
        }

        bundle_ip_reconfigure(up, bundle, set);
    }
    else if (bundle->intf_id != FPA_INVALID_INTF_ID) {
        /* If ofproto's name is equal to bundle name => interface internal. */
        if (STR_EQ(up->name, bundle->name)) {
            VLOG_INFO("INTERNAL INTERFACE");
            /* Nothing to do for internal port. */
            return 0;
        }

        struct fpa_ofport *port = ops_fpa_get_ofport_by_pid(bundle->intf_id);
        /* convert set trunks,vlan_mode and vlan members to vnew bitmap */
        unsigned long vnew[OPS_FPA_VMAP_LONGS];
        memset(vnew, 0, OPS_FPA_VMAP_BYTES);
        if (set->trunks) {
            int vid;
            BITMAP_FOR_EACH_1(vid, VLAN_BITMAP_SIZE, set->trunks) {
                bitmap_set1(vnew, OPS_FPA_VIDX_INGRESS(vid, true));
                bitmap_set1(vnew, OPS_FPA_VIDX_EGRESS(vid, false));
            }
        }
        if (set->vlan_mode != PORT_VLAN_TRUNK) {
            bitmap_set1(vnew, OPS_FPA_VIDX_INGRESS(set->vlan, false));
            bitmap_set1(vnew, OPS_FPA_VIDX_EGRESS(set->vlan, set->vlan_mode != PORT_VLAN_NATIVE_TAGGED));
        }
        /* fetch FPA L2 port state into vdiff bitmap */
        unsigned long vdiff[OPS_FPA_VMAP_LONGS];
        ops_fpa_vlan_fetch(this->switch_id, netdev_get_ifindex(port->up.netdev), vdiff);
        /* calc diff between actual and desired state */
        for (int i = 0; i < OPS_FPA_VMAP_LONGS; i++) {
            vdiff[i] ^= vnew[i];
        }
        /* apply diff to FPA and port->vmap */
        memset(port->vmap, 0, OPS_FPA_VMAP_BYTES);
        int vidx;
        BITMAP_FOR_EACH_1(vidx, OPS_FPA_VMAP_BITS, vdiff) {
            int vid = OPS_FPA_VIDX_VID(vidx);
            if (bitmap_is_set(this->vlans, vid)) {
                if (bitmap_is_set(vnew, vidx)) {
                    ops_fpa_vlan_add(this->switch_id, netdev_get_ifindex(port->up.netdev), vidx);
                }
                else {
                    ops_fpa_vlan_del(this->switch_id, netdev_get_ifindex(port->up.netdev), vidx);
                }
            }
            else {
                if (bitmap_is_set(vnew, vidx)) {
                    bitmap_set1(port->vmap, vidx);
                }
            }
        }
    }

done:

    return err_no;
}

static void
ops_fpa_ofproto_bundle_remove(struct ofport *up)
{
    VLOG_INFO("%s<%s,%s>: ofp_port=%d", __func__,
        up->ofproto->type, up->ofproto->name, up->ofp_port);
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
    VLOG_INFO("%s<%s,%s>: %s vid %d",
        __func__, up->type, up->name, add ? "ENABLE" : "DISABLE", vid);

    struct fpa_ofproto *this = FPA_OFPROTO(up);

    /* update FPA and ports vmaps*/
    struct fpa_bundle *bundle;
    int vidx;
    HMAP_FOR_EACH(bundle, node, &this->bundles) {
        struct fpa_ofport *port = ops_fpa_get_ofport_by_pid(bundle->intf_id);
        if (port) {
            if (add) {
                BITMAP_FOR_EACH_1(vidx, OPS_FPA_VMAP_BITS, port->vmap) {
                    /* add vid to FPA, remove from pending */
                    if (OPS_FPA_VIDX_VID(vidx) == vid) {
                        ops_fpa_vlan_add(this->switch_id, netdev_get_ifindex(port->up.netdev), vidx);
                        bitmap_set0(port->vmap, vidx);
                    }
                }
            }
            else {
                unsigned long l2asic[OPS_FPA_VMAP_LONGS];
                ops_fpa_vlan_fetch(this->switch_id, port->up.ofp_port, l2asic);
                BITMAP_FOR_EACH_1(vidx, OPS_FPA_VMAP_BITS, l2asic) {
                    /* delete vid from FPA, add to pending */
                    if (OPS_FPA_VIDX_VID(vidx) == vid) {
                        ops_fpa_vlan_del(this->switch_id, netdev_get_ifindex(port->up.netdev), vidx);
                        bitmap_set1(port->vmap, vidx);
                    }
                }
            }
        }
    }
    /* update ofproto vlans state bitmap */
    bitmap_set(this->vlans, vid, add);

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
add_l3_host_entry(const struct ofproto *up, void *aux,
                  bool is_ipv6_addr, char *ip_addr,
                  char *next_hop_mac_addr, int *l3_egress_id)
{
    VLOG_INFO("%s<%s,%s>: ip_addr=%s next_hop_mac_addr=%s",
        __func__, up->type, up->name, ip_addr, next_hop_mac_addr);

    if (is_ipv6_addr) {
        VLOG_ERR("%s: IPv6 is not supported yet.", __func__);
        return EINVAL;
    }

    if (!next_hop_mac_addr) {
        /* TODO: If the next hop is NULL, we should configure FPA to trap
         * packets on CPU. Really? */
        return EINVAL;
    }

    /* Get target bundle and port. */
    struct fpa_bundle *bundle = ops_fpa_find_bundle(up, aux);
    struct fpa_ofport *port = ops_fpa_get_ofport_by_pid(bundle->intf_id);

    if (!bundle->l3_intf) {
        VLOG_ERR("%s: L3 interface is disabled on bundle %s.", __func__,
                 bundle->name);
        return EINVAL;
    }

    uint32_t switch_id = bundle->l3_intf->switchId;
    uint32_t vlan_id = bundle->l3_intf->vlan_id;

    VLOG_INFO("    bundle->name = %s", bundle->name);
    VLOG_INFO("    switch_id = %d", switch_id);
    VLOG_INFO("    vlan_id = %d", vlan_id);

    struct eth_addr src_mac_addr;
    netdev_get_etheraddr(port->up.netdev, &src_mac_addr);

    struct eth_addr dst_mac_addr;
    if (!eth_addr_from_string(next_hop_mac_addr, &dst_mac_addr)) {
        VLOG_ERR("%s: Bad nexthop address %s.", __func__, next_hop_mac_addr);
        return EINVAL;
    }

    if (ops_fpa_vlan_internal(vlan_id)) {
        *l3_egress_id = bundle->l3_intf->intf_id;
    } else {
        /* Lookup for the right egress id in FDB. */
        struct fpa_dev *dev = ops_fpa_dev_by_id(switch_id);
        if (!dev) {
            return FPA_BAD_PARAM;
        }

        FPA_MAC_ADDRESS_STC mac;
        memcpy(&mac, &dst_mac_addr, sizeof mac);

        ovs_rwlock_rdlock(&dev->ml->rwlock);
        struct fpa_mac_entry *mac_entry =
            ops_fpa_mac_learning_lookup_by_vlan_and_mac(dev->ml, vlan_id, mac);
        ovs_rwlock_unlock(&dev->ml->rwlock);

        *l3_egress_id = mac_entry->fdb_entry.portNum;
    }
    VLOG_INFO("    l3_egress_id = %d", *l3_egress_id);

    /* Parse host's IPv4 address. */
    int mask_len;
    in_addr_t ipv4_addr;
    int ret = ops_fpa_str2ip(ip_addr, &ipv4_addr, &mask_len);
    if (ret != 0 || mask_len != 32) {
        VLOG_ERR("%s: Bad IPv4 address %s.", __func__, ip_addr);
        return EINVAL;
    }

    /* Add L2 interface group for the port. */
    uint32_t l2_group;
    if (ops_fpa_route_add_l2_group(
            switch_id, *l3_egress_id, vlan_id, false, &l2_group)) {
        return EINVAL;
    }

    /* Allocate ARP index. */
    if (arp_indices[0] == 0) {
        VLOG_ERR("%s: Can't allocate ARP index", __func__);
        ops_fpa_route_del_group(switch_id, l2_group);
        return EINVAL;
    }
    int arp_index = arp_indices[0];
    arp_indices[0] = arp_indices[arp_index];
    arp_indices[arp_index] = 0;

    /* Add L3 unicast group. */
    uint32_t l3_group;
    if (ops_fpa_route_add_l3_group(
            switch_id, l2_group, arp_index, vlan_id, port->up.mtu,
            &src_mac_addr, &dst_mac_addr, &l3_group)) {
        ops_fpa_route_del_group(switch_id, l2_group);
        return EINVAL;
    }

    /* Add entry about host's IP into the unicast routing flow table. */
    if (ops_fpa_route_add_route(switch_id, l3_group, ipv4_addr, mask_len)) {
        ops_fpa_route_del_group(switch_id, l2_group);
        ops_fpa_route_del_group(switch_id, l3_group);
        return EINVAL;
    }

    /* Add record into the host table. */
    host_table_add(ipv4_addr, arp_index, l3_group, l2_group);

    /* Increment routes counter. */
    bundle->l3_intf->routes_count++;

    return 0;
}

static int
delete_l3_host_entry(const struct ofproto *up, void *aux,
                     bool is_ipv6_addr, char *ip_addr,
                     int *l3_egress_id)
{
    VLOG_INFO("%s<%s,%s>: ip_addr=%s l3_egress_id=%d", __func__,
        up->type, up->name, ip_addr, *l3_egress_id);

    if (is_ipv6_addr) {
        VLOG_ERR("%s: IPv6 is not supported yet.", __func__);
    	return EINVAL;
    }

    /* Get target bundle. */
    struct fpa_bundle *bundle = ops_fpa_find_bundle(up, aux);

    if (!bundle->l3_intf) {
        VLOG_ERR("%s: L3 interface is disabled on bundle %s.", __func__,
                 bundle->name);
        return EINVAL;
    }

    uint32_t switch_id = bundle->l3_intf->switchId;

    /* Parse host's IPv4 address. */
    int mask_len;
    in_addr_t ipv4_addr;
    int ret = ops_fpa_str2ip(ip_addr, &ipv4_addr, &mask_len);
    if (ret != 0 || mask_len != 32) {
        VLOG_ERR("%s: Bad IPv4 address %s.", __func__, ip_addr);
        return EINVAL;
    }

    struct host_table_entry *entry = host_table_find(ipv4_addr);
    if (entry == NULL){
        VLOG_ERR("%s: Can't find entry for %s.", __func__, ip_addr);
        return EINVAL;
    }

    /* Delete routes. */
    ops_fpa_route_del_route(switch_id, ipv4_addr, mask_len);
    ops_fpa_route_del_group(switch_id, entry->l3_group);
    ops_fpa_route_del_group(switch_id, entry->l2_group);
    /* release ARP index */
    arp_indices[entry->arp_index] = arp_indices[0];
    arp_indices[0] = entry->arp_index;

    host_table_delete(entry);

    /* Decrement routes counter. */
    if (--bundle->l3_intf->routes_count == 0) {
        /* Disable routing only when there is no more active routes. */
        VLOG_INFO("Disable ROUTING");
        ops_fpa_disable_routing(bundle->l3_intf);
        bundle->l3_intf = NULL;
    }

    return 0;
}

static int
get_l3_host_hit(const struct ofproto *up, void *aux,
                bool is_ipv6_addr, char *ip_addr, bool *hit_bit)
{
    VLOG_INFO("%s<%s,%s>: ip_addr=%s", __func__, up->type, up->name, ip_addr);
    struct fpa_ofproto *this = FPA_OFPROTO(up);

    if (is_ipv6_addr) {
        VLOG_ERR("%s: IPv6 is not supported yet.", __func__);
        return EINVAL;
    }

    /* Parse host's IPv4 address. */
    int mask_len;
    in_addr_t ipv4_addr;
    int ret = ops_fpa_str2ip(ip_addr, &ipv4_addr, &mask_len);
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
    int err = fpaLibGroupEntryStatisticsGet(
        this->switch_id, host_entry->l3_group, &counters_entry);
    if (err) {
        return EINVAL;
    }

    *hit_bit = (counters_entry.referenceCount > 1);
    return 0;
}

static int
l3_route_action(const struct ofproto *up,
                enum ofproto_route_action action,
                struct ofproto_route *route)
{
    struct fpa_ofproto *this = FPA_OFPROTO(up);

    VLOG_INFO("%s<%s,%s>: action=%s route->prefix=%s route->nexthop=%s",
        __func__, up->type, up->name,
        ops_fpa_str_raction(action), route->prefix, route->nexthops[0].id
    );

    int ret = 0;
    in_addr_t route_ipv4_address;
    int route_mask_len;

    switch (route->family) {
    case OFPROTO_ROUTE_IPV4:
        ret = ops_fpa_str2ip(route->prefix, &route_ipv4_address,
                                  &route_mask_len);
        if (ret != 0) {
            VLOG_ERR("%s: Bad IPv4 address %s.", __func__, route->prefix);
            return EINVAL;
        }

        VLOG_INFO("   IPv4 = %d.%d.%d.%d", route_ipv4_address & 0xFF,
                                           (route_ipv4_address >> 8) & 0xFF,
                                           (route_ipv4_address >> 16) & 0xFF,
                                           (route_ipv4_address >> 24) & 0xFF);
        VLOG_INFO("   route_mask_len = %d", route_mask_len);

        break;

    default:
        VLOG_ERR("%s: IPv6 is not supported yet.", __func__);
        return EINVAL;
    }

    int err;
    in_addr_t host_ipv4_address;
    int host_mask_len;
    struct host_table_entry *host_entry = NULL;

    uint32_t switchid = this->switch_id;

    switch (action) {
    case OFPROTO_ROUTE_ADD:
        /* No ecmp - only 1 nexhhop. */
        if (route->n_nexthops != 1) {
            VLOG_ERR("%s: ECMP is not supported yet.", __func__);
            return EINVAL;
        }

        if (route->nexthops[0].state == OFPROTO_NH_RESOLVED) {
            ret = ops_fpa_str2ip(route->nexthops[0].id,
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

            ops_fpa_route_del_route(switchid, route_ipv4_address, route_mask_len);
            err = ops_fpa_route_add_route(switchid, host_entry->l3_group,
                                        route_ipv4_address, route_mask_len);
#if 0
        } else if (route->nexthops[0].id) {
            /* TODO: Route packets into a port. */
#endif
        } else {
            /* Unresolved - trap to cpu. */
            ops_fpa_route_del_route(switchid, route_ipv4_address, route_mask_len);
            err = ops_fpa_route_add_route_trap(switchid, route_ipv4_address,
                                               route_mask_len);
        }

        if (err) {
            return EINVAL;
        }

        break;

    case OFPROTO_ROUTE_DELETE:
        /* XXX TRAP TO CPU DELETE ????*/
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
    .port_poll               = NULL,
    .port_poll_wait          = ops_fpa_ofproto_port_poll_wait,
    .port_is_lacp_current    = ops_fpa_ofproto_port_is_lacp_current,
    .port_get_lacp_stats     = ops_fpa_ofproto_port_get_lacp_stats,
    .rule_choose_table       = ops_fpa_ofproto_rule_choose_table,
    .rule_alloc              = ops_fpa_rule_alloc,
    .rule_construct          = NULL,
    .rule_insert             = NULL,
    .rule_delete             = NULL,
    .rule_destruct           = NULL,
    .rule_dealloc            = NULL,
    .rule_get_stats          = NULL,
    .rule_execute            = NULL,
    .set_frag_handling       = NULL,
    .packet_out              = NULL,
    .set_netflow             = NULL,
    .get_netflow_ids         = NULL,
    .set_sflow               = NULL,
    .set_ipfix               = NULL,
    .set_cfm                 = NULL,
    .cfm_status_changed      = NULL,
    .get_cfm_status          = NULL,
    .set_lldp                = NULL,
    .get_lldp_status         = NULL,
    .set_aa                  = NULL,
    .aa_mapping_set          = NULL,
    .aa_mapping_unset        = NULL,
    .aa_vlan_get_queued      = NULL,
    .aa_vlan_get_queue_size  = NULL,
    .set_bfd                 = NULL,
    .bfd_status_changed      = NULL,
    .get_bfd_status          = NULL,
    .set_stp                 = NULL,
    .get_stp_status          = NULL,
    .set_stp_port            = NULL,
    .get_stp_port_status     = NULL,
    .get_stp_port_stats      = NULL,
    .set_rstp                = NULL,
    .get_rstp_status         = NULL,
    .set_rstp_port           = NULL,
    .get_rstp_port_status    = NULL,
    .set_queues              = NULL,
    .bundle_set              = ops_fpa_ofproto_bundle_set,
    .bundle_remove           = ops_fpa_ofproto_bundle_remove,
    .bundle_get              = NULL,
    .set_vlan                = ops_fpa_ofproto_set_vlan,
    .mirror_set              = NULL,
    .mirror_get_stats        = NULL,
    .set_flood_vlans         = NULL,
    .is_mirror_output_bundle = NULL,
    .forward_bpdu_changed    = NULL,
    .set_mac_table_config    = NULL,
    .set_mcast_snooping      = NULL,
    .set_mcast_snooping_port = NULL,
    .set_realdev             = NULL,
    .meter_get_features      = NULL,
    .meter_set               = NULL,
    .meter_get               = NULL,
    .meter_del               = NULL,
    .group_alloc             = NULL,
    .group_construct         = NULL,
    .group_destruct          = NULL,
    .group_dealloc           = NULL,
    .group_modify            = NULL,
    .group_get_stats         = NULL,
    .get_datapath_version    = NULL,
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

static struct fpa_ofproto *
ops_fpa_ofproto_lookup(const char *name)
{
    ovs_assert(name);

    struct fpa_ofproto *p;
    HMAP_FOR_EACH_WITH_HASH(p, node, hash_string(name, 0), &protos) {
        if (STR_EQ(p->up.name, name)) {
            return p;
        }
    }
    return NULL;
}

static void
fpa_unixctl_fdb_flush(struct unixctl_conn *conn, int argc OVS_UNUSED,
                     const char *argv[] OVS_UNUSED, void *aux OVS_UNUSED)
{
    struct fpa_ofproto *ofproto;

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
    const struct fpa_ofproto *ofproto = NULL;

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
    const struct fpa_ofproto *ofproto = NULL;
    unsigned int idle_time;
    int err;

    /* Get bridge */
    ofproto = ops_fpa_ofproto_lookup(argv[1]);
    if (!ofproto) {
        unixctl_command_reply_error(conn, "no such bridge");
        return;
    }

    err = ops_fpa_mac_learning_get_idle_time(ofproto->dev->ml, &idle_time);
    if (err) {
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
    const struct fpa_ofproto *ofproto = NULL;
    unsigned int idle_time;
    int err;

    /* Get bridge */
    ofproto = ops_fpa_ofproto_lookup(argv[1]);
    if (!ofproto) {
        unixctl_command_reply_error(conn, "no such bridge");
        return;
    }

    /* Get Age time. */
    err = ovs_scan(argv[2], "%"SCNu32, &idle_time);
    if (!err) {
        unixctl_command_reply_error(conn, "invalid aging time");
        return;
    }

    err = ops_fpa_mac_learning_set_idle_time(ofproto->dev->ml, idle_time);
    if (err) {
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
