project (ovs_fpa_plugin)
cmake_minimum_required (VERSION 2.8)
set (SRC_DIR src)
set (INCL_DIR include)

set (SOURCES
    ${SRC_DIR}/ops-fpa-copp.c
    ${SRC_DIR}/ops-fpa-ofproto.c
    ${SRC_DIR}/ops-fpa-plugins.c
    ${SRC_DIR}/ops-fpa-qos.c
    ${SRC_DIR}/ops-fpa-stg.c
    ${SRC_DIR}/ops-fpa-netdev.c
    ${SRC_DIR}/ops-fpa-bufmon.c
    ${SRC_DIR}/ops-fpa.c
    ${SRC_DIR}/ops-fpa-init.c
    ${SRC_DIR}/ops-fpa-vlan.c
    ${SRC_DIR}/ops-fpa-util.c
    ${SRC_DIR}/ops-fpa-dev.c
    ${SRC_DIR}/ops-fpa-mac-learning.c
    ${SRC_DIR}/ops-fpa-tap.c
    ${SRC_DIR}/ops-fpa-route.c
    ${SRC_DIR}/ops-fpa-routing.c
    ${SRC_DIR}/ops-fpa-wrap.c
)

include(FindPkgConfig)
pkg_check_modules(OVSCOMMON REQUIRED libovscommon)
pkg_check_modules(FPA REQUIRED libfpa)

set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} ${FPA_CFLAGS} -std=gnu99 -Wall -Werror -Wno-unused-function -O0")

include_directories(${CMAKE_SOURCE_DIR}/${INCL_DIR} ${OVSCOMMON_INCLUDE_DIRS})

add_library (ovs_fpa_plugin SHARED ${SOURCES})

target_link_libraries (ovs_fpa_plugin ${OVSCOMMON_LIBRARIES} ${FPA_LIBRARIES} pthread)

install(TARGETS ovs_fpa_plugin
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}/openvswitch/plugins
)
