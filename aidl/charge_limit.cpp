/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright 2026 The DiamaneOS Project
 */

#define LOG_TAG "android.hardware.health-service.qti"

#include "charge_limit.h"

#include <fcntl.h>
#include <unistd.h>

#include <android-base/logging.h>
#include <android-base/properties.h>
#include <android-base/unique_fd.h>

namespace aidl::android::hardware::health {

namespace {
constexpr const char* kChargeDisablePath = "/sys/class/qcom-battery/charge_disable";
// "off", "on" or "paused". init pauses charging from it early in boot (device boot/init.qcom.rc).
constexpr const char* kStateProperty = "persist.vendor.charge_limit";
}  // namespace

bool ShouldPauseCharging(BatteryChargingPolicy policy, int level, bool paused) {
    if (policy != BatteryChargingPolicy::LONG_LIFE) return false;
    if (level >= kChargeLimitStop) return true;
    if (level <= kChargeLimitResume) return false;
    return paused;
}

ChargeLimitHealth::ChargeLimitHealth(std::string_view instance_name,
                                     std::unique_ptr<healthd_config>&& config)
    : Health(instance_name, std::move(config)) {
    // Go on from the last boot or the last instance until the framework sets the policy.
    saved_state_ = ::android::base::GetProperty(kStateProperty, "off");
    if (saved_state_ != "off") policy_ = BatteryChargingPolicy::LONG_LIFE;
    WritePause(saved_state_ == "paused");
}

ndk::ScopedAStatus ChargeLimitHealth::setChargingPolicy(BatteryChargingPolicy in_value) {
    if (in_value != BatteryChargingPolicy::DEFAULT &&
        in_value != BatteryChargingPolicy::LONG_LIFE) {
        return ndk::ScopedAStatus::fromExceptionCode(EX_ILLEGAL_ARGUMENT);
    }
    if (in_value != policy_) {
        LOG(INFO) << "Charging policy " << toString(in_value);
        policy_ = in_value;
        SaveState();
    }
    // Apply it now and report it to the framework.
    return update();
}

ndk::ScopedAStatus ChargeLimitHealth::getChargingPolicy(BatteryChargingPolicy* out) {
    *out = policy_;
    return ndk::ScopedAStatus::ok();
}

void ChargeLimitHealth::UpdateHealthInfo(HealthInfo* info) {
    bool pause = ShouldPauseCharging(policy_, info->batteryLevel, paused_.value_or(false));
    if (paused_ != pause) WritePause(pause);

    info->chargingPolicy = policy_;
    bool plugged = info->chargerAcOnline || info->chargerUsbOnline ||
                   info->chargerWirelessOnline || info->chargerDockOnline;
    if (plugged && paused_.value_or(false)) {
        // Plugged in and held: Android shows "Charging on hold to protect battery".
        info->chargingState = BatteryChargingState::LONG_LIFE;
        info->batteryStatus = BatteryStatus::NOT_CHARGING;
    } else {
        info->chargingState = BatteryChargingState::NORMAL;
    }
}

void ChargeLimitHealth::WritePause(bool pause) {
    ::android::base::unique_fd fd(
            TEMP_FAILURE_RETRY(open(kChargeDisablePath, O_WRONLY | O_CLOEXEC)));
    if (fd.ok() && TEMP_FAILURE_RETRY(write(fd.get(), pause ? "1" : "0", 1)) == 1) {
        paused_ = pause;
        write_failed_ = false;
        SaveState();
        return;
    }
    // Retried at the next update. Log once until a write works again.
    if (!write_failed_) PLOG(ERROR) << "Cannot write " << kChargeDisablePath;
    write_failed_ = true;
    paused_.reset();
}

void ChargeLimitHealth::SaveState() {
    if (!paused_.has_value()) return;
    std::string state = policy_ != BatteryChargingPolicy::LONG_LIFE ? "off"
                        : *paused_                                 ? "paused"
                                                                   : "on";
    if (state == saved_state_) return;
    if (::android::base::SetProperty(kStateProperty, state)) {
        saved_state_ = state;
    } else {
        LOG(ERROR) << "Cannot set " << kStateProperty;
    }
}

}  // namespace aidl::android::hardware::health
