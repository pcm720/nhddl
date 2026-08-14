# IRX files
set(IRX_FILES
    sio2man
    mcman
    mcserv
    fileXio
    iomanX
    freepad
    poweroff
    ps2dev9
    bdm
    bdmfs_fatfs
    ata_bd
    usbd_mini
    usbmass_bd_mini
    mx4sio_bd_mini
    iLinkman
    IEEE1394_bd_mini
    ps2hdd-bdm
    ps2fs
    ps2ip
    ps2ips
    smap-ps2ip
)

# Local IRX files
set(LOCAL_IRX_FILES
    mmceman
    resetspu
    udpfs_ioman_ps2ip
)

# mmceman
add_custom_command(
    OUTPUT
        ${CMAKE_CURRENT_BINARY_DIR}/mmceman.irx
    COMMAND make -C ${CMAKE_CURRENT_SOURCE_DIR}/iop/mmceman/mmceman
    COMMAND ${CMAKE_COMMAND} -E rename
        ${CMAKE_CURRENT_SOURCE_DIR}/iop/mmceman/mmceman/irx/mmceman.irx
        ${CMAKE_CURRENT_BINARY_DIR}/mmceman.irx
    WORKING_DIRECTORY
        ${CMAKE_CURRENT_SOURCE_DIR}/iop/mmceman/mmceman
    COMMENT "Building mmceman"
)


# Legacy ministack targets are kept available for Neutrino/fallback builds,
# but NHDDL embeds only the PS2IP-backed IOMAN variant below.
add_custom_command(
    OUTPUT
        ${CMAKE_CURRENT_BINARY_DIR}/smap.irx
    COMMAND make -C ${CMAKE_CURRENT_SOURCE_DIR}/iop/udpfs/smap
    COMMAND ${CMAKE_COMMAND} -E rename
        ${CMAKE_CURRENT_SOURCE_DIR}/iop/udpfs/smap/irx/smap.irx
        ${CMAKE_CURRENT_BINARY_DIR}/smap.irx
    WORKING_DIRECTORY
        ${CMAKE_CURRENT_SOURCE_DIR}/iop/udpfs/smap
    COMMENT "Building smap"
)

# OPL's one-shot SPU2 reset module. It exits with MODULE_NO_RESIDENT_END after
# clearing both SPU cores, so NHDDL loads it through a dedicated helper rather
# than treating it as a normal resident module.
add_custom_command(
    OUTPUT
        ${CMAKE_CURRENT_BINARY_DIR}/resetspu.irx
    COMMAND make -C ${CMAKE_CURRENT_SOURCE_DIR}/iop/resetspu clean
    COMMAND make -C ${CMAKE_CURRENT_SOURCE_DIR}/iop/resetspu
    COMMAND ${CMAKE_COMMAND} -E rename
        ${CMAKE_CURRENT_SOURCE_DIR}/iop/resetspu/resetspu.irx
        ${CMAKE_CURRENT_BINARY_DIR}/resetspu.irx
    DEPENDS
        ${CMAKE_CURRENT_SOURCE_DIR}/iop/resetspu/Makefile
        ${CMAKE_CURRENT_SOURCE_DIR}/iop/resetspu/resetspu.c
        ${CMAKE_CURRENT_SOURCE_DIR}/iop/resetspu/imports.lst
        ${CMAKE_CURRENT_SOURCE_DIR}/iop/resetspu/irx_imports.h
    WORKING_DIRECTORY
        ${CMAKE_CURRENT_SOURCE_DIR}/iop/resetspu
    COMMENT "Building OPL resetspu"
)
add_custom_command(
    OUTPUT
        ${CMAKE_CURRENT_BINARY_DIR}/udpfs_ioman_ps2ip.irx
    COMMAND make -C ${CMAKE_CURRENT_SOURCE_DIR}/iop/udpfs/udpfs UDPFS_IOMAN_PS2IP=1
    COMMAND ${CMAKE_COMMAND} -E rename
        ${CMAKE_CURRENT_SOURCE_DIR}/iop/udpfs/udpfs/irx/udpfs_ioman_ps2ip.irx
        ${CMAKE_CURRENT_BINARY_DIR}/udpfs_ioman_ps2ip.irx
    WORKING_DIRECTORY
        ${CMAKE_CURRENT_SOURCE_DIR}/iop/udpfs/udpfs
    COMMENT "Building PS2IP-backed udpfs"
)
add_custom_command(
    OUTPUT
        ${CMAKE_CURRENT_BINARY_DIR}/ministack.irx
    COMMAND make -C ${CMAKE_CURRENT_SOURCE_DIR}/iop/udpfs/ministack
    COMMAND ${CMAKE_COMMAND} -E rename
        ${CMAKE_CURRENT_SOURCE_DIR}/iop/udpfs/ministack/irx/ministack.irx
        ${CMAKE_CURRENT_BINARY_DIR}/ministack.irx
    WORKING_DIRECTORY
        ${CMAKE_CURRENT_SOURCE_DIR}/iop/udpfs/ministack
    COMMENT "Building ministack"
)
add_custom_command(
    OUTPUT
        ${CMAKE_CURRENT_BINARY_DIR}/udpfs_ioman.irx
    COMMAND make -C ${CMAKE_CURRENT_SOURCE_DIR}/iop/udpfs/udpfs UDPFS_IOMAN=1
    COMMAND ${CMAKE_COMMAND} -E rename
        ${CMAKE_CURRENT_SOURCE_DIR}/iop/udpfs/udpfs/irx/udpfs_ioman.irx
        ${CMAKE_CURRENT_BINARY_DIR}/udpfs_ioman.irx
    WORKING_DIRECTORY
        ${CMAKE_CURRENT_SOURCE_DIR}/iop/udpfs/udpfs
    COMMENT "Building udpfs"
)

