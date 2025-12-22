#include "main.h"
#include <stdio.h>
#include <inttypes.h>
#include "sdkconfig.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_chip_info.h"
#include "esp_flash.h"
#include "esp_system.h"
#include "driver/gpio.h"
#include "iot_button.h"
#include "esp_log.h"
#include "esp_pm.h"
#include "button_gpio.h"
#include "led_indicator_gpio.h"
#include "esp_timer.h"
#include "ha/esp_zigbee_ha_standard.h"
#include "nvs_flash.h"
#include "esp_check.h"
#include "nvs.h"
#include "Ota.h"

#define LED_GPIO GPIO_NUM_10
#define HEAT_GPIO GPIO_NUM_11
#define BUTTON_GPIO GPIO_NUM_12
//#define BUTTON_GPIO GPIO_NUM_4

#define NVS_NAMESPACE "suxsem"
#define NVS_LEVEL_KEY "level"
#define NVS_LEVEL_CONFIG_KEY "level_config_%d"

void turn_on();
void turn_off();

bool heat_on = false;
uint8_t heat_level = 0;

led_indicator_handle_t led_handle;

#define HEAT_LEVELS_NUM 5

uint8_t heat_levels[] = {
    60,
    70,
    80,
    90,
    100,
};

/**
 * @brief Define blinking type and priority.
 *
 */
enum
{
    BLINK_TRIPLE = 0,
    BLINK_DOUBLE,
    BLINK_NORMAL,
    BLINK_ALWAYS_ON,
    BLINK_MAX,
};

/**
 * @brief Blinking twice times
 *
 */
static const blink_step_t double_blink[] = {
    {LED_BLINK_HOLD, LED_STATE_ON, 100},
    {LED_BLINK_HOLD, LED_STATE_OFF, 100},
    {LED_BLINK_HOLD, LED_STATE_ON, 100},
    {LED_BLINK_HOLD, LED_STATE_OFF, 100},
    {LED_BLINK_STOP, 0, 0},
};

/**
 * @brief Blinking three times
 *
 */
static const blink_step_t triple_blink[] = {
    {LED_BLINK_HOLD, LED_STATE_ON, 100},
    {LED_BLINK_HOLD, LED_STATE_OFF, 100},
    {LED_BLINK_HOLD, LED_STATE_ON, 100},
    {LED_BLINK_HOLD, LED_STATE_OFF, 100},
    {LED_BLINK_HOLD, LED_STATE_ON, 100},
    {LED_BLINK_HOLD, LED_STATE_OFF, 100},
    {LED_BLINK_STOP, 0, 0},
};

/**
 * @brief Normal blinking
 *
 */
static blink_step_t normal_blink[] = {
    {LED_BLINK_HOLD, LED_STATE_ON, 1000},
    {LED_BLINK_HOLD, LED_STATE_OFF, 1000},
    {LED_BLINK_LOOP, 0, 0},
};

blink_step_t const *led_mode[] = {
    [BLINK_TRIPLE] = triple_blink,
    [BLINK_DOUBLE] = double_blink,
    [BLINK_NORMAL] = normal_blink,
    [BLINK_MAX] = NULL,
};

void sendZigbeeOnOff(bool on)
{
    esp_zb_lock_acquire(portMAX_DELAY);
    esp_zb_zcl_set_attribute_val(ZB_ENDPOINT, ESP_ZB_ZCL_CLUSTER_ID_ON_OFF, ESP_ZB_ZCL_CLUSTER_SERVER_ROLE, ESP_ZB_ZCL_ATTR_ON_OFF_ON_OFF_ID, &on, false);
    esp_zb_lock_release();
}

void sendZigbeeLevel(uint8_t level)
{
    esp_zb_lock_acquire(portMAX_DELAY);
    esp_zb_zcl_set_attribute_val(ZB_ENDPOINT, MY_CLUSTER, ESP_ZB_ZCL_CLUSTER_SERVER_ROLE, MY_ATTR_POWER_ID, &level, false);
    esp_zb_lock_release();
}

