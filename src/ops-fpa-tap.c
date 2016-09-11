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
 *  File: ops-fpa-tap.c
 *
 *  Purpose: This file contains OpenSwitch CPU TAP interface related
 *           application code for the FPA library.
 */

#include <unistd.h>
#include <sys/ioctl.h>
#include <net/if.h>
#include <fcntl.h>
#include <linux/if_tun.h>
#include <netinet/ether.h>
#include <openswitch-idl.h>

#include <ofp-parse.h>
#include <socket-util.h>
#include <hmap.h>

#include "ops-fpa-util.h"
#include "ops-fpa-dev.h"
#include "ops-fpa-tap.h"
#include "ops-fpa-vlan.h"

#define FPA_HAL_MAX_MTU_CNS     10240

#define OPS_FPA_THREAD_TAP          0
#define OPS_FPA_THREAD_ASIC         1
#define OPS_FPA_THREAD_CNT          2

#define OPS_FPA_THREAD_PIPE_RECV    0 /* Pipe witch use for recv command into thread listener */
#define OPS_FPA_THREAD_PIPE_SEND    1 /* Pipe witch use for send command to thread listener */

#define OPS_FPA_PRINT_10_BYTES_FMT \
    "0x%02"PRIx8" 0x%02"PRIx8" 0x%02"PRIx8" 0x%02"PRIx8"  0x%02"PRIx8" 0x%02"PRIx8" 0x%02"PRIx8" 0x%02"PRIx8"  0x%02"PRIx8" 0x%02"PRIx8
#define OPS_FPA_PRINT_10_BYTES_ARGS(EAB) \
    (EAB)[0], (EAB)[1], (EAB)[2], (EAB)[3], (EAB)[4], (EAB)[5], (EAB)[6], (EAB)[7], (EAB)[8], (EAB)[9]

VLOG_DEFINE_THIS_MODULE(ops_fpa_tap);

static struct vlog_rate_limit rl = VLOG_RATE_LIMIT_INIT(5, 20);

/* Linux tun/tap interface entry */
struct tap_if_entry {

    /* TAP interface name */
    char *name;

    union {
        /* FD of TAP interface */
        int fd;

        /* HW port number */
        uint32_t portNum;
    };

    /* TAP MAC address. */
    struct ether_addr mac; /*TODO remove when TAP MAC will be obtained from netlink socket */

    /* Node in map */
    struct hmap_node node;
};

/* Linux tun/tap interface information for switch once */
struct tap_info {

    /* FPA device ID */
    uint32_t switchId;

    /* FD to TAP interface entry map */
    struct hmap fd_to_tap_if_map;

    /* Threads */
    pthread_t thread[OPS_FPA_THREAD_CNT];

    /* Pipe which will notify thread about new/delete interface */
    int ctrl_fds[OPS_FPA_THREAD_CNT][2];
};
/****************************************************************************/

typedef void * (*main_thread_func)(void *);

struct listener_info {
    const char *name;
    main_thread_func main_func;
};

void *tap_listener(void *arg);
void *asic_listener(void *arg);

struct listener_info listener[OPS_FPA_THREAD_CNT] = {
    {"tap-listener", tap_listener},
    {"asic-listener", asic_listener}
};

/****************************************************************************
* Commands for notify thread about new/delete interface and thread exit
****************************************************************************/

typedef enum {
    OPS_FPA_CMD_THREAD_EXIT,
    OPS_FPA_CMD_ADD_IF,
    OPS_FPA_CMD_DEL_IF,
    OPS_FPA_CMD_CNT
} ops_fpa_cmd_type;

struct ctrl_cmd {

    ops_fpa_cmd_type type;

    union {
        struct {
            /* TAP interface name */
            char tap_if_name[IFNAMSIZ];

            /* FD of TAP interface */
            int fd;

            /* HW port number */
            uint32_t portNum;

            /* TAP MAC address. */
            struct ether_addr mac; /*TODO remove when TAP MAC will be obtained from netlink socket */
        } add;
        struct {
            /* FD of TAP interface */
            int fd;

            /* HW port number */
            uint32_t portNum;
        } del;
    };
};

int send_ctrl_cmd(int fd, const struct ctrl_cmd *cmd);
int recv_ctrl_cmd(int fd, struct ctrl_cmd *cmd);

