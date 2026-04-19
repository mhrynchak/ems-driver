# Build and Run

## `Dashboard`

Install deps:

```bash
cd dashboard-new
npm install
```

Run:

```bash
npm run dev
```

## `Simulator`

Install deps:

```bash
python3 -m pip install pymodbus
```

Run:

```bash
cd ems-simulator
python3 mock_inverter_sim_unified.py --scenario scenario.example.json
```

Use `scenario.multi.example.json` if you want to test with multiple Modbus TCP endpoints.
Or `scenario.scale.example.json` if you want to perform some scale testing with a lot of devices.

## `Modbus driver`

Install deps:

```bash
sudo apt-get update
sudo apt-get install -y build-essential libmodbus-dev libsqlite3-dev libcurl4-openssl-dev
```

```bash
cd ems-driver
make
./ems-mvp
```

Use `config.multi.example.json` if you want to test with multiple Modbus TCP endpoints.

