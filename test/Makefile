# Unit test build for OFM-IO-Homecontrol protocol layer
# Requires g++ (MinGW on Windows, GCC on Linux/macOS)
#
# Usage:
#   make suite             - build every native suite
#   make check-protocol    - run protocol/unit checks
#   make check-controller  - run controller checks
#   make check-exchange    - run pairing/key-exchange checks
#   make check-radio       - run radio checks
#   make check-ui          - verify ETS channel-selection conventions
#   make corpus            - run only the golden RF corpus
#   make clean             - remove isolated build artifacts

CXX ?= clang++
PYTHON ?= python3
CXXFLAGS = -std=c++17 -Wall -Wextra -DTEST_NATIVE -I../src -I.
BUILD_DIR = build
PROTOCOL_SRCS = test_protocol.cpp test_suite_main.cpp ../src/controller/IoHomeController.cpp ../src/protocol/IoHomeCrypto.cpp ../src/protocol/IoHomeFrame.cpp ../src/protocol/IoHomeProfileRegistry.cpp ../src/IoHomeRemoteMap.cpp
SX1262_ERROR_SRCS = test_sx1262_device_errors.cpp ../src/radio/SX1262DeviceErrors.cpp
SX1262_PHY_SRCS = test_sx1262_iohome_phy.cpp ../src/radio/SX1262IoHomePhy.cpp ../src/protocol/IoHomeCrypto.cpp
PROTOCOL_TARGET = $(BUILD_DIR)/test_suite
SX1262_ERROR_TARGET = $(BUILD_DIR)/test_sx1262_device_errors
SX1262_PHY_TARGET = $(BUILD_DIR)/test_sx1262_iohome_phy
TARGETS = $(PROTOCOL_TARGET) $(SX1262_ERROR_TARGET) $(SX1262_PHY_TARGET)

.PHONY: all suite clean run check-protocol check-controller check-exchange check-radio check-ui corpus list check-ets

all: run

suite: $(TARGETS)

$(BUILD_DIR):
	mkdir -p $@

CORPUS_GENERATED = build/laberning_captures.h