int send_ctrl_cmd(int fd, const struct ctrl_cmd *cmd)
{
    ovs_assert(fd >= 0);
    ovs_assert(cmd);

    /* TODO: Need processing return value from call 'write' */
    ignore(write(fd, cmd, sizeof(struct ctrl_cmd)));

    return 0;
}

int recv_ctrl_cmd(int fd, struct ctrl_cmd *cmd)
{
    int size;

    ovs_assert(fd >= 0);
    ovs_assert(cmd);

    size = read(fd, cmd, sizeof(struct ctrl_cmd));

    return size;
}
/****************************************************************************/

struct tap_if_entry *get_if_entry(const struct hmap *map, int key);

extern bool
ops_fpa_is_internal_vlan(int vid);

int ops_fpa_net_if_setup(const char *name, const struct ether_addr *mac);
int ops_fpa_tun_alloc(char *name, int flags);

int get_port_eg_tag_state(uint32_t switchId, uint32_t portNum, uint16_t vlanId, bool *pop_tag);

uint16_t
ops_fpa_get_eth_type(void *pkt)
{
    return ntohs(((struct ether_header *)pkt)->ether_type);
}

struct tap_info *
ops_fpa_tap_init(uint32_t switchId)
{
    int i, n, rc;
    struct tap_info *info;

    ovs_assert(switchId != FPA_INVALID_SWITCH_ID);

    VLOG_INFO("TAP interface init for FPA device (%d)", switchId);

    info = get_tap_info_by_switch_id(switchId);
    if (info) {
        VLOG_ERR("TAP interfaces for FPA device (%d) already exist", switchId);
        return info;
    }

    /* Create TAP info for switch */
    info = xzalloc(sizeof *info);
    info->switchId = switchId;

    hmap_init(&info->fd_to_tap_if_map);

    /* For all threads */
    for (i = 0; i < OPS_FPA_THREAD_CNT; i++) {

        /* Create control pipes */
        xpipe(info->ctrl_fds[i]);

        /* Set a fds into nonblocking mode */
        for (n = 0; n < 2; n++) {
            rc = set_nonblocking(info->ctrl_fds[i][n]);
            if (rc) {
                VLOG_ERR("Unable to set control pipe %d into nonblocking mode", info->ctrl_fds[i][n]);
                close(info->ctrl_fds[i][n]);
                return NULL;
            }
        }

        /* Run thread */
        info->thread[i] = ovs_thread_create(listener[i].name, listener[i].main_func, info);
        VLOG_INFO("%s thread started", listener[i].name);
    }

    return info;
}

void
ops_fpa_tap_deinit(uint32_t switchId)
{
    int i;
    struct tap_if_entry *e;
    struct tap_if_entry *next;
    struct tap_info *info;
    struct ctrl_cmd cmd = { .type = OPS_FPA_CMD_THREAD_EXIT };

    ovs_assert(switchId != FPA_INVALID_SWITCH_ID);

    info = get_tap_info_by_switch_id(switchId);

    if (!info) {
        return;
    }

    /* Remove TAP interfaces */
    HMAP_FOR_EACH_SAFE(e, next, node, &info->fd_to_tap_if_map) {
        ops_fpa_tap_if_delete(switchId, e->fd);
    }

    hmap_destroy(&info->fd_to_tap_if_map);

    /* For all threads */
    for (i = 0; i < OPS_FPA_THREAD_CNT; i++) {

        /* Stop listener thread */
        send_ctrl_cmd(info->ctrl_fds[i][OPS_FPA_THREAD_PIPE_SEND], &cmd);
        xpthread_join(info->thread[i], NULL);

        close(info->ctrl_fds[i][OPS_FPA_THREAD_PIPE_RECV]);
        close(info->ctrl_fds[i][OPS_FPA_THREAD_PIPE_SEND]);
    }

    free(info);

    VLOG_INFO("Host interface TAP-based instance deallocated");
}

int
ops_fpa_bridge_create(const char *name, const struct ether_addr *mac)
{
    int rc;

    rc = ops_fpa_system("ip link add name %s type bridge", name);
    if (rc) {
        VLOG_WARN("Error executing ip for creating bridge '%s', rc=%d. Possibly bridge is already created",
            name, rc);
    }

    if (0 != ops_fpa_net_if_setup(name, mac)) {
        VLOG_ERR("Unable to setup bridge interface '%s'", name);
        return EPERM;
    }

    rc = ops_fpa_system("ip link set %s up", name);
    if (rc) {
        VLOG_ERR("Error setting bridge '%s' up, rc=%d", name, rc);
        return EPERM;
    }

    return 0;
}

