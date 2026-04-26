# Include all package .mk files
# Refer external.desc for the NAME to form BR2_EXTERNAL_$(NAME)_PATH
include $(sort $(wildcard \
    $(BR2_EXTERNAL_ALGAE_BP_PATH)/package/*/*.mk))