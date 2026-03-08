# IRX files
set(IRX_FILES
    sio2man
    mcman
    mcserv
    fileXio
    iomanX
    freepad
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
)

# Local IRX files
set(LOCAL_IRX_FILES
    mmceman
    udpfs_ioman
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

# udpfs_ioman
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
