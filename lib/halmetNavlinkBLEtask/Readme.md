
NavlinkBlue Task – notifies all channel data with BLE messages

* BLE serial over characteristic 
    * Transmit/receive raw binary NMEA messages with base64 encoding 
    * Match Navlink Blue published format 


Plan
* Use NimBLEServerCallbacks to set counter of connected clients (display in web interface)
* Create channel message callback which sends BLE notification (in NavLinkChannelImpl loop)

