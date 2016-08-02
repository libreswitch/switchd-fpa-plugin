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
 *  File: ops-fpa-util.c
 *
 *  Purpose: This file provides public definitions for OpenSwitch utilities
 *           related application code for the FPA library.
 */

#include <unistd.h>
#include <sys/ioctl.h>
#include <net/if.h>
#include <netinet/ether.h>

#include <openvswitch/vlog.h>
#include <socket-util.h>
#include <util.h>
#include <dynamic-string.h>

#include "ops-fpa-util.h"

extern void
setSimulationLogFullPathName(const char* fullPathName);
extern void
startSimulationLog(void);
extern void
stopSimulationLog(void);

static bool sim_log_is_running = false;

VLOG_DEFINE_THIS_MODULE(fpa_util);

/* Executes a command composed from @format string and va_list
 * by calling system() function from standard library.
 * Returns after the command has been completed. */
/*TODO to be removed*/
int
ops_fpa_system(const char * format, ...)
{
    va_list arg;
    char cmd[256];
    int ret;

    va_start(arg, format);
    ret = vsnprintf(cmd, sizeof(cmd), format, arg);
    va_end(arg);

    if (ret < 0) {
        VLOG_ERR("Failed to parse command.",
                cmd, WEXITSTATUS(ret));
        return EFAULT;
    }

    cmd[sizeof(cmd) - 1] = '\0';
    ret = system(cmd);

    VLOG_INFO("Execute command \"%s\"", cmd);

    if (WIFEXITED(ret)) {
        if (!WEXITSTATUS(ret)) {
            return 0;
        }

        VLOG_ERR("Failed to execute \"%s\". Exit status (%d)",
                cmd, WEXITSTATUS(ret));
        return EFAULT;
    }

    if (WIFSIGNALED(ret)) {
        VLOG_ERR("Execution of \"%s\" has been terminated by signal %d",
                cmd, WTERMSIG(ret));
        return EFAULT;
    }

    VLOG_ERR("Failed to execute \"%s\". RC=%d", cmd, ret);
    return EFAULT;
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

void ops_fpa_start_simulation_log(struct unixctl_conn *conn,
                                  int argc OVS_UNUSED,
                                  const char *argv[], void *aux OVS_UNUSED)
{
    struct ds d_str = DS_EMPTY_INITIALIZER;
    const size_t fname_sz = 255;
    const size_t log_file_path_sz = 127;
    char file_touch_cmd[fname_sz];
    char log_file_path[log_file_path_sz];

    if(!sim_log_is_running) {
        sim_log_is_running = true;
        snprintf(log_file_path, log_file_path_sz, "/var/log/sim_log_%lu.log",
                 (unsigned long)time(NULL));
        snprintf(file_touch_cmd, fname_sz, "touch %s", log_file_path);

        if(system(file_touch_cmd) == 0) {
            setSimulationLogFullPathName(log_file_path);
            startSimulationLog();
            ds_put_cstr(&d_str, "Start SIMULATION LOG");
            VLOG_INFO("Start SIMULATION LOG");
        } else {
            VLOG_ERR("Can\'t create SIMULATION LOG file");
            ds_put_cstr(&d_str, "Can\'t create SIMULATION LOG file");
            sim_log_is_running = false;
        }
    } else {
        VLOG_ERR("Can\'t start. Simulation log is running now.");
        ds_put_cstr(&d_str, "Can\'t start. Simulation log is running now.");
    }
    unixctl_command_reply(conn, ds_cstr(&d_str));
    ds_destroy(&d_str);

}
void ops_fpa_stop_simulation_log(struct unixctl_conn *conn, int argc OVS_UNUSED,
                                 const char *argv[], void *aux OVS_UNUSED)
{
	struct ds d_str = DS_EMPTY_INITIALIZER;
    if(sim_log_is_running) {
        stopSimulationLog();
        VLOG_INFO("Stop SIMULATION LOG");
        ds_put_cstr(&d_str, "Stop simulation log");
        sim_log_is_running = false;
    } else {
        VLOG_ERR("Can\'t stop simulation log. Not running now");
        ds_put_cstr(&d_str, "Can\'t stop simulation log. Not running now");
    }
    unixctl_command_reply(conn, ds_cstr(&d_str));
    ds_destroy(&d_str);
}


