
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


def test_vlan_internal_cli(topology, step):
    step("### VLAN CLI validations ###")
    sw1 = topology.get('sw1')

    assert sw1 is not None
#    sleep(5)
    sw1('configure terminal'.format(**locals()))
    ret = sw1('do show vlan internal'.format(**locals()))
    assert 'Internal VLAN range  : 1024-4094' in ret and \
           'Internal VLAN policy : ascending' in ret, \
           'Internal VLAN range at bootup validation failed'

    ret = sw1('vlan internal range 100 10 ascending'.format(**locals()))
    assert 'Invalid VLAN range. End VLAN must be greater ' \
           'or equal to start VLAN' in ret, \
           'Internal VLAN range (start > end) validation failed'

    sw1('vlan internal range 10 10 ascending'.format(**locals()))
    ret = sw1('do show vlan internal'.format(**locals()))
    assert 'Internal VLAN range  : 10-10' in ret and \
           'Internal VLAN policy : ascending', \
           'Internal VLAN range (start = end) validation failed'

    sw1('vlan internal range 10 100 ascending'.format(**locals()))
    ret = sw1('do show vlan internal'.format(**locals()))
    assert 'Internal VLAN range  : 10-100' in ret and \
           'Internal VLAN policy : ascending' in ret, \
           'Ascending Internal VLAN range validation failed'

    sw1('vlan internal range 100 200 descending'.format(**locals()))
    ret = sw1('do show vlan internal'.format(**locals()))
    assert 'Internal VLAN range  : 100-200' in ret and \
           'Internal VLAN policy : descending' in ret, \
           'Descending Internal VLAN range validation failed'

    sw1('no vlan internal range'.format(**locals()))
    ret = sw1('do show vlan internal'.format(**locals()))
    assert 'Internal VLAN range  : 1024-4094' in ret and \
           'Internal VLAN policy : ascending' in ret, \
           'Default Internal VLAN range validation failed'
