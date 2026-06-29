# diagnose_readlog.py — Diagnostic for system.readLog / system.getFormattedLogs.
# Connects to ESP32 via serial, waits for boot, sends readLog and
# getFormattedLogs commands, displays JSON and raw responses.
import serial
import time
from find_port import find_esp32_port

PORT = find_esp32_port() or 'COM5'  # auto-detect, fallback to COM5
BAUDRATE = 115200
TIMEOUT = 10

def wait_for_boot(ser, timeout=20):
    """Wait for ESP32 to finish booting (checks for 'Setup Complete')."""
    print("Waiting for ESP32 boot...")
    start = time.time()
    while time.time() - start < timeout:
        if ser.in_waiting > 0:
            line = ser.readline().decode('utf-8', errors='ignore').strip()
            if "Setup Complete" in line:
                print("Boot complete!")
                return True
        time.sleep(0.1)
    print("Timeout waiting for boot")
    return False

def send_and_receive(ser, cmd, timeout=5):
    """Send a command and return the JSON response string."""
    # Очищаем буфер
    ser.reset_input_buffer()
    time.sleep(0.5)
    
    print(f"\nSending: {cmd}")
    ser.write((cmd + "\n").encode())
    ser.flush()
    
    # Ждём ответ
    start = time.time()
    json_response = None
    while time.time() - start < timeout:
        if ser.in_waiting > 0:
            line = ser.readline().decode('utf-8', errors='ignore').strip()
            if line.startswith('{') and '"status"' in line:
                json_response = line
                print(f"Got JSON response!")
                break
            elif line:
                print(f"  Non-JSON: {line[:80]}")
        time.sleep(0.05)
    
    return json_response

def main():
    ser = serial.Serial(
        port=PORT, baudrate=BAUDRATE,
        timeout=0.5, xonxoff=False, rtscts=False, dsrdtr=False
    )
    print(f"Using port: {PORT}")
    
    # Ждём загрузки
    if not wait_for_boot(ser):
        ser.close()
        return
    
    # Очищаем буфер после загрузки
    ser.reset_input_buffer()
    time.sleep(1)
    ser.reset_input_buffer()
    
    # Тест 1: system.readLog
    print("\n=== Test 1: system.readLog ===")
    cmd1 = '{"id": 1, "cmd": "system.readLog", "params": {"lines": 3}}'
    resp1 = send_and_receive(ser, cmd1)
    if resp1:
        print(f"Response: {resp1[:200]}")
    else:
        print("NO JSON RESPONSE!")
    
    time.sleep(1)
    
    # Тест 2: system.getFormattedLogs  
    print("\n=== Test 2: system.getFormattedLogs ===")
    cmd2 = '{"id": 2, "cmd": "system.getFormattedLogs", "params": {"lines": 3}}'
    resp2 = send_and_receive(ser, cmd2)
    if resp2:
        print(f"Response: {resp2[:200]}")
    else:
        print("NO JSON RESPONSE!")
    
    time.sleep(1)
    
    # Тест 3: system.getStatus (baseline)
    print("\n=== Test 3: system.getStatus (baseline) ===")
    cmd3 = '{"id": 3, "cmd": "system.getStatus", "params": {}}'
    resp3 = send_and_receive(ser, cmd3)
    if resp3:
        print(f"Response: {resp3[:200]}")
    else:
        print("NO JSON RESPONSE!")
    
    ser.close()
    print("\n=== Done ===")

if __name__ == "__main__":
    main()
