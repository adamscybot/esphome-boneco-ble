#include "boneco_ble.h"

#ifdef USE_ESP32

#include <algorithm>
#include <cctype>
#include <cstring>
#include <string>
#include <vector>

#include "esphome/core/log.h"

#include <esp_bt_defs.h>
#include <esp_gatt_defs.h>
#include <esp_gattc_api.h>
#include <mbedtls/aes.h>

namespace esphome {
namespace boneco_ble {

static const char *const TAG = "boneco_ble";
static const uint8_t DEVICE_SPEED_COUNT = 32;

static const char *const CHALLENGE_RESPONSE_SERVICE_UUID_STR = "fdce1236-1013-4120-b919-1dbb32a2d132";
static const char *const CHALLENGE_RESPONSE_CHAR_UUID_STR = "fdce2347-1013-4120-b919-1dbb32a2d132";
static const char *const STATE_SERVICE_UUID_STR = "fdce1234-1013-4120-b919-1dbb32a2d132";
static const char *const STATE_CHAR_UUID_STR = "fdce2345-1013-4120-b919-1dbb32a2d132";

static int hex_char_to_int_(char value) {
  if (value >= '0' && value <= '9')
    return value - '0';
  value = static_cast<char>(std::tolower(value));
  if (value >= 'a' && value <= 'f')
    return value - 'a' + 10;
  return -1;
}

static std::string bytes_to_hex_(const uint8_t *data, size_t length) {
  static const char kHexMap[] = "0123456789abcdef";
  std::string out;
  out.reserve(length * 2);
  for (size_t i = 0; i < length; i++) {
    out.push_back(kHexMap[(data[i] >> 4) & 0x0F]);
    out.push_back(kHexMap[data[i] & 0x0F]);
  }
  return out;
}

void BonecoBleFan::set_device_key(const std::string &device_key) { this->device_key_ = device_key; }

void BonecoBleFan::set_optimistic(bool optimistic) { this->optimistic_ = optimistic; }

void BonecoBleFan::setup() {
  this->device_key_ok_ = this->parse_device_key_();
  if (!this->parse_uuid_(CHALLENGE_RESPONSE_SERVICE_UUID_STR, &this->auth_service_uuid_) ||
      !this->parse_uuid_(CHALLENGE_RESPONSE_CHAR_UUID_STR, &this->auth_char_uuid_) ||
      !this->parse_uuid_(STATE_SERVICE_UUID_STR, &this->state_service_uuid_) ||
      !this->parse_uuid_(STATE_CHAR_UUID_STR, &this->state_char_uuid_)) {
    ESP_LOGE(TAG, "Failed to parse UUID constants");
  }
}

void BonecoBleFan::dump_config() {
  ESP_LOGCONFIG(TAG, "Boneco BLE Fan:");
  ESP_LOGCONFIG(TAG, "  Device key: %s", this->device_key_ok_ ? "set" : "invalid");
  ESP_LOGCONFIG(TAG, "  Optimistic: %s", this->optimistic_ ? "true" : "false");
}

fan::FanTraits BonecoBleFan::get_traits() {
  auto traits = fan::FanTraits();
  traits.set_speed(true);
  traits.set_supported_speed_count(DEVICE_SPEED_COUNT);
  traits.set_direction(false);
  traits.set_oscillation(false);
  return traits;
}

void BonecoBleFan::control(const fan::FanCall &call) {
  bool next_state = this->state;
  uint8_t next_speed = static_cast<uint8_t>(this->speed);
  if (call.get_state().has_value())
    next_state = *call.get_state();
  if (call.get_speed().has_value())
    next_speed = static_cast<uint8_t>(*call.get_speed());
  if (next_speed > DEVICE_SPEED_COUNT)
    next_speed = DEVICE_SPEED_COUNT;
  if (next_state && next_speed == 0)
    next_speed = 1;

  if (this->connected_ && this->state_char_handle_ != 0) {
    this->send_fan_state_(next_state, next_speed);
  } else {
    ESP_LOGW(TAG, "Not connected, skipping write");
  }

  if (this->optimistic_) {
    this->state = next_state;
    this->speed = next_speed;
    this->publish_state();
  }
}

void BonecoBleFan::gattc_event_handler(esp_gattc_cb_event_t event, esp_gatt_if_t gattc_if,
                                       esp_ble_gattc_cb_param_t *param) {
  switch (event) {
    case ESP_GATTC_OPEN_EVT: {
      if (param->open.status != ESP_GATT_OK) {
        ESP_LOGW(TAG, "Failed to open connection, status=%d", param->open.status);
        return;
      }
      this->gattc_if_ = gattc_if;
      this->conn_id_ = param->open.conn_id;
      this->connected_ = true;
      std::memcpy(this->remote_bda_, param->open.remote_bda, sizeof(this->remote_bda_));
      this->reset_handles_();
      esp_ble_gattc_search_service(this->gattc_if_, this->conn_id_, nullptr);
      break;
    }
    case ESP_GATTC_DISCONNECT_EVT: {
      this->connected_ = false;
      this->reset_handles_();
      break;
    }
    case ESP_GATTC_SEARCH_RES_EVT: {
      const auto &uuid = param->search_res.srvc_id.uuid;
      if (uuid_equals_(uuid, this->auth_service_uuid_)) {
        this->auth_service_start_handle_ = param->search_res.start_handle;
        this->auth_service_end_handle_ = param->search_res.end_handle;
      } else if (uuid_equals_(uuid, this->state_service_uuid_)) {
        this->state_service_start_handle_ = param->search_res.start_handle;
        this->state_service_end_handle_ = param->search_res.end_handle;
      }
      break;
    }
    case ESP_GATTC_SEARCH_CMPL_EVT: {
      if (this->auth_service_start_handle_ != 0) {
        if (this->discover_characteristic_(this->auth_service_start_handle_, this->auth_service_end_handle_,
                                           this->auth_char_uuid_, &this->auth_char_handle_)) {
          this->register_notifications_(this->auth_char_handle_);
        }
      }
      if (this->state_service_start_handle_ != 0) {
        if (this->discover_characteristic_(this->state_service_start_handle_, this->state_service_end_handle_,
                                           this->state_char_uuid_, &this->state_char_handle_)) {
          this->register_notifications_(this->state_char_handle_);
        }
      }
      break;
    }
    case ESP_GATTC_REG_FOR_NOTIFY_EVT: {
      if (param->reg_for_notify.status != ESP_GATT_OK) {
        ESP_LOGW(TAG, "Notify registration failed, status=%d", param->reg_for_notify.status);
        break;
      }
      this->enable_notifications_(param->reg_for_notify.handle);
      break;
    }
    case ESP_GATTC_NOTIFY_EVT: {
      if (param->notify.value == nullptr || param->notify.value_len == 0)
        break;
      if (param->notify.handle == this->auth_char_handle_) {
        this->handle_auth_notify_(param->notify.value, param->notify.value_len);
      } else if (param->notify.handle == this->state_char_handle_) {
        this->handle_state_notify_(param->notify.value, param->notify.value_len);
      }
      break;
    }
    default:
      break;
  }
}

void BonecoBleFan::send_fan_state_() {
  this->send_fan_state_(this->state, static_cast<uint8_t>(this->speed));
}

void BonecoBleFan::send_fan_state_(bool state, uint8_t speed) {
  uint8_t buffer[20];
  std::memset(buffer, 0, sizeof(buffer));

  auto device_speed = this->to_device_speed_(speed);

  // See `handle_state_notify_`, as this is the same as what the device reports in the
  // other direction.
  buffer[0] = device_speed & 0x7F;
  buffer[1] = (state ? 1 : 0) << 3;

  // These statics were being sent every time at least on my connection. Possibly they are for something else and
  // hardcoding these here overwrites some other setting (timer) to my value. Not looked into it yet,
  // just doing it because it works.
  buffer[18] = 0x04;
  buffer[19] = 0x64;

  this->write_value_(this->state_char_handle_, buffer, sizeof(buffer));
}

// A challenge-response mechanism is used and can be observed by noting seemingly random payloads going
// back and forth. 
// 
// We already obtained the static device key. Broadcasting identical spoof challenges to a known-good
// client shows identical responses for a given payload, which indicates a deterministic symmetric cipher.
// The key is stable and 128 bits in size, and the variable part of the response (ignoring the wrapper)
// is also 128 bits. This strongly suggests a 128-bit block cipher; in all probability AES-ECB, which is
// common on BLE devices with basic chipsets.
void BonecoBleFan::handle_auth_notify_(const uint8_t *data, uint16_t length) {
  if (length < 1)
    return;

  // Auth notifications carry either the challenge (0x01) or the acknowledgement (0x04).
  uint8_t first = data[0];
  if (first == 0x01) {
    if (!this->device_key_ok_) {
      ESP_LOGE(TAG, "Device key missing or invalid; cannot respond to challenge");
      return;
    }
    if (length < 18) {
      ESP_LOGW(TAG, "Challenge payload too short");
      return;
    }
    // All challenge packets begin with 0x01 and expect a 20-byte response packet.
    ESP_LOGI(TAG, "Challenge received: %s", bytes_to_hex_(data, length).c_str());

    uint8_t response[20];
    std::memset(response, 0, sizeof(response));
    // 0x03 0x01 appears to mark that this as a challenge response.
    response[0] = 0x03;
    response[1] = 0x01;

    uint8_t input[16];
    std::memcpy(input, data + 2, sizeof(input));

    // Naively encrypting the whole challenge as received from the device with
    // the device key did not yield the expected result compared against actual
    // captured and working challenge/response.
    //
    // There was obviously either some extra transformation applied along the
    // way (at first, I thought it was the key that was transformed) that I was
    // missing, or it's not even ECB. It turned out to be the former.
    //
    // This thing almost had me give up on my interop dream by making me think
    // my punt on AES/ECB was wrong all along. In the end a hopeful effort to
    // try all sorts of different pads and truncations (based on what I saw in
    // the wild for "app layer BLE auth") via a script delivered a eureeka!!
    //
    // The last byte within the challenge is always swapped with 0x01 before
    // being inputted into the block cipher.
    input[15] = 0x01;


    uint8_t output[16];
    mbedtls_aes_context aes_ctx;
    mbedtls_aes_init(&aes_ctx);
    // Challenge response uses AES-128 ECB with the device key.
    mbedtls_aes_setkey_enc(&aes_ctx, this->device_key_bytes_.data(), 128);
    mbedtls_aes_crypt_ecb(&aes_ctx, MBEDTLS_AES_ENCRYPT, input, output);
    mbedtls_aes_free(&aes_ctx);

    // Copy the cipher output into the response payload.
    std::memcpy(response + 2, output, sizeof(output));
    ESP_LOGI(TAG, "Sending challenge response: %s", bytes_to_hex_(response, sizeof(response)).c_str());
    this->write_value_(this->auth_char_handle_, response, sizeof(response));
  } 

  // 0x04 seems to mean an acknowledgement of our challenge response. You get it back wether your challenge
  // response was right or wrong.
  else if (first == 0x04) {
    // The second byte being 0x002 seems to mean "correct". If its not that I just assume is some kind of failure.
    // Probably there's meaning behind the other values but I have no idea.
    if (length >= 3 && data[2] == 0x02) {
      ESP_LOGI(TAG, "Auth successful: %s", bytes_to_hex_(data, length).c_str());
    } else {
      ESP_LOGE(TAG, "Auth failed: %s", bytes_to_hex_(data, length).c_str());
    }
  } else {
    ESP_LOGW(TAG, "Unknown auth response: %s", bytes_to_hex_(data, length).c_str());
  }
}

void BonecoBleFan::handle_state_notify_(const uint8_t *data, uint16_t length) {
  if (length < 2)
    return;

  // Speed is encoded in byte 0, bits [0..6]. Bit 7 seems to be something else as doesn't change
  // with speed (don't know, maybe a mask). The actual value is a number 1-32 which matches steps
  // on the device.
  uint8_t device_speed = data[0] & 0x7F;
  if (device_speed > DEVICE_SPEED_COUNT)
    device_speed = DEVICE_SPEED_COUNT;
  uint8_t fan_level = this->from_device_speed_(device_speed);

  // Power state correlates with byte 1, bit 3 (0-indexed).
  bool is_on = ((data[1] >> 3) & 0x01) == 1;

  // Note there's more in there which I haven't looked into yet.

  bool changed = (this->state != is_on) || (static_cast<uint8_t>(this->speed) != fan_level);
  this->state = is_on;
  this->speed = fan_level;
  if (changed) {
    this->publish_state();
  }
}

bool BonecoBleFan::parse_device_key_() {
  if (this->device_key_.size() != 32) {
    ESP_LOGE(TAG, "Device key must be 32 hex characters");
    return false;
  }
  for (size_t i = 0; i < 16; i++) {
    int hi = hex_char_to_int_(this->device_key_[2 * i]);
    int lo = hex_char_to_int_(this->device_key_[2 * i + 1]);
    if (hi < 0 || lo < 0) {
      ESP_LOGE(TAG, "Device key contains non-hex characters");
      return false;
    }
    this->device_key_bytes_[i] = static_cast<uint8_t>((hi << 4) | lo);
  }
  return true;
}

bool BonecoBleFan::parse_uuid_(const char *uuid_str, esp_bt_uuid_t *out) {
  if (uuid_str == nullptr || out == nullptr)
    return false;

  std::string hex;
  hex.reserve(32);
  for (size_t i = 0; uuid_str[i] != '\0'; i++) {
    char c = uuid_str[i];
    if (c == '-')
      continue;
    hex.push_back(c);
  }
  if (hex.size() != 32)
    return false;

  out->len = ESP_UUID_LEN_128;
  for (size_t i = 0; i < 16; i++) {
    int hi = hex_char_to_int_(hex[2 * i]);
    int lo = hex_char_to_int_(hex[2 * i + 1]);
    if (hi < 0 || lo < 0)
      return false;
    out->uuid.uuid128[15 - i] = static_cast<uint8_t>((hi << 4) | lo);
  }
  return true;
}

bool BonecoBleFan::uuid_equals_(const esp_bt_uuid_t &a, const esp_bt_uuid_t &b) {
  if (a.len != b.len)
    return false;
  switch (a.len) {
    case ESP_UUID_LEN_16:
      return a.uuid.uuid16 == b.uuid.uuid16;
    case ESP_UUID_LEN_32:
      return a.uuid.uuid32 == b.uuid.uuid32;
    case ESP_UUID_LEN_128:
      return std::memcmp(a.uuid.uuid128, b.uuid.uuid128, ESP_UUID_LEN_128) == 0;
    default:
      return false;
  }
}

uint8_t BonecoBleFan::to_device_speed_(uint8_t speed) const {
  if (speed == 0)
    return 0;
  if (speed > DEVICE_SPEED_COUNT)
    speed = DEVICE_SPEED_COUNT;
  return speed;
}

uint8_t BonecoBleFan::from_device_speed_(uint8_t speed) const {
  if (speed == 0)
    return 0;
  if (speed > DEVICE_SPEED_COUNT)
    speed = DEVICE_SPEED_COUNT;
  return speed;
}

void BonecoBleFan::reset_handles_() {
  this->auth_service_start_handle_ = 0;
  this->auth_service_end_handle_ = 0;
  this->state_service_start_handle_ = 0;
  this->state_service_end_handle_ = 0;
  this->auth_char_handle_ = 0;
  this->state_char_handle_ = 0;
}

bool BonecoBleFan::discover_characteristic_(uint16_t start_handle, uint16_t end_handle,
                                            const esp_bt_uuid_t &char_uuid, uint16_t *out_handle) {
  if (start_handle == 0 || end_handle == 0)
    return false;

  uint16_t count = 0;
  if (esp_ble_gattc_get_attr_count(this->gattc_if_, this->conn_id_, ESP_GATT_DB_CHARACTERISTIC, start_handle,
                                   end_handle, 0, &count) != ESP_GATT_OK ||
      count == 0) {
    ESP_LOGW(TAG, "No characteristics found in service range");
    return false;
  }

  std::vector<esp_gattc_char_elem_t> result(count);
  if (esp_ble_gattc_get_char_by_uuid(this->gattc_if_, this->conn_id_, start_handle, end_handle, char_uuid,
                                     result.data(), &count) != ESP_GATT_OK ||
      count == 0) {
    ESP_LOGW(TAG, "Characteristic not found");
    return false;
  }

  *out_handle = result[0].char_handle;
  return true;
}

void BonecoBleFan::register_notifications_(uint16_t char_handle) {
  if (!this->connected_ || char_handle == 0)
    return;
  esp_err_t err = esp_ble_gattc_register_for_notify(this->gattc_if_, this->remote_bda_, char_handle);
  if (err != ESP_OK) {
    ESP_LOGW(TAG, "Failed to register for notify, err=%d", err);
  }
}

void BonecoBleFan::enable_notifications_(uint16_t char_handle) {
  esp_bt_uuid_t cccd_uuid{};
  cccd_uuid.len = ESP_UUID_LEN_16;
  cccd_uuid.uuid.uuid16 = ESP_GATT_UUID_CHAR_CLIENT_CONFIG;

  uint16_t count = 1;
  esp_gattc_descr_elem_t descr_elem{};
  if (esp_ble_gattc_get_descr_by_char_handle(this->gattc_if_, this->conn_id_, char_handle, cccd_uuid, &descr_elem,
                                             &count) != ESP_GATT_OK ||
      count == 0) {
    ESP_LOGW(TAG, "CCCD descriptor not found for handle=%u", char_handle);
    return;
  }

  uint8_t notify_en[2] = {0x01, 0x00};
  esp_err_t err = esp_ble_gattc_write_char_descr(this->gattc_if_, this->conn_id_, descr_elem.handle,
                                                 sizeof(notify_en), notify_en, ESP_GATT_WRITE_TYPE_RSP,
                                                 ESP_GATT_AUTH_REQ_NONE);
  if (err != ESP_OK) {
    ESP_LOGW(TAG, "Failed to enable notifications, err=%d", err);
  }
}

void BonecoBleFan::write_value_(uint16_t char_handle, const uint8_t *data, size_t length) {
  if (!this->connected_ || char_handle == 0)
    return;

  esp_err_t err =
      esp_ble_gattc_write_char(this->gattc_if_, this->conn_id_, char_handle, length, const_cast<uint8_t *>(data),
                               ESP_GATT_WRITE_TYPE_RSP, ESP_GATT_AUTH_REQ_NONE);
  if (err != ESP_OK) {
    ESP_LOGW(TAG, "Write failed, err=%d", err);
  }
}

}  // namespace boneco_ble
}  // namespace esphome

#endif  // USE_ESP32
