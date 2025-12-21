#pragma once

#include "esp_check.h"
#include "esp_zigbee_core.h"

#define OTA_UPGRADE_MANUFACTURER            0xa538                                  /* The attribute indicates the value for the manufacturer of the device */
#define OTA_UPGRADE_IMAGE_TYPE              0x5303                                  /* The attribute indicates the the image type of the file that the client is currently downloading */
#define OTA_UPGRADE_RUNNING_FILE_VERSION    0x00000011                              /* The attribute indicates the file version of the running firmware image on the device */
#define OTA_UPGRADE_DOWNLOADED_FILE_VERSION 0x00000011                              /* The attribute indicates the file version of the downloaded firmware image on the device */
#define OTA_UPGRADE_HW_VERSION              0x0001                                  /* The parameter indicates the version of hardware */
#define OTA_UPGRADE_MAX_DATA_SIZE           64                                      /* The parameter indicates the maximum data size of query block image */

esp_err_t zb_ota_upgrade_status_handler(esp_zb_zcl_ota_upgrade_value_message_t message);

#define min(a,b)             \
({                           \
    __typeof__ (a) _a = (a); \
    __typeof__ (b) _b = (b); \
    _a < _b ? _a : _b;       \
})

#define max(a,b)             \
({                           \
    __typeof__ (a) _a = (a); \
    __typeof__ (b) _b = (b); \
    _a > _b ? _a : _b;       \
})
