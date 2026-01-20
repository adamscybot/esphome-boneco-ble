#pragma once

#ifdef USE_ESP32

#include <array>
#include <cstdint>
#include <string>

#include "esphome/components/ble_client/ble_client.h"
#include "esphome/components/fan/fan.h"
#include "esphome/core/component.h"

namespace esphome {
namespace boneco_ble {

class BonecoBleFan : public Component, public fan::Fan, public ble_client::BLEClientNode {
 public:
  void set_device_key(const std::string &device_key);
  void set_optimistic(bool optimistic);

  void setup() override;
  void dump_config() override;
  fan::FanTraits get_traits() override;
  void control(const fan::FanCall &call) override;

  void gattc_event_handler(esp_gattc_cb_event_t event, esp_gatt_if_t gattc_if,
                           esp_ble_gattc_cb_param_t *param) override;

 protected:
  void send_fan_state_();
  void send_fan_state_(bool state, uint8_t speed);
  void handle_auth_notify_(const uint8_t *data, uint16_t length);
  void handle_state_notify_(const uint8_t *data, uint16_t length);
  uint8_t to_device_speed_(uint8_t speed) const;
  uint8_t from_device_speed_(uint8_t speed) const;

  bool parse_device_key_();
  bool parse_uuid_(const char *uuid_str, esp_bt_uuid_t *out);
  static bool uuid_equals_(const esp_bt_uuid_t &a, const esp_bt_uuid_t &b);

  void reset_handles_();
  bool discover_characteristic_(uint16_t start_handle, uint16_t end_handle, const esp_bt_uuid_t &char_uuid,
                                uint16_t *out_handle);
  void register_notifications_(uint16_t char_handle);
  void enable_notifications_(uint16_t char_handle);
  void write_value_(uint16_t char_handle, const uint8_t *data, size_t length);

  std::string device_key_;
  std::array<uint8_t, 16> device_key_bytes_{};
  bool device_key_ok_{false};
  bool optimistic_{false};

  esp_gatt_if_t gattc_if_{ESP_GATT_IF_NONE};
  uint16_t conn_id_{0};
  bool connected_{false};
  esp_bd_addr_t remote_bda_{};

  esp_bt_uuid_t auth_service_uuid_{};
  esp_bt_uuid_t auth_char_uuid_{};
  esp_bt_uuid_t state_service_uuid_{};
  esp_bt_uuid_t state_char_uuid_{};

  uint16_t auth_service_start_handle_{0};
  uint16_t auth_service_end_handle_{0};
  uint16_t state_service_start_handle_{0};
  uint16_t state_service_end_handle_{0};

  uint16_t auth_char_handle_{0};
  uint16_t state_char_handle_{0};
};

}  // namespace boneco_ble
}  // namespace esphome

#endif  // USE_ESP32
