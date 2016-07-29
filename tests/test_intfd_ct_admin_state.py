# (C) Copyright 2016 Hewlett Packard Enterprise Development LP
# All Rights Reserved.
#
#    Licensed under the Apache License, Version 2.0 (the "License"); you may
#    not use this file except in compliance with the License. You may obtain
#    a copy of the License at
#
#         http://www.apache.org/licenses/LICENSE-2.0
#
#    Unless required by applicable law or agreed to in writing, software
#    distributed under the License is distributed on an "AS IS" BASIS, WITHOUT
#    WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied. See the
#    License for the specific language governing permissions and limitations
#    under the License.
#
##########################################################################

"""
OpenSwitch Test for interface related configurations.
"""

# from pytest import mark
from time import sleep

TOPOLOGY = """
# +-------+
# |  ops1 |
# +-------+

# Nodes
[type=marvellswitch name="OpenSwitch 1"] ops1
[type=host name="Host 1"] hs1

# Links
ops1:if01
ops1:if02
ops1:if03
ops1:if04
"""


def test_intfd_ct_admin_state(topology, step):
    ops1 = topology.get("ops1")
    assert ops1 is not None

#     step("Step 1- Configure interface 1 on switch 1")
#     with ops1.libs.vtysh.ConfigInterface('if01') as ctx:
#         ctx.ip_address("10.0.10.1/24")
#         ctx.ipv6_address("2000::1/120")
# 
#     step("Step 2- Verify the interface is created with same name for L3 port")
    port = ops1.ports["if01"]
#     out = ops1("get interface {port} name".format(
#         port=port
#     ), shell="vsctl")
#     assert port in out

    step("Step 3- Verify the interface is created with same name for VLAN "
         "interface")
    with ops1.libs.vtysh.ConfigInterfaceVlan("1") as ctx:
        pass
    out = ops1("get interface vlan{vlan} name".format(vlan="1"),
               shell="vsctl")
    expected_output = "vlan{no}".format(no="1")
    assert expected_output in out

    step("Step 4- Verify the interface is down by default")
    out = ops1("get interface {port} hw_intf_config".format(port=port),
               shell="vsctl")
    #step("output:{output_string}".format(output_string=out))
    assert "enable=\"false\"" in out

    step("Step 5- Verify the interface associated with the port goes down"
         " when port is disabled for L3 interface")
    with ops1.libs.vtysh.ConfigInterface('if01') as ctx:
        ctx.no_shutdown()
    out = ops1("get interface {port} hw_intf_config".format(port=port),
               shell="vsctl")
    assert "enable=\"true\"" in out

    step("Step 6- Change the admin state of port to down")
    ops1("set interface {port} user_config:admin=down".format(port=port),
         shell="vsctl")

    step("Step 7- Verify the interface associated to the port is down")
    # sleep(2)
    out = ops1("get interface {port} hw_intf_config".format(port=port),
               shell="vsctl")
    assert "enable=\"false\"" in out

    step("Step 8- Verify the interface associated with the port goes down"
         " when port is disabled for VLAN interface")
    with ops1.libs.vtysh.ConfigInterfaceVlan("1") as ctx:
        ctx.no_shutdown()
    sleep(2)
    out = ops1("get interface vlan{vlan} hw_intf_config".format(vlan="1"),
               shell="vsctl")
    assert "enable=\"true\"" in out.strip()

    step("Step 9- Change the admin state of port to down")
    ops1("set port vlan{vlan} admin=down".format(vlan="1"), shell="vsctl")

    step("Step 10- Verify the interface associated to the port is down")
    out = ops1("get interface vlan{vlan} hw_intf_config".format(vlan="1"),
               shell="vsctl")
    assert "enable=\"false\"" in out.strip()

#     step("Step 11- Verify multiple interfaces associated with LAG port goes "
#          "down when LAG port is disabled")
#     with ops1.libs.vtysh.ConfigInterfaceLag("1") as ctx:
#         pass
#     with ops1.libs.vtysh.ConfigInterface('if02') as ctx:
#         ctx.no_shutdown()
#         ctx.lag("1")
#     with ops1.libs.vtysh.ConfigInterface('if03') as ctx:
#         ctx.no_shutdown()
#         ctx.lag("1")
#     with ops1.libs.vtysh.ConfigInterface('if04') as ctx:
#         ctx.no_shutdown()
#         ctx.lag("1")
# 
#     step("Step 12- All interfaces under the lag is up as soon as lag is up")
#     ops1("set port {port} admin=down".format(port="lag1"),
#          shell="vsctl")
#     out = ops1("get interface {port} hw_intf_config".format(
#         port=ops1.ports["if02"]
#     ), shell="vsctl")
#     assert "enable=\"false\"" in out
#     out = ops1("get interface {port} hw_intf_config".format(
#         port=ops1.ports["if03"]
#     ), shell="vsctl")
#     assert "enable=\"false\"" in out
#     out = ops1("get interface {port} hw_intf_config".format(
#         port=ops1.ports["if04"]
#     ), shell="vsctl")
#     assert "enable=\"false\"" in out
# 
#     step("Step 14- All interfaces under the lag is up as soon as lag is up")
#     ops1("set port {port} admin=up".format(port="lag1"),
#          shell="vsctl")
#     out = ops1("get interface {port} hw_intf_config".format(
#         port=ops1.ports["if02"]
#     ), shell="vsctl")
#     assert "enable=\"true\"" in out
#     out = ops1("get interface {port} hw_intf_config".format(
#         port=ops1.ports["if03"]
#     ), shell="vsctl")
#     assert "enable=\"true\"" in out
#     out = ops1("get interface {port} hw_intf_config".format(
#         port=ops1.ports["if04"]
#     ), shell="vsctl")
#     assert "enable=\"true\"" in out
# 
#     step("Step 15- Interfaces detached from lag gets back to default state"
#          " down")
#     ops1("conf t")
#     ops1("no interface lag 1")
#     ops1("end")
#     with ops1.libs.vtysh.ConfigInterface('if03') as ctx:
#         ctx.shutdown()
#     with ops1.libs.vtysh.ConfigInterface('if04') as ctx:
#         ctx.shutdown()
#     out = ops1("get interface {port} hw_intf_config".format(
#         port=ops1.ports["if03"]
#     ), shell="vsctl")
#     assert "enable=\"false\"" in out
#     out = ops1("get interface {port} hw_intf_config".format(
#         port=ops1.ports["if04"]
#     ), shell="vsctl")
#     assert "enable=\"false\"" in out
# 
#     step("Step 16- Event logging happens for interface up/down")
#     with ops1.libs.vtysh.ConfigInterface('if01') as ctx:
#         ctx.no_shutdown()
#     output = ops1("show events")
#     lines = output.split('\n')
#     counter1 = 0
#     for line in lines:
#         if "Interface port_admin set to down for 1 interface" in line:
#             counter1 += 1
#         if "Interface port_admin set to up for 1 interface" in line:
#             counter1 += 1
# 
#     with ops1.libs.vtysh.ConfigInterface('if01') as ctx:
#         ctx.shutdown()
#         ctx.no_shutdown()
#     output = ops1("show events")
#     lines = output.split('\n')
#     counter2 = 0
#     for line in lines:
#         if "Interface port_admin set to down for 1 interface" in line:
#             counter2 += 1
#         if "Interface port_admin set to up for 1 interface" in line:
#             counter2 += 1
# 
#     assert (counter2 - counter1) is 2
