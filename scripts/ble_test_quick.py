import asyncio
from bleak import BleakScanner, BleakClient

SERVICE_UUID = "0000ffe0-0000-1000-8000-00805f9b34fb"
CHAR_CMD_UUID = "0000ffe1-0000-1000-8000-00805f9b34fb"
CHAR_RESP_UUID = "0000ffe2-0000-1000-8000-00805f9b34fb"


async def test():
    dev = None
    devices = await BleakScanner.discover(timeout=5)
    for d in devices:
        if d.name and "EcoTiter" in d.name:
            dev = d
            break
    if not dev:
        print("NOT FOUND")
        return
    print(f"Found: {dev.name} ({dev.address})")

    async with BleakClient(dev.address) as client:
        print(f"Connected! MTU={client.mtu_size}")
        for svc in client.services:
            print(f"  Service: {svc.uuid}")
            for ch in svc.characteristics:
                props = ", ".join(ch.properties)
                print(f"    Char: {ch.uuid} [{props}]")

        print()
        print('Writing ping to 0xFFE1...')
        await client.write_gatt_char(
            CHAR_CMD_UUID, b'{"id":1,"cmd":"serial.ping"}\n'
        )
        print("Write OK")

        await asyncio.sleep(3)
        print("Done")


asyncio.run(test())
