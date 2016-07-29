
"""
OpenSwitch Test for switchd related configurations.
"""

# from pytest import set_trace
# from time import sleep
import re
from time import sleep

TOPOLOGY = """
# +-------+
# |  ops1 |
# +-------+

# Nodes
[type=marvellswitch name="OpenSwitch 1"] ops1
"""


def createvlan(dut):
        vlan_created = False
        dut('conf t')
        dut('vlan 2')
        dut('end')
        out = dut('show running-config')
        lines = out.splitlines()
        for line in lines:
            if 'vlan 2' in line:
                vlan_created = True
        assert (vlan_created is True)


def showvlansummary(dut):
    vlan_summary_present = False
    dut('conf t')
    # Vlan 1 didn't exist before by default
    dut('vlan 1')
    dut('exit')
    dut('vlan 2')
    dut('exit')
    dut('vlan 12')
    dut('exit')
    dut('vlan 123')
    dut('exit')
    dut('vlan 1234')
    dut('end')
    out = dut('show vlan summary')
    lines = out.splitlines()
    for line in lines:
        if 'Number of existing VLANs: 5' in line:
            vlan_summary_present = True
    assert (vlan_summary_present is True)


def deletevlan(dut):
    vlan_deleted = True
    dut('conf t')
    dut('vlan 99')
    dut('exit')
    dut('no vlan 99')
    dut('end')
    out = dut('show vlan')
    lines = out.splitlines()
    for line in lines:
        if 'VLAN99' in line:
            vlan_deleted = False
    assert (vlan_deleted is True)


def addaccessvlantointerface(dut):
    dut('conf t')
    dut('vlan 2')
    dut('exit')
    #dut('interface 21')
    #We have only 4 ports! 21 => 3
    dut('interface 3')
    out = dut('vlan access 2')
    success = 0
    assert 'Disable routing on the interface' in out

    dut('no routing')
    dut('vlan access 2')
    out = dut('do show running-config')
    lines = out.splitlines()
    for line in lines:
        if 'vlan access 1' in line:
            success += 1

    out = dut('do show vlan')
    lines = out.splitlines()
    for line in lines:
        if 'VLAN2' in line and '3' in line:
            success += 1

    assert 'success == 2'

    vlan_access_cmd_found = False
    dut('no vlan access')
    out = dut('do show running-config')
    lines = out.splitlines()
    for line in lines:
        if 'vlan access 2' in line:
            vlan_access_cmd_found = True
    assert (vlan_access_cmd_found is False)

#     dut('exit')
#     dut('interface lag 1')
#     dut('exit')
#     dut('interface 21')
#     dut('lag 1')
#     out = dut('vlan access 2')
#     assert "Can't configure VLAN, interface is part of LAG" in out
# 
#     dut('exit')
#     dut('no interface lag 1')
#     dut('end')


def addtrunkvlantointerface(dut):
    dut('conf t')
    dut('vlan 2')
    dut('exit')
    dut('vlan 12')
    dut('exit')
    # Split the parent interface to enable L2/L3 configurations
    # on child interfaces
    dut('interface 52')
    dut._shells['vtysh']._prompt = (
        'Do you want to continue [y/n]?'
    )
    dut('split')
    dut._shells['vtysh']._prompt = (
        '(^|\n)switch(\\([\\-a-zA-Z0-9]*\\))?#'
    )
    dut('y')
    dut('interface 52-1')
    out = dut('vlan trunk allowed 2')
    success = 0
    assert 'Disable routing on the interface' in out

    dut('no routing')
    dut('vlan trunk allowed 2')
    out = dut('do show running-config')
    lines = out.splitlines()
    for line in lines:
        if 'vlan trunk allowed 2' in line:
            success += 1

    out = dut('do show vlan')
    lines = out.splitlines()
    for line in lines:
        if 'VLAN2' in line and '52-1' in line:
            success += 1
    assert success == 2

    vlan_trunk_allowed_cmd_found = True
    dut('no vlan trunk allowed 2')
    out = dut('do show running-config')
    lines = out.split('\n')
    for line in lines:
        if 'vlan trunk allowed 2' in line:
            vlan_trunk_allowed_cmd_found = False

    assert (vlan_trunk_allowed_cmd_found is True)

    dut('exit')
    dut('interface lag 1')
    dut('exit')
    dut('interface 52-1')
    dut('lag 1')
    out = dut('vlan trunk allowed 1')
    assert "Can't configure VLAN, interface is part of LAG" in out, \
        'Test to add VLAN to interface - FAILED!'

    dut('exit')
    dut('no interface lag 1')
    dut('end')