int
ops_fpa_bridge_delete(const char *name)
{
    int rc;

    rc = ops_fpa_system("ip link delete %s type bridge", name);
    if (rc) {
        VLOG_ERR("Error deleting bridge '%s', rc=%d", name, rc);
        return EFAULT;
    }

    return 0;
}

int
ops_fpa_bridge_port_add(const char *name)
{
    int rc;

    rc = ops_fpa_system("ip link set %s master %s", name, DEFAULT_BRIDGE_NAME);
    if (rc) {
        VLOG_ERR("Error adding TAP '%s' to bridge '%s' up, rc=%d", name, DEFAULT_BRIDGE_NAME, rc);
        return EFAULT;
    }

    return 0;
}

int
ops_fpa_bridge_port_rm(const char *name)
{
    int rc;

    rc = ops_fpa_system("ip link set %s nomaster", name);
    if (rc) {
        VLOG_ERR("Error removing TAP '%s' from bridge, rc=%d", name, rc);
    }

    return 0;
}

int
ops_fpa_tap_if_create(uint32_t switchId, uint32_t portNum, const char *name,
                      const struct ether_addr *mac, int* tap_fd)
{
    int i, rc, fd;
    char tap_if_name[IFNAMSIZ];
    struct tap_info *info;
    struct tap_if_entry *if_entry;
    struct ctrl_cmd cmd;

    ovs_assert(switchId != FPA_INVALID_SWITCH_ID);
    ovs_assert(name);
    ovs_assert(mac);
    ovs_assert(tap_fd);

    ops_fpa_dev_mutex_lock();
    info = get_tap_info_by_switch_id(switchId);
    if (!info) {
        VLOG_ERR("TAP interface not initialized for FPA device (%d)", switchId);
        return EFAULT;
    }

    snprintf(tap_if_name, IFNAMSIZ, "%s", name);
    fd = ops_fpa_tun_alloc(tap_if_name, (IFF_TAP | IFF_NO_PI));
    if (fd <= 0) {
        VLOG_ERR("Unable to create TAP interface '%s'", tap_if_name);
        return EFAULT;
    }

    rc = set_nonblocking(fd);
    if (rc) {
        VLOG_ERR("Unable to set TAP interface '%s' into nonblocking mode", tap_if_name);
        close(fd);
        return EFAULT;
    }

    if (0 != ops_fpa_net_if_setup(tap_if_name, mac)) {
        VLOG_ERR("Unable to setup TAP interface '%s'", tap_if_name);
        close(fd);
        return EFAULT;
    }

    /* Creates new TAP interface entry */
    if_entry = xzalloc(sizeof(* if_entry));
    if_entry->portNum = portNum;
    if_entry->name = xstrdup(tap_if_name);
    memcpy(&if_entry->mac, mac, ETH_ALEN);

    VLOG_INFO("TAP interface '%s' created: portNum=%d, fd=%d, MAC=" ETH_ADDR_FMT,
              tap_if_name, portNum, fd,
              ETH_ADDR_BYTES_ARGS(mac->ether_addr_octet));

    *tap_fd = fd;

    /* Inserts TAP info entry to map */
    hmap_insert(&info->fd_to_tap_if_map, &if_entry->node, fd);

    /* Creates add ctrl command for listener threads */
    cmd.type = OPS_FPA_CMD_ADD_IF;
    cmd.add.fd = fd;
    cmd.add.portNum = portNum;
    memcpy(cmd.add.tap_if_name, tap_if_name, IFNAMSIZ);
    memcpy(&cmd.add.mac, &if_entry->mac, ETH_ALEN);

    /* Notifies all listener threads about new TAP interface */
    for (i = 0; i < OPS_FPA_THREAD_CNT; i++) {
        send_ctrl_cmd(info->ctrl_fds[i][OPS_FPA_THREAD_PIPE_SEND], &cmd);
    }

    ops_fpa_dev_mutex_unlock();

    return 0;
}

