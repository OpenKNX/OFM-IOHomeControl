#pragma once
#include <cstdint>

namespace IoHomeDiagnosticStatus {
enum class Model : uint8_t { Alfea=1, Tahual, Guelma, Hekla, ExplorerV4, AdelisMixed };
enum class Signal : uint8_t { EnergyWh, HotWaterWh, OutdoorTenths, PowerW, Occupancy,
                              OccupancyDetection, Boost, Absence, TargetTenths, CentralHeating };
}
