# Unit test build for OFM-IO-Homecontrol protocol layer
# Requires g++ (MinGW on Windows, GCC on Linux/macOS)
#
# Usage:
#   make          - build and run tests
#   make build    - build only
#   make clean    - remove build artifacts

CXX ?= clang++
CXXFLAGS = -std=c++17 -Wall -Wextra -DTEST_NATIVE -I../src -I.
PROTOCOL_SRCS = test_protocol.cpp ../src/controller/IoHomeController.cpp ../src/protocol/IoHomeCrypto.cpp ../src/protocol/IoHomeFrame.cpp ../src/IoHomeRemoteMap.cpp
SX1262_ERROR_SRCS = test_sx1262_device_errors.cpp ../src/radio/SX1262DeviceErrors.cpp
SX1262_PHY_SRCS = test_sx1262_iohome_phy.cpp ../src/radio/SX1262IoHomePhy.cpp ../src/protocol/IoHomeCrypto.cpp
TARGETS = test_protocol test_sx1262_device_errors test_sx1262_iohome_phy

.PHONY: all build clean run

all: build run

build: $(TARGETS)

test_protocol: $(PROTOCOL_SRCS) mbedtls/aes.h
	$(CXX) $(CXXFLAGS) -o $@ $(PROTOCOL_SRCS)

test_sx1262_device_errors: $(SX1262_ERROR_SRCS)
	$(CXX) $(CXXFLAGS) -o $@ $(SX1262_ERROR_SRCS)

test_sx1262_iohome_phy: $(SX1262_PHY_SRCS)
	$(CXX) $(CXXFLAGS) -o $@ $(SX1262_PHY_SRCS)

run: $(TARGETS)
	./test_protocol
	./test_sx1262_device_errors
	./test_sx1262_iohome_phy

clean:
	rm -f $(TARGETS) $(addsuffix .exe,$(TARGETS))
