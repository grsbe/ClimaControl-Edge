Hi claude,
the wildnismanufaktur group wants to grow mushrooms.
The building is already prepared and isolated, has electic etc.
We need software for the ESP32.

More specifically, the ESP32 gets the data from all of these sensors over an I2C interface.
It also controlls the air, humidity fans etc.
More on this is in C:\Users\grsbe\Desktop\Projects\wildnismanufaktur-edge\hardware.jpg

I have never worked with this stuff before.
We need a wifi interface that the ESP creates that lets people to be able to connect to it, see the data and be able to control the outputs

Further over MQTT we also want the ESP to connect to a remote computer / server which tracks the data and also allows full control of the system.

The system is critical to not malfunction, else mushrooms die. Redundancy options?

I dont have an ESP to test here.