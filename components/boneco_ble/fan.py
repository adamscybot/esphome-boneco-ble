import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import ble_client, fan
from esphome.components.ble_client import CONF_BLE_CLIENT_ID
from esphome.const import CONF_ID, CONF_OPTIMISTIC, PLATFORM_ESP32

CODEOWNERS = ["@adamscybot"]
DEPENDENCIES = ["ble_client", "esp32"]
AUTO_LOAD = ["ble_client"]

CONF_BLE_CLIENT = "ble_client_id"
CONF_DEVICE_KEY = "device_key"

boneco_ble_ns = cg.esphome_ns.namespace("boneco_ble")
BonecoBleFan = boneco_ble_ns.class_(
    "BonecoBleFan", cg.Component, fan.Fan, ble_client.BLEClientNode
)


def _validate_device_key(value):
    value = cv.string_strict(value).lower()
    if len(value) != 32:
        raise cv.Invalid("device_key must be 32 hex characters")
    if any(c not in "0123456789abcdef" for c in value):
        raise cv.Invalid("device_key must be hex characters only")
    return value


CONFIG_SCHEMA = cv.All(
    fan.fan_schema(BonecoBleFan)
    .extend(
        {
            cv.Required(CONF_BLE_CLIENT): cv.use_id(ble_client.BLEClient),
            cv.Required(CONF_DEVICE_KEY): _validate_device_key,
            cv.Optional(CONF_OPTIMISTIC, default=False): cv.boolean,
        }
    )
    .extend(cv.COMPONENT_SCHEMA),
    cv.only_on([PLATFORM_ESP32]),
)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    await fan.register_fan(var, config)
    await ble_client.register_ble_node(var, {CONF_BLE_CLIENT_ID: config[CONF_BLE_CLIENT]})
    cg.add_build_flag('-DMBEDTLS_CONFIG_FILE=\\"mbedtls/esp_config.h\\"')
    cg.add(var.set_optimistic(config[CONF_OPTIMISTIC]))
    cg.add(var.set_device_key(config[CONF_DEVICE_KEY]))
