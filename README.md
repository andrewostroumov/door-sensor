# ESP32 Door sensor

Starts a FreeRTOS task to handle door opening.
Sends request when door opened.

# Hardware

Connect 2k2 resistor to input pin.
Connect reed switch to resistor and to 3v3 pin.

# Flash and monitor

make flash
make monitor