
// This file implements all BLE related functionality.

#include "dfu.h"

#include <stdint.h>
#include <stddef.h>
#include "ble.h"
#include "nrf_sdm.h"
#include "nrf_mbr.h"
#include "ble_gatts.h"

#define MSEC_TO_UNITS(TIME, RESOLUTION) (((TIME) * 1000) / (RESOLUTION))
#define UNIT_0_625_MS (625)
#define UNIT_10_MS    (10000)

#define DEVICE_NAME {'D', 'F', 'U'}

#define GATT_MTU_SIZE_DEFAULT (23)

// Use the highest speed possible (lowest connection interval allowed,
// 7.5ms), while trying to keep the connection alive by setting the
// connection timeout to the largest allowed (4 seconds).
#define BLE_MIN_CONN_INTERVAL        BLE_GAP_CP_MIN_CONN_INTVL_MIN
#define BLE_MAX_CONN_INTERVAL        BLE_GAP_CP_MAX_CONN_INTVL_MIN
#define BLE_SLAVE_LATENCY            0
#define BLE_CONN_SUP_TIMEOUT         MSEC_TO_UNITS(4000, UNIT_10_MS)

// Randomly generated UUID. This UUID is the base UUID, but also the
// service UUID.
// cb150001-2404-4e66-ab07-a5f1053f14ce
#define UUID_BASE {0xce, 0x14, 0x3f, 0x05, 0xf1, 0xa5, 0x07, 0xab, 0x66, 0x4e, 0x04, 0x24, 0x01, 0x00, 0x15, 0xcb}
#define UUID_DFU_SERVICE      0x0001
#define UUID_DFU_CHAR_COMMAND 0x0002
#define UUID_DFU_CHAR_BUFFER  0x0003

static uint16_t ble_command_conn_handle;

extern uint32_t _sdata;
static uint32_t app_ram_base = (uint32_t)&_sdata;

static uint8_t adv_handle;

static ble_uuid128_t uuid_base = {
    UUID_BASE,
};

static uint8_t device_name[] = DEVICE_NAME;
static struct {
    uint8_t flags_len;
    uint8_t flags_type;
    uint8_t flags_value;
    uint8_t name_len;
    uint8_t name_type;
    uint8_t name_value[sizeof(device_name)];
    uint8_t uuid_len;
    uint8_t uuid_type;
    uint8_t uuid_value[16];
} adv_data = {
    2,
    BLE_GAP_AD_TYPE_FLAGS,
    BLE_GAP_ADV_FLAGS_LE_ONLY_GENERAL_DISC_MODE,
    sizeof(device_name) + 1, // type + name
    BLE_GAP_AD_TYPE_COMPLETE_LOCAL_NAME,
    DEVICE_NAME,
    16 + 1, // uuid-128 is 16 bytes, plus a type
    BLE_GAP_AD_TYPE_128BIT_SERVICE_UUID_COMPLETE,
    UUID_BASE,
};

static ble_gap_adv_data_t m_adv_data = {
    .adv_data = {
        .p_data = (uint8_t*)&adv_data,
        .len = sizeof(adv_data),
    },
};

static ble_gap_conn_params_t gap_conn_params = {
    .min_conn_interval = BLE_MIN_CONN_INTERVAL,
    .max_conn_interval = BLE_MAX_CONN_INTERVAL,
    .slave_latency     = BLE_SLAVE_LATENCY,
    .conn_sup_timeout  = BLE_CONN_SUP_TIMEOUT,
};

static ble_gap_conn_sec_mode_t sec_mode = {
    // Values as set with:
    // BLE_GAP_CONN_SEC_MODE_SET_OPEN(&sec_mode);
    .sm = 1,
    .lv = 1,
};

static ble_gap_adv_params_t m_adv_params = {
    .properties = {
        .type             = BLE_GAP_ADV_TYPE_CONNECTABLE_SCANNABLE_UNDIRECTED,
        .anonymous        = 0,
        .include_tx_power = 0,
    },
    .p_peer_addr = NULL,
    .interval    = MSEC_TO_UNITS(100, UNIT_0_625_MS), // approx 100ms
    .duration    = 0,    // unlimited advertisement?
    .max_adv_evts = 0,   // no max advertisement events
    .channel_mask = {0}, // ?
    .filter_policy = BLE_GAP_ADV_FP_ANY,
    .primary_phy = BLE_GAP_PHY_AUTO,
    .secondary_phy = BLE_GAP_PHY_AUTO,
    .set_id = 0,
    .scan_req_notification = 0,
};

static ble_uuid_t uuid;

static ble_gatts_attr_md_t attr_md_writeonly = {
    .vloc    = BLE_GATTS_VLOC_STACK,
    .rd_auth = 0,
    .wr_auth = 0,
    .vlen    = 1,

    // Equivalent of:
    // BLE_GAP_CONN_SEC_MODE_SET_OPEN(&attr_md_writeonly.write_perm);
    .write_perm = {
        .sm = 1,
        .lv = 1,
    },
};

static ble_gatts_attr_t attr_char_write = {
    .p_uuid    = &uuid,
    .p_attr_md = (ble_gatts_attr_md_t*)&attr_md_writeonly,
    .init_len  = 0,
    .init_offs = 0,
    .p_value   = NULL,
    .max_len   = (GATT_MTU_SIZE_DEFAULT - 3),
};

static ble_gatts_char_md_t char_md_write_notify = {
    .char_props.broadcast      = 0,
    .char_props.read           = 0,
    .char_props.write_wo_resp  = 0,
    .char_props.write          = 1,
    .char_props.notify         = 1,
    .char_props.indicate       = 0,

    .p_char_user_desc  = NULL,
    .p_char_pf         = NULL,
    .p_user_desc_md    = NULL,
    .p_sccd_md         = NULL,
    .p_cccd_md         = NULL,
};

static ble_gatts_char_md_t char_md_write_wo_resp = {
    .char_props.broadcast      = 0,
    .char_props.read           = 0,
    .char_props.write_wo_resp  = 1,
    .char_props.write          = 0,
    .char_props.notify         = 0,
    .char_props.indicate       = 0,

    .p_char_user_desc  = NULL,
    .p_char_pf         = NULL,
    .p_user_desc_md    = NULL,
    .p_sccd_md         = NULL,
    .p_cccd_md         = NULL,
};

static ble_gatts_char_handles_t char_command_handles;
static ble_gatts_char_handles_t char_data_handles;

// Initialize the BLE stack.
void ble_init(void) {
    LOG("enable ble");

    // Enable BLE stack.

    uint32_t err_code = sd_ble_enable(&app_ram_base);
    if (err_code != 0) {
        LOG_NUM("cannot enable BLE:", err_code);
    }

    if (sd_ble_gap_device_name_set(&sec_mode,
                                   adv_data.name_value,
                                   sizeof(adv_data.name_value)) != 0) {
        LOG("cannot apply GAP parameters");
    }

    // set connection parameters
    if (sd_ble_gap_ppcp_set(&gap_conn_params) != 0) {
        LOG("cannot set PPCP parameters");
    }

    // start advertising
    if (sd_ble_gap_adv_set_configure(&adv_handle, &m_adv_data, &m_adv_params) != 0) {
        LOG("cannot configure advertisment");
    }
    if (sd_ble_gap_adv_start(adv_handle, BLE_CONN_CFG_TAG_DEFAULT) != 0) {
        LOG("cannot start advertisment");
    }

    uuid.uuid = UUID_DFU_SERVICE;
    if (sd_ble_uuid_vs_add(&uuid_base, &uuid.type) != 0) {
        LOG("cannot add UUID");
    }

    uint16_t service_handle;
    if (sd_ble_gatts_service_add(BLE_GATTS_SRVC_TYPE_PRIMARY,
                                 &uuid,
                                 &service_handle) != 0) {
        LOG("cannot add service");
    }

    // Add 'command' characteristic
    uuid.uuid = UUID_DFU_CHAR_COMMAND;
    if (sd_ble_gatts_characteristic_add(BLE_GATT_HANDLE_INVALID,
                                        &char_md_write_notify,
                                        &attr_char_write,
                                        &char_command_handles) != 0) {
        LOG("cannot add cmd char");
    }

    // Add 'data' characteristic
    uuid.uuid = UUID_DFU_CHAR_BUFFER;
    if (sd_ble_gatts_characteristic_add(BLE_GATT_HANDLE_INVALID,
                                        &char_md_write_wo_resp,
                                        &attr_char_write,
                                        &char_data_handles) != 0) {
        LOG("cannot add buf char");
    }
}



static uint8_t m_ble_evt_buf[sizeof(ble_evt_t) + (GATT_MTU_SIZE_DEFAULT)] __attribute__ ((aligned (4)));

static void ble_evt_handler(ble_evt_t * p_ble_evt);

