# ESP12-SmartMeter
This project utilizes an ESP12 module to read out an energy meter via SM5.0 protocol.

It is largely based on this article on Tweakers.net: [link](https://gathering.tweakers.net/forum/list_messages/1872361)

The following improvements are made:
 1. there is no need for 5V to 3.3V conversion as the open collector of the smart meter can be pulled to 3.3V directly;
 2. there is no need for signal inversion as the ESP8266 UART can be told to understand inverted signals;
 3. the CRC-16-IBM calculation is executed on each received datagram so that only valid data is forwarded
 4. the data is written to a Blynk.io app, see further instructions

---

For Blynk.io app, see [link](https://blynk.io/en/getting-started), create an account and a device token, embed the token and wifi information in secrets.h and use the following 'virtual pins' in the Blynk.io app:
 * V1: 15 seconds average of power consumption
 * V2: consumption low tariff
 * V3: consumption high tariff
 * V4: total consumption
 * V5: gas consumption