int
ops_fpa_tap_if_delete(uint32_t switchId, int tap_fd)
{
    int i;
    struct tap_info *info;
    struct tap_if_entry *if_entry;
    struct ctrl_cmd cmd;

    ovs_assert(switchId != FPA_INVALID_SWITCH_ID);
    ovs_assert(tap_fd>0);

    ops_fpa_dev_mutex_lock();
    info = get_tap_info_by_switch_id(switchId);
    if (!info) {
        VLOG_ERR("TAP interface not initialized for FPA device (%d)", switchId);
        return EFAULT;
    }

    /* Get TAP interface entry for fd */
    if_entry = get_if_entry(&info->fd_to_tap_if_map, tap_fd);
    if (!if_entry) {
        return ENOENT;
    }

    /* Creates delete ctrl command for listener threads */
    cmd.type = OPS_FPA_CMD_DEL_IF;
    cmd.del.fd = tap_fd;
    cmd.del.portNum = if_entry->portNum;

    /* Notifies all listener thread about remove TAP interface */
    for (i = 0; i < OPS_FPA_THREAD_CNT; i++) {
        send_ctrl_cmd(info->ctrl_fds[i][OPS_FPA_THREAD_PIPE_SEND], &cmd);
    }

    /* Remove TAP info from maps */
    hmap_remove(&info->fd_to_tap_if_map, &if_entry->node);

    ops_fpa_dev_mutex_unlock();

    /* Try to remove interface from bridge before shutting down */
    ops_fpa_bridge_port_rm(if_entry->name);

    ops_fpa_system("/sbin/ifconfig %s down", if_entry->name);

    free(if_entry->name);

    /* Close FD */
    close(tap_fd);
    free(if_entry);

    return 0;
}

/* Finds TAP interface entry by key */
struct tap_if_entry *
get_if_entry(const struct hmap *map, int key)
{
    struct hmap_node *entry;

    ovs_assert(map);

    entry = hmap_first_with_hash(map, key);
    if (entry) {
        return CONTAINER_OF(entry, struct tap_if_entry, node);
    } else {
       VLOG_ERR("%s: Not found TAP interface entry by key %d", __func__, key);
    }

    return NULL;
}

/* Creates Linux tun/tap interface.
 *
 * Arguments taken by the function:
 * name:  The name of an interface (or '\0'). MUST have enough
 *        space to hold the interface name if '\0' is passed.
 * flags: IFF_TUN   - TUN device (no Ethernet headers)
 *        IFF_TAP   - TAP device
 *
 *        IFF_NO_PI - Do not provide packet information
 */

int
ops_fpa_tun_alloc(char *name, int flags)
{

    struct ifreq ifr;
    int fd, err;
    static const char tun_dev[] = "/dev/net/tun";

    ovs_assert(name);

    /* Open the clone device */
    fd = open(tun_dev, O_RDWR);
    if (fd < 0) {
        VLOG_WARN("opening \"%s\" failed: %s", tun_dev, ovs_strerror(errno));
        return -errno;
    }

    memset(&ifr, 0, sizeof(ifr));
    ifr.ifr_flags = flags;
    if (*name) {
        /* if a device name was specified, put it in the structure; otherwise,
         * the kernel will try to allocate the "next" device of the
         * specified type */
        ovs_strzcpy(ifr.ifr_name, name, sizeof ifr.ifr_name);
    }

    /* try to create the device */
    err = ioctl(fd, TUNSETIFF, &ifr);
    if (err < 0) {
        VLOG_WARN("%s: creating TAP device failed: %s", name,
                  ovs_strerror(errno));
        close(fd);
        return -errno;
    }

    /* Write back the name of the interface */
    strcpy(name, ifr.ifr_name);
    return fd;
}

#define FPA_PKT_SAFEGUARD   32 /* offset due to bug in FPA */
#define DOT1Q_LEN           4  /* size of 802.1q header */

/* Handles packets received from TAP interfaces to corresponding ASIC */
void *
tap_listener(void *arg)
{
    uint32_t switchId;
    int i, bytes_recv, ctrl_fd;
    struct ctrl_cmd cmd;
    struct tap_info *info = arg;
    struct hmap fd_to_tap_if_map; /* FD to TAP interface entry map */
    struct timeval init_time;
    const uint8_t INIT_DRAIN_TIME = 5;
    bool time_initialized = false;
    FPA_PACKET_OUT_BUFFER_STC pkt = {0};
    char *pbuf;
    FPA_STATUS err;
    fd_set rd, read_fd_set;
    struct tap_if_entry *if_entry;

    if (!info) {
        VLOG_ERR("No TAP listener args specified");
        return NULL;
    }

    /* Save thread parameters */
    switchId = info->switchId;
    ctrl_fd = info->ctrl_fds[OPS_FPA_THREAD_TAP][OPS_FPA_THREAD_PIPE_RECV];

    VLOG_INFO("%s, Run TAP listener, switchId: %d, ctrl fd: %d", __func__, switchId, ctrl_fd);

    /* Allocate memory for single packet */
    pbuf = xzalloc(FPA_HAL_MAX_MTU_CNS * sizeof(uint8_t));

    /* Init map */
    hmap_init(&fd_to_tap_if_map);

    /* Register control pipe fd in read_fd_set */
    FD_ZERO(&read_fd_set);
    FD_SET(ctrl_fd, &read_fd_set);

    /* Handling loop. */
    for (;;) {

        /* Copies read fd set */
        rd = read_fd_set;

        if (select(FD_SETSIZE, &rd, NULL, NULL, NULL) < 0) {
            VLOG_ERR("%s, Select failed. Error(%d) - %s",
                     __func__, errno, strerror(errno));
            if (errno == EINTR)
                goto exit;
        }

        /* Firstly check control commands */
        if (FD_ISSET(ctrl_fd, &rd)) {

            while (recv_ctrl_cmd(ctrl_fd, &cmd) == sizeof(struct ctrl_cmd)) {
                switch (cmd.type) {
                    case OPS_FPA_CMD_THREAD_EXIT: {
                        VLOG_INFO("TAP listener thread finished");
                        goto exit;
                    } break;
                    case OPS_FPA_CMD_ADD_IF: {

                        /* Creates new TAP info entry */
                        if_entry = xzalloc(sizeof(* if_entry));
                        if_entry->portNum = cmd.add.portNum;
                        if_entry->name = xstrdup(cmd.add.tap_if_name);
                        memcpy(&if_entry->mac, &cmd.add.mac, ETH_ALEN);

                        /* Inserts TAP info entry to map */
                        hmap_insert(&fd_to_tap_if_map, &if_entry->node, cmd.add.fd);

                        /* Register interface fd in TAP read_fd_set. */
                        FD_SET(cmd.add.fd, &read_fd_set);

                        VLOG_INFO("%s, New TAP interface '%s' added to TAP listener", __func__, if_entry->name);
                    } break;
                    case OPS_FPA_CMD_DEL_IF: {

                        /* Find interface entry by fd */
                        if_entry = get_if_entry(&fd_to_tap_if_map, cmd.del.fd);
                        if (!if_entry) {
                            continue;
                        }

                        /* Remove TAP info from map */
                        hmap_remove(&fd_to_tap_if_map, &if_entry->node);

                        /* Clear interface socket in TAP read_fd_set. */
                        FD_CLR(cmd.del.fd, &read_fd_set);

                        VLOG_INFO("%s, Old TAP interface '%s' removed from TAP listener", __func__, if_entry->name);

                        free(if_entry->name);
                        free(if_entry);

                    } break;
                    default: {
                        VLOG_ERR("%s, Invalid command type %d", __func__, cmd.type);
                    }
                }
            }
        }

        /* TODO: need create cycle only for read_fd_set FD */
        for (i = 0; i < FD_SETSIZE; i++) {
            if (FD_ISSET(i, &rd) && i != ctrl_fd) {

                struct timeval cur_time, delta_time;
                struct ether_header *eth_hdr;
                uint16_t eth_type;

                pkt.pktDataPtr = (uint8_t*)pbuf + FPA_PKT_SAFEGUARD;

                do {
                    bytes_recv = read(i, pkt.pktDataPtr,
                                      FPA_HAL_MAX_MTU_CNS * sizeof(uint8_t));
                } while ((bytes_recv < 0) && (errno == EINTR));

                if ((bytes_recv < 0) && (errno != EWOULDBLOCK)) {
                    VLOG_WARN("%s, Read from recv socket failed. Error(%d) - %s",
                              __func__, errno, strerror(errno));
                    continue;
                }
                pkt.pktDataSize = bytes_recv;

               /* Find interface entry by fd */
                if_entry = get_if_entry(&fd_to_tap_if_map, i);
                if (!if_entry) {
                    continue;
                }

                /* Check time */
                if (!time_initialized) {
                    gettimeofday(&init_time, NULL);
                    time_initialized = true;
                }

                gettimeofday(&cur_time, NULL);
                timersub(&cur_time, &init_time, &delta_time);

                if (delta_time.tv_sec < INIT_DRAIN_TIME) {
                    VLOG_WARN_RL(&rl, "%s, Drain %u bytes from TAP interface '%s'",
                                 __func__, pkt.pktDataSize, if_entry->name);
                    continue;
                }

                eth_hdr = (struct ether_header *)pkt.pktDataPtr;
                eth_type = ops_fpa_get_eth_type(pkt.pktDataPtr);
                VLOG_INFO("%s, RX packet of %d bytes (dst: "ETH_ADDR_FMT" src: "ETH_ADDR_FMT" type: 0x%04x)"
                                  " on TAP '%s'\n  data: "OPS_FPA_PRINT_10_BYTES_FMT,
                         __func__, pkt.pktDataSize,
                         ETH_ADDR_BYTES_ARGS(eth_hdr->ether_dhost),
                         ETH_ADDR_BYTES_ARGS(eth_hdr->ether_shost),
                         eth_type,
                         if_entry->name,
                         OPS_FPA_PRINT_10_BYTES_ARGS((uint8_t*)eth_hdr+sizeof(struct ether_header)));

                /* If tagged packet from port TAP (forwarded from some vlanXXX subinterface)
                 * then check if port is member of that VLAN.
                 * If not - drop it.
                 * Also check egress tag state and decide to strip tag or not before sending to ASIC
                */
                if (eth_type == ETHERTYPE_VLAN) {
                    uint16_t vid = ntohs(*(uint16_t*)(eth_hdr+1));
                    bool pop;
                    int err;

                    err = get_port_eg_tag_state(switchId, if_entry->portNum, vid, &pop);
                    if (!err) {
                        VLOG_INFO("---> get_eg_tag_state_by_port: popTag:%d for vid %d and portNum %d", /* need to know if need to strip VLAN tag */
                            pop, vid, if_entry->portNum);
                    }
                    else {
                        /* No L2 group entry found for port/VID combination - dropping packet */
                        VLOG_INFO("---> packet DROPPED: pid %d, vid %d",
                            if_entry->portNum, vid);
                        continue;
                    }

                    if (pop) {
                        VLOG_INFO("%s, tagged frame detected - stripping", __func__);
                        memmove((uint8_t*)eth_hdr+DOT1Q_LEN, eth_hdr, 2*ETH_ALEN);
                        pkt.pktDataPtr += DOT1Q_LEN;
                        pkt.pktDataSize -= DOT1Q_LEN;
                    }
                }
                else {
                    /* Check for packet forwarded by bridge_normal from other TAP interface with SMAC different from system MAC.
                     * If detected - drop it. */
                    if (memcmp(&eth_hdr->ether_shost, &if_entry->mac, ETH_ALEN)) { /*TODO get actual TAP MAC by fd from netlink socket */
                        continue;
                    }

                }

                pkt.outPortNum = if_entry->portNum;

                eth_hdr = (struct ether_header *)pkt.pktDataPtr;
                VLOG_INFO("%s, TX %d bytes (dst: "ETH_ADDR_FMT" src: "ETH_ADDR_FMT" type: 0x%04x)" /*TODO to be reverted to VLOG_INFO_RL after debug */
                          " to ASIC port %d\n  data: "OPS_FPA_PRINT_10_BYTES_FMT,
                 __func__, pkt.pktDataSize,
                 ETH_ADDR_BYTES_ARGS(eth_hdr->ether_dhost),
                 ETH_ADDR_BYTES_ARGS(eth_hdr->ether_shost),
                 ntohs(eth_hdr->ether_type),
                 pkt.outPortNum,
                 OPS_FPA_PRINT_10_BYTES_ARGS((uint8_t*)eth_hdr+sizeof(struct ether_header)));

                /* Send packet to ASIC */
                err = fpaLibPortPktSend(switchId, FPA_INVALID_INTF_ID, &pkt);
                if (err != FPA_OK) {
                    VLOG_ERR("%s, fpaLibPortPktSend: failed send packet for TAP '%s', portNum %d. Status: %s",
                             __func__, if_entry->name, if_entry->portNum, ops_fpa_strerr(err));
                }
            } /* if (FD_ISSET (i, &rd) */
        } /* for (i = 0; i < FD_SETSIZE; i++) */
    } /* while (1) */

exit:
    /* Release memory allocated */
    free(pbuf);
    hmap_destroy(&fd_to_tap_if_map);

    return NULL;
}

/* Handles packets received from ASIC to corresponding TAP interfaces */
void *
asic_listener(void *arg)
{
    int ret, ctrl_fd;
    FPA_STATUS err;
    FPA_PACKET_BUFFER_STC pkt = {0};
    uint32_t timeout = 10000; /* timeout (in ms) to wait until event occure */
    struct hmap port_num_to_tap_if_map; /* HW port number to TAP interface entry map */
    struct tap_info *info = arg;
    struct tap_if_entry *if_entry;
    struct ctrl_cmd cmd;
    uint32_t switchId;
    char *pbuf;

    if (!info) {
        VLOG_ERR("No ASIC listener args specified");
        return NULL;
    }

    /* Save thread parameters */
    switchId = info->switchId;
    ctrl_fd = info->ctrl_fds[OPS_FPA_THREAD_ASIC][OPS_FPA_THREAD_PIPE_RECV];

    VLOG_INFO("%s, Run ASIC listener, switchId: %d,  ctrl fd: %d", __func__, switchId, ctrl_fd);

    /* Allocate memory for single packet */
    pbuf = xzalloc(FPA_HAL_MAX_MTU_CNS * sizeof(uint8_t));

    /* Init map */
    hmap_init(&port_num_to_tap_if_map);

    for (;;) {
        struct ether_header *eth_hdr;
        uint16_t eth_type;

        pkt.pktDataPtr = (uint8_t*)pbuf + FPA_PKT_SAFEGUARD;
        /* Wait single packet from ASIC */
        err = fpaLibPktReceive(switchId, timeout, &pkt);

        /* Firstly check control commands */
        while (recv_ctrl_cmd(ctrl_fd, &cmd) == sizeof(struct ctrl_cmd)) {
            switch (cmd.type) {
                case OPS_FPA_CMD_THREAD_EXIT: {
                    VLOG_INFO("ASIC listener thread finished");
                    goto exit;
                } break;
                case OPS_FPA_CMD_ADD_IF: {

                    /* Creates new TAP info entry */
                    if_entry = xzalloc(sizeof(* if_entry));
                    if_entry->fd = cmd.add.fd;
                    if_entry->name = xstrdup(cmd.add.tap_if_name);
                    memcpy(&if_entry->mac, &cmd.add.mac, ETH_ALEN);

                    /* Inserts TAP info entry to map */
                    hmap_insert(&port_num_to_tap_if_map, &if_entry->node, cmd.add.portNum);

                    VLOG_INFO("%s, New TAP interface '%s' added to ASIC listener", __func__, if_entry->name);
                } break;
                case OPS_FPA_CMD_DEL_IF: {

                    /* Find interface entry by port number */
                    if_entry = get_if_entry(&port_num_to_tap_if_map, cmd.del.portNum);
                    if (!if_entry) {
                        continue;
                    }

                    /* Remove TAP info from map */
                    hmap_remove(&port_num_to_tap_if_map, &if_entry->node);

                    VLOG_INFO("%s, Old TAP interface '%s' removed from ASIC listener", __func__, if_entry->name);

                    free(if_entry->name);
                    free(if_entry);

                 } break;
                 default: {
                     VLOG_ERR("%s, Invalid command type %d", __func__, cmd.type);
                 }
            }
        }

        if (err == FPA_NO_MORE) { /* timeout ended */
            VLOG_DBG("%s, wake up - no received data", __func__);
            continue;
        }

        if (err != FPA_OK) {
            VLOG_ERR("%s, failed receive packet from FPA. Status: %s", __func__, ops_fpa_strerr(err));
            continue;
        }

        if (!ops_fpa_vlan_internal(pkt.vid) && (ops_fpa_get_eth_type(pkt.pktDataPtr) != ETHERTYPE_VLAN)) {
            /* for normal vlan need to add correct 802.1q header for vlansubintf master interface*/
            eth_hdr = (struct ether_header *)pkt.pktDataPtr;
            memmove((uint8_t*)eth_hdr-DOT1Q_LEN, eth_hdr, 2*ETH_ALEN);
            pkt.pktDataPtr -= DOT1Q_LEN;
            pkt.pktDataSize += DOT1Q_LEN;
            /*TODO fill 802.1q header: CoS */
            eth_hdr = (struct ether_header *)pkt.pktDataPtr;
            eth_hdr->ether_type = htons(ETHERTYPE_VLAN);
            *(uint16_t *)((char *)eth_hdr + sizeof(struct ether_header)) = htons(pkt.vid);
            VLOG_INFO("%s, added 802.1q header VID: %d", __func__, pkt.vid);
        }

        /* Find interface entry by port number */
        if_entry = get_if_entry(&port_num_to_tap_if_map, pkt.inPortNum);
        if (!if_entry) {
            continue;
        }

        /* Send a packet to TAP interface */
        VLOG_DBG("%s, TX packet of %d bytes to TAP interface '%s' (ingressed on port %d, vid %d)",
            __func__, pkt.pktDataSize, if_entry->name, pkt.inPortNum, pkt.vid);

        eth_hdr = (struct ether_header *)pkt.pktDataPtr;
        eth_type = ops_fpa_get_eth_type(pkt.pktDataPtr);
        VLOG_INFO("%s, TX packet of bytes:%d (ingressed on port:%d, vid:%d, reason:%d tableId:%d (dst: " /*TODO to be changed to VLOG_INFO_Rl */
                     ETH_ADDR_FMT" src: "ETH_ADDR_FMT" type: 0x%04x) to TAP '%s'\n  data:"OPS_FPA_PRINT_10_BYTES_FMT,
                     __func__, pkt.pktDataSize,
                     pkt.inPortNum, pkt.vid, pkt.reason, pkt.tableId,
                     ETH_ADDR_BYTES_ARGS(eth_hdr->ether_dhost),
                     ETH_ADDR_BYTES_ARGS(eth_hdr->ether_shost),
                     eth_type,
                     if_entry->name,
                     OPS_FPA_PRINT_10_BYTES_ARGS((uint8_t*)eth_hdr+sizeof(struct ether_header)));

        do {
            ret = write(if_entry->fd, pkt.pktDataPtr, pkt.pktDataSize);
        } while ((ret < 0) && (errno == EINTR));

        if (ret < 0) {
            VLOG_ERR_RL(&rl, "%s, Error sending packet to TAP interface '%s'. rc(%d) - %s",
                        __func__, if_entry->name, errno, strerror(errno));
        }
    }

exit:
    /* Release memory from single packet */
    free(pbuf);
    hmap_destroy(&port_num_to_tap_if_map);

    return NULL;
}

