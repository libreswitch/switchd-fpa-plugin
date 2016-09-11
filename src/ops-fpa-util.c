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
