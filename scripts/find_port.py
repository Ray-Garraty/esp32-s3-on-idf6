import sys
import serial.tools.list_ports

# VID 0x303A = Espressif native USB (ESP32-S2/S3/C3/C6/H2 via USB_SERIAL_JTAG)
ESP32_VIDS = {0x10C4, 0x1A86, 0x0403, 0x303A}

def find_esp32_port():
    for p in serial.tools.list_ports.comports():
        if hasattr(p, "vid") and p.vid in ESP32_VIDS:
            return p.device
    return None

if __name__ == "__main__":
    port = find_esp32_port()
    if port:
        print(port)
    else:
        sys.exit(1)
