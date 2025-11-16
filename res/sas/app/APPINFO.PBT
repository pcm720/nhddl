# SAS (Save Application Support) compliant BM Script
# Due to wildcard bug on memory card and mmce, bootdevice:/BM/SCRIPTS/BMRTFLDR.PBT must exist to work. Update as needed

# Change this information to describe the application and where it should be ran from for memcard (SAS) or other devices (SAS_NON_MC)
# SAS is the App folder name. SAS_NON_MC defines if app is in root of non-memcard device (non_mc:/$SAS$) or APP folder (non_mc:/APPS/$SAS$)
# This is so that you do not have to edit any info below line 28.
#
# Some devices are case sensitive! Make sure your case matches!
#
# Source: github.com/pcm720/nhddl
SET "TITLE" "[APP] NHDDL"
SET "VERSION" "1.2.0"
SET "AUTHOR" "pcm720"
SET "DESC" "A Neutrino launcher"
SET "ELF" "nhddl.elf"
SET "SAS" "APP_NHDDL"
# Comment out 1 of the 2 lines below. If app is in non-mc:/$SAS$ comment out the first, if in non-mc:/APPS/$SAS$ comment out the second
# SET "SAS_NON_MC" "$SAS$"
SET "SAS_NON_MC" "APPS/$SAS$"
#
# If an app does not boot, then one of these compatibility options may need to be set to 0, 1, 2 or 3
# 0= No compatibility options, 1= SHUTDOWN "MM", 2= SET "BM.AUTOLOAD_FSD_EN" "0", 3= CC SLEEP
SET "COMP_MODE_MC" "1"
SET "COMP_MODE_MMCE" "1"
SET "COMP_MODE_USB" "1"
SET "COMP_MODE_HDD_APA" "1"
SET "COMP_MODE_DFFS" "1"
#
###############################################################################################
# Do not change these 2 lines!
GOTO "$ARG1$"
RETURN -1

# Used for Autoboot Labels
:LABEL_NAME
    ADDWIDGET "LABEL" "$ARG2$$TITLE$ v$VERSION$"
    EXIT 0

:QUERY

    ADDWIDGET "CALL" "$TITLE$" "$BM.TXT_VERSION$: $VERSION$ $BM.TXT_AUTHOR$: $AUTHOR$ $BM.TXT_DESC$: $DESC$" $ARG2$ "$ARG0$" "$ARG3$" "$ARG4$" "$ARG5$" "$TITLE$" "$PWD$" "$SAS$" "$SAS_NON_MC$"
    EXIT 0

:APP_CONFIG

    SETTITLE "$TITLE$ Config:"
    ADDWIDGET "LABEL" "Choose up to 2 devices for NHDDL"
    ADDWIDGET "LABEL" "to load ISO's from."
    ADDWIDGET "CHOICE" "Device 1:" "First device for NHDDL to scan for ISOs" "BM.APPCNF_APP_NHDDL_DEV1" "None" "ATA" "HDL" "MMCE" "MX4SIO" "USB" "iLINK" "UDPBD"
    ADDWIDGET "CHOICE" "Device 2:" "Second device for NHDDL to scan for ISOs" "BM.APPCNF_APP_NHDDL_DEV2" "None" "ATA" "HDL" "MMCE" "MX4SIO" "USB" "iLINK" "UDPBD"
    ADDWIDGET "CHOICE" "Device 3:" "Third device for NHDDL to scan for ISOs" "BM.APPCNF_APP_NHDDL_DEV3" "None" "ATA" "HDL" "MMCE" "MX4SIO" "USB" "iLINK" "UDPBD"
    ADDWIDGET "LABEL" ""
    ADDWIDGET "CALL" "Delete $TITLE$ variables." "Deletes $TITLE$ variables from BootManager" "$ARG0$" "DEL_CFG"
    SAVEVARS "BM.APPCNF*" "$BM.BM_PATH$/CONFIG/APPCNF.PBT"
    EXIT 0

:DEL_CFG
    CLEARWIDGETS
    SKIPBACK
    UNSET "BM.APPCNF_APP_NHDDL_DEV1"
    UNSET "BM.APPCNF_APP_NHDDL_DEV2"
    UNSET "BM.APPCNF_APP_NHDDL_DEV3"
    SAVEVARS "BM.APPCNF*" "$BM.BM_PATH$/CONFIG/APPCNF.PBT"
    GOTO "APP_CONFIG"
    EXIT 0

:INSTALL
    IF MATCHES "mc?" "$ARG2$"
        GOTO "INSTALL_TO_MC"
    ELSEIF NOT MATCHES "mc?" "$ARG2$"
        GOTO "INSTALL_TO_NONMC"
    ENDIF

:INSTALL_TO_MC
    IF FAIL COPY "$PWD$" "$ARG2$:/$SAS$"
        ECHO "Failed installing $TITLE$"
        MESSAGE "Failed installing $TITLE$"
        RRM "$ARG2$:/$SAS$"
        RETURN -1
    ENDIF
    EXIT 0

:INSTALL_TO_NONMC
    IF FAIL COPY "$PWD$" "$ARG2$:/$SAS_NON_MC$"
        ECHO "Failed installing $TITLE$"
        MESSAGE "Failed installing $TITLE$"
        RRM "$ARG2$:/$SAS_NON_MC$"
        RETURN -1
    ENDIF
    EXIT 0

:REMOVE
    IF FAIL RRM "$PWD$"
        IF FAIL RRM "$PWD$"
            IF FAIL RRM "$PWD$"
                MESSAGE "Failed removing $TITLE$!"
                RETURN -1
            ENDIF
        ENDIF
    ENDIF
    EXIT 0

:RUN
    IF MATCHES "mc?:/*" "$PWD$"
        GOTO "RUN_MC"
    ELSEIF MATCHES "mmce?:/*" "$PWD$"
        GOTO "RUN_MMCE"
    ELSEIF MATCHES "mass:/*" "$PWD$"
        GOTO "RUN_USB"
    ELSEIF MATCHES "pfs0:/*" "$PWD$"
        GOTO "RUN_HDD_APA"
    ELSEIF MATCHES "dffs:/*" "$PWD$"
        GOTO "RUN_DFFS"
    ENDIF

:RUN_MC
    IF EQU "$COMP_MODE_MC$" "1"
        SHUTDOWN "MM"
    ELSEIF EQU "$COMP_MODE_MC$" "2"
        SET "BM.AUTOLOAD_FSD_EN" "0"
    ELSEIF EQU "$COMP_MODE_MC$" "3"
        SETBIOS "OFF"
        SHUTDOWN "ALL"
    ENDIF
    GOTO "RUN_COMP_MODE"

:RUN_MMCE
    IF EQU "$COMP_MODE_MMCE$" "1"
        SHUTDOWN "MM"
    ELSEIF EQU "$COMP_MODE_MMCE$" "2"
        SET "BM.AUTOLOAD_FSD_EN" "0"
    ELSEIF EQU "$COMP_MODE_MMCE$" "3"
        SETBIOS "OFF"
        SHUTDOWN "ALL"
    ENDIF
    GOTO "RUN_COMP_MODE"

:RUN_USB
    IF EQU "$COMP_MODE_USB$" "1"
        SHUTDOWN "MM"
    ELSEIF EQU "$COMP_MODE_USB$" "2"
        SET "BM.AUTOLOAD_FSD_EN" "0"
    ELSEIF EQU "$COMP_MODE_USB$" "3"
        SETBIOS "OFF"
        SHUTDOWN "ALL"
    ENDIF
    GOTO "RUN_COMP_MODE"

:RUN_HDD_APA
    IF EQU "$COMP_MODE_HDD_APA$" "1"
        SHUTDOWN "MM"
    ELSEIF EQU "$COMP_MODE_HDD_APA$" "2"
        SET "BM.AUTOLOAD_FSD_EN" "0"
    ELSEIF EQU "$COMP_MODE_HDD_APA$" "3"
        SETBIOS "OFF"
        SHUTDOWN "ALL"
    ENDIF
    GOTO "RUN_COMP_MODE"

:RUN_DFFS
    IF EQU "$COMP_MODE_DFFS$" "1"
        SHUTDOWN "MM"
    ELSEIF EQU "$COMP_MODE_DFFS$" "2"
        SET "BM.AUTOLOAD_FSD_EN" "0"
    ELSEIF EQU "$COMP_MODE_DFFS$" "3"
        SETBIOS "OFF"
        SHUTDOWN "ALL"
    ENDIF
    GOTO "RUN_COMP_MODE"

:RUN_COMP_MODE

    # DEVICE 1
    IF EQU "$BM.APPCNF_APP_NHDDL_DEV1$" "0"
        SET "DEV1" ""
    ELSEIF EQU "$BM.APPCNF_APP_NHDDL_DEV1$" "1"
        SET "DEV1" "-mode=ata"
    ELSEIF EQU "$BM.APPCNF_APP_NHDDL_DEV1$" "2"
        SET "DEV1" "-mode=hdl"
    ELSEIF EQU "$BM.APPCNF_APP_NHDDL_DEV1$" "3"
        SET "DEV1" "-mode=mmce"
    ELSEIF EQU "$BM.APPCNF_APP_NHDDL_DEV1$" "4"
        SET "DEV1" "-mode=mx4sio"
    ELSEIF EQU "$BM.APPCNF_APP_NHDDL_DEV1$" "5"
        SET "DEV1" "-mode=usb"
    ELSEIF EQU "$BM.APPCNF_APP_NHDDL_DEV1$" "6"
        SET "DEV1" "-mode=ilink"
    ELSEIF EQU "$BM.APPCNF_APP_NHDDL_DEV1$" "7"
        SET "DEV1" "-mode=udpbd -udpbd_ip=$BM.CNF_NET_IP0$.$BM.CNF_NET_IP1$.$BM.CNF_NET_IP2$.$BM.CNF_NET_IP3$"
    ENDIF
    # DEVICE 2
    IF EQU "$BM.APPCNF_APP_NHDDL_DEV2$" "0"
        SET "DEV2" ""
    ELSEIF EQU "$BM.APPCNF_APP_NHDDL_DEV2$" "1"
        SET "DEV2" "-mode=ata"
    ELSEIF EQU "$BM.APPCNF_APP_NHDDL_DEV2$" "2"
        SET "DEV2" "-mode=hdl"
    ELSEIF EQU "$BM.APPCNF_APP_NHDDL_DEV2$" "3"
        SET "DEV2" "-mode=mmce"
    ELSEIF EQU "$BM.APPCNF_APP_NHDDL_DEV2$" "4"
        SET "DEV2" "-mode=mx4sio"
    ELSEIF EQU "$BM.APPCNF_APP_NHDDL_DEV2$" "5"
        SET "DEV2" "-mode=usb"
    ELSEIF EQU "$BM.APPCNF_APP_NHDDL_DEV2$" "6"
        SET "DEV2" "-mode=ilink"
    ELSEIF EQU "$BM.APPCNF_APP_NHDDL_DEV2$" "7"
        SET "DEV2" "-mode=udpbd -udpbd_ip=$BM.CNF_NET_IP0$.$BM.CNF_NET_IP1$.$BM.CNF_NET_IP2$.$BM.CNF_NET_IP3$"
    ENDIF
    # DEVICE 3
    IF EQU "$BM.APPCNF_APP_NHDDL_DEV3$" "0"
        SET "DEV3" ""
    ELSEIF EQU "$BM.APPCNF_APP_NHDDL_DEV3$" "1"
        SET "DEV3" "-mode=ata"
    ELSEIF EQU "$BM.APPCNF_APP_NHDDL_DEV3$" "2"
        SET "DEV3" "-mode=hdl"
    ELSEIF EQU "$BM.APPCNF_APP_NHDDL_DEV3$" "3"
        SET "DEV3" "-mode=mmce"
    ELSEIF EQU "$BM.APPCNF_APP_NHDDL_DEV3$" "4"
        SET "DEV3" "-mode=mx4sio"
    ELSEIF EQU "$BM.APPCNF_APP_NHDDL_DEV3$" "5"
        SET "DEV3" "-mode=usb"
    ELSEIF EQU "$BM.APPCNF_APP_NHDDL_DEV3$" "6"
        SET "DEV3" "-mode=ilink"
    ELSEIF EQU "$BM.APPCNF_APP_NHDDL_DEV3$" "7"
        SET "DEV3" "-mode=udpbd -udpbd_ip=$BM.CNF_NET_IP0$.$BM.CNF_NET_IP1$.$BM.CNF_NET_IP2$.$BM.CNF_NET_IP3$"
    ENDIF

    LOADEXEC "PBAT" "$BM.SCRIPTS$/LOADEXEC.PBT" "$PWD$/$ELF$" "$DEV1$ $DEV2$ $DEV3$"
    EXIT 0