def addtrunknativevlantointerface(dut):
    dut('conf t')
    dut('vlan 2')
    dut('exit')
    dut('vlan 77')
    dut('exit')
    dut('interface 52-2')
    out = dut('vlan trunk native 2')
    success = 0
    assert 'Disable routing on the interface' in out

    dut('no routing')
    dut('vlan trunk native 2')
    dut('vlan trunk allowed 12')
    out = dut('do show running-config')
    lines = out.splitlines()
    for line in lines:
        if 'vlan trunk native 2' in line:
            success += 1
        if 'vlan trunk allowed 12' in line:
            success += 1

    out = dut('do show vlan')
    lines = out.splitlines()
    for line in lines:
        if 'VLAN2' in line and '52-2' in line:
            success += 1
        if 'VLAN12' in line and '52-2' in line:
            success += 1

    assert success == 4

    vlan_trunk_native_cmd_found = False
    dut('no vlan trunk native')
    out = dut('do show running-config')
    lines = out.splitlines()
    for line in lines:
        if 'vlan trunk native' in line:
            vlan_trunk_native_cmd_found = True
    assert (vlan_trunk_native_cmd_found is False)

    dut('exit')
    dut('interface lag 1')
    dut('exit')
    dut('interface 52-2')
    dut('lag 1')
    out = dut('vlan trunk native 1')
    assert "Can't configure VLAN, interface is part of LAG" in out, \
        'Test to add trunk native to interface - FAILED!'

    dut('exit')
    dut('no interface lag 1')
    dut('end')


def addtrunknativetagvlantointerface(dut):
    dut('conf t')
    dut('vlan 1789')
    dut('exit')
    dut('vlan 88')
    dut('exit')
    dut('interface 52-3')
    out = dut('vlan trunk native tag')
    success = 0
    assert 'Disable routing on the interface' in out

    dut('no routing')
    dut('vlan trunk native 1789')
    dut('vlan trunk allowed 88')
    dut('vlan trunk native tag')
    out = dut('do show running-config')
    lines = out.splitlines()
    for line in lines:
        if 'vlan trunk native 1789' in line:
            success += 1
        if 'vlan trunk allowed 88' in line:
            success += 1
        if 'vlan trunk native tag' in line:
            success += 1

    out = dut('do show vlan')
    lines = out.split('\n')
    for line in lines:
        if 'VLAN1789' in line and '52-3' in line:
            success += 1
        if 'VLAN88' in line and '52-3' in line:
            success += 1

    assert success == 5

    vlan_trunk_native_tag_present = False
    dut('no vlan trunk native tag')
    out = dut('do show running-config')
    lines = out.splitlines()
    for line in lines:
        if 'vlan trunk native tag' in line:
            return True
    assert (vlan_trunk_native_tag_present is False)

    dut('exit')
    dut('interface lag 1')
    dut('exit')
    dut('interface 52-3')
    dut('lag 1')
    out = dut('vlan trunk native tag')
    assert "Can't configure VLAN, interface is part of LAG" in out, \
        'Test add trunk native tag vlan to interface - FAILED!'

    dut('exit')
    dut('no interface lag 1')
    dut('end')


