# Capturing Device Details

In order to use the component, you must first retrieve some key identifiers from your device.

## Device key

Each device has a unique key embedded within it which is used as part of the BLE communication mechanism to interact with clients, e.g. the mobile application.

This project does not implement the initial pairing mechanism that the mobile app uses to retrieve this key from the device and, whilst that maybe possible — it is not a focus of my interopability efforts — which are limited to what I myself am able to figure out by analysing BLE packets over-the-air (at least as a software engineer wading into BLE for the first time to just-get-it-to-work, which whilst very enjoyable, also took a lot more brain-power, research, time, and trial-and-error than I bargained for!).

You are therefore required to use a BLE sniffer to capture your own device key during a normal pair process between the mobile app on your mobile device, and the fan. This key can then be provided in your ESPHome YAML configuration. Identically to the mobile app, this process requires physical access to your fan to perform the pairing process.

Unfortunately, ESP32's Bluetooth stack can not perform this sniffing task. Therefore, dedicated BLE sniffer hardware is needed. These are cheap and generally available. The sniffer is only needed once to capture the key via interfacing with it from some standard Windows/Mac/Linux machine, in order to then go on to correctly configure the component in ESPHome. It is not an ongoing or recurring requirement once the key is retrieved and known to you.

Please note that this necessary prequisite for interopability also eventuates the requirement for _you_ to be responsible for the safe keeping of your own device key. Understand any risks before proceeding.

## MAC Address

It is also necessary to obtain the MAC address of the device. This will be collected as a matter of course as an aside of the device key sniffing process. Note that the "BLE MAC ID" shown in the mobile app appears *not* to be the desired value.

# Guides

This should be possible with any BLE sniffer on any platform, and the following guide(s) can probably be adapted to your specific sniffer/platform. 

* [Nordic Semiconductors (e.g. nRF52840, nRF52833, nRF52 etc) + Wireshark](./nordic-nrf.md) 