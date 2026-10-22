/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright 2026 The DiamaneOS Project
 */

#pragma once

#include <optional>
#include <string>
#include <string_view>

#include <health-impl/Health.h>

namespace aidl::android::hardware::health {

// The charge limit, as on stock Fairphone OS. With the LONG_LIFE charging policy, charging pauses
// when the level the user sees reaches kChargeLimitStop and resumes when it drops to
// kChargeLimitResume, plugged in or not, through the charger firmware's charge_disable switch.
constexpr int kChargeLimitStop = 80;
constexpr int kChargeLimitResume = 78;

// Whether charging should be paused now. |paused| is the current state, kept between the levels.
bool ShouldPauseCharging(BatteryChargingPolicy policy, int level, bool paused);

// Whether to set a pause again that the charger firmware dropped. charge_disable reading back 0
// (|firmware_paused|) means it did. So does the battery starting to charge (|charging| after not
// |was_charging|), whatever the node reads.
bool ShouldRepeatPause(std::optional<bool> firmware_paused, bool charging, bool was_charging);

// Whether the copy of the user's choice holds the limit. init writes "1" or "0".
bool LimitChosen(std::string_view copy);

// Runs on the health loop thread only: binder calls are polled on the same loop.
class ChargeLimitHealth : public Health {
  public:
    // While Android runs |choice_path| is null. The framework then sets the policy and the
    // service keeps its state in persist.vendor.charge_limit. Off-mode charging passes the copy
    // of the user's choice instead. There the service only pauses and resumes.
    ChargeLimitHealth(std::string_view instance_name, std::unique_ptr<healthd_config>&& config,
                      const char* choice_path = nullptr);

    ndk::ScopedAStatus setChargingPolicy(BatteryChargingPolicy in_value) override;
    ndk::ScopedAStatus getChargingPolicy(BatteryChargingPolicy* out) override;

  protected:
    void UpdateHealthInfo(HealthInfo* health_info) override;

  private:
    std::optional<bool> ReadPause();
    bool WriteNode(bool pause);
    void WritePause(bool pause);
    void SaveState();

    // Whether the state is kept in persist.vendor.charge_limit, while Android runs.
    const bool keep_state_;
    BatteryChargingPolicy policy_ = BatteryChargingPolicy::DEFAULT;
    // What charge_disable holds. Unknown until a write succeeds, and again when the firmware
    // dropped a pause.
    std::optional<bool> paused_;
    bool write_failed_ = false;
    // Whether charge_disable read back the last value written.
    bool read_back_ = false;
    // Whether the battery charged during a pause at the last update.
    bool charging_while_paused_ = false;
    std::string saved_state_;
};

}  // namespace aidl::android::hardware::health