void sendZigbeeLevelConfig(int index, uint8_t level_config)
{
    esp_zb_lock_acquire(portMAX_DELAY);
    esp_zb_zcl_set_attribute_val(ZB_ENDPOINT, MY_CLUSTER, ESP_ZB_ZCL_CLUSTER_SERVER_ROLE, MY_ATTR_LEVEL_CONFIG_ID + index, &level_config, false);
    esp_zb_lock_release();
}

nvs_handle_t my_nvs_handle;

void save_level()
{
    nvs_set_u8(my_nvs_handle, NVS_LEVEL_KEY, heat_level);
    nvs_commit(my_nvs_handle);
}

void save_level_config(int i)
{
    char key[16];
    snprintf(key, sizeof(key), NVS_LEVEL_CONFIG_KEY, i);
    nvs_set_u8(my_nvs_handle, key, heat_levels[i]);
}

static esp_timer_handle_t heat_timer;
static uint32_t counter_ms = 0; // contatore interno per duty cycle

#define HEAT_PERIOD_MS 10000 // 10 secondi

// callback del timer, chiamata ogni 100ms
static void heat_timer_cb(void *arg)
{
    uint32_t on_time = HEAT_PERIOD_MS * heat_levels[heat_level] / 100;

    if (!heat_on)
    {
        gpio_set_level(HEAT_GPIO, 1); // spento
        return;
    }

    if (counter_ms < on_time)
    {
        gpio_set_level(HEAT_GPIO, 0); // acceso
    }
    else
    {
        gpio_set_level(HEAT_GPIO, 1); // spento
    }

    counter_ms += 100; // incremento in ms
    if (counter_ms >= HEAT_PERIOD_MS)
        counter_ms = 0;
}

// inizializza GPIO e timer
void heat_pwm_init(void)
{
    gpio_reset_pin(HEAT_GPIO);
    gpio_set_direction(HEAT_GPIO, GPIO_MODE_OUTPUT);
    gpio_set_level(HEAT_GPIO, 1);

    const esp_timer_create_args_t timer_args = {
        .callback = &heat_timer_cb,
        .name = "heat_timer"};
    esp_timer_create(&timer_args, &heat_timer);
    esp_timer_start_periodic(heat_timer, 100 * 1000); // 100ms
}

static void short_press_cb(void *arg, void *data)
{
    ESP_LOGI("BUTTON", "Short press!");
    if (heat_on)
    {
        heat_level++;
        if (heat_level >= HEAT_LEVELS_NUM)
        {
            heat_level = 0;
        }
        save_level();
        sendZigbeeLevel(heat_level);
    }
    turn_on();
    sendZigbeeOnOff(true);
    // led_indicator_start(led_handle, BLINK_DOUBLE);
}

static void long_press_cb(void *arg, void *data)
{
    ESP_LOGI("BUTTON", "Long press!");
    turn_off();
    sendZigbeeOnOff(false);
    // led_indicator_start(led_handle, BLINK_TRIPLE);
}

static void reset_task(void *pvParameters)
{
    ESP_LOGI("Zigbee", "Waiting 3 seconds before factory reset...");
    vTaskDelay(pdMS_TO_TICKS(3000));
    ESP_LOGI("Zigbee", "Performing factory reset now");
    esp_zb_factory_reset();
    vTaskDelete(NULL);
}

static void very_long_press_cb(void *arg, void *data)
{
    ESP_LOGI("BUTTON", "Very long press!");
    led_indicator_start(led_handle, BLINK_TRIPLE);
    xTaskCreate(reset_task, "reset_task", 2048, NULL, 5, NULL);
}

