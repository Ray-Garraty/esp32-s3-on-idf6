import serial.tools.list_ports

ESP32_VIDS = {0x10C4, 0x1A86, 0x0403, 0x303A}

def find_esp32_port():
    for p in serial.tools.list_ports.comports():
        if hasattr(p, "vid") and p.vid in ESP32_VIDS:
            return p.device
    return None
