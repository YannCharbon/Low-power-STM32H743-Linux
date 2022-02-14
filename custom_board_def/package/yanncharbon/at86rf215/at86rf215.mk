AT86RF215_VERSION = 1.0
AT86RF215_SITE =  $(BR2_EXTERNAL_UCL_STM32_PATH)/package/yanncharbon/at86rf215
AT86RF215_SITE_METHOD = local
AT86RF215_MODULE_SUBDIRS = src
AT86RF215_INSTALL_TARGET = YES
AT86RF215_LICENSE = GPLv2
AT86RF215_LICENSE_FILES = COPYING
AT86RF215_NAME = AT86RF215

AT86RF215_DEPENDENCIES = linux


define AT86RF215_BUILD_CMDS
    #make sure that obj-y += at86rf215/ is only in the build makefile once
    sed -i '/obj-y += at86rf215/d' $(BUILD_DIR)/linux-$(LINUX_VERSION)/drivers/Makefile
    echo "obj-y += at86rf215/" >> $(BUILD_DIR)/linux-$(LINUX_VERSION)/drivers/Makefile
    rm -rf $(BUILD_DIR)/linux-$(LINUX_VERSION)/drivers/at86rf215
    cp -r $(@D)/src $(BUILD_DIR)/linux-$(LINUX_VERSION)/drivers/at86rf215
    echo "obj-y += at86rf215.o" > $(BUILD_DIR)/linux-$(LINUX_VERSION)/drivers/at86rf215/Makefile
endef

define AT86RF215_INSTALL_STAGING_CMDS
endef


define AT86RF215_INSTALL_TARGET_CMDS
endef

define AT86RF215_DEVICES
endef

define AT86RF215_PERMISSIONS
endef

define AT86RF215_USERS
endef

#$(eval $(kernel-module))
$(eval $(generic-package))