# ps2ftpd: the exact hardware-validated module is retained in-repo so a fresh
# clone produces the tested dashboard. Its AFL-2.0 source and build notes are
# retained under iop/ps2ftpd-src/.
add_custom_command(
    OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/ps2ftpd_irx.c"
    COMMAND ${PS2SDK}/bin/bin2c ${CMAKE_CURRENT_SOURCE_DIR}/iop/ps2ftpd.irx
            "${CMAKE_CURRENT_BINARY_DIR}/ps2ftpd_irx.c"
            "ps2ftpd_irx"
    DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/iop/ps2ftpd.irx
    COMMENT "Converting ps2ftpd with bin2c"
)
list(APPEND SOURCES "${CMAKE_CURRENT_BINARY_DIR}/ps2ftpd_irx.c")

foreach(IRX_FILE ${IRX_FILES})
    string(REPLACE "-" "_" irx_name_clean ${IRX_FILE})
    add_custom_command(
        OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/${irx_name_clean}_irx.c"
        COMMAND ${PS2SDK}/bin/bin2c ${PS2SDK}/iop/irx/${IRX_FILE}.irx
                "${CMAKE_CURRENT_BINARY_DIR}/${irx_name_clean}_irx.c"
                "${irx_name_clean}_irx"
        DEPENDS ${PS2SDK}/iop/irx/${IRX_FILE}.irx
        COMMENT "Converting ${IRX_FILE} with bin2c"
    )

    list(APPEND SOURCES "${CMAKE_CURRENT_BINARY_DIR}/${irx_name_clean}_irx.c")
endforeach()
foreach(IRX_FILE ${LOCAL_IRX_FILES})
    add_custom_command(
        OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/${IRX_FILE}_irx.c"
        COMMAND ${PS2SDK}/bin/bin2c ${CMAKE_CURRENT_BINARY_DIR}/${IRX_FILE}.irx
                "${CMAKE_CURRENT_BINARY_DIR}/${IRX_FILE}_irx.c"
                "${IRX_FILE}_irx"
        DEPENDS ${CMAKE_CURRENT_BINARY_DIR}/${IRX_FILE}.irx
        COMMENT "Converting ${IRX_FILE} with bin2c"
    )

    list(APPEND SOURCES "${CMAKE_CURRENT_BINARY_DIR}/${IRX_FILE}_irx.c")
endforeach()
