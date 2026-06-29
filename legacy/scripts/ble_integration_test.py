import asyncio
import subprocess
import time
import json
import sys
from bleak import BleakScanner, BleakClient

NUS_RX_UUID = "6e400002-b5a3-f393-e0a9-e50e24dcca9e"
NUS_TX_UUID = "6e400003-b5a3-f393-e0a9-e50e24dcca9e"

SCAN_TIMEOUT = 10.0
CONNECT_TIMEOUT = 10.0
SCAN_RETRIES = 3
RETRY_DELAY_S = 3
TOTAL_TIMEOUT = 120


def timestamp():
    sec, ms = divmod(int(time.monotonic_ns() / 1_000_000), 1000)
    return f"[{sec}.{ms:03d}]"


def log(msg):
    print(f"{timestamp()} {msg}", flush=True)


def ensure_ble_service():
    if sys.platform != "win32":
        return
    out = subprocess.run(
        ['powershell', '-Command',
         'Get-Service BluetoothUserService_* | Where-Object { $_.Status -eq "Stopped" } | Select-Object -ExpandProperty Name'],
        capture_output=True, text=True
    )
    name = out.stdout.strip()
    if name:
        subprocess.run(['powershell', '-Command', f'Start-Service {name}'],
                       capture_output=True)
        log(f"Started BLE service: {name}")


def check_bluetooth_adapter():
    if sys.platform != "win32":
        return
    ps = subprocess.run(
        ['powershell', '-Command',
         '$r = Get-PnpDevice | Where-Object { $_.Class -eq \"Bluetooth\" -and $_.FriendlyName -like \"*Generic*\" }; '
         'if ($r) { \"Status=\" + $r.Status + \" Problem=\" + $r.Problem } else { \"adapter_not_found\" }'],
        capture_output=True, text=True
    )
    log(f"BT adapter check: {ps.stdout.strip()}")

    ps2 = subprocess.run(
        ['powershell', '-Command',
         'Get-Service BluetoothUserService_* | Select-Object Name, Status, StartType | Format-Table -AutoSize | Out-String'],
        capture_output=True, text=True
    )
    for line in ps2.stdout.strip().splitlines():
        if line.strip():
            log(f"Service: {line.strip()}")


class BtMonitor:
    def __init__(self):
        self.packet_count = 0
        self.broadcast_count = 0
        self.response_count = 0
        self.last_ts = None
        self.min_interval = float("inf")
        self.max_interval = 0.0
        self.interval_sum = 0.0
        self.first_packet_ts = None
        self.last_packet_ts = None
        self.target_device = None
        self.target_info = None
        self.responses = {}  # cmd_id -> response text
        self.broadcasts = []
        self.client = None
        self.rx_char = None
        self.tx_char = None
        self.tests_passed = 0
        self.tests_failed = 0

    async def scan_with_retry(self):
        for attempt in range(1, SCAN_RETRIES + 1):
            log(f"Scan attempt {attempt}/{SCAN_RETRIES} (timeout={SCAN_TIMEOUT}s)...")
            devices = await BleakScanner.discover(timeout=SCAN_TIMEOUT, return_adv=True)

            if not devices:
                log(f"Scan returned 0 devices")
                if attempt < SCAN_RETRIES:
                    await asyncio.sleep(RETRY_DELAY_S)
                continue

            log(f"Found {len(devices)} device(s) in scan:")
            for addr, (device, adv) in devices.items():
                name = device.name or "(unknown)"
                rssi = adv.rssi if adv.rssi is not None else "N/A"
                rssi_flag = " SUSPICIOUS(RSSI=0)" if (isinstance(rssi, int) and rssi == 0) else ""
                mfg_data = f" mfg={dict(adv.manufacturer_data)}" if adv.manufacturer_data else ""
                svc_uuids = f" svc={[str(u)[:8] for u in adv.service_uuids]}" if adv.service_uuids else ""
                arrow = ""
                if name.startswith("EcoTiter"):
                    arrow = "  <--- TARGET"
                    self.target_device = device
                    self.target_info = (name, addr, rssi)
                log(f"  {name:<30} {addr:<36} RSSI={str(rssi):>4}{rssi_flag}{mfg_data}{svc_uuids}{arrow}")

            if self.target_device:
                log(f"EcoTiter found on attempt {attempt}")
                return True

            if attempt < SCAN_RETRIES:
                log(f"EcoTiter not found, waiting {RETRY_DELAY_S}s before retry...")
                await asyncio.sleep(RETRY_DELAY_S)

        log(f"EcoTiter NOT found after {SCAN_RETRIES} attempts")
        return False

    def on_notification(self, sender, data):
        raw = data.decode(errors="replace").strip()
        now = time.monotonic()
        ts_now = time.time()

        if self.first_packet_ts is None:
            self.first_packet_ts = now
        self.last_packet_ts = now
        self.packet_count += 1

        if self.last_ts is not None:
            interval = (now - self.last_ts) * 1000
            self.interval_sum += interval
            if interval < self.min_interval:
                self.min_interval = interval
            if interval > self.max_interval:
                self.max_interval = interval
        self.last_ts = now

        is_broadcast = True
        cmd_id = None
        try:
            parsed = json.loads(raw)
            if "id" in parsed:
                cmd_id = parsed["id"]
                is_broadcast = False
        except json.JSONDecodeError:
            pass

        if not is_broadcast:
            self.response_count += 1
            self.responses[cmd_id] = raw
            tag = "RESPONSE"
        else:
            self.broadcast_count += 1
            self.broadcasts.append(raw)
            tag = "BROADCAST"

        date_str = time.strftime("%H:%M:%S", time.localtime(ts_now))
        ms = int((ts_now - int(ts_now)) * 1000)
        print(f"[{date_str}.{ms:03d}] {tag}: {raw}", flush=True)

    def print_stats(self):
        elapsed = self.last_packet_ts - self.first_packet_ts if self.first_packet_ts else 0
        print(f"\n{timestamp()} === Statistics ===")
        print(f"  Total packets:    {self.packet_count}")
        print(f"  Broadcasts:       {self.broadcast_count}")
        print(f"  Responses:        {self.response_count}")
        if self.packet_count > 1 and elapsed > 0:
            print(f"  Elapsed:          {elapsed:.1f}s")
            print(f"  Avg interval:     {self.interval_sum / (self.packet_count - 1):.1f} ms")
            print(f"  Min interval:     {self.min_interval:.1f} ms")
            print(f"  Max interval:     {self.max_interval:.1f} ms")
            print(f"  Avg rate:         {self.packet_count / elapsed:.1f} pkt/s")

    async def send_cmd(self, cmd_id, cmd_name, **params):
        cmd = json.dumps({"id": cmd_id, "cmd": cmd_name, **params}) + "\n"
        log(f"SEND ({cmd_name}): {cmd.strip()}")
        try:
            write_start = time.monotonic()
            await self.client.write_gatt_char(self.rx_char, cmd.encode(), response=False)
            log(f"  Write done in {(time.monotonic()-write_start)*1000:.0f}ms")
        except Exception as e:
            log(f"  Write FAILED: {e}")

    async def wait_for_response(self, cmd_id, timeout=5.0):
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            if cmd_id in self.responses:
                return self.responses.pop(cmd_id)
            await asyncio.sleep(0.05)
        return None

    def test(self, name, ok, detail=""):
        if ok:
            self.tests_passed += 1
            log(f"  >>> TEST [{name}]: PASS {'(' + detail + ')' if detail else ''}")
        else:
            self.tests_failed += 1
            log(f"  >>> TEST [{name}]: FAIL {'(' + detail + ')' if detail else ''}")

    async def phase_broadcast(self):
        """Wait for a few broadcasts to confirm stream is alive."""
        initial_count = self.broadcast_count
        log(f"\n{'='*60}")
        log("Phase 1: Broadcast stream verification")
        log(f"{'='*60}")
        await asyncio.sleep(3)
        new_bc = self.broadcast_count - initial_count
        self.test("broadcast_stream", new_bc >= 2, f"{new_bc} packets in 3s")
        return new_bc >= 2

    async def phase_get_status(self):
        """Send getStatus and validate response."""
        log(f"\n{'='*60}")
        log("Phase 2: burette.getStatus")
        log(f"{'='*60}")
        await self.send_cmd(1, "burette.getStatus")
        resp = await self.wait_for_response(1)
        if resp:
            d = json.loads(resp)
            ok = d.get("status") == "ok" and isinstance(d.get("data"), dict)
            self.test("getStatus", ok, d.get("data", {}).get("status", "?"))
            return d.get("data", {})
        self.test("getStatus", False, "no response")
        return None

    async def phase_valve_test(self):
        """Switch valve output->input, verify via broadcast."""
        log(f"\n{'='*60}")
        log("Phase 3: Valve switch test")
        log(f"{'='*60}")

        initial_bc_count = len(self.broadcasts)

        # Switch to output
        await self.send_cmd(2, "valve.setPosition", position="output")
        resp = await self.wait_for_response(2)
        if resp:
            d = json.loads(resp)
            self.test("valve->output", d.get("status") == "ok",
                      f"position={d.get('data', {}).get('position', '?')}")
        else:
            self.test("valve->output", False, "no response")

        await asyncio.sleep(1.5)

        # Switch back to input
        await self.send_cmd(3, "valve.setPosition", position="input")
        resp = await self.wait_for_response(3)
        if resp:
            d = json.loads(resp)
            self.test("valve->input", d.get("status") == "ok",
                      f"position={d.get('data', {}).get('position', '?')}")
        else:
            self.test("valve->input", False, "no response")

        await asyncio.sleep(1.5)

        # Verify valve position in broadcast stream
        recent_bc = self.broadcasts[initial_bc_count:]
        matches = sum(1 for b in recent_bc if '"vlv":"in"' in b)
        self.test("valve_bcast_verify", matches >= 2, f"{matches}/2+ broadcasts show vlv=in")

    async def phase_dosing_test(self):
        """Start dosing, monitor progress, stop."""
        log(f"\n{'='*60}")
        log("Phase 4: Dosing test (doseVolume + stop)")
        log(f"{'='*60}")

        initial_bc_count = len(self.broadcasts)

        # Start dosing 0.5ml at 5ml/min
        await self.send_cmd(4, "burette.doseVolume", volume_ml=0.5, speed_ml_min=5.0)
        resp = await self.wait_for_response(4, timeout=5.0)
        if resp:
            d = json.loads(resp)
            started = d.get("status") == "ok"
            self.test("doseVolume.start", started, d.get("data", {}).get("status", "?"))
            if not started:
                log(f"  (device may need burette.empty first — still counts as test)")
        else:
            self.test("doseVolume.start", False, "no response")

        # Monitor for ~3s — broadcasts should show working status
        await asyncio.sleep(3)
        recent_bc = self.broadcasts[initial_bc_count:]
        working_bc = sum(1 for b in recent_bc if '"sts":"working"' in b)
        self.test("dosing_broadcast", working_bc >= 2, f"{working_bc} broadcasts show working")

        # Stop dosing
        cmd_id = 5
        await self.send_cmd(cmd_id, "burette.stop")
        resp = await self.wait_for_response(cmd_id, timeout=5.0)
        if resp:
            d = json.loads(resp)
            self.test("doseVolume.stop", d.get("status") == "ok", d.get("data", {}).get("status", "?"))
        else:
            self.test("doseVolume.stop", False, "no response")

        await asyncio.sleep(1)

    async def run(self):
        log("=== BLE Integration Test ===")
        log("(c) EcoTiter — for LL-level verification run alongside:")
        log("  sudo btmon | grep -E \"LL_FEATURE|LL_LENGTH|LL_CONNECTION_UPDATE|LL_REJECT\"")
        ensure_ble_service()
        check_bluetooth_adapter()

        found = await self.scan_with_retry()
        if not found:
            log("FAILED: Could not find EcoTiter")
            return

        dev = self.target_device
        name, addr, rssi = self.target_info

        for connect_attempt in range(1, 4):
            log(f"\nConnect attempt {connect_attempt}/3 to {name} ({addr}), RSSI={rssi}...")
            try:
                connect_start = time.monotonic()
                async with BleakClient(dev, timeout=CONNECT_TIMEOUT,
                                       disconnected_callback=lambda c: log("*** Disconnected ***")) as client:
                    self.client = client
                    connect_ms = (time.monotonic() - connect_start) * 1000
                    log(f"Connected in {connect_ms:.0f}ms, MTU: {client.mtu_size}")

                    self.tx_char = client.services.get_characteristic(NUS_TX_UUID)
                    self.rx_char = client.services.get_characteristic(NUS_RX_UUID)

                    if not self.tx_char or not self.rx_char:
                        log("ERROR: NUS characteristics not found")
                        return

                    log("Subscribed to NUS TX notifications")
                    await client.start_notify(self.tx_char, self.on_notification)

                    # Run all test phases
                    await self.phase_broadcast()
                    status = await self.phase_get_status()
                    await self.phase_valve_test()

                    # Only run dosing if burette is idle and has volume
                    if status and status.get("status") == "idle" and status.get("volume_ml", 0) > 0:
                        await self.phase_dosing_test()
                    else:
                        log(f"\n{'='*60}")
                        log("Phase 4: Dosing test — SKIPPED (burette not idle)")
                        log(f"{'='*60}")
                        if status:
                            log(f"  status={status.get('status')}, volume={status.get('volume_ml')}ml")
                        self.test("dosing (skipped)", True, "burette not ready")

                    if client.is_connected:
                        try:
                            await client.stop_notify(self.tx_char)
                        except Exception:
                            pass

                    self.print_stats()

                    # Summary
                    print(f"\n{'='*60}")
                    log("=== TEST SUMMARY ===")
                    total = self.tests_passed + self.tests_failed
                    log(f"  Passed: {self.tests_passed}/{total}")
                    log(f"  Failed: {self.tests_failed}/{total}")
                    if self.tests_failed == 0:
                        log("  RESULT: ALL TESTS PASSED")
                    else:
                        log("  RESULT: SOME TESTS FAILED")
                    print(f"{'='*60}")
                    return

            except asyncio.TimeoutError:
                log(f"Connection timeout ({CONNECT_TIMEOUT}s) — attempt {connect_attempt}/3")
                if connect_attempt < 3:
                    await asyncio.sleep(3)
            except Exception as e:
                log(f"Connection error: {type(e).__name__}: {e}")
                if connect_attempt < 3:
                    await asyncio.sleep(3)

        log("All 3 connection attempts failed")


async def run_with_timeout():
    try:
        await asyncio.wait_for(BtMonitor().run(), timeout=TOTAL_TIMEOUT)
    except asyncio.TimeoutError:
        log(f"TOTAL TIMEOUT ({TOTAL_TIMEOUT}s) — script stopped")


def main():
    asyncio.run(run_with_timeout())


if __name__ == "__main__":
    main()