def addaccessvlantolag(dut):
    dut('conf t')
    dut('vlan 2')
    dut('exit')
    dut('interface lag 21')
    out = dut('vlan access 2')
    success = 0
    assert 'Disable routing on the LAG' in out

    dut('no routing')
    dut('vlan access 2')
    out = dut('do show running-config')
    lines = out.splitlines()
    for line in lines:
        if 'vlan access 2' in line:
            success += 1

    out = dut('do show vlan')
    lines = out.splitlines()
    for line in lines:
        if 'VLAN2' in line and 'lag21' in line:
            success += 1

    assert success == 2
    vlan_access_cmd_present = False
    dut('no vlan access')
    out = dut('do show running-config')
    lines = out.splitlines()
    for line in lines:
        if 'vlan access 2' in line:
            vlan_access_cmd_present = True

    assert (vlan_access_cmd_present is False)
    dut('end')


def addtrunkvlantolag(dut):
    dut('conf t')
    dut('vlan 2345')
    dut('exit')
    dut('vlan 55')
    dut('exit')
    dut('interface lag 31')
    out = dut('vlan trunk allowed 55')
    success = 0
    assert 'Disable routing on the LAG' in out

    dut('no routing')
    dut('vlan trunk allowed 55')
    out = dut('do show running-config')
    lines = out.splitlines()
    for line in lines:
        if 'vlan trunk allowed 55' in line:
            success += 1

    out = dut('do show vlan')
    lines = out.splitlines()
    for line in lines:
        if 'VLAN55' in line and 'lag31' in line:
            success += 1
    assert success == 2

    vlan_trunk_allowed_cmd_present = False
    dut('no vlan trunk allowed 55')
    dut('do show running-config')
    lines = out.split('\n')
    for line in lines:
        if 'vlan trunk allowed 55' in line:
            vlan_trunk_allowed_cmd_present = True
    assert (vlan_trunk_allowed_cmd_present is False)
    dut('end')


def addtrunknativevlantolag(dut):
    dut('conf t')
    dut('vlan 1234')
    dut('exit')
    dut('vlan 66')
    dut('exit')
    dut('interface lag 41')
    out = dut('vlan trunk native 1234')
    success = 0
    assert 'Disable routing on the LAG' in out

    dut('no routing')
    dut('vlan trunk native 1234')
    dut('vlan trunk allowed 66')
    out = dut('do show running-config')
    lines = out.splitlines()
    for line in lines:
        if 'vlan trunk native 1234' in line:
            success += 1
        if 'vlan trunk allowed 66' in line:
            success += 1

    out = dut('do show vlan')
    lines = out.splitlines()
    for line in lines:
        if 'VLAN1234' in line and 'lag41' in line:
            success += 1
        if 'VLAN66' in line and 'lag41' in line:
            success += 1

    dut('no vlan trunk native')
    out = dut('do show running-config')

    assert success == 4
    dut('end')


def addtrunknativetagvlantolag(dut):
    dut('conf t')
    dut('vlan 1567')
    dut('exit')
    dut('vlan 44')
    dut('exit')
    dut('vlan 2')
    dut('exit')
    dut('interface lag 51')
    out = dut('vlan trunk native tag')
    success = 0
    assert 'Disable routing on the LAG' in out

    dut('no routing')

    dut('vlan trunk native 1567')
    dut('vlan trunk allowed 44')
    dut('vlan trunk native tag')
    out = dut('do show running-config')
    lines = out.splitlines()
    for line in lines:
        if 'vlan trunk native 1567' in line:
            success += 1
        if 'vlan trunk allowed 44' in line:
            success += 1
        if 'vlan trunk native tag' in line:
            success += 1

    out = dut('do show vlan')
    lines = out.splitlines()
    for line in lines:
        if 'VLAN1567' in line and 'lag51' in line:
            success += 1
        if 'VLAN44' in line and 'lag51' in line:
            success += 1
    assert success == 5

    vlan_trunk_native_tag_present = False
    dut('no vlan trunk native tag')
    out = dut('do show running-config')
    lines = out.splitlines()
    for line in lines:
        if 'vlan trunk native tag' in line:
            vlan_trunk_native_tag_present = True
    assert (vlan_trunk_native_tag_present is False)
    dut('end')


