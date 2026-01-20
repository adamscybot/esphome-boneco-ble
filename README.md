# esphome-boneco-ble

> [!WARNING]  
> This is WIP/alpha software and is not feature complete. As such, their are no tagged releses yet. The API is subject to change.

This is an external component for [ESPHome](https://esphome.io/) that allows integration of Boneco BLE devices (**currently just fans**) with Home Assistant.

In my opinion, these are amongst the highest quality, nicely designed & aesthetically pleasing fans for the home on the market. They already support local interaction via Bluetooth.

But now...I invite you to go to the next level with this gratuitous ESPHome integration! Want to be bathed in a cooling breeze when the temperature crosses your threshold? Left the house and forgot to turn your fan off? Or will you simply never rest until you can enjoy the strange nerdy-satisfaction of Home Assistant totality? Well, now you can!

## ESPHome Component vs HA Integration

> [!NOTE]  
> I wrote most of the code a long time back and put it on the backburner half done. I am now revisiting. Since then I found https://github.com/DeKaN/ha-boneco exists and device support, and binding without sniffing the key, has been figured out. I will begin to bring this project up to speed.

This project is a native ESPHome component and so does not utilise "BLE proxies". This approach is a different niche that may or may not be good for you:

* This solution does not add to contention problems with other Home Assistant integrations, and is not effected by them in the other direction. The scalability model is different (predicatable and controlled, but more rigid and configured by yourself).
  * This means state updates are almost instant and stability is consistent. 
  * See [Multiple devices](#multiple-devices) for more details.
* Support for niche use cases, e.g:
  * Direct interaction with other hardware components.
  * High level of advanced configuration/integration opportunities via ESPHome YAML.
  * Novel solutions such as using Zigbee instead of WiFI as backhaul.
  * Don't need Home Assistant _at all_ if you don't want. Works when its down.


## Features

### Device support

> [!WARNING]  
> There is a possibility to expand device support, including to other Boneco BLE devices. See the [FAQ](#what-about-other-boneco-devices).

The project currently only supports fans in the Boneco BLE range.

#### Fans

Models supported:

* ✅ F235 (fully tested)
* ✅ F225 (⚠️ likely, unconfirmed)

Capabilities:

* ✅ Bidirectional on/off control.
* ✅ Bidirectional variable speed control.
* ❌️ The built-in timer can not be configured (yet). However, you can now automate via ESPHome/HA anyway.


## Prequisites

The following are required:

* A supported device as above.
* An ESP32 controller.

In order for you to capture your own device key, you will also require the following (once-per-device only):

* A mobile device with the Boneco app installed.
* Physical access to your fan in order to execute a pairing process to your mobile device.
* ⚠️ A BLE sniffer dongle such as the [nRF52840](https://www.nordicsemi.com/Products/Development-hardware/nRF52840-Dongle) and a Windows/Mac/Linux machine.

## Preparation

You must first retrieve your device MAC address and device key by sniffing it over the air waves. As outlined above, a BLE sniffer device is needed to complete this.

This process is somewhat involved at the moment so head over to [Capturing device details](./docs/capturing-device-details/README.md) and come back here when you have obtained the necessary details.


## Configuration

### Quick Start

First import this component to your base ESPHome YAML via this GitHub repository:

```yaml
external_components:
  - source: github://adamscybot/esphome-boneco-ble
    components: [boneco_ble]
```

Now let's wire in our first fan by way of example:

1. Setup a [`ble_client`](https://esphome.io/components/ble_client/) that targets the discovered MAC address of the fan you want to bind to.
2. Create a `fan` entity using the new `boneco_ble` platform from the external component you registered earlier.
   1. Link its `ble_client_id` to the `ble_client` we just created.
   2. Set the device key to that which you obtained in the [Preparation](#preparation) section. This should be kept in your `secrets.yaml`.
3. Optionally (recommended) add a ESPHome native signal sensor for the BLE client as shown in the example. This becomes unavailable in Home Assistant if the fan is not connectable via BLE which helps visibility.

```yaml

## Step 1
ble_client:
  - id: bedroom_fan_ble_client
    # This is the default but surfacing for visibility. When the device is found, it will immediately connect. If the 
    # connection is lost, it will poll and retry when the fan is found again.
    auto_connect: true
    # Change to reference unique MAC of target fan.
    mac_address: "AA:BB:CC:DD:EE:FF" 

## Step 2
fan:
  - platform: boneco_ble
    id: bedroom_fan
    name: "Bedroom Fan"
    ble_client_id: bedroom_fan_ble_client

    # Must reference unique sniffed device key for the fan with the previously provided MAC as a 32 character hex string.
    #
    # For example, in `secrets.yaml`:
    #
    # ```
    # # DUMMMY VALUE
    # bedroom_fan_device_key: "24dbd03a157722ed1d92dfa106786d6e" 
    # ```
    #
    device_key: !secret bedroom_fan_device_key
   
## Step 3 (optional)
sensor:
  - platform: ble_client
    type: rssi
    entity_category: diagnostic
    ble_client_id: bedroom_fan_ble_client
    device_id: bedroom_fan_controller
    name: "Bedroom Fan RSSI"
```

### Multiple Devices

As per the [BLE Client](https://esphome.io/components/ble_client/) docs a maximum of three active connections is the default, but you can configure this to a higher number. The ESPHome docs recommend not exceeding five. To raise this limit as follows:

```
esp32_ble:
  max_connections: 5
```

Note this project is a native on-device ESP32 solution that does not require connections to be orchestrate from Home Assistant like "Bluetooth proxy" approaches. Scalability problems are still a thing, but the model is different:

* You define yourself which ESP32 holds connections with which devices.
* You are not fighting for contention with any other integrations. 
* For this reason there is no time slicing so BLE connections should be rock solid and not suddenly degrading because of other/new integrations. Behaviour is predictable and contention is isolated to only that which you could create inside the single ESP32 you are configuring.
* The defined connections are always-connected and state updates are instant.
* You place the device in an area convenient for the BLE devices you configured for it in the YAML. 
* If you need to scale horizontally (probbaly unlikely someone has >5 Boneco devices), you

### Fan Configuration API

In addition to all options from the [Base Fan Configuration](https://esphome.io/components/fan/#base-fan-configuration), the platform takes the following configuration variables:

- **ble_client_id** (*Required*, ID): A reference to the ID of the `ble_client` to use for this fan.
- **device_key** (*Required*, string): 32-character hex key captured from your device.
- **optimistic** (*Optional*, boolean): If `true`, publish state immediately after a write instead of waiting for the fan to confirm its newly set speed. This makes the Home Assistant UI control feel marginally more responsive but comes at the cost of potential inaccuracy for edge cases I am yet to enumerate (probably leave this off for now). Defaults to `false`.



## FAQ

### Can I continue to use the mobile app alongside this project?

Currently, not in a practical way. The device seems to only allow for one exclusive client. This component occupies this slot since it needs to hold a long-term "connection" such that it can receive updates from the device about its state. This is initated as soon as the ESPHome boots.

Mobile app clients will not be able to connect. Note, that if a mobile device is actively talking to the device *before* the ESPHome tries to connect, then the *latter* would be blocked.

Home Assistant becomes the centralised shared location to manage the device.

It may be theoretically possible to retain app control with some sort of Bluetooth host on the ESP32, but I have not explored this yet. However, this seems lower priority since you can just use Home Assistant.

If you wish to use the app, switch off your ESPHome device.



## Disclaimer

This external component is provided “as is” and maintained by the community. It is not affiliated with nor supported by Boneco.

Contributors and maintainers are not responsible for any damage, downtime, warranty issues, or other consequences arising from its use. If you choose to use or modify this project, you do so at your own risk.
