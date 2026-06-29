# serial_monitor.py — Real-time serial monitor with auto mock-mode injection.
# Auto-detects ESP32 port, enables mock mode via system.setMockMode,
# then continuously prints all incoming serial data.

import serial
import threading
import time
from pathlib import Path
import sys

sys.path.insert(0, str(Path(__file__).parent))
from find_port import find_esp32_port


def reader_thread(ser):
    while True:
        try:
            if ser.in_waiting:
                data = ser.read(ser.in_waiting).decode("utf-8", errors="replace")
                sys.stdout.write(data)
                sys.stdout.flush()
        except serial.SerialException:
            break
        except Exception:
            pass


def main():
    PORT = find_esp32_port()
    if not PORT:
        print(f"ERROR: ESP32 not found")
        return 1

    BAUDRATE = 115200

    try:
        ser = serial.Serial(
            port=PORT,
            baudrate=BAUDRATE,
            bytesize=serial.EIGHTBITS,
            parity=serial.PARITY_NONE,
            stopbits=serial.STOPBITS_ONE,
            timeout=1,
            xonxoff=False,
            rtscts=False,
            dsrdtr=False,
        )
        try:
            ser.dtr = False
            ser.rts = False
        except Exception:
            pass

        print(f"=== Connected to {PORT} ===")

        time.sleep(2)
        ser.reset_input_buffer()

        cmd = '{"id":1,"cmd":"system.setMockMode","params":{"enabled":true}}\n'
        ser.write(cmd.encode())
        print(f"[TX] {cmd.strip()}")

        t = threading.Thread(target=reader_thread, args=(ser,), daemon=True)
        t.start()

        print("\n=== MONITORING (Ctrl+C to exit) ===\n")

        try:
            while True:
                time.sleep(0.1)
        except KeyboardInterrupt:
            print("\n=== Exiting ===")

        ser.close()
        print("Port closed.")

    except serial.SerialException as e:
        print(f"Error: {e}")
        return 1

    return 0


if __name__ == "__main__":
    main()