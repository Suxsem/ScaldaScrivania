#include "Ota.h"

#include "esp_check.h"
#include "esp_log.h"
#include "esp_zigbee_core.h"
#include "nvs_flash.h"
#include "esp_ota_ops.h"
#include "esp_timer.h"

static const esp_partition_t *s_ota_partition = NULL;
static esp_ota_handle_t s_ota_handle = 0;
static size_t ota_data_len_;
static size_t ota_header_len_;
static bool ota_upgrade_subelement_;
static uint8_t ota_header_[6];

esp_err_t zb_ota_upgrade_status_handler(esp_zb_zcl_ota_upgrade_value_message_t message)
{
    static uint32_t total_size = 0;
    static uint32_t offset = 0;
    static int64_t start_time = 0;
    esp_err_t ret = ESP_OK;
    if (message.info.status == ESP_ZB_ZCL_STATUS_SUCCESS) {
        switch (message.upgrade_status) {
        case ESP_ZB_ZCL_OTA_UPGRADE_STATUS_START:
            ESP_LOGI("OTA", "-- OTA upgrade start");
			ota_upgrade_subelement_ = false;
			ota_data_len_ = 0;
            ota_header_len_ = 0;
            start_time = esp_timer_get_time();
            s_ota_partition = esp_ota_get_next_update_partition(NULL);
            assert(s_ota_partition);
            ret = esp_ota_begin(s_ota_partition, OTA_WITH_SEQUENTIAL_WRITES, &s_ota_handle);
            ESP_RETURN_ON_ERROR(ret, "OTA", "Failed to begin OTA partition, status: %s", esp_err_to_name(ret));
            break;
        case ESP_ZB_ZCL_OTA_UPGRADE_STATUS_RECEIVE:
        	size_t payload_size = message.payload_size;
            const uint8_t *payload = message.payload;

            total_size = message.ota_header.image_size;
            offset += payload_size;

            ESP_LOGI("OTA", "-- OTA Client receives data: progress [%ld/%ld]", offset, total_size);

            /* Read and process the first sub-element, ignoring everything else */
			while (ota_header_len_ < 6 && payload_size > 0) {
                ota_header_[ota_header_len_] = payload[0];
                ota_header_len_++;
				payload++;
				payload_size--;
			}

			if (!ota_upgrade_subelement_ && ota_header_len_ == 6) {
				if (ota_header_[0] == 0 && ota_header_[1] == 0) {
					ota_upgrade_subelement_ = true;
					ota_data_len_ =
						  (((int)ota_header_[5] & 0xFF) << 24)
						| (((int)ota_header_[4] & 0xFF) << 16)
						| (((int)ota_header_[3] & 0xFF) << 8 )
						|  ((int)ota_header_[2] & 0xFF);
					ESP_LOGI("OTA", "OTA sub-element size %zu", ota_data_len_);
				} else {
					ESP_LOGW("OTA", "OTA sub-element type %02x%02x not supported", ota_header_[0], ota_header_[1]);
					return ESP_FAIL;
				}
			}

            if (ota_data_len_) {
                payload_size = min(ota_data_len_, payload_size);
				ota_data_len_ -= payload_size;

                if (message.payload_size && message.payload) {
                    ret = esp_ota_write(s_ota_handle, payload, payload_size);
                    ESP_RETURN_ON_ERROR(ret, "OTA", "Failed to write OTA data to partition, status: %s", esp_err_to_name(ret));
                }
            }
            break;
        case ESP_ZB_ZCL_OTA_UPGRADE_STATUS_APPLY:
            ESP_LOGI("OTA", "-- OTA upgrade apply");
            break;
        case ESP_ZB_ZCL_OTA_UPGRADE_STATUS_CHECK:
            ret = offset == total_size ? ESP_OK : ESP_FAIL;
            ESP_LOGI("OTA", "-- OTA upgrade check status: %s", esp_err_to_name(ret));
            break;
        case ESP_ZB_ZCL_OTA_UPGRADE_STATUS_FINISH:
            ESP_LOGI("OTA", "-- OTA Finish");
            ESP_LOGI("OTA",
                     "-- OTA Information: version: 0x%lx, manufactor code: 0x%x, image type: 0x%x, total size: %ld bytes, cost time: %lld ms,",
                     message.ota_header.file_version, message.ota_header.manufacturer_code, message.ota_header.image_type,
                     message.ota_header.image_size, (esp_timer_get_time() - start_time) / 1000);
            ret = esp_ota_end(s_ota_handle);
            ESP_RETURN_ON_ERROR(ret, "OTA", "Failed to end OTA partition, status: %s", esp_err_to_name(ret));
            ret = esp_ota_set_boot_partition(s_ota_partition);
            ESP_RETURN_ON_ERROR(ret, "OTA", "Failed to set OTA boot partition, status: %s", esp_err_to_name(ret));
            ESP_LOGW("OTA", "Prepare to restart system");
            esp_restart();
            break;
        default:
            ESP_LOGI("OTA", "OTA status: %d", message.upgrade_status);
            break;
        }
    }
    return ret;
}