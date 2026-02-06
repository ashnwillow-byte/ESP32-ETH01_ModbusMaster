# Owen_ESP32_flask_host Firmware for the host based on the esp32-eth01 reads data from modbus slaves and sends it to the server via the specified IP in json format 
### Hardware (V1.0) 
- esp32-eth01 v1.4 (esp32 with LAN) 
https://aliexpress.ru/popular/wt32-eth01-esp32?g=y&page=1&searchInfo=zV-fwRzqDKqmlD-N1b6YmtSbe07HPfagG82TbkZiuNR2x_BbmDG1iRLcDZGxSJPdduBp0boXZyec3S5fWXMVmoVqyy23-wfKXqQTHcNHKfUwAqGug965CPyN6lIXzYYsNnzIJS2B2WQJ 
- rs-485/TTL converter module (MAX485 based)
https://aliexpress.ru/wholesale?SearchText=rs+485+ttl+&g=y&page=1&searchInfo=A%2B%2BYVGTtT1%2F6ZMIT5GWzqekfrfAwT1P6cR3sP7YFfeSY7zuTGaI2As%2FkssSt%2FoCVDYY4IaovT2XmmFVZGssDqv2u4%2FECdkORAOGR9nSk0YCnb61DZWJg78HZHFKgVLoExZzCO5FUuy7UG5xQWG5wSEMiSlO7k2d9LnyqRZJUpnez1CloWgOk2OThVQ%3D%3D
- USB/UART (for boot) https://aliexpress.ru/wholesale?SearchText=usb+uart&g=y&page=1&searchInfo=AwY6JHa0jkclMxFxrou4Xuhiy6qAROMsVT%2BCaMTVtK5gZyZ%2FxILd%2BoIPPPaxCSKAc3LAwmbkhKmV0PERbcezZR7T7%2BfzMpkrhYf5GshC6gjPOV%2BupZbHjqDzGee3ALCUx4g5uAcv5RkDy7QV%2F1qwypxYiwIMPhoX20FPd37thvgEyqFpdxpOaye5GQ%3D%3D
### Recommended:
- (2x) 120 Ohm resistors on edges of the line (rs-485 standart)
- Use hardware UART for rs-485 (5, 17 pins). Software UART have problems
### Connections
- Connect modbus slaves in line
- Add 2 resistor or 1 if converter has it
- Connect A/B wires to converter * Connect converter to esp32
- Connect vcc to 3.3v !!! Connect GND
- Connect DI, RO to TX, RX pins in UART (5, 17 GPIO)
- Connect DE and RE and then connect them to receive control pin (2 GPIO)


## Boot
# For custom scheme
1. Disconnect RS-485 converter source (VCC)
2. Use 5v UART-pin 
3. Connect IO0 to GND (wait) and connect/disconnect EN (RST) to GND after that
4. Disconnect IO0
5. And now device ready to boot

# For default scheme
1. Switch RS toggle off
2. Push boot button and RST after that
3. Release the boot button
4. It's work!!!