$(CORPUS_GENERATED): $(shell find corpus -name "*.yaml") corpus/laberning-manifest.json $(wildcard corpus/laberning_tools/*.py) ../tools/laberning_corpus.py
	$(PYTHON) ../tools/laberning_corpus.py --generate $@

STEFAN_GENERATED = build/stefan_dual_controller.h
$(STEFAN_GENERATED): corpus/stefan-dual-controller-20261027.json ../tools/generate_stefan_vectors.py
	$(PYTHON) ../tools/generate_stefan_vectors.py $@

$(PROTOCOL_TARGET): $(STEFAN_GENERATED) $(CORPUS_GENERATED) $(wildcard corpus/*.h ../src/protocol/*.h ../src/controller/*.h) $(PROTOCOL_SRCS) mbedtls/aes.h test_registry.h ../src/radio/IoHomeTransmitIntent.h ../src/radio/IoHomeRfAccessPolicy.h ../src/radio/IoHomeDutyBudget.h ../src/protocol/IoHomeBatteryDiagnostics.h ../src/radio/RadioTestStub.h | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) -o $@ $(PROTOCOL_SRCS)

$(SX1262_ERROR_TARGET): $(SX1262_ERROR_SRCS) | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) -o $@ $(SX1262_ERROR_SRCS)

$(SX1262_PHY_TARGET): $(SX1262_PHY_SRCS) | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) -o $@ $(SX1262_PHY_SRCS)

run: check-rf-build-modes check-dual-controller-tools check-remaining-time-evidence check-remaining-time-mutations check-discovery-evidence check-captured-motion-mutations check-original-captures check-inbound-replies check-release-evidence check-heating-recovery check-heating-publication check-rf-access-policy check-module-version check-wake-production check-wake-trials check-protocol check-controller check-exchange check-radio check-ui check-battery-publication check-standalone check-battery-diagnostics check-key-import-identity

check-protocol: $(PROTOCOL_TARGET)
	./$(PROTOCOL_TARGET) --suite protocol

check-controller: $(PROTOCOL_TARGET)
	./$(PROTOCOL_TARGET) --suite controller

check-exchange: $(PROTOCOL_TARGET)
	./$(PROTOCOL_TARGET) --suite exchange

check-radio: $(SX1262_ERROR_TARGET) $(SX1262_PHY_TARGET) $(BUILD_DIR)/test_sx1276_adapter $(BUILD_DIR)/test_sx1276_timing $(BUILD_DIR)/test_sx1276_timing_disabled
	./$(SX1262_ERROR_TARGET)
	./$(SX1262_PHY_TARGET)
	./$(BUILD_DIR)/test_sx1276_adapter
	./$(BUILD_DIR)/test_sx1276_timing
	./$(BUILD_DIR)/test_sx1276_timing_disabled
	$(PYTHON) test_radio_timing_analysis.py
	$(PYTHON) test_sx1262_wait_busy.py

check-ui:
	$(PYTHON) ../tools/generate_channel_selections.py --check
	$(PYTHON) test_channel_ui.py

corpus: check-original-captures $(PROTOCOL_TARGET)
	./$(PROTOCOL_TARGET) --suite protocol --filter golden_rf_corpus

list: $(PROTOCOL_TARGET)
	./$(PROTOCOL_TARGET) --list

clean:
	rm -rf $(BUILD_DIR)

# Needs Node.js or the Python quickjs package; executes the actual ETS script.
check-ets:
	$(PYTHON) test_ets_recognition.py

SX1276_TARGET = $(BUILD_DIR)/test_sx1276_adapter
$(SX1276_TARGET): test_sx1276_adapter.cpp ../src/radio/RadioSX1276.cpp ../src/radio/RadioSX1276.h | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) -o $@ test_sx1276_adapter.cpp ../src/radio/RadioSX1276.cpp

$(BUILD_DIR)/test_sx1276_timing: test_sx1276_timing.cpp ../src/radio/RadioSX1276.cpp ../src/radio/RadioSX1276.h ../src/radio/IoHomeRadioTiming.h | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) -pthread -DIOHC_SX1276_TIMING=1 -o $@ test_sx1276_timing.cpp ../src/radio/RadioSX1276.cpp

$(BUILD_DIR)/test_sx1276_timing_disabled: test_sx1276_timing.cpp ../src/radio/RadioSX1276.cpp ../src/radio/RadioSX1276.h ../src/radio/IoHomeRadioTiming.h | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) -pthread -o $@ test_sx1276_timing.cpp ../src/radio/RadioSX1276.cpp

ESP32_STORAGE_TARGET = $(BUILD_DIR)/test_esp32_storage
STORAGE_TARGET = $(BUILD_DIR)/test_rp2040_storage
$(STORAGE_TARGET): test_rp2040_storage.cpp fsstub/LittleFS.h ../src/protocol/IoHomeStorageBackend.h ../src/protocol/IoHomeCheckedJournal.h | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) -Ifsstub -o $@ test_rp2040_storage.cpp
.PHONY: check-storage
check-storage: $(STORAGE_TARGET) $(ESP32_STORAGE_TARGET)
	$(STORAGE_TARGET)
	$(ESP32_STORAGE_TARGET)

$(ESP32_STORAGE_TARGET): test_esp32_storage.cpp nvsstub/nvs.h ../src/protocol/IoHomeEsp32Storage.h | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) -Invsstub -o $@ test_esp32_storage.cpp

.PHONY: check-battery-publication
check-battery-publication:
	$(PYTHON) test_battery_publication.py

.PHONY: check-standalone
check-standalone:
	$(PYTHON) test_standalone_diag.py

.PHONY: check-battery-diagnostics
check-battery-diagnostics:
	$(PYTHON) test_battery_diagnostics.py

.PHONY: check-key-import-identity
check-key-import-identity:
	$(PYTHON) test_key_import_identity.py

.PHONY: check-wake-production check-wake-trials
check-wake-production:
	$(PYTHON) test_wake_production.py
check-wake-trials:
	$(PYTHON) test_wake_trials.py

.PHONY: check-module-version
check-module-version:
	$(PYTHON) test_module_version.py

.PHONY: check-rf-access-policy
check-rf-access-policy:
	$(PYTHON) test_rf_access_policy.py

.PHONY: check-heating-publication
check-heating-publication:
	$(PYTHON) test_heating_publication.py

.PHONY: check-heating-recovery
check-heating-recovery:
	$(PYTHON) test_heating_recovery.py

.PHONY: check-release-evidence
check-release-evidence:
	$(PYTHON) test_release_evidence.py

.PHONY: check-inbound-replies
check-inbound-replies: $(PROTOCOL_TARGET)
	CXX="$(CXX)" $(PYTHON) test_inbound_reply_mutation.py

.PHONY: check-original-captures
check-original-captures: $(CORPUS_GENERATED)
	$(PYTHON) ../tools/laberning_corpus.py
	$(PYTHON) test_laberning_corpus.py

.PHONY: check-captured-motion-mutations
check-captured-motion-mutations: $(PROTOCOL_TARGET)
	CXX="$(CXX)" $(PYTHON) test_original_capture_mutation.py

.PHONY: check-discovery-evidence
check-discovery-evidence:
	$(PYTHON) test_discovery_evidence.py
	$(PYTHON) test_diagnostic_commands.py

.PHONY: check-remaining-time-mutations
check-remaining-time-mutations:
	CXX="$(CXX)" $(PYTHON) test_remaining_time_mutation.py

.PHONY: check-remaining-time-evidence
check-remaining-time-evidence:
	$(PYTHON) ../tools/audit_remaining_time_corpus.py --check ../docs/remaining-time-corpus-audit-2026-10-27.json

.PHONY: check-dual-controller-tools
check-dual-controller-tools:
	$(PYTHON) test_dual_controller_tools.py

# Run identical real-controller admission/protocol cases with both build modes.
DEBUG_PROTOCOL_TARGET = $(BUILD_DIR)/test_suite_debug
$(DEBUG_PROTOCOL_TARGET): $(STEFAN_GENERATED) $(CORPUS_GENERATED) $(wildcard corpus/*.h ../src/protocol/*.h ../src/controller/*.h ../src/radio/*.h) $(PROTOCOL_SRCS) mbedtls/aes.h test_registry.h | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) -DOPENKNX_DEBUG -o $@ $(PROTOCOL_SRCS)

.PHONY: check-rf-build-modes
check-rf-build-modes: $(PROTOCOL_TARGET) $(DEBUG_PROTOCOL_TARGET)
	./$(PROTOCOL_TARGET) --filter controller_rf_build_mode_
	./$(DEBUG_PROTOCOL_TARGET) --filter controller_rf_build_mode_
	./$(DEBUG_PROTOCOL_TARGET) --filter controller_verify_direct_2f_is_not_authenticated_success
	./$(DEBUG_PROTOCOL_TARGET) --filter controller_inbound_status_proof_binds_mac_identity_frequency_state_and_age
	./$(DEBUG_PROTOCOL_TARGET) --filter controller_inbound_reply_preparation_busy_keeps_original_25ms_deadline