static void handle_irq(void) {
    uint32_t evt_id;
    while (sd_evt_get(&evt_id) != NRF_ERROR_NOT_FOUND) {
        sd_evt_handler(evt_id);
    }

    while (1) {
        uint16_t evt_len = sizeof(m_ble_evt_buf);
        uint32_t err_code = sd_ble_evt_get(m_ble_evt_buf, &evt_len);
#if DEBUG
        if (err_code != NRF_SUCCESS) {
           if (err_code == NRF_ERROR_NOT_FOUND) {
               // expected
           } else if (err_code == NRF_ERROR_INVALID_ADDR) {
               LOG("ble event error: invalid addr");
           } else if (err_code == NRF_ERROR_DATA_SIZE) {
               LOG("ble event error: data size");
           } else {
               LOG("ble event error: other");
           }
        }
#endif
        if (err_code != NRF_SUCCESS) return; // may be "not found" or a serious issue
        ble_evt_handler((ble_evt_t *)m_ble_evt_buf);
    };
}

// Main loop for BLE. This function will not return.
void ble_run() {
    // Now wait for incoming events, using the 'thread model' (instead of
    // the IRQ model). This saves 20 bytes.
    while (1) {
        __WFE();
        sd_app_evt_wait();
        handle_irq();
    }
}

static void ble_evt_handler(ble_evt_t * p_ble_evt) {
    switch (p_ble_evt->header.evt_id) {
        // GAP events
        case BLE_GAP_EVT_CONNECTED: {
            LOG("ble: connected");
            uint16_t  conn_handle = p_ble_evt->evt.gatts_evt.conn_handle;
            if (sd_ble_gap_conn_param_update(conn_handle, &gap_conn_params) != 0) {
                LOG("! failed to update conn params");
            }
            break;
        }
        case BLE_GAP_EVT_DISCONNECTED: {
            LOG("ble: disconnected");
            handle_disconnect();
            if (sd_ble_gap_adv_start(adv_handle, BLE_CONN_CFG_TAG_DEFAULT) != 0) {
                LOG("Could not restart advertising after disconnect.");
            }
            break;
        }
#if NRF52XXX
        case BLE_GAP_EVT_ADV_REPORT:
            LOG("ble: adv report");
            break;
        case BLE_GAP_EVT_CONN_PARAM_UPDATE: {
            LOG_NUM("ble: conn param update", p_ble_evt->evt.gap_evt.params.conn_param_update.conn_params.min_conn_interval);
            break;
        }
        case BLE_GAP_EVT_CONN_PARAM_UPDATE_REQUEST:
            LOG("ble: conn param update request");
            sd_ble_gap_conn_param_update(p_ble_evt->evt.gap_evt.conn_handle, NULL);
            break;
#endif

        // GATTS events
        case BLE_GATTS_EVT_HVC: {
            LOG("ble: hvc");
            break;
        }
        case BLE_GATTS_EVT_WRITE: {
            uint16_t  conn_handle = p_ble_evt->evt.gatts_evt.conn_handle;
            uint16_t  attr_handle = p_ble_evt->evt.gatts_evt.params.write.handle;
            uint16_t  data_len    = p_ble_evt->evt.gatts_evt.params.write.len;
            uint8_t * data        = &p_ble_evt->evt.gatts_evt.params.write.data[0];

            if (attr_handle == char_command_handles.value_handle) {
                ble_command_conn_handle = conn_handle;
                handle_command(data_len, (ble_command_t*)data);
            } else if (attr_handle == char_data_handles.value_handle) {
                ble_command_conn_handle = conn_handle;
                handle_data(data_len, data);
            }
            break;
        }
        case BLE_GATTS_EVT_SYS_ATTR_MISSING:
            LOG("ble: sys attr missing");
            break;
#if NRF52XXX
        case BLE_GATTS_EVT_EXCHANGE_MTU_REQUEST:
            LOG("ble: exchange MTU request");
            sd_ble_gatts_exchange_mtu_reply(p_ble_evt->evt.gatts_evt.conn_handle, GATT_MTU_SIZE_DEFAULT);
            break;
#endif

        default: {
            LOG("ble: ???");
            break;
        }
    }
}


// ble_send_reply sends a notification to the connected client on the command
// characteristic. It is used for various status updates.
void ble_send_reply(uint8_t code) {
    uint8_t reply_ok[] = {code};
    uint16_t reply_ok_len = sizeof(reply_ok);
    const ble_gatts_hvx_params_t hvx_params = {
        .handle = char_command_handles.value_handle,
        .type = BLE_GATT_HVX_NOTIFICATION,
        .offset = 0,
        .p_len = &reply_ok_len,
        .p_data = reply_ok,
    };
    uint32_t err_val = sd_ble_gatts_hvx(ble_command_conn_handle, &hvx_params);
    if (err_val != 0) {
        LOG_NUM("  notify: failed to send notification", err_val);
    }
}

// ble_disconnect_reset will disconnect the currently connected client.
void ble_disconnect(void) {
    sd_ble_gap_disconnect(ble_command_conn_handle, BLE_HCI_REMOTE_USER_TERMINATED_CONNECTION);
}
