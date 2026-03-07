# cxx_flags.py - Add C++ specific compiler flags
# These flags only apply to C++ files, not C files

Import("env")

# -fpermissive needed for AsyncTCP/ESPAsyncWebServer compatibility
env.Append(CXXFLAGS=["-fpermissive"])