void turn_on()
{
    heat_on = true;
    ESP_LOGI("HEATER", "Heater ON, level: %d", heat_level);

    uint32_t max_time = 500;
    uint32_t min_time = 100;
    int max_level = HEAT_LEVELS_NUM - 2;
    uint32_t hold_time = max_time - ((max_time - min_time) * heat_level) / max_level;
    normal_blink[0].hold_time_ms = hold_time;
    normal_blink[1].hold_time_ms = heat_level == HEAT_LEVELS_NUM - 1 ? 0 : hold_time;
    led_indicator_start(led_handle, BLINK_NORMAL);
}

void turn_off(void)
{
    heat_on = false;
    ESP_LOGI("HEATER", "Heater OFF");
    led_indicator_stop(led_handle, BLINK_NORMAL);
    gpio_set_level(LED_GPIO, 1);
}

esp_err_t esp_zcl_utility_add_ep_basic_manufacturer_info(esp_zb_ep_list_t *ep_list, uint8_t endpoint_id, zcl_basic_manufacturer_info_t *info)
{
    esp_err_t ret = ESP_OK;
    esp_zb_cluster_list_t *cluster_list = NULL;
    esp_zb_attribute_list_t *basic_cluster = NULL;

    cluster_list = esp_zb_ep_list_get_ep(ep_list, endpoint_id);
    ESP_RETURN_ON_FALSE(cluster_list, ESP_ERR_INVALID_ARG, "Zigbee", "Failed to find endpoint id: %d in list: %p", endpoint_id, ep_list);
    basic_cluster = esp_zb_cluster_list_get_cluster(cluster_list, ESP_ZB_ZCL_CLUSTER_ID_BASIC, ESP_ZB_ZCL_CLUSTER_SERVER_ROLE);
    ESP_RETURN_ON_FALSE(basic_cluster, ESP_ERR_INVALID_ARG, "Zigbee", "Failed to find basic cluster in endpoint: %d", endpoint_id);
    ESP_RETURN_ON_FALSE((info && info->manufacturer_name), ESP_ERR_INVALID_ARG, "Zigbee", "Invalid manufacturer name");
    ESP_ERROR_CHECK(esp_zb_basic_cluster_add_attr(basic_cluster, ESP_ZB_ZCL_ATTR_BASIC_MANUFACTURER_NAME_ID, info->manufacturer_name));
    ESP_RETURN_ON_FALSE((info && info->model_identifier), ESP_ERR_INVALID_ARG, "Zigbee", "Invalid model identifier");
    ESP_ERROR_CHECK(esp_zb_basic_cluster_add_attr(basic_cluster, ESP_ZB_ZCL_ATTR_BASIC_MODEL_IDENTIFIER_ID, info->model_identifier));
    return ret;
}

static void bdb_start_top_level_commissioning_cb(uint8_t mode_mask)
{
    ESP_RETURN_ON_FALSE(esp_zb_bdb_start_top_level_commissioning(mode_mask) == ESP_OK, , "Zigbee", "Failed to start Zigbee commissioning");
}

void esp_zb_app_signal_handler(esp_zb_app_signal_t *signal_struct)
{
    uint32_t *p_sg_p = signal_struct->p_app_signal;
    esp_err_t err_status = signal_struct->esp_err_status;
    esp_zb_app_signal_type_t sig_type = *p_sg_p;
    switch (sig_type)
    {
    case ESP_ZB_ZDO_SIGNAL_SKIP_STARTUP:
        ESP_LOGI("Zigbee", "Initialize Zigbee stack");
        esp_zb_bdb_start_top_level_commissioning(ESP_ZB_BDB_MODE_INITIALIZATION);
        break;
    case ESP_ZB_BDB_SIGNAL_DEVICE_FIRST_START:
    case ESP_ZB_BDB_SIGNAL_DEVICE_REBOOT:
        if (err_status == ESP_OK)
        {
            ESP_LOGI("Zigbee", "Device started up in%s factory-reset mode", esp_zb_bdb_is_factory_new() ? "" : " non");
            if (esp_zb_bdb_is_factory_new())
            {
                ESP_LOGI("Zigbee", "Start network steering");
                esp_zb_bdb_start_top_level_commissioning(ESP_ZB_BDB_MODE_NETWORK_STEERING);
            }
            else
            {
                ESP_LOGI("Zigbee", "Device rebooted");
                sendZigbeeLevel(heat_level);
                sendZigbeeOnOff(heat_on);
                for (int i = 0; i < HEAT_LEVELS_NUM; i++)
                {
                    sendZigbeeLevelConfig(i, heat_levels[i]);
                }
            }
        }
        else
        {
            ESP_LOGW("Zigbee", "%s failed with status: %s, retrying", esp_zb_zdo_signal_to_string(sig_type),
                     esp_err_to_name(err_status));
            esp_zb_scheduler_alarm((esp_zb_callback_t)bdb_start_top_level_commissioning_cb,
                                   ESP_ZB_BDB_MODE_INITIALIZATION, 1000);
        }
        break;
    case ESP_ZB_BDB_SIGNAL_STEERING:
        if (err_status == ESP_OK)
        {
            esp_zb_ieee_addr_t extended_pan_id;
            esp_zb_get_extended_pan_id(extended_pan_id);
            ESP_LOGI("Zigbee", "Joined network successfully (Extended PAN ID: %02x:%02x:%02x:%02x:%02x:%02x:%02x:%02x, PAN ID: 0x%04hx, Channel:%d, Short Address: 0x%04hx)",
                     extended_pan_id[7], extended_pan_id[6], extended_pan_id[5], extended_pan_id[4],
                     extended_pan_id[3], extended_pan_id[2], extended_pan_id[1], extended_pan_id[0],
                     esp_zb_get_pan_id(), esp_zb_get_current_channel(), esp_zb_get_short_address());
            sendZigbeeLevel(heat_level);
            sendZigbeeOnOff(heat_on);
            for (int i = 0; i < HEAT_LEVELS_NUM; i++)
            {
                sendZigbeeLevelConfig(i, heat_levels[i]);
            }
        }
        else
        {
            ESP_LOGI("Zigbee", "Network steering was not successful (status: %s)", esp_err_to_name(err_status));
            esp_zb_scheduler_alarm((esp_zb_callback_t)bdb_start_top_level_commissioning_cb, ESP_ZB_BDB_MODE_NETWORK_STEERING, 1000);
        }
        break;
    default:
        ESP_LOGI("Zigbee", "ZDO signal: %s (0x%x), status: %s", esp_zb_zdo_signal_to_string(sig_type), sig_type,
                 esp_err_to_name(err_status));
        break;
    }
}

