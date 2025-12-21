#include "esp_zigbee_core.h"

/* Zigbee configuration */
#define INSTALLCODE_POLICY_ENABLE       false    /* enable the install code policy for security */
#define ESP_ZB_PRIMARY_CHANNEL_MASK     ESP_ZB_TRANSCEIVER_ALL_CHANNELS_MASK  /* Zigbee primary channel mask use in the example */
#define ZB_ENDPOINT                     10

/* Basic manufacturer information */
#define ESP_MANUFACTURER_NAME "\x06""Suxsem"          /* Customized manufacturer name */
#define ESP_MODEL_IDENTIFIER  "\x0F""ScaldaScrivania" /* Customized model identifier */

#define ESP_ZB_ROUTER_CONFIG()                                      \
    {                                                               \
        .esp_zb_role = ESP_ZB_DEVICE_TYPE_ROUTER,                   \
        .install_code_policy = INSTALLCODE_POLICY_ENABLE,           \
        .nwk_cfg.zczr_cfg = {                                       \
            .max_children = 10,                                     \
        },                                                          \
    }

#define ESP_ZB_DEFAULT_RADIO_CONFIG()                           \
    {                                                           \
        .radio_mode = ZB_RADIO_MODE_NATIVE,                     \
    }

#define ESP_ZB_DEFAULT_HOST_CONFIG()                            \
    {                                                           \
        .host_connection_mode = ZB_HOST_CONNECTION_MODE_NONE,   \
    }

typedef struct zcl_basic_manufacturer_info_s {
    char *manufacturer_name;
    char *model_identifier;
} zcl_basic_manufacturer_info_t;


#define MY_CLUSTER                0xfeb2
#define MY_ATTR_POWER_ID          0x0000  // ESP_ZB_ZCL_ATTR_TYPE_U8
#define MY_ATTR_LEVEL_CONFIG_ID   0x0001  // ESP_ZB_ZCL_ATTR_TYPE_U8
// ... 0x0005 reserved
