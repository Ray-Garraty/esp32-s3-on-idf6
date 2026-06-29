# find_port.py — Auto-detect ESP32 COM port by matching USB VID.
# Supported chips: CP2102 (0x10C4), CH340 (0x1A86), FTDI (0x0403), ESP-JTAG.
# Returns the port name or detailed port info. Used by flash/monitor scripts.

import serial.tools.list_ports

ESP32_VIDS = {0x10C4, 0x1A86, 0x0403, 0x303A}


def find_esp32_port():
    """Find the first ESP32 COM port by VID. Returns port string or None."""
    for port in serial.tools.list_ports.comports():
        if hasattr(port, "vid") and port.vid in ESP32_VIDS:
            return port.device
    return None


def get_esp32_port_info():
    """Find ESP32 and return detailed port info dict or None."""
    for port in serial.tools.list_ports.comports():
        if hasattr(port, "vid") and port.vid in ESP32_VIDS:
            return {
                "port": port.device,
                "vid": hex(port.vid),
                "pid": hex(port.pid) if hasattr(port, "pid") else "unknown",
                "description": port.description,
            }
    return None


if __name__ == "__main__":
    info = get_esp32_port_info()
    if info:
        print(info["port"])
    else:
        print("ESP32 not found")
        exit(1)