static esp_err_t zb_attribute_handler(const esp_zb_zcl_set_attr_value_message_t *message)
{
    esp_err_t ret = ESP_OK;

    ESP_RETURN_ON_FALSE(message, ESP_FAIL, "Zigbee", "Empty message");
    ESP_RETURN_ON_FALSE(message->info.status == ESP_ZB_ZCL_STATUS_SUCCESS, ESP_ERR_INVALID_ARG, "Zigbee", "Received message: error status(%d)",
                        message->info.status);
    ESP_LOGI("Zigbee", "Received message: endpoint(%d), cluster(0x%x), attribute(0x%x), data size(%d)", message->info.dst_endpoint, message->info.cluster,
             message->attribute.id, message->attribute.data.size);
    if (message->info.dst_endpoint == ZB_ENDPOINT)
    {
        if (message->info.cluster == ESP_ZB_ZCL_CLUSTER_ID_ON_OFF && message->attribute.id == ESP_ZB_ZCL_ATTR_ON_OFF_ON_OFF_ID && message->attribute.data.type == ESP_ZB_ZCL_ATTR_TYPE_BOOL)
        {
            bool onOff = message->attribute.data.value ? *(bool *)message->attribute.data.value : 0;
            ESP_LOGI("Zigbee", "OnOff sets to %s", onOff ? "On" : "Off");
            if (onOff)
            {
                turn_on();
            }
            else
            {
                turn_off();
            }
        }
        else if (message->info.cluster == MY_CLUSTER && message->attribute.id == MY_ATTR_POWER_ID)
        {
            uint8_t level = message->attribute.data.value ? *(uint8_t *)message->attribute.data.value : 0;
            ESP_LOGI("Zigbee", "Level sets to %d", level);
            if (level < HEAT_LEVELS_NUM)
            {
                heat_level = level;
                save_level();
                if (heat_on)
                {
                    turn_on();
                }
            }
            else
            {
                ESP_LOGW("Zigbee", "Level %d out of range", level);
            }
        }
        else if (message->info.cluster == MY_CLUSTER &&
                 message->attribute.id >= MY_ATTR_LEVEL_CONFIG_ID &&
                 message->attribute.id < MY_ATTR_LEVEL_CONFIG_ID + HEAT_LEVELS_NUM)
        {
            uint8_t level_config = message->attribute.data.value ? *(uint8_t *)message->attribute.data.value : 0;
            int index = message->attribute.id - MY_ATTR_LEVEL_CONFIG_ID;
            ESP_LOGI("Zigbee", "Level config %d sets to %d", index, level_config);
            heat_levels[index] = level_config;
            save_level_config(index);
        }
        else
        {
            ESP_LOGW("Zigbee", "Unhandled attribute set: cluster(0x%x), attribute(0x%x)", message->info.cluster, message->attribute.id);
        }
    }
    return ret;
}

static esp_err_t zb_action_handler(esp_zb_core_action_callback_id_t callback_id, const void *message)
{
    esp_err_t ret = ESP_OK;
    switch (callback_id)
    {
    case ESP_ZB_CORE_SET_ATTR_VALUE_CB_ID:
        ret = zb_attribute_handler((esp_zb_zcl_set_attr_value_message_t *)message);
        break;
    case ESP_ZB_CORE_CMD_DEFAULT_RESP_CB_ID:
        break;
    case ESP_ZB_CORE_OTA_UPGRADE_VALUE_CB_ID:
        ret = zb_ota_upgrade_status_handler(*(esp_zb_zcl_ota_upgrade_value_message_t *)message);
        break;
    default:
        ESP_LOGW("Zigbee", "Receive Zigbee action(0x%x) callback", callback_id);
        break;
    }
    return ret;
}

static esp_zb_attribute_list_t *my_attr_list;