def vlancommands(dut):
    dut('conf t')
    dut('vlan 2')
    dut('no shutdown')
    out = dut('list vlan VLAN2', shell="vsctl")
    lines = out.splitlines()
    success = 0
    for line in lines:
        if 'admin' in line:
            if 'up' in line:
                success += 1

    assert success == 1
    dut('end')


def internalvlanchecks(dut):
    # Internal VLANs are not assigned in VSI by default.
    # To test functionality, we use below command
    # to generate internal VLANs for L3 interfaces.
    dut('/usr/bin/ovs-vsctl set Subsystem base '
        'other_info:l3_port_requires_internal_vlan=1', shell="bash")

    dut('conf t')
    dut('interface 1')
    dut('ip address 1.1.1.1/8')
    dut('exit')

    ret = dut('vlan 1024')
    assert (
        'VLAN1024 is used as an internal VLAN. No further configuration '
        'allowed.' in ret
    )

    ret = dut('no vlan 1024')
    assert (
        'VLAN1024 is used as an internal VLAN. Deletion not allowed.' in ret
    )
    dut('end')


def display_vlan_id_in_numerical_order(dut):
    dut('conf t')
    dut('vlan 10')
    dut('vlan 1')
    dut('vlan 9')
    dut('vlan 15')
    dut('vlan 7')
    out = dut('do show vlan')
    list_interface = out.splitlines()
    result_sortted = []
    result_orig = []
    for x in list_interface:
        test = re.match('\d+\s+VLAN\d+\s+[down|up]', x)
        if test:
            test_number = test.group(0)
            number = re.match('\d+', test_number).group(0)
            result_orig.append(int(number))
            result_sortted.append(int(number))

    result_sortted.sort()

    assert result_orig == result_sortted
    dut('end')


def novlantrunkallowed(dut):
    dut('conf t')
    dut('int 1')
    dut('no routing')
    out1 = dut('do show running-config')
    dut('no vlan trunk allowed 100')
    out2 = dut('do show running-config')
    assert out1 in out2
    dut('end')


def test_vtysh_ct_vlan(topology, step):
    
#    sleep(5)
    ops1 = topology.get('ops1')
    assert ops1 is not None

    step('Test to create VLAN')
    createvlan(ops1)

    step('Test "show vlan summary" command')
    showvlansummary(ops1)

    step('Test to delete VLAN')
    deletevlan(ops1)

    step('Test "vlan access" command')
    addaccessvlantointerface(ops1)

#    step('Test to add VLAN to interface')
#    addtrunkvlantointerface(ops1)

#     step('Test to add trunk native to interface')
#     addtrunknativevlantointerface(ops1)

#     step(' Test to add trunk native tag to interface')
#     addtrunknativetagvlantointerface(ops1)

#     step('Test add trunk native tag vlan to interface')
#     addaccessvlantolag(ops1)

#     step('Test to add access vlan to LAG')
#     addtrunkvlantolag(ops1)
# 
#     step('Test to add trunk native vlan to LAG')
#     addtrunknativevlantolag(ops1)
# 
#     step('Test to add trunk native tag vlan to LAG')
#     addtrunknativetagvlantolag(ops1)
# 
#     step('Test to add trunk native tag vlan to LAG')
#     vlancommands(ops1)

#     step('Test to check internal vlan validations')
#     internalvlanchecks(ops1)

    step('Test to verify that vlan id is displaying in numerical order')
    display_vlan_id_in_numerical_order(ops1)

    step('Test to check no vlan trunk allowed')
    novlantrunkallowed(ops1)
