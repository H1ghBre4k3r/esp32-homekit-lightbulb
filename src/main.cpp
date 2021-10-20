#include <Arduino.h>
#include <ESP32HomeKit.h>
#include <WiFi.h>

#include "lightbulb.h"

const char* ssid = "YOUR_NETWORK_SSID";
const char* password = "YOUR_NETWORK_PASSWORD";

/* Mandatory identify routine for the accessory.
 * In a real accessory, something like LED blink should be implemented
 * got visual identification
 */
static int identify(hap_acc_t* ha) {
    ESP_LOGI(TAG, "Accessory identified");
    return HAP_SUCCESS;
}

/* A dummy callback for handling a read on the "Direction" characteristic of Fan.
 * In an actual accessory, this should read from hardware.
 * Read routines are generally not required as the value is available with th HAP core
 * when it is updated from write routines. For external triggers (like fan switched on/off
 * using physical button), accessories should explicitly call hap_char_update_val()
 * instead of waiting for a read request.
 */
static int fan_read(hap_char_t* hc, hap_status_t* status_code, void* serv_priv, void* read_priv) {
    if (hap_req_get_ctrl_id(read_priv)) {
        ESP_LOGI(TAG, "Received read from %s", hap_req_get_ctrl_id(read_priv));
    }
    if (!strcmp(hap_char_get_type_uuid(hc), HAP_CHAR_UUID_ROTATION_DIRECTION)) {
        /* Read the current value, toggle it and set the new value.
         * A separate variable should be used for the new value, as the hap_char_get_val()
         * API returns a const pointer
         */
        const hap_val_t* cur_val = hap_char_get_val(hc);

        hap_val_t new_val;
        if (cur_val->i == 1) {
            new_val.i = 0;
        } else {
            new_val.i = 1;
        }
        hap_char_update_val(hc, &new_val);
        *status_code = HAP_STATUS_SUCCESS;
    }
    return HAP_SUCCESS;
}

/* A dummy callback for handling a write on the "On" characteristic of Fan.
 * In an actual accessory, this should control the hardware
 */
static int fan_write(hap_write_data_t write_data[], int count, void* serv_priv, void* write_priv) {
    if (hap_req_get_ctrl_id(write_priv)) {
        ESP_LOGI(TAG, "Received write from %s", hap_req_get_ctrl_id(write_priv));
    }

    ESP_LOGI(TAG, "Fan Write called with %d chars", count);
    int i, ret = HAP_SUCCESS;
    hap_write_data_t* write;
    for (i = 0; i < count; i++) {
        write = &write_data[i];
        if (!strcmp(hap_char_get_type_uuid(write->hc), HAP_CHAR_UUID_ON)) {
            ESP_LOGI(TAG, "Received Write. Fan %s", write->val.b ? "On" : "Off");

            /* TODO: Control Actual Hardware */
            hap_char_update_val(write->hc, &(write->val));
            *(write->status) = HAP_STATUS_SUCCESS;
        } else if (!strcmp(hap_char_get_type_uuid(write->hc), HAP_CHAR_UUID_ROTATION_DIRECTION)) {
            if (write->val.i > 1) {
                *(write->status) = HAP_STATUS_VAL_INVALID;
                ret = HAP_FAIL;
            } else {
                ESP_LOGI(TAG, "Received Write. Fan %s", write->val.i ? "AntiClockwise" : "Clockwise");
                hap_char_update_val(write->hc, &(write->val));
                *(write->status) = HAP_STATUS_SUCCESS;
            }
        } else {
            *(write->status) = HAP_STATUS_RES_ABSENT;
        }
    }
    return ret;
}

static int lightbulb_write(hap_write_data_t write_data[], int count, void* serv_priv, void* write_priv) {
    int i, ret = HAP_SUCCESS;
    hap_write_data_t* write;
    for (i = 0; i < count; i++) {
        write = &write_data[i];
        /* Setting a default error value */
        *(write->status) = HAP_STATUS_VAL_INVALID;
        if (!strcmp(hap_char_get_type_uuid(write->hc), HAP_CHAR_UUID_ON)) {
            ESP_LOGI(TAG, "Received Write for Light %s", write->val.b ? "On" : "Off");
            if (lightbulb_set_on(write->val.b) == 0) {
                *(write->status) = HAP_STATUS_SUCCESS;
            }
        } else if (!strcmp(hap_char_get_type_uuid(write->hc), HAP_CHAR_UUID_BRIGHTNESS)) {
            ESP_LOGI(TAG, "Received Write for Light Brightness %d", write->val.i);
            if (lightbulb_set_brightness(write->val.i) == 0) {
                *(write->status) = HAP_STATUS_SUCCESS;
            }
        } else if (!strcmp(hap_char_get_type_uuid(write->hc), HAP_CHAR_UUID_HUE)) {
            ESP_LOGI(TAG, "Received Write for Light Hue %f", write->val.f);
            if (lightbulb_set_hue(write->val.f) == 0) {
                *(write->status) = HAP_STATUS_SUCCESS;
            }
        } else if (!strcmp(hap_char_get_type_uuid(write->hc), HAP_CHAR_UUID_SATURATION)) {
            ESP_LOGI(TAG, "Received Write for Light Saturation %f", write->val.f);
            if (lightbulb_set_saturation(write->val.f) == 0) {
                *(write->status) = HAP_STATUS_SUCCESS;
            }
        } else {
            *(write->status) = HAP_STATUS_RES_ABSENT;
        }
        /* If the characteristic write was successful, update it in hap core
         */
        if (*(write->status) == HAP_STATUS_SUCCESS) {
            hap_char_update_val(write->hc, &(write->val));
        } else {
            /* Else, set the return value appropriately to report error */
            ret = HAP_FAIL;
        }
    }
    return ret;
}

