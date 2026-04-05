# Simple Makefile for EMS C++ Application

# Project settings
TARGET = ems-mvp
CXX = g++
CXXFLAGS = -std=c++17 -Wall -O2
LIBS = -lmodbus -lsqlite3 -lcurl -lpthread

# Source files
SOURCES = Main.cpp core/Database.cpp core/CacheManager.cpp core/SyncService.cpp core/AnalyticsService.cpp core/ModbusScanner.cpp

# Default target
all: $(TARGET)

# Build executable
$(TARGET): $(SOURCES)
	@echo "Building $(TARGET)..."
	$(CXX) $(CXXFLAGS) $(SOURCES) -o $(TARGET) $(LIBS)
	@echo "Build complete!"

# Debug build
debug: CXXFLAGS = -std=c++17 -Wall -g -DDEBUG
debug: $(TARGET)
	@echo "Debug build complete!"

# Run the application
run: $(TARGET)
	./$(TARGET)

# Clean build files
clean:
	rm -f $(TARGET)
	@echo "Clean complete!"

# Install dependencies (Ubuntu/Debian)
deps:
	sudo apt-get update
	sudo apt-get install -y libmodbus-dev libsqlite3-dev libcurl4-openssl-dev

# Help
help:
	@echo "Available commands:"
	@echo "  make        - Build the project"
	@echo "  make debug  - Build debug version"
	@echo "  make run    - Build and run"
	@echo "  make clean  - Remove build files"
	@echo "  make deps   - Install dependencies"
	@echo "  make help   - Show this help"

.PHONY: all debug run clean deps help