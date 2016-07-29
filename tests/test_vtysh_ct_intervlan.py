
from time import sleep


TOPOLOGY = """
#
# +-------+
# |  sw1  |
# +-------+
#

# Nodes
[type=marvellswitch name="Switch 1"] sw1
"""


def test_intervlan_cli(topology, step):
#    sleep(5)
    error_str_range = "Vlanid outside valid vlan range <1-4094>"
    error_str = "Unknown command."
    error_str_invalid_input = "Invalid vlan input"
    max_vlan = "4095"
    min_vlan = "0"
    sw1 = topology.get('sw1')

    assert sw1 is not None

    step("### Test invalid vlan interface names ###")
    sw1('configure terminal')

    # Checking out of range Vlan inputs
    ret = sw1('interface vlan ' + max_vlan)
    assert error_str in ret, \
        'Adding Vlan id = %s validation failed' % max_vlan

    ret = sw1('interface vlan' + max_vlan)
    assert error_str_range in ret, \
        'Adding Vlan id = %s validation failed' % max_vlan

    ret = sw1('interface vlan ' + min_vlan)
    assert error_str in ret, \
        'Adding Vlan id = 0 validation failed'

    ret = sw1('interface vlan' + min_vlan)
    assert error_str_invalid_input in ret, \
        'Adding Vlan id = 0 validation failed'

    # Checking invalid interface vlan input parameters
    ret = sw1('interface vlan 2abc ')
    assert error_str in ret, \
        'Vlan id = 2abc validation failed'

    ret = sw1('interface vlan2abc')
    assert error_str_invalid_input in ret, \
        'Vlan id = 2abc validation failed'

    ret = sw1('interface vlan abc2abc ')
    assert error_str in ret, \
        'Vlan id = 2abc validation failed'

    ret = sw1('interface vlanabc2abc')
    assert error_str_invalid_input in ret, \
        'Vlan id = 2abc validation failed'

    ret = sw1('interface vlan abc#$ ')
    assert error_str in ret, \
        'Vlan id = abc#$ validation failed'

    ret = sw1('interface vlanabc#$')
    assert error_str_invalid_input in ret, \
        'Vlan id = abc#$ validation failed'

    # Deleting interface vlan outside range <1-4094>
    ret = sw1('no interface vlan ' + max_vlan)
    assert error_str in ret, \
        'Deleting vlan id = %s validation failed' % max_vlan

    ret = sw1('no interface vlan' + max_vlan)
    assert error_str_range in ret, \
        'Deleting vlan id = %s validation failed' % max_vlan

    ret = sw1('no interface vlan ' + min_vlan)
    assert error_str in ret, \
        'Deleting vlan id = %s validation failed' % min_vlan

    ret = sw1('no interface vlan' + min_vlan)
    assert error_str_invalid_input in ret, \
        'Deleting vlan id = %s validation failed' % min_vlan

    step("Test to verify if interface vlan can be created with out L2 Vlan")
    ret = sw1('interface vlan 100')
    assert "VLAN 100 should be created before creating interface VLAN100" \
           in ret, "Able to create interface vlan without creating L2 vlan"

    step("Test to verify if interface VLAN can be created after L2 VLAN")
    sw1('vlan 100')
    ret = sw1('interface vlan 100')
    assert error_str not in ret, \
        'Failed to create interface VLAN after creating L2 VLAN'

    step("Test to verify if L2 VLAN can be removed when interface VLAN exists")
    ret = sw1('no vlan 100')
    assert "VLAN100 is used as an interface VLAN. Deletion not allowed." \
           in ret, "Able to delete L2 vlan with outt deleting interface VLAN"

    step("Test add and delete vlan interface")
    sw1('interface vlan 1')
    ret = sw1('do show vrf')
    assert "vlan1" in ret, 'Failed to add vlan interface'

    sw1('vlan 2')
    sw1('no shutdown')
    sw1('interface vlan 2')
    # Verify interface name
    list_cmd = sw1('get interface vlan2 name', shell='vsctl').strip()
    assert "vlan2" in list_cmd, 'Failed to add interface to DB'

    # Verify interface type
    list_cmd = sw1('get interface vlan2 type', shell='vsctl').strip()
    assert 'internal' in list_cmd, 'Failed to add interface to DB'

    # Verify port name
    list_cmd = sw1('get port vlan2 name', shell='vsctl').strip()
    assert "vlan2" in list_cmd, 'Failed to add port to DB'

    # verify interface uuid in port row
    uuid = sw1('get interface vlan2 _uuid', shell='vsctl').strip()
    uuid = uuid.split('\n')
    port_list = sw1('get port vlan2 interfaces', shell='vsctl').strip()
    port_list = port_list.split('\n')
    assert uuid[0] in port_list[0], 'Failed to add port to DB'

    # Verify port in bridge
    port_uuid = sw1('get port vlan2 _uuid', shell='vsctl').strip()
    port_uuid = port_uuid.split('\n')
    port_list = sw1('get bridge bridge_normal ports', shell='vsctl').strip()
    port_list = port_list.split('\n')
    assert port_uuid[0] in port_list[0], 'Failed to add port to DB'

    # Verify port in vrf
    port_uuid = sw1('get port vlan2 _uuid', shell='vsctl').strip()
    port_uuid = port_uuid.split('\n')
    port_list = sw1('get vrf vrf_default ports', shell='vsctl').strip()
    port_list = port_list.split('\n')
    assert port_uuid[0] in port_list[0], 'Failed to add port to DB'

    # Deleting interface vlan and verify if VRF can see it
    sw1('end')
    sw1('config terminal')
    sw1('no interface vlan 1')
    ret = sw1('do show vrf')
    assert "vlan1 " not in ret, 'Failed to delete vlan interface'

    # Deleting non existing vlan interface
    ret = sw1('no interface vlan 1')
    assert "Vlan interface does not exist. Cannot delete" in ret, \
        'Able to delete non existing vlan interface'

    # Deleting vlan interface from OVSDB with name same as Interface Name
    sw1('no interface vlan 2')
    # Check for interface name in OVSDB
    list_cmd = sw1('get interface vlan2 name', shell='vsctl').strip()
    assert "no row \"vlan2\" in table Interface" in list_cmd, \
        'Failed to delete vlan interface in DB'

    # Check for port name in OVSDB
    list_cmd = sw1('get port vlan2 name', shell='vsctl').strip()
    assert "no row \"vlan2\" in table Port" in list_cmd, \
        'Failed to delete vlan interface in DB'

    # Checking multiple interfaces add and delete
    sw1('interface vlan 1')
    sw1('interface vlan 2')
    sw1('vlan 3')
    sw1('interface vlan 3')
    sw1('no interface vlan 2')
    ret = sw1('do show vrf')
    assert "vlan2" not in ret, 'Multiple Interface delete failed'

    sw1('no interface vlan 1')
    sw1('no interface vlan 3')
    
"""
    step('Test show running-config for vlan interface changes')
    # Modifying interface data to test show running-config
    sw1('interface vlan 2')
    sw1('ipv6 address 2002::1/120')
    sw1('ip address 10.1.1.1/8')
    sw1('ip address 11.1.1.3/8 secondary')
    sw1('ipv6 address 3002::2/120 secondary')

    sw1('interface vlan 3')
    ret = sw1('do show running-config')
    assert 'ip address 10.1.1.1/8' in ret and \
        'ip address 11.1.1.3/8 secondary' in ret and \
        'ipv6 address 2002::1/120' in ret and \
        'ipv6 address 3002::2/120 secondary' in ret, \
        'Show running config for interface vlan2 failed'

    assert 'no shutdown' in ret, 'Show running ' \
        'config for interface vlan3 failed'

    sw1('no interface vlan 2')
    sw1('no interface vlan 3')
"""
