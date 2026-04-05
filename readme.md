compile:

g++ *.cpp -o ems-mvp -lmodbus -lsqlite3

Device connectivity:
connecting: device has not had its first successful read yet
warning: device was reachable before, but now has intermittent read failures, reports an inverter fault, or is reachable-but-inactive
offline: device hit 3 consecutive read failures
