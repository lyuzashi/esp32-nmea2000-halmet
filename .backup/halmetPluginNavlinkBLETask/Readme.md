
ESP32-NMEA2000 uses Channels to share BoatData – this should be the source of truth 


NavlinkBlue Task – handles switching between BLE / Wifi and notifies all channel data with BLE messages

* BLE serial over characteristic 
    * Transmit/receive raw binary NMEA messages with base64 encoding 
    * Match Navlink Blue published format 
