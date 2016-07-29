# -*- coding: utf-8 -*-
#  Copyright (C) 2016, Marvell International Ltd. ALL RIGHTS RESERVED.
#
#    Licensed under the Apache License, Version 2.0 (the "License"); you may
#    not use this file except in compliance with the License. You may obtain
#    a copy of the License at http://www.apache.org/licenses/LICENSE-2.0
#
#    THIS CODE IS PROVIDED ON AN  *AS IS* BASIS, WITHOUT WARRANTIES OR
#    CONDITIONS OF ANY KIND, EITHER EXPRESS OR IMPLIED, INCLUDING WITHOUT
#    LIMITATION ANY IMPLIED WARRANTIES OR CONDITIONS OF TITLE, FITNESS
#    FOR A PARTICULAR PURPOSE, MERCHANTABILITY OR NON-INFRINGEMENT.
#
#    See the Apache Version 2.0 License for specific language governing
#    permissions and limitations under the License.
##########################################################################

"""
OpenSwitch Test for switchd related configurations.
"""

from time import sleep
from pytest import mark

TOPOLOGY = """
# +-------+
# |  ops1 |
# +-------+

# Nodes
[type=marvellswitch name="OpenSwitch 1"] ops1
[type=host name="Host 1"] hs1
[type=host name="Host 2"] hs2

ops1:if01 -- hs1:if01
ops1:if02 -- hs2:if01
"""

def test_switchd_marvell_sim_init_state_ports_access(topology, step):
    ops1 = topology.get("ops1")
    hs1 = topology.get("hs1")
    hs2 = topology.get("hs2")
    assert ops1 is not None
    assert hs1 is not None
    assert hs2 is not None
    
#    sleep(5)

    p1 = ops1.ports["if01"]
    p2 = ops1.ports["if02"]
    
    ops1("configure terminal");

    ops1("interface 1");
    ops1("no routing");
    ops1("no shutdown");
    ops1("exit");

    ops1("interface 2");
    ops1("no routing");
    ops1("no shutdown");
    ops1("exit");

#    sleep(4)

    hs1.libs.ip.interface('if01', addr='192.168.198.10/24', up=True)
    hs2.libs.ip.interface('if01', addr='192.168.198.11/24', up=True)
    ping4 = hs1.libs.ping.ping(10,"192.168.198.11")
    assert  ping4["received"] >= 3
    ping4 = hs2.libs.ping.ping(10,"192.168.198.10")
    assert ping4["received"] >= 3
    
