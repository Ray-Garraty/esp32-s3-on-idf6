# send_after_ready.py — Wait for ESP32 boot, then send a single command.
# Connects via serial, waits 8s for boot, sends temperature.read,
# and prints the raw response via a background reader thread.

import serial
import threading
import time
from find_port import find_esp32_port

PORT = find_esp32_port() or 'COM5'  # auto-detect, fallback to COM5
BAUDRATE = 115200

def reader_thread(ser):
    """Background thread that continuously reads and prints serial data."""
    while True:
        try:
            if ser.in_waiting:
                data = ser.read(ser.in_waiting).decode('utf-8', errors='replace')
                print(data, end='', flush=True)
        except serial.SerialException:
            break
        except Exception:
            pass

def main():
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
            dsrdtr=False
        )
        try:
            ser.dtr = False
            ser.rts = False
        except:
            pass
        
        print(f"Using port: {PORT}. Starting reader thread...")
        # Запускаем поток чтения
        t = threading.Thread(target=reader_thread, args=(ser,), daemon=True)
        t.start()
        
        # Ждём 8 секунд (время на загрузку)
        for i in range(8, 0, -1):
            print(f"Waiting {i} seconds...")
            time.sleep(1)
        
        # Отправляем команду temperature.read
        cmd = '{"cmd":"temperature.read"}\n'
        ser.write(cmd.encode())
        print(f"\n>>> Sent: {cmd.strip()}")
        
        # Даём время на ответ
        time.sleep(3)
        
        ser.close()
        print("\nPort closed.")
    except serial.SerialException as e:
        print(f"Error: {e}")

if __name__ == "__main__":
    main()