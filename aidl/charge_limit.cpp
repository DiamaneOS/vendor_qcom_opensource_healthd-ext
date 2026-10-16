/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright 2026 The DiamaneOS Project
 */

#define LOG_TAG "android.hardware.health-service.qti"

#include "charge_limit.h"

#include <fcntl.h>
#include <unistd.h>

#include <android-base/logging.h>
#include <android-base/unique_fd.h>

namespace aidl::android::hardware::health {

namespace {
constexpr const char* kChargeDisablePath = "/sys/class/qcom-battery/charge_disable";
}  // namespace

bool ShouldPauseCharging(BatteryChargingPolicy policy, bool plugged, int level, bool paused) {
    if (policy != BatteryChargingPolicy::LONG_LIFE || !plugged) return false;
    if (level >= kChargeLimitStop) return true;
    if (level <= kChargeLimitResume) return false;
    return paused;
}

ChargeLimitHealth::ChargeLimitHealth(std::string_view instance_name,
                                     std::unique_ptr<healthd_config>&& config)
    : Health(instance_name, std::move(config)) {
    // A previous instance may have left charging paused.
    WritePause(false);
}

ndk::ScopedAStatus ChargeLimitHealth::setChargingPolicy(BatteryChargingPolicy in_value) {
    if (in_value != BatteryChargingPolicy::DEFAULT &&
        in_value != BatteryChargingPolicy::LONG_LIFE) {
        return ndk::ScopedAStatus::fromExceptionCode(EX_ILLEGAL_ARGUMENT);
    }
    if (in_value != policy_) {
        LOG(INFO) << "Charging policy " << toString(in_value);
        policy_ = in_value;
    }
    // Apply it now and report it to the framework.
    return update();
}

ndk::ScopedAStatus ChargeLimitHealth::getChargingPolicy(BatteryChargingPolicy* out) {
    *out = policy_;
    return ndk::ScopedAStatus::ok();
}

void ChargeLimitHealth::UpdateHealthInfo(HealthInfo* info) {
    bool plugged = info->chargerAcOnline || info->chargerUsbOnline ||
                   info->chargerWirelessOnline || info->chargerDockOnline;
    bool pause = ShouldPauseCharging(policy_, plugged, info->batteryLevel, paused_.value_or(false));
    if (paused_ != pause) WritePause(pause);

    info->chargingPolicy = policy_;
    if (paused_.value_or(false)) {
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
        return;
    }
    // Retried at the next update. Log once until a write works again.
    if (!write_failed_) PLOG(ERROR) << "Cannot write " << kChargeDisablePath;
    write_failed_ = true;
    paused_.reset();
}

}  // namespace aidl::android::hardware::health
