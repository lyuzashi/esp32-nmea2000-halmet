#ifdef BOARD_HALMET
#include "GwHalmetTask.h"
#include "GwApi.h"
#include "GwSerial.h"
#include "VeDirectFrameHandler.h"
#include "VeDirectHelper.h"
#include "GwXdrTypeMappings.h"
#include "N2kMessages.h"
#include <ArduinoOTA.h>
#include <WiFi.h>

#include <NimBLEDevice.h>

#define SERVICE_UUID        "4fafc201-1fb5-459e-8fcc-c5c9c331914b"
#define CHARACTERISTIC_UUID "beb5483e-36e1-4688-b7f5-ea07361b26a8"


HardwareSerial SerialInput(2); // Use UART2
VeDirectFrameHandler myve;
VeDirectHelper veHelper(&myve);

void halmetTask(GwApi *api)
{
    Serial.println("Halmet serial task started (GwSerialImpl)");

    // Wait for WiFi to be connected before starting OTA
    int retries = 0;
    while (WiFi.status() != WL_CONNECTED && retries < 50)
    {
        delay(100);
        retries++;
    }

    if (WiFi.status() == WL_CONNECTED)
    {
        // ArduinoOTA.setHostname("esp32-halmet");
        // ArduinoOTA.begin();
        // api->getLogger()->logDebug(GwLog::LOG, "OTA initialized on %s", WiFi.localIP().toString().c_str());



    //    // 1. Initialize the NimBLE device
    NimBLEDevice::init("ESP32-NimBLE");


    // // 2. Create the Server
    NimBLEServer *pServer = NimBLEDevice::createServer();

    // // 3. Create a Service (using a simple 4-digit or 128-bit UUID)
    NimBLEService *pService = pServer->createService("ABCD");

    // // 4. Create a Characteristic with Read/Write properties
    NimBLECharacteristic *pCharacteristic = pService->createCharacteristic(
                                               "1234",
                                               NIMBLE_PROPERTY::READ | 
                                               NIMBLE_PROPERTY::WRITE
                                             );

    pCharacteristic->setValue("Hello from NimBLE");

    // // 5. Start the service and advertising
    // pService->start();
    NimBLEDevice::getAdvertising()->start();

    }
    else
    {
        api->getLogger()->logDebug(GwLog::LOG, "WiFi not connected, OTA not started");
    }

    // delay(10000);





    // Create a GwSerialImpl for RX-only usage
    GwSerial *gwSerial = new GwSerialImpl<HardwareSerial>(
        api->getLogger(), &SerialInput, 100, true // logger, stream, id, allowRead
    );
    // Try to initialize serial on DIGITAL_INPUT_1 (GPIO_NUM_23)
    // RX only, TX pin -1
    gwSerial->begin(19200, SERIAL_8N1, DIGITAL_INPUT_1, -1); // baud, mode, rx, tx

    api->getLogger()->logDebug(GwLog::LOG, "Halmet serial task started (GwSerialImpl)");

    unsigned long lastData = millis();
    const unsigned long timeoutMs = 10000; // 10 seconds

    Stream *stream = gwSerial->getStream(false);

    // Get voltage/current configuration
    String voltageTransducer = api->getConfig()->getString(GwConfigDefinitions::halVoltTxdr);
    String currentTransducer = api->getConfig()->getString(GwConfigDefinitions::halCurrTxdr);
    int mpptBatteryInstance = api->getConfig()->getInt(GwConfigDefinitions::halVoltInst);

    // TODO Implement own sensor following Sensors.md

    while (true)
    {
        ArduinoOTA.handle();
        gwSerial->loop(true, false);

        while (stream->available())
        {
            myve.rxData(stream->read());
            delay(10); // Small delay to yield
        }

        // Get voltage and current with defaults if not available
        double voltage = veHelper.getValueAsDouble("V", N2kDoubleNA) / 1000.0;
        double current = veHelper.getValueAsDouble("I", N2kDoubleNA) / 1000.0;

        tN2kMsg solarBatteryMsg;
        if (!N2kIsNA(voltage)) { // TODO don't include current if not available?
           SetN2kDCBatStatus(solarBatteryMsg, mpptBatteryInstance, voltage, current);
           api->sendN2kMessage(solarBatteryMsg);
        }

        // Provide DC status message for information about the type of power source (voltage on battery)
        tN2kMsg solarBatteryDCStatusMsg;
        // N2kDCt_SolarCell for solar
        SetN2kDCStatus(solarBatteryDCStatusMsg, 0xff, mpptBatteryInstance, tN2kDCType::N2kDCt_Battery, N2kUInt8NA, N2kUInt8NA, N2kDoubleNA, N2kDoubleNA);
        api->sendN2kMessage(solarBatteryDCStatusMsg);


        tN2kMsg testMsg;
        SetN2kPGN128267(testMsg, 0, 7.8, 0.5, 100);
        api->sendN2kMessage(testMsg);

        tN2kMsg testMsg2;
        SetN2kTemperatureExt(testMsg2, 0, 1, N2kts_SeaTemperature, CToKelvin(25.5));
        api->sendN2kMessage(testMsg2);
        


        tN2kMsg testMsg3;
        SetN2kTemperatureExt(testMsg3, 0, 2, N2kts_SeaTemperature, CToKelvin(18.5));
        api->sendN2kMessage(testMsg3);
        

        // Create and send DC detail message

        // The Battery instance for PGNs 127508 can be changed. After you did that, you can still distinguish between the Battery and PV information by looking at
        // the DC detailed status PGN, 127506 0x1F212. It will report the DC Type, field 3, as Battery or Solar Cell. Field 2, DC Instance, equals the Battery instance in
        // the Battery Status PGN for battery and solar information.

        delay(2000); // Always yield to avoid watchdog reset
    }
    delete stream;
    vTaskDelete(NULL);
}

void halmetInit(GwApi *api)
{
    const String taskName("halmetTask");

    api->addUserTask(halmetTask, taskName, 4000);
    // this would create our task with a stack size of 4000 bytes

    // Configure XDR mapping for Halmet voltage and current data from MPPT Solar controller
    String voltageTransducer = api->getConfig()->getString(GwConfigDefinitions::halVoltTxdr);
    String currentTransducer = api->getConfig()->getString(GwConfigDefinitions::halCurrTxdr);
    int mpptBatteryInstance = api->getConfig()->getInt(GwConfigDefinitions::halVoltInst);

    if (!voltageTransducer.isEmpty()) {
        GwXDRMappingDef xdr(voltageTransducer, GwXDRCategory::XDRBAT, -1, GWXDRFIELD_BATTERY_BATTERYVOLTAGE, GwXDRMappingDef::IS_SINGLE, mpptBatteryInstance, GwXDRMappingDef::Direction::M_FROM2K);
        api->addXdrMapping(xdr);
    }

    if (!currentTransducer.isEmpty()) {
        GwXDRMappingDef xdr(currentTransducer, GwXDRCategory::XDRBAT, -1, GWXDRFIELD_BATTERY_BATTERYCURRENT, GwXDRMappingDef::IS_SINGLE, mpptBatteryInstance, GwXDRMappingDef::Direction::M_FROM2K);
        api->addXdrMapping(xdr);
    }
}
#endif