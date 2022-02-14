AT86RF215ICP_VERSION = 1.0
AT86RF215ICP_SITE =  $(BR2_EXTERNAL_CUSTOM_STM32_XIP_PATH)/package/yanncharbon/at86rf215icp
AT86RF215ICP_SITE_METHOD = local
AT86RF215ICP_MODULE_SUBDIRS = src
AT86RF215ICP_INSTALL_TARGET = YES
AT86RF215ICP_LICENSE = GPLv2
AT86RF215ICP_LICENSE_FILES = COPYING
AT86RF215ICP_NAME = AT86RF215ICP

AT86RF215_DEPENDENCIES = linux


define AT86RF215ICP_BUILD_CMDS
    #make sure that obj-y += at86rf215icp/ is only in the build makefile once
    sed -i '/obj-y += at86rf215icp/d' $(BUILD_DIR)/linux-$(LINUX_VERSION)/drivers/Makefile
    echo "obj-y += at86rf215icp/" >> $(BUILD_DIR)/linux-$(LINUX_VERSION)/drivers/Makefile
    rm -rf $(BUILD_DIR)/linux-$(LINUX_VERSION)/drivers/at86rf215icp
    cp -r $(@D)/src $(BUILD_DIR)/linux-$(LINUX_VERSION)/drivers/at86rf215icp
    echo "obj-y += at86rf215.o" > $(BUILD_DIR)/linux-$(LINUX_VERSION)/drivers/at86rf215icp/Makefile
endef

define AT86RF215ICP_INSTALL_STAGING_CMDS
endef


define AT86RF215ICP_INSTALL_TARGET_CMDS
endef

define AT86RF215ICP_DEVICES
endef

define AT86RF215ICP_PERMISSIONS
endef

define AT86RF215ICP_USERS
endef

#$(eval $(kernel-module))
$(eval $(generic-package))