void setup() {
    // begin serial output
    Serial.begin(115200);

    // connect to wifi
    WiFi.begin(ssid, password);
    while (WiFi.status() != WL_CONNECTED) {
        delay(1000);
        Serial.println("Establishing connection to WiFi..");
    }
    Serial.println("Connected to network.");

    hap_acc_t* accessory;
    hap_serv_t* service;

    /* Initialize the HAP core */
    hap_init(HAP_TRANSPORT_WIFI);

    /* Initialise the mandatory parameters for Accessory which will be added as
     * the mandatory services internally
     */
    hap_acc_cfg_t cfg = {
        .name = "PescaLight",
        .model = "PescaLight",
        .manufacturer = "PescaDev",
        .serial_num = "1337",
        .fw_rev = "0.9.0",
        .hw_rev = "1.0",
        .pv = "1.1.0",
        .cid = HAP_CID_LIGHTING,
        .identify_routine = identify,
    };

    /* Create accessory object */
    accessory = hap_acc_create(&cfg);
    if (!accessory) {
        ESP_LOGE(TAG, "Failed to create accessory");
        // goto light_err;
    }

    /* Add a dummy Product Data */
    uint8_t product_data[] = {'E', 'S', 'P', '3', '2', 'H', 'A', 'P'};
    hap_acc_add_product_data(accessory, product_data, sizeof(product_data));

    /* Create the Light Bulb Service. Include the "name" since this is a user visible service  */
    service = hap_serv_lightbulb_create(true);
    if (!service) {
        ESP_LOGE(TAG, "Failed to create LightBulb Service");
        // TODO proper error handling
        // goto light_err;
    }

    /* Add the optional characteristic to the Light Bulb Service */
    int ret = hap_serv_add_char(service, hap_char_name_create("My Light"));
    ret |= hap_serv_add_char(service, hap_char_brightness_create(50));
    ret |= hap_serv_add_char(service, hap_char_hue_create(180));
    ret |= hap_serv_add_char(service, hap_char_saturation_create(100));

    if (ret != HAP_SUCCESS) {
        ESP_LOGE(TAG, "Failed to add optional characteristics to LightBulb");
        goto light_err;
    }
    /* Set the write callback for the service */
    hap_serv_set_write_cb(service, lightbulb_write);

    /* Add the Light Bulb Service to the Accessory Object */
    hap_acc_add_serv(accessory, service);

#ifdef CONFIG_FIRMWARE_SERVICE
    /*  Required for server verification during OTA, PEM format as string  */
    static char server_cert[] = {};
    hap_fw_upgrade_config_t ota_config = {
        .server_cert_pem = server_cert,
    };
    /* Create and add the Firmware Upgrade Service, if enabled.
     * Please refer the FW Upgrade documentation under components/homekit/extras/include/hap_fw_upgrade.h
     * and the top level README for more information.
     */
    service = hap_serv_fw_upgrade_create(&ota_config);
    if (!service) {
        ESP_LOGE(TAG, "Failed to create Firmware Upgrade Service");
        goto light_err;
    }
    hap_acc_add_serv(accessory, service);
#endif

    /* Add the Accessory to the HomeKit Database */
    hap_add_accessory(accessory);

    /* Initialize the Light Bulb Hardware */
    lightbulb_init();

    /* Register a common button for reset Wi-Fi network and reset to factory.
     */
    // reset_key_init(RESET_GPIO);

    /* TODO: Do the actual hardware initialization here */

    /* For production accessories, the setup code shouldn't be programmed on to
     * the device. Instead, the setup info, derived from the setup code must
     * be used. Use the factory_nvs_gen utility to generate this data and then
     * flash it into the factory NVS partition.
     *
     * By default, the setup ID and setup info will be read from the factory_nvs
     * Flash partition and so, is not required to set here explicitly.
     *
     * However, for testing purpose, this can be overridden by using hap_set_setup_code()
     * and hap_set_setup_id() APIs, as has been done here.
     */
#ifdef CONFIG_EXAMPLE_USE_HARDCODED_SETUP_CODE
    /* Unique Setup code of the format xxx-xx-xxx. Default: 111-22-333 */
    hap_set_setup_code(CONFIG_EXAMPLE_SETUP_CODE);
    /* Unique four character Setup Id. Default: ES32 */
    hap_set_setup_id(CONFIG_EXAMPLE_SETUP_ID);
#ifdef CONFIG_APP_WIFI_USE_WAC_PROVISIONING
    app_hap_setup_payload(CONFIG_EXAMPLE_SETUP_CODE, CONFIG_EXAMPLE_SETUP_ID, true, cfg.cid);
#else
    app_hap_setup_payload(CONFIG_EXAMPLE_SETUP_CODE, CONFIG_EXAMPLE_SETUP_ID, false, cfg.cid);
#endif
#endif

    /* Enable Hardware MFi authentication (applicable only for MFi variant of SDK) */
    // hap_enable_mfi_auth(HAP_MFI_AUTH_HW);
    hap_set_setup_code("111-22-333");
    /* Unique four character Setup Id. Default: ES32 */
    hap_set_setup_id("ES32");

    /* After all the initializations are done, start the HAP core */
    hap_start();

light_err:
    hap_acc_delete(accessory);
}

void loop() {
    /* Main loop code */
}
