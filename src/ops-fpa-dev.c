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

#include <errno.h>
#include <ovs/util.h>

#include "ops-fpa-dev.h"
#include "ops-fpa-mac-learning.h"

VLOG_DEFINE_THIS_MODULE(ops_fpa_dev);

static struct ovs_mutex fpa_dev_mutex = OVS_MUTEX_INITIALIZER;

static struct fpa_dev *dev = NULL; 

void
ops_fpa_dev_mutex_lock(void)
{
    ovs_mutex_lock(&fpa_dev_mutex);
}

void
ops_fpa_dev_mutex_unlock(void)
{
    ovs_mutex_unlock(&fpa_dev_mutex);
}

/* Returns device instance by device ID value.
 *
 * Increments device's reference counter on success. The caller
 * must free the returned fpa_dev with fpa_dev_free(). */
struct fpa_dev *
ops_fpa_dev_by_id(uint32_t id)
{
    if (dev) {
        return dev;
    }
    OVS_NOT_REACHED();
}

int 
ops_fpa_dev_init(uint32_t switchId, struct fpa_dev **fdev)
{
    int err = 0;

    ops_fpa_dev_mutex_lock();

    VLOG_INFO("FPA device (%d) init", switchId);

    /* TODO: check FPA device have only one instance */
    if (dev) {
        VLOG_ERR("FPA device (%d) already exist", dev->switchId);
        err = 0;/*TODO: ? EEXIST; */
        goto error;
    }

    dev = xzalloc(sizeof * dev);
    dev->switchId = switchId;

#if 0
    /* Initialize TAP interface. */
    err = ops_fpa_tap_init(dev);
    if (!err)
        VLOG_INFO("FPA device (%d) initialize was successful", dev->switchId);
#endif

    err = ops_fpa_mac_learning_create(dev, &dev->ml);
    if (err) {
        VLOG_ERR("Unable to create mac learning feature");
        err = EAGAIN;
        goto error;
    }

    *fdev = dev;
    
error:
    ops_fpa_dev_mutex_unlock();

    return err;
}

int 
ops_fpa_dev_deinit(uint32_t switchId)
{
    int err = 0;

    ops_fpa_dev_mutex_lock();
    
    VLOG_INFO("FPA device (%d) deinit", switchId);

    if (!dev || dev->switchId != switchId) {

        VLOG_ERR("FPA device (%d) not exist", switchId);
        ops_fpa_dev_mutex_unlock();

        return ENODEV;
    }

#if 0
    err = ops_fpa_tap_deinit(dev);
#endif
    ops_fpa_mac_learning_unref(dev->ml);

    if (!err)
    {
        free(dev);
        dev = NULL;

        VLOG_INFO("FPA device (%d) free was successful", switchId);
    }

    ops_fpa_dev_mutex_unlock();

    return 0;
}

