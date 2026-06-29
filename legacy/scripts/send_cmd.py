# send_cmd.py — Send a single JSON command to ESP32 and print response.
# Connects via serial on the configured COM port, sends the command
# up to 10 times, and displays each response line.

import serial
import time
from find_port import find_esp32_port

# --- CONFIG ---
PORT = find_esp32_port() or 'COM5'  # auto-detect, fallback to COM5
BAUDRATE = 115200
TIMEOUT = 2             # Response timeout in seconds
# ---------------

def send_command(ser, cmd_str):
    """Send a command and return the response lines."""
    ser.reset_input_buffer()
    ser.reset_input_buffer()
    # Добавляем символ конца строки
    full_cmd = cmd_str.strip() + '\n'
    print(f"Sending: {full_cmd.strip()}")
    ser.write(full_cmd.encode())
    
    # Ждём ответ (читаем все строки)
    start_time = time.time()
    response_lines = []
    while time.time() - start_time < TIMEOUT:
        if ser.in_waiting > 0:
            line = ser.readline().decode('utf-8', errors='ignore').strip()
            if line:
                response_lines.append(line)
        else:
            time.sleep(0.05)
    
    return response_lines

def main():
    try:
        print(f"Using port: {PORT}")
        ser = serial.Serial(
            port=PORT,
            baudrate=BAUDRATE,
            bytesize=serial.EIGHTBITS,
            parity=serial.PARITY_NONE,
            stopbits=serial.STOPBITS_ONE,
            timeout=0.5,
            xonxoff=False,      # Отключаем программный контроль
            rtscts=False,       # Отключаем аппаратный контроль (RTS/CTS)
            dsrdtr=False        # Отключаем DSR/DTR
        )
        print(f"Using port: {PORT}")
        time.sleep(2)  # Даём ESP32 время после сброса при подключении

        # Тестовая команда: получить состояние клапана
        cmd = '{"id":1,"cmd":"valve.getState"}'
        # Читаем все ответы для debug
        for i in range(10):
            response = send_command(ser, cmd)
            for line in response:
                print(f"LINE {i}: {line[:100]}")  # first 100 chars
        response = send_command(ser, cmd)
        
        if response:
            print(f"Response: {response}")
        else:
            print("No response received (timeout).")
            
        ser.close()
    except serial.SerialException as e:
        print(f"Error opening port {PORT}: {e}")
    except Exception as e:
        print(f"Unexpected error: {e}")

if __name__ == "__main__":
    main()