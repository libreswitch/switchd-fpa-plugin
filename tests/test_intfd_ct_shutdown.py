# Copyright (C) 2015-2016 Hewlett Packard Enterprise Development LP
# All Rights Reserved.
#
# Licensed under the Apache License, Version 2.0 (the "License"); you may
# not use this file except in compliance with the License. You may obtain
# a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS, WITHOUT
# WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied. See the
# License for the specific language governing permissions and limitations
# under the License.

TOPOLOGY = """
#
# +-------+
# |  sw1  |
# +-------+
#

# Nodes
[type=marvellswitch name="Switch 1"] sw1
"""


def test_lag_shutdown(topology, step):
    sw1 = topology.get('sw1')

    assert sw1 is not None
"""
    step('\n########## Test shutdown and no shutdown for LAG ##########\n')
    n_intfs = 4
    sw1('configure terminal')
    sw1('interface lag 1')
    sw1('exit')

    for intf_num in range(1, n_intfs):
        sw1('interface %d' % intf_num)
        sw1('lag 1')
        sw1('exit')

    sw1('interface lag 1')
    sw1('no shutdown')
    sw1('end')

    out = sw1('show running-config interface')

    total_lag = 0
    lines = out.split("\n")

    for line in lines:
        if 'no shutdown' in line:
            total_lag += 1

    # total_lag should be the no shutdowns of intfs + lag + default vlan
    assert total_lag is 5, \
        "Failed test, all interfaces are not up!"

    sw1('configure terminal')
    sw1('interface lag 1')
    sw1('shutdown')
    sw1('end')

    out = sw1('show running-config interface')

    total_lag = 0
    lines = out.split("\n")

    for line in lines:
        if 'no shutdown' in line:
            total_lag += 1

    # total_lag should be the no shutdown of the default vlan
    assert total_lag is 1, \
        "Failed test, all interfaces are not down!"
"""