static void esp_zb_task(void *pvParameters)
{
    /* initialize Zigbee stack */
    esp_zb_cfg_t zb_nwk_cfg = ESP_ZB_ROUTER_CONFIG();
    esp_zb_init(&zb_nwk_cfg);

    /* basic cluster create with fully customized */
    esp_zb_attribute_list_t *basic_attr_list = esp_zb_zcl_attr_list_create(ESP_ZB_ZCL_CLUSTER_ID_BASIC);
    uint8_t zcl_version_id = 8; // default
    esp_zb_basic_cluster_add_attr(basic_attr_list, ESP_ZB_ZCL_ATTR_BASIC_ZCL_VERSION_ID, &zcl_version_id);
    uint8_t power_source = 1; // mains (single phase)
    esp_zb_basic_cluster_add_attr(basic_attr_list, ESP_ZB_ZCL_ATTR_BASIC_POWER_SOURCE_ID, &power_source);
    esp_zb_basic_cluster_add_attr(basic_attr_list, ESP_ZB_ZCL_ATTR_BASIC_MANUFACTURER_NAME_ID, ESP_MANUFACTURER_NAME);
    esp_zb_basic_cluster_add_attr(basic_attr_list, ESP_ZB_ZCL_ATTR_BASIC_MODEL_IDENTIFIER_ID, ESP_MODEL_IDENTIFIER);

    /* identify cluster create with fully customized */
    esp_zb_attribute_list_t *identify_attr_list = esp_zb_zcl_attr_list_create(ESP_ZB_ZCL_CLUSTER_ID_IDENTIFY);
    uint16_t identify_time_secs = 30;
    esp_zb_identify_cluster_add_attr(identify_attr_list, ESP_ZB_ZCL_ATTR_IDENTIFY_IDENTIFY_TIME_ID, &identify_time_secs);

    /* onoff */
    esp_zb_attribute_list_t *onoff_attr_list = esp_zb_zcl_attr_list_create(ESP_ZB_ZCL_CLUSTER_ID_ON_OFF);
    bool onoff_state = false;
    esp_zb_on_off_cluster_add_attr(onoff_attr_list, ESP_ZB_ZCL_ATTR_ON_OFF_ON_OFF_ID, &onoff_state);

    /* custom cluster */
    my_attr_list = esp_zb_zcl_attr_list_create(MY_CLUSTER);
    uint8_t current_level = 0;
    esp_zb_custom_cluster_add_custom_attr(my_attr_list, MY_ATTR_POWER_ID, ESP_ZB_ZCL_ATTR_TYPE_U8, ESP_ZB_ZCL_ATTR_ACCESS_READ_WRITE | ESP_ZB_ZCL_ATTR_ACCESS_REPORTING, &current_level);
    for (int i = 0; i < HEAT_LEVELS_NUM; i++)
    {
        esp_zb_custom_cluster_add_custom_attr(my_attr_list, MY_ATTR_LEVEL_CONFIG_ID + i, ESP_ZB_ZCL_ATTR_TYPE_U8, ESP_ZB_ZCL_ATTR_ACCESS_READ_WRITE | ESP_ZB_ZCL_ATTR_ACCESS_REPORTING, &heat_levels[i]);
    }

    /* ota cluster */
    esp_zb_ota_cluster_cfg_t ota_cluster_cfg = {
        .ota_upgrade_file_version = OTA_UPGRADE_RUNNING_FILE_VERSION,
        .ota_upgrade_downloaded_file_ver = OTA_UPGRADE_DOWNLOADED_FILE_VERSION,
        .ota_upgrade_manufacturer = OTA_UPGRADE_MANUFACTURER,
        .ota_upgrade_image_type = OTA_UPGRADE_IMAGE_TYPE,
    };
    esp_zb_attribute_list_t *ota_client_attr_list = esp_zb_ota_cluster_create(&ota_cluster_cfg);
    /** add client parameters to ota client cluster */
    esp_zb_zcl_ota_upgrade_client_variable_t variable_config = {
        .timer_query = ESP_ZB_ZCL_OTA_UPGRADE_QUERY_TIMER_COUNT_DEF,
        .hw_version = OTA_UPGRADE_HW_VERSION,
        .max_data_size = OTA_UPGRADE_MAX_DATA_SIZE,
    };
    esp_zb_ota_cluster_add_attr(ota_client_attr_list, ESP_ZB_ZCL_ATTR_OTA_UPGRADE_CLIENT_DATA_ID, (void *)&variable_config);

    /* create cluster lists for this endpoint */
    esp_zb_cluster_list_t *esp_zb_cluster_list = esp_zb_zcl_cluster_list_create();
    esp_zb_cluster_list_add_basic_cluster(esp_zb_cluster_list, basic_attr_list, ESP_ZB_ZCL_CLUSTER_SERVER_ROLE);
    esp_zb_cluster_list_add_identify_cluster(esp_zb_cluster_list, identify_attr_list, ESP_ZB_ZCL_CLUSTER_SERVER_ROLE);
    esp_zb_cluster_list_add_on_off_cluster(esp_zb_cluster_list, onoff_attr_list, ESP_ZB_ZCL_CLUSTER_SERVER_ROLE);
    esp_zb_cluster_list_add_custom_cluster(esp_zb_cluster_list, my_attr_list, ESP_ZB_ZCL_CLUSTER_SERVER_ROLE);
    esp_zb_cluster_list_add_ota_cluster(esp_zb_cluster_list, ota_client_attr_list, ESP_ZB_ZCL_CLUSTER_CLIENT_ROLE);

    esp_zb_ep_list_t *esp_zb_ep_list = esp_zb_ep_list_create();
    /* add created endpoint (cluster_list) to endpoint list */
    esp_zb_endpoint_config_t ep_config = {
        .app_profile_id = ESP_ZB_AF_HA_PROFILE_ID,
        .app_device_id = ESP_ZB_HA_LEVEL_CONTROLLABLE_OUTPUT_DEVICE_ID,
        .app_device_version = 0,
        .endpoint = ZB_ENDPOINT};
    esp_zb_ep_list_add_ep(esp_zb_ep_list, esp_zb_cluster_list, ep_config);

    esp_zb_device_register(esp_zb_ep_list);

    esp_zb_core_action_handler_register(zb_action_handler);
    esp_zb_set_primary_network_channel_set(ESP_ZB_PRIMARY_CHANNEL_MASK);
    ESP_ERROR_CHECK(esp_zb_start(false));

    esp_zb_stack_main_loop();
}

void app_main(void)
{

    esp_log_level_set("*", ESP_LOG_INFO);

    // pm
    /*
    esp_pm_config_t pm_config = {
        .max_freq_mhz = CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ,
        .min_freq_mhz = CONFIG_XTAL_FREQ,
    };
    ESP_ERROR_CHECK(esp_pm_configure(&pm_config));
    */

    // nvs

    ESP_ERROR_CHECK(nvs_flash_init());
    ESP_ERROR_CHECK(nvs_open(NVS_NAMESPACE, NVS_READWRITE, &my_nvs_handle));
    nvs_get_u8(my_nvs_handle, NVS_LEVEL_KEY, &heat_level);
    for (int i = 0; i <= HEAT_LEVELS_NUM; i++)
    {
        char key[16];
        snprintf(key, sizeof(key), NVS_LEVEL_CONFIG_KEY, i);
        nvs_get_u8(my_nvs_handle, key, &heat_levels[i]);
    }

    // button

    button_config_t btn_cfg = {0};
    button_gpio_config_t gpio_cfg = {
        .gpio_num = BUTTON_GPIO,
        .active_level = 1,
        .enable_power_save = true,
    };

    button_handle_t btn;
    esp_err_t ret = iot_button_new_gpio_device(&btn_cfg, &gpio_cfg, &btn);
    assert(ret == ESP_OK);

    ret = iot_button_register_cb(btn, BUTTON_SINGLE_CLICK, NULL, short_press_cb, NULL);

    button_event_args_t args = {
        .long_press.press_time = 600,
    };
    ret |= iot_button_register_cb(btn, BUTTON_LONG_PRESS_START, &args, long_press_cb, NULL);

    args.long_press.press_time = 10000;
    ret |= iot_button_register_cb(btn, BUTTON_LONG_PRESS_START, &args, very_long_press_cb, NULL);

    ESP_ERROR_CHECK(ret);

    // led

    led_indicator_gpio_config_t gpio_config = {
        .gpio_num = LED_GPIO,
        .is_active_level_high = false,
    };

    const led_indicator_config_t config = {
        .blink_lists = led_mode,
        .blink_list_num = BLINK_MAX,
    };

    ret = led_indicator_new_gpio_device(&config, &gpio_config, &led_handle);
    ESP_ERROR_CHECK(ret);
    assert(led_handle != NULL);

    // heat PWM

    heat_pwm_init();

    // Zigbee

    esp_zb_platform_config_t zb_config = {
        .radio_config = ESP_ZB_DEFAULT_RADIO_CONFIG(),
        .host_config = ESP_ZB_DEFAULT_HOST_CONFIG(),
    };

    ESP_ERROR_CHECK(esp_zb_platform_config(&zb_config));
    xTaskCreate(esp_zb_task, "Zigbee_main", 4096, NULL, 5, NULL);
}