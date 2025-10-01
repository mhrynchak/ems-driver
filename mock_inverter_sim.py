from pymodbus.server import ServerAsyncStop, StartAsyncTcpServer
from pymodbus.datastore import ModbusSequentialDataBlock, ModbusSlaveContext, ModbusServerContext
from pymodbus.device import ModbusDeviceIdentification
from threading import Thread
import random
import time
import asyncio
import argparse

def update_values(ctx, id):
    register = 3
    dc_voltage = random.randint(300, 320)
    dc_current = random.randint(5, 10)
    dc_power   = dc_voltage * dc_current

    ac_voltage = 230
    ac_current = random.randint(8, 12)
    ac_power   = ac_voltage * ac_current

    error_chance = 0.05
    error_code = random.choice([0] * int((1-error_chance)*20) + [1])

    values = [dc_voltage, dc_current, dc_power,
              ac_voltage, ac_current, ac_power,
              error_code]

    ctx[id].setValues(register, 0, values)
    print(f"Inverter [{id}] values: {values}")

def start_modbus_tcp_server(ctx, slave_ids, address, port):
    def updater():
        while True:
            for id in slave_ids:
                update_values(ctx, id)
            print("-" * 50)
            time.sleep(2)  # update every 2s
        
    Thread(target=updater, daemon=True).start()

    async def run_server():
        await StartAsyncTcpServer(
            context=ctx,
            identity=identity,
            address=(address, port)
        )
    
    asyncio.run(run_server())


def create_modbus_device_identity():
    identity = ModbusDeviceIdentification()
    identity.VendorName  = 'SolarDataConcentratorSim'
    identity.ProductCode = 'SDC-001'
    identity.VendorUrl   = 'http://solar-company.com'
    identity.ProductName = 'Solar Data Concentrator Sim'
    identity.ModelName   = 'Concentrator-v1'
    identity.MajorMinorRevision = '2.1'
    
    return identity

def get_datastore():
    store = ModbusSequentialDataBlock(0, [0]*100)  # defaulting registers
    return ModbusSlaveContext(hr=store, zero_mode=True)

def create_multi_device_context(slave_ids):
    devices = {}
    for id in slave_ids:
        devices[id] = get_datastore()
    return ModbusServerContext(slaves=devices, single=False)

if __name__ == "__main__":
    parser = argparse.ArgumentParser(
        prog='mock_inverter_sim',
    )
    parser.add_argument('-c', '--count', type=int, default=3, help="Number of devices")
    parser.add_argument('-a', '--address', default="127.0.0.1", help="Server address")
    parser.add_argument('-p', '--port', type=int, default=502, help="Server port")
    args = parser.parse_args()

    device_count = args.count
    address = args.address
    port = args.port

    # print("device_count: ", device_count)
    slave_ids = random.sample(range(0, 100), device_count)

    ctx = create_multi_device_context(slave_ids)
    identity = create_modbus_device_identity()

    print(f"Starting Sim Modbus TCP server on {address}:{port}")   
    start_modbus_tcp_server(ctx, slave_ids, address, port)
