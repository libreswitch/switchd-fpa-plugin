/*
 *  Copyright (C) 2016, Marvell International Ltd. ALL RIGHTS RESERVED.
 *
 *    Licensed under the Apache License, Version 2.0 (the "License"); you may
 *    not use this file except in compliance with the License. You may obtain
 *    a copy of the License at http://www.apache.org/licenses/LICENSE-2.0
 *
 *    THIS CODE IS PROVIDED ON AN  *AS IS* BASIS, WITHOUT WARRANTIES OR
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
#include <net/ethernet.h>

#include <ofp-parse.h>
#include <socket-util.h>
#include <hmap.h>

#include "ops-fpa-util.h"
#include "ops-fpa-dev.h"
#include "ops-fpa-tap.h"
#include "ops-fpa-vlan.h"

/* TODO: need set actual value */
#define FPA_HAL_MAX_MTU_CNS     10240

#define PRINT_10_BYTES_FMT \
    "0x%02"PRIx8" 0x%02"PRIx8" 0x%02"PRIx8" 0x%02"PRIx8"  0x%02"PRIx8" 0x%02"PRIx8" 0x%02"PRIx8" 0x%02"PRIx8"  0x%02"PRIx8" 0x%02"PRIx8
#define PRINT_10_BYTES_ARGS(EAB) \
    (EAB)[0], (EAB)[1], (EAB)[2], (EAB)[3], (EAB)[4], (EAB)[5], (EAB)[6], (EAB)[7], (EAB)[8], (EAB)[9]

VLOG_DEFINE_THIS_MODULE(ops_fpa_tap);

static struct vlog_rate_limit rl = VLOG_RATE_LIMIT_INIT(5, 20);

/* Linux tun/tap interface entry */
struct tap_if_entry {

    /* TAP interface name */
    char *name;

    /* FD of TAP interface */
    int fd;

    /* HW port number */
    uint32_t portNum;

    /* Node in a port_num_to_tap_if_map */
    struct hmap_node port_num_node;
    /* Node in a fd_to_tap_if_map */
    struct hmap_node fd_node;
};

/* Linux tun/tap interface information for switch once */
struct tap_info {

    /* FPA device ID */
    uint32_t switchId;

    struct ovs_mutex mutex;

    /* HW port number to TAP interface entry map */
    struct hmap port_num_to_tap_if_map;
    /* FD to TAP interface entry map */
    struct hmap fd_to_tap_if_map;

    /* TAP listener thread ID */
    pthread_t tap_thread;
    /* Pipe which will notify TAP listener thread about new TAP interface added */
    int if_upd_fds[2];
    /* Exit pipe for TAP listener thread */
    int exit_fds[2];
    /* FD set which waits data from TAP interface */
    fd_set read_fd_set;

    /* ASIC listener thread ID */
    pthread_t asic_thread;
};

struct tap_if_entry *get_if_entry_by_fd(struct tap_info *info, int fd);
struct tap_if_entry *get_if_entry_by_port_num(struct tap_info *info, uint32_t portNum);

int ops_fpa_tun_alloc(char *name, int flags);

void *tap_listener(void *arg);
void *asic_listener(void *arg);

uint16_t get_vlan_id_for_unt_port(uint32_t switchId, uint32_t portNum);
int get_port_eg_tag_state(uint32_t switchId, uint32_t portNum, uint16_t vlanId, bool *pop_tag);

struct tap_info *
ops_fpa_tap_init(uint32_t switchId)
{
    struct tap_info *info;

    ovs_assert(switchId>=0);

    VLOG_INFO("TAP interface init for FPA device (%d)", switchId);

    info = get_tap_info_by_switch_id(switchId);
    if (info) {
        VLOG_ERR("TAP interfaces for FPA device (%d) already exist", switchId);
        return NULL;
    }

    /* Create TAP info for switch */
    info = xzalloc(sizeof *info);
    info->switchId = switchId;

    ovs_mutex_init_recursive(&info->mutex);

    /* Create control pipes and add their read fds to info->read_fd_set. */
    xpipe(info->if_upd_fds);
    xpipe(info->exit_fds);
    FD_SET(info->if_upd_fds[0], &info->read_fd_set);
    FD_SET(info->exit_fds[0], &info->read_fd_set);

    hmap_init(&info->port_num_to_tap_if_map);
    hmap_init(&info->fd_to_tap_if_map);

    /* Run threads */
    info->tap_thread = ovs_thread_create("tap-listener", tap_listener, info);
    VLOG_INFO("TAP listener thread started");

    info->asic_thread = ovs_thread_create("asic-listener", asic_listener, info);
    VLOG_INFO("ASIC listener thread started");

    return info;
}

int
ops_fpa_tap_deinit(uint32_t switchId)
{
    struct tap_if_entry *e;
    struct tap_if_entry *next;
    struct tap_info *info;

    ovs_assert(switchId>=0);

    info = get_tap_info_by_switch_id(switchId);

    if (!info) {
        return 0;
    }

    /* Stop ASIC lisener thread */
    /* TODO: add this faclilities */

    /* Stop TAP listener thread */
    ignore(write(info->exit_fds[1], "", 1));
    xpthread_join(info->tap_thread, NULL);

    close(info->if_upd_fds[0]);
    close(info->if_upd_fds[1]);
    close(info->exit_fds[0]);
    close(info->exit_fds[1]);

    /* Remove TAP interfaces */
    HMAP_FOR_EACH_SAFE (e, next, fd_node, &info->fd_to_tap_if_map) {
        ops_fpa_tap_if_delete(switchId, e->fd);
    }

    hmap_destroy(&info->port_num_to_tap_if_map);
    hmap_destroy(&info->fd_to_tap_if_map);

    ovs_mutex_destroy(&info->mutex);

    free(info);

    VLOG_INFO("Host interface TAP-based instance deallocated");

    /* TODO: check return error code */
    return 0;
}

int
ops_fpa_tap_if_create(uint32_t switchId, uint32_t portNum, const char *name, 
                      const struct ether_addr *mac, int* tap_fd)
{
    int err;
    int fd;
    char tap_if_name[IFNAMSIZ];
    struct tap_info *info;
    struct tap_if_entry *if_entry;
    
    ovs_assert(switchId>=0);
    ovs_assert(name);/* TODO check why ovs_assert doesnt work */
    ovs_assert(mac);
    ovs_assert(tap_fd);

    ops_fpa_dev_mutex_lock();
    info = get_tap_info_by_switch_id(switchId);
    if (!info) {
        VLOG_ERR("TAP interface not initialized for FPA device (%d)", switchId);
        return EFAULT;
    }
    ops_fpa_dev_mutex_unlock();

    /*TODO add check internal bridge TAP is created only once - currently only 'bridge_normal' is supported */
    snprintf(tap_if_name, IFNAMSIZ, "%s", name);
    fd = ops_fpa_tun_alloc(tap_if_name, (IFF_TAP | IFF_NO_PI));
    if (fd <= 0) {
        VLOG_ERR("Unable to create TAP interface '%s'", tap_if_name);
        return EFAULT;
    }

    err = set_nonblocking(fd);
    if (err) {
        VLOG_ERR("Unable to set TAP interface '%s' into nonblocking mode", tap_if_name);
        close(fd);
        return EFAULT;
    }

    if (0 != ops_fpa_net_if_setup(tap_if_name, mac)) {
        VLOG_ERR("Unable to setup TAP interface '%s'", tap_if_name);
        close(fd);
        return EFAULT;
    }

    /* Creates new TAP info entry */
    if_entry = xzalloc(sizeof(* if_entry));
    if_entry->fd = fd;
    if_entry->portNum = portNum;
    if_entry->name = xstrdup(tap_if_name);
    VLOG_INFO("TAP interface '%s' created: portNum=%d, fd=%d, MAC=" ETH_ADDR_FMT,
              tap_if_name, if_entry->portNum, fd, 
              ETH_ADDR_BYTES_ARGS(mac->ether_addr_octet));

    *tap_fd = fd; 

    ovs_mutex_lock(&info->mutex);

    /* Inserts TAP info entry to maps */
    hmap_insert(&info->fd_to_tap_if_map, &if_entry->fd_node, if_entry->fd);
    hmap_insert(&info->port_num_to_tap_if_map, &if_entry->port_num_node, if_entry->portNum);

    /* Register interface fd in TAP read_fd_set. */
    FD_SET(fd, &info->read_fd_set);

    ovs_mutex_unlock(&info->mutex);

    /* Notify TAP listener thread about new TAP interface. */
    ignore(write(info->if_upd_fds[1], "", 1));

    return 0;
}

int 
ops_fpa_tap_if_delete(uint32_t switchId, int tap_fd)
{
    struct tap_info *info;
    struct tap_if_entry *if_entry;

    ovs_assert(switchId>=0);
    ovs_assert(tap_fd>0);

    ops_fpa_dev_mutex_lock();
    info = get_tap_info_by_switch_id(switchId);
    if (!info) {
        VLOG_ERR("TAP interface not initialized for FPA device (%d)", switchId);
        return EFAULT;
    }
    ops_fpa_dev_mutex_unlock();

    /* Get TAP entry for port */
    ovs_mutex_lock(&info->mutex);

    if_entry = get_if_entry_by_fd(info, tap_fd);
    if (!if_entry) {
        ovs_mutex_unlock(&info->mutex);
        return ENOENT;
    }

    /* Remove TAP info from maps */
    hmap_remove(&info->fd_to_tap_if_map, &if_entry->fd_node);
    hmap_remove(&info->port_num_to_tap_if_map, &if_entry->port_num_node);

    /* Clear interface socket in TAP read_fd_set. */
    FD_CLR(if_entry->fd, &info->read_fd_set);

    ovs_mutex_unlock(&info->mutex);

    ops_fpa_system("/sbin/ifconfig %s down", if_entry->name); /*TODO - eliminate system syscall usage */

    free(if_entry->name);

    /* Close FD */
    close(if_entry->fd);
    free(if_entry);

    return 0;
}

/* Finds TAP info entry by FD */
struct tap_if_entry *
get_if_entry_by_fd(struct tap_info *info, int fd)
{
    struct hmap_node *entry;

    ovs_assert(info);
    ovs_assert(fd>0);

    entry = hmap_first_with_hash(&info->fd_to_tap_if_map, fd);
    if (entry) {
        return CONTAINER_OF(entry, struct tap_if_entry, fd_node);
    } else {
       VLOG_ERR("%s: Not found TAP interface by fd %d", __FUNCTION__, fd);
    }

    return NULL;
}

/* Finds TAP info entry by portNum */
struct tap_if_entry *
get_if_entry_by_port_num(struct tap_info *info, uint32_t portNum)
{
    struct hmap_node *entry;

    ovs_assert(info);
    ovs_assert(portNum>=0);

    entry = hmap_first_with_hash(&info->port_num_to_tap_if_map, portNum);
    if (entry) {
        return CONTAINER_OF(entry, struct tap_if_entry, port_num_node);
    } else {
       VLOG_ERR("%s: Not found TAP interface by port number %d", __FUNCTION__, portNum);
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
        VLOG_WARN("%s: creating tap device failed: %s", name,
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
#define HARDCODED_IP_VLAN_VID  765 
#define HARDCODED_IP_VLAN_PORT 3

/* Handles packets received from TAP interfaces to corresponding ASIC */
void *
tap_listener(void *arg)
{
    int i, bytes_recv;
    struct tap_info *info = arg;
    struct timeval init_time;
    const uint8_t INIT_DRAIN_TIME = 5;
    bool time_initialized = false;
    FPA_PACKET_OUT_BUFFER_STC pkt = {0};
    char *pbuf;
    FPA_STATUS status;

    if (!info) {
        VLOG_ERR("No TAP info specified");
        return NULL;
    }

    /* Allocate memory for single packet */
    pbuf = xzalloc(FPA_HAL_MAX_MTU_CNS * sizeof(uint8_t));

    /* Handling loop. */
    for (;;) {

        ovs_mutex_lock(&info->mutex);
        fd_set read_fd_set = info->read_fd_set;
        ovs_mutex_unlock(&info->mutex);

        if (select(FD_SETSIZE, &read_fd_set, NULL, NULL, NULL) < 0) {
            VLOG_ERR("%s, Select failed. Error(%d) - %s",
                     __FUNCTION__, errno, strerror(errno));
            goto exit;
        }

        /* TODO: need create cycle only for read_fd_set FD */
        for (i = 0; i < FD_SETSIZE; i++) {
            if (FD_ISSET (i, &read_fd_set)) {

                struct tap_if_entry *if_entry;
                struct timeval cur_time, delta_time;
                uint32_t egport;
                struct ether_header *eth_hdr;
                uint16_t eth_type;

                pkt.pktDataPtr = (uint8_t*)pbuf + FPA_PKT_SAFEGUARD;

                do {
                    bytes_recv = read(i, pkt.pktDataPtr,
                                      FPA_HAL_MAX_MTU_CNS * sizeof(uint8_t));
                } while ((bytes_recv < 0) && (errno == EINTR));

                if ((bytes_recv < 0) && (errno != EWOULDBLOCK)) {
                    VLOG_WARN("%s, Read from recv socket failed. Error(%d) - %s",
                              __FUNCTION__, errno, strerror(errno));
                    continue;
                }
                pkt.pktDataSize = bytes_recv;

                if (i == info->exit_fds[0]) {
                    VLOG_INFO("TAP listener thread finished");
                    goto exit;
                } else if (i == info->if_upd_fds[0]) {
                    /* New TAP interface has been added so
                     * need to listen to it as well. */
                    VLOG_INFO("%s, New TAP interface added for listening", __FUNCTION__);
                    continue;
                }

                /* Find inerface entry by fd */
                ovs_mutex_lock(&info->mutex);
                if_entry = get_if_entry_by_fd(info, i);
                if (!if_entry) {
                    ovs_mutex_unlock(&info->mutex);
                    continue;
                }
                ovs_mutex_unlock(&info->mutex);

                /* Check time */
                if (!time_initialized) {
                    gettimeofday(&init_time, NULL);
                    time_initialized = true;
                }

                gettimeofday(&cur_time, NULL);
                timersub(&cur_time, &init_time, &delta_time);

                if (delta_time.tv_sec < INIT_DRAIN_TIME) {
                    VLOG_WARN_RL(&rl, "%s, Drain %u bytes from TAP interface '%s'",
                                 __FUNCTION__, pkt.pktDataSize, if_entry->name);
                    continue;
                }

                eth_hdr = (struct ether_header *)pkt.pktDataPtr;
                eth_type = ntohs(eth_hdr->ether_type);
                VLOG_INFO("%s, RX packet of %d bytes (dst: "ETH_ADDR_FMT" src: "ETH_ADDR_FMT" type: 0x%04x)"
                                  " on TAP '%s'\n  data: "PRINT_10_BYTES_FMT,
                         __FUNCTION__, pkt.pktDataSize,
                         ETH_ADDR_BYTES_ARGS(eth_hdr->ether_dhost), 
                         ETH_ADDR_BYTES_ARGS(eth_hdr->ether_shost),
                         eth_type,
                         if_entry->name,
                         PRINT_10_BYTES_ARGS((uint8_t*)eth_hdr+sizeof(struct ether_header)));

                /*TODO if tagged packet from port TAP then check if port is member of VLAN, 
                 * If not - drop it,
                 * if member - check ASIC port tag state, then decide to strip tag or not before sending to FPA
                */

                /* logic for brigde interface */
                if (if_entry->portNum == FPA_INVALID_INTF_ID) { /* packet from 'bridge_normal' TAP */
                    egport = HARDCODED_IP_VLAN_PORT; /*XXX hardcoded */

                    /* strip 802.1q header */
                    if (eth_type==ETHERTYPE_VLAN) {
                        uint16_t vid = ntohs(*(uint16_t*)(eth_hdr+1));
                        bool pop;
                        int err;

                        VLOG_INFO("%s, tagged frame detected - stripping", __FUNCTION__);
                        memmove((uint8_t*)eth_hdr+DOT1Q_LEN, eth_hdr, 2*ETH_ALEN);
                        pkt.pktDataPtr += DOT1Q_LEN;
                        pkt.pktDataSize -= DOT1Q_LEN;

                        err = get_port_eg_tag_state(info->switchId, egport, vid, &pop);
                        if (!err) {
                            VLOG_INFO("---> get_eg_tag_state_by_port: popTag:%d for vid %d and portNum %d", /* need to know if need to strip VLAN tag */ 
                                pop, vid, egport);
                        }


#if 0 /*HACK*/
                        *(char*)eth_hdr = 0xAA;
                        eth_hdr->ether_type = htons(0x88b5);
#endif
                    }

                }
                else {
                    egport = if_entry->portNum;
                }

                /* Send packet to ASIC */
                pkt.outPortNum = egport;
                /* padding frame to min Ethernet frame length 
                   TODO to be removed for real ASIC */
                pkt.pktDataSize = pkt.pktDataSize > ETH_ZLEN ? pkt.pktDataSize : ETH_ZLEN; /* FIXME added zeroing of padding bytes */

                eth_hdr = (struct ether_header *)pkt.pktDataPtr;
                VLOG_INFO("%s, TX %d bytes (dst: "ETH_ADDR_FMT" src: "ETH_ADDR_FMT" type: 0x%04x)" /*TODO to be changed to VLOG_INFO_Rl */
                          " to ASIC port %d\n  data: "PRINT_10_BYTES_FMT,
                 __FUNCTION__, pkt.pktDataSize,
                 ETH_ADDR_BYTES_ARGS(eth_hdr->ether_dhost), 
                 ETH_ADDR_BYTES_ARGS(eth_hdr->ether_shost),
                 ntohs(eth_hdr->ether_type),
                 pkt.outPortNum,
                 PRINT_10_BYTES_ARGS((uint8_t*)eth_hdr+sizeof(struct ether_header)));

                status = fpaLibPortPktSend(info->switchId, FPA_INVALID_INTF_ID, &pkt);
                if (status != FPA_OK) {
                    VLOG_ERR("%s, fpaLibPortPktSend: failed send packet for TAP '%s', portNum %d. Status: %s",
                             __FUNCTION__, if_entry->name, egport, ops_fpa_strerr(status));
                }
            } /* if (FD_ISSET (i, &read_fd_set)) */
        } /* for (i = 0; i < FD_SETSIZE; i++) */
    } /* while (1) */

exit:
    /* Release memory from single packet */
    free(pbuf);

    return NULL;
}

/* Handles packets received from ASIC to corresponding TAP interfaces */
void *
asic_listener(void *arg)
{
    int ret;
    FPA_STATUS status;
    FPA_PACKET_BUFFER_STC pkt = {0};
    uint32_t timeout = 10000; /* timeout (in ms) to wait until event occure */
    struct tap_info *info = arg;
    struct tap_if_entry *if_entry;
    char *pbuf;

    if (!info) {
        VLOG_ERR("No TAP info specified");
        return NULL;
    }

    /* Allocate memory for single packet */
    pbuf = xzalloc(FPA_HAL_MAX_MTU_CNS * sizeof(uint8_t));

    for (;;) {
        uint32_t dst_tap_portnum;
        struct ether_header *eth_hdr;
        uint16_t eth_type;

        pkt.pktDataPtr = (uint8_t*)pbuf + FPA_PKT_SAFEGUARD;
        /* Wait single packet from ASIC */
        status = fpaLibPktReceive(info->switchId, timeout, &pkt);

        if (status == FPA_NO_MORE) { /* timeout ended */
            VLOG_WARN("%s, wake up - no received data", __FUNCTION__);
            continue;
        }

        if (status != FPA_OK) {
            VLOG_ERR("%s, failed receive packet from FPA. Status: %s", __FUNCTION__, ops_fpa_strerr(status));
            continue;
        }

        /*TODO for ARP/IP packet need to know if IP interface is set on VLAN to choose 'bridge_normal' as outgoing TAP */

        VLOG_INFO("---> get_vlan_id_for_unt_port: vid %d for portNum %d", 
            get_vlan_id_for_unt_port(info->switchId, pkt.inPortNum), pkt.inPortNum); /* need to know VID for tagging packet before sending to 'bridge_normal' */

        if (pkt.inPortNum==HARDCODED_IP_VLAN_PORT) {/*check if need to send to bridge interface*/ /*XXX hardcoded*/
            uint16_t vid = HARDCODED_IP_VLAN_VID; /*XXX hardcoded*/

            dst_tap_portnum = FPA_INVALID_INTF_ID;
            eth_hdr = (struct ether_header *)pkt.pktDataPtr;
            memmove((uint8_t*)eth_hdr-DOT1Q_LEN, eth_hdr, 2*ETH_ALEN);
            pkt.pktDataPtr -= DOT1Q_LEN;
            pkt.pktDataSize += DOT1Q_LEN;
            /*TODO fill 802.1q header */
            eth_hdr = (struct ether_header *)pkt.pktDataPtr;
            eth_hdr->ether_type = htons(ETHERTYPE_VLAN);
            *(uint16_t *)((char *)eth_hdr + sizeof(struct ether_header)) = htons(vid);
            VLOG_INFO("%s, added 802.1q header VID: %d", __FUNCTION__, vid);

        }
        else {
            dst_tap_portnum = pkt.inPortNum;
        }

        /* Find interface entry by port number */
        ovs_mutex_lock(&info->mutex);
        if_entry = get_if_entry_by_port_num(info, dst_tap_portnum);
        if (!if_entry) {
            ovs_mutex_unlock(&info->mutex);
            continue;
        }
        ovs_mutex_unlock(&info->mutex);

        /* Send a packet to TAP interface */
        VLOG_DBG("%s, TX packet of %d bytes to TAP interface '%s' (ingressed on port %d)", 
            __FUNCTION__, pkt.pktDataSize, if_entry->name, pkt.inPortNum);

        eth_hdr = (struct ether_header *)pkt.pktDataPtr;
        eth_type = ntohs(eth_hdr->ether_type);
        VLOG_INFO("%s, TX packet of bytes:%d (ingressed on port:%d, reason:%d tableId:%d (dst: " /*TODO to be changed to VLOG_INFO_Rl */
                     ETH_ADDR_FMT" src: "ETH_ADDR_FMT" type: 0x%04x) to TAP '%s'\n  data:"PRINT_10_BYTES_FMT,
                     __FUNCTION__, pkt.pktDataSize, 
                     pkt.inPortNum, pkt.reason, pkt.tableId,
                     ETH_ADDR_BYTES_ARGS(eth_hdr->ether_dhost), 
                     ETH_ADDR_BYTES_ARGS(eth_hdr->ether_shost),
                     eth_type,
                     if_entry->name,
                     PRINT_10_BYTES_ARGS((uint8_t*)eth_hdr+sizeof(struct ether_header)));

        do {
            ret = write(if_entry->fd, pkt.pktDataPtr, pkt.pktDataSize);
        } while ((ret < 0) && (errno == EINTR));

        if (ret < 0) {
            VLOG_ERR_RL(&rl, "%s, Error sending packet to TAP interface '%s'. rc(%d) - %s",
                        __FUNCTION__, if_entry->name, errno, strerror(errno));
        }
    }

    /* Release memory from single packet */
    free(pbuf);
}

/* Utility functions */

/* getting FPA vlan VID assignment for untagged frames on ASIC port */
uint16_t get_vlan_id_for_unt_port(uint32_t switchId, uint32_t portNum)
{
    FPA_STATUS status;
    FPA_FLOW_TABLE_ENTRY_STC flowEntry;

    flowEntry.cookie = ops_fpa_vlan_cookie(portNum, 0, false);

    /* retrive the vlan flow table entry with given cookie */
    status = fpaLibFlowTableGetByCookie(switchId, FPA_FLOW_TABLE_TYPE_VLAN_E, &flowEntry);
    if (status != FPA_OK) {
        VLOG_ERR("%s: fpaLibFlowTableGetByCookie: faild cookie %llu. Status: %s",
            __FUNCTION__, (long long unsigned int)flowEntry.cookie, ops_fpa_strerr(status));

        return 0;
    }

    return flowEntry.data.vlan.newTagVid;
}

/* getting FPA to get egress tag state on ASIC port */
int
get_port_eg_tag_state(uint32_t switchId, uint32_t portNum, uint16_t vlanId, bool *pop_tag)
{
    uint32_t gid;
    FPA_STATUS status;
    FPA_GROUP_BUCKET_ENTRY_STC bucket;

    FPA_GROUP_ENTRY_IDENTIFIER_STC ident = {
        .groupType = FPA_GROUP_L2_INTERFACE_E,
        .portNum = portNum,
        .vlanId = vlanId
    };

    status = fpaLibGroupIdentifierBuild(&ident, &gid);
    if (status != FPA_OK) {
        VLOG_ERR("fpaLibGroupIdentifierBuild: %s", ops_fpa_strerr(status));
        return EFAULT;
    }

    status = fpaLibGroupEntryBucketGet(switchId, gid, 0, &bucket);
    if (status != FPA_OK) {
        VLOG_ERR("fpaLibGroupEntryBucketGet: %s", ops_fpa_strerr(status));
        return EFAULT;
    }

    *pop_tag =  bucket.data.l2Interface.popVlanTagAction;

    return 0;
}

