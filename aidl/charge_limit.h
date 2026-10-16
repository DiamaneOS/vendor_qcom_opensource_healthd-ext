/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright 2026 The DiamaneOS Project
 */

#pragma once

#include <optional>

#include <health-impl/Health.h>

namespace aidl::android::hardware::health {

// The charge limit. With the LONG_LIFE charging policy, charging pauses when the level the user
// sees reaches kChargeLimitStop and resumes at kChargeLimitResume, through the charger
// firmware's charge_disable switch. These are stock Fairphone's levels.
constexpr int kChargeLimitStop = 80;
constexpr int kChargeLimitResume = 78;

// Whether charging should be paused now. |paused| is the current state, kept between the levels.
bool ShouldPauseCharging(BatteryChargingPolicy policy, bool plugged, int level, bool paused);

// Runs on the health loop thread only: binder calls are polled on the same loop.
class ChargeLimitHealth : public Health {
  public:
    ChargeLimitHealth(std::string_view instance_name, std::unique_ptr<healthd_config>&& config);

    ndk::ScopedAStatus setChargingPolicy(BatteryChargingPolicy in_value) override;
    ndk::ScopedAStatus getChargingPolicy(BatteryChargingPolicy* out) override;

  protected:
    void UpdateHealthInfo(HealthInfo* health_info) override;

  private:
    void WritePause(bool pause);

    BatteryChargingPolicy policy_ = BatteryChargingPolicy::DEFAULT;
    // What charge_disable holds. Unknown until a write succeeds.
    std::optional<bool> paused_;
    bool write_failed_ = false;
};

}  // namespace aidl::android::hardware::health