/* Utility functions */

/* getting FPA to get egress tag state on ASIC port */
int
get_port_eg_tag_state(uint32_t switchId, uint32_t portNum, uint16_t vlanId, bool *pop_tag)
{
    uint32_t gid;
    FPA_STATUS err;
    FPA_GROUP_BUCKET_ENTRY_STC bucket;

    FPA_GROUP_ENTRY_IDENTIFIER_STC ident = {
        .groupType = FPA_GROUP_L2_INTERFACE_E,
        .portNum = portNum,
        .vlanId = vlanId
    };

    err = fpaLibGroupIdentifierBuild(&ident, &gid);
    if (err != FPA_OK) {
        VLOG_ERR("failed to build group ID for portNum %d, vid %d, rc=%s",
            portNum, vlanId, ops_fpa_strerr(err));
        return EFAULT;
    }

    err = fpaLibGroupEntryBucketGet(switchId, gid, 0, &bucket);
    if (err != FPA_OK) {
        VLOG_INFO("Bucket not found for portNum %d, vid %d, rc=%s",
            portNum, vlanId, ops_fpa_strerr(err));
        return ENOENT;
    }

    *pop_tag = bucket.data.l2Interface.popVlanTagAction;

    return 0;
}

int
ops_fpa_net_if_setup(const char *name, const struct ether_addr *mac)
{
    int  rc = 0;
    char buf[32] = {0};

    /* Bring the Ethernet interface DOWN. */
    rc = ops_fpa_system("/sbin/ifconfig %s down", name);
    if (rc != 0) {
        VLOG_ERR("Failed to bring down %s interface. (rc=%d)",
                 name, rc);
        return EFAULT;
    }

    /* Set MAC address for the Ethernet interface. */
    rc = ops_fpa_system("/sbin/ip link set %s address %s",
                       name, ether_ntoa_r(mac, buf));
    if (rc != 0) {
        VLOG_ERR("Failed to set MAC address for %s interface. (rc=%d)",
                 name, rc);
        return EFAULT;
    }

    VLOG_INFO("Set MAC address for %s to %s", name, buf);

    /* Bring the Ethernet interface UP. */
    rc = ops_fpa_system("/sbin/ifconfig %s up", name);
    if (rc != 0) {
        VLOG_ERR("Failed to bring up %s interface. (rc=%d)",
                 name, rc);
        return EFAULT;
    }

    return 0;
}
