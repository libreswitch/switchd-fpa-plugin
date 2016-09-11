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

#include <unistd.h>
#include "ops-fpa-route.h"

VLOG_DEFINE_THIS_MODULE(ops_fpa_init);

extern FPA_STATUS fpaWrapInitialize(void);
extern char **environ;

static void *
ops_fpa_main_start(void *arg)
{
    char *argv[] = {
        "switchd",
        "-i",
        "/opt/fpa/wm/bobcat3_A0_pss_wm.ini",
        NULL
    };

    FPA_TRACE_FN();

    int rc = fpa_main(sizeof(argv)/sizeof(argv[0]) - 1, argv, environ);
    VLOG_INFO("Main FPA thread exited with code %d", rc);

    return NULL;
}

void
ops_fpa_init()
{
    FPA_TRACE_FN();
    ovs_thread_create("fpa_main", &ops_fpa_main_start, NULL);
    while (fpa_init_done != true) {
        usleep(200);
    }

    /* fpa_init_done means fpa_init_almost_done */
    usleep(2000000);

    int err = fpaWrapInitialize();
    if (err) {
        ovs_abort(EAGAIN, "fpaWrapInitialize failed: %s", ops_fpa_strerr(err));
    }
    system("touch /var/run/fpa-sim-init.done");
}
