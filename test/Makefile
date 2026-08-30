# Unit test build for OFM-IO-Homecontrol protocol layer
# Requires g++ (MinGW on Windows, GCC on Linux/macOS)
#
# Usage:
#   make suite             - build every native suite
#   make check-protocol    - run protocol/unit checks
#   make check-controller  - run controller checks
#   make check-exchange    - run pairing/key-exchange checks
#   make check-radio       - run radio checks
#   make corpus            - run only the golden RF corpus
#   make clean             - remove isolated build artifacts

CXX ?= clang++
CXXFLAGS = -std=c++17 -Wall -Wextra -DTEST_NATIVE -I../src -I.
BUILD_DIR = build
PROTOCOL_SRCS = test_protocol.cpp test_suite_main.cpp ../src/controller/IoHomeController.cpp ../src/protocol/IoHomeCrypto.cpp ../src/protocol/IoHomeFrame.cpp ../src/IoHomeRemoteMap.cpp
SX1262_ERROR_SRCS = test_sx1262_device_errors.cpp ../src/radio/SX1262DeviceErrors.cpp
SX1262_PHY_SRCS = test_sx1262_iohome_phy.cpp ../src/radio/SX1262IoHomePhy.cpp ../src/protocol/IoHomeCrypto.cpp
PROTOCOL_TARGET = $(BUILD_DIR)/test_suite
SX1262_ERROR_TARGET = $(BUILD_DIR)/test_sx1262_device_errors
SX1262_PHY_TARGET = $(BUILD_DIR)/test_sx1262_iohome_phy
TARGETS = $(PROTOCOL_TARGET) $(SX1262_ERROR_TARGET) $(SX1262_PHY_TARGET)

.PHONY: all build suite clean run check-protocol check-controller check-exchange check-radio corpus list

all: run

build: $(TARGETS)

suite: build

$(BUILD_DIR):
	mkdir -p $@

$(PROTOCOL_TARGET): $(PROTOCOL_SRCS) mbedtls/aes.h test_registry.h | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) -o $@ $(PROTOCOL_SRCS)

$(SX1262_ERROR_TARGET): $(SX1262_ERROR_SRCS) | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) -o $@ $(SX1262_ERROR_SRCS)

$(SX1262_PHY_TARGET): $(SX1262_PHY_SRCS) | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) -o $@ $(SX1262_PHY_SRCS)

run: check-protocol check-controller check-exchange check-radio

check-protocol: $(PROTOCOL_TARGET)
	./$(PROTOCOL_TARGET) --suite protocol

check-controller: $(PROTOCOL_TARGET)
	./$(PROTOCOL_TARGET) --suite controller

check-exchange: $(PROTOCOL_TARGET)
	./$(PROTOCOL_TARGET) --suite exchange

check-radio: $(SX1262_ERROR_TARGET) $(SX1262_PHY_TARGET)
	./$(SX1262_ERROR_TARGET)
	./$(SX1262_PHY_TARGET)

corpus: $(PROTOCOL_TARGET)
	./$(PROTOCOL_TARGET) --suite protocol --filter golden_rf_corpus

list: $(PROTOCOL_TARGET)
	./$(PROTOCOL_TARGET) --list

clean:
	rm -rf $(BUILD_DIR)
