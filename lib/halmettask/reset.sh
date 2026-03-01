#! /bin/bash

# Reset everything
rm -rf .pio .dummy managed_components sdkconfig.* src/idf_component.yml CMakeLists.txt dependencies.lock lib/generated
# rm -rf ~/.platformio/
# pip uninstall platformio-core -y
# pip install --force-reinstall -U https://github.com/pioarduino/platformio-core/archive/refs/tags/v6.1.19.zip


echo "Now run $ platformio run -e halmet -t upload --upload-port /dev/cu.usbserial-8010"