#ifdef BOARD_HALMET
#include "GwHalmetTask.h"
#include "GwApi.h"
#include <NimBLEDevice.h>
#include <WiFi.h>
#include <esp_wifi.h>
#include <ESPmDNS.h>
#include "N2kMessages.h"


// Static pointers to reduce stack usage - these are allocated once
static NimBLEServer *g_pServer = nullptr;
static NimBLECharacteristic *g_pCharacteristic = nullptr;

void halmetTask(GwApi *api)
{    
/*
     * If you never intend to use Bluetooth in a current boot-up cycle, calling `esp_bt_controller_mem_release(ESP_BT_MODE_BTDM)` could release the BSS and data consumed by both Classic Bluetooth and BLE Controller to heap.
 *
 * If you intend to use BLE only, calling `esp_bt_controller_mem_release(ESP_BT_MODE_CLASSIC_BT)` could release the BSS and data consumed by Classic Bluetooth Controller. You can then continue using BLE.
 *
 * If you intend to use Classic Bluetooth only, calling `esp_bt_controller_mem_release(ESP_BT_MODE_BLE)` could release the BSS and data consumed by BLE Controller. You can then continue using Classic Bluetooth.
 *
*/

    MDNS.end();  // Stop mDNS to free memory

    delay(7000);


    // 1. Properly shut down WiFi
    // WiFi.disconnect(true);      // Disconnect and turn off WiFi
    // WiFi.mode(WIFI_OFF);       // Set mode to off
    
    // 2. Deep clean WiFi drivers (Low-level ESP-IDF)
    // esp_wifi_stop();           // Stops WiFi driver and frees station/AP control blocks
    // esp_wifi_deinit();         // De-initializes WiFi stack completely
    

    // esp_bt_controller_mem_release(ESP_BT_MODE_CLASSIC_BT);
    // NimBLEDevice::deinit();

    delay(5000);


    // 1. Initialize the Device with minimal settings
    NimBLEDevice::init("NimBLE-ESP32");
    
    // Set lower MTU to reduce memory (default is 517, reducing saves ~1KB per connection)
    // NimBLEDevice::setMTU(23);  // Minimum BLE MTU
    
    // Set to lowest power for testing (saves a tiny bit, can increase later)
    // NimBLEDevice::setPower(ESP_PWR_LVL_N12);  // -12dBm

    // 2. Create the Server
    g_pServer = NimBLEDevice::createServer();
    
    // 3. Create Service and Characteristic
    NimBLEService *pService = g_pServer->createService("ABCD");
    g_pCharacteristic = pService->createCharacteristic(
        "1234",
        NIMBLE_PROPERTY::NOTIFY  // Only notify, not read/write saves a bit
    );
    
    // Use const char* to avoid string copy overhead
    g_pCharacteristic->setValue("Hello");  // Shorter string = less memory
    pService->start();

    // 4. Start Advertising with minimal data
    NimBLEAdvertising *pAdvertising = NimBLEDevice::getAdvertising();
    pAdvertising->addServiceUUID("ABCD");
    pAdvertising->enableScanResponse(false);  // Disable scan response to save memory
    pAdvertising->start();

    while (true)
    {
        delay(2000);
        // Only update characteristic value when needed, not continuously

                tN2kMsg solarBatteryMsg;
           SetN2kDCBatStatus(solarBatteryMsg, 1, 12, 2);
           api->sendN2kMessage(solarBatteryMsg);
    }
    vTaskDelete(NULL);
}


void halmetInit(GwApi *api)
{


    api->getLogger()->logDebug(GwLog::LOG, "halmetInit: scheduling BLE task");



    // Reduced stack from 4096 to 3072 - NimBLE task needs most stack, not this one
    api->addUserTask(halmetTask, "halmetTask", 3072);
}
#endif