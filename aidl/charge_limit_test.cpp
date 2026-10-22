/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright 2026 The DiamaneOS Project
 */

#include "charge_limit.h"

#include <gtest/gtest.h>

namespace aidl::android::hardware::health {

TEST(ChargeLimitTest, PausesAtTheLimitAndResumesBelow) {
    auto policy = BatteryChargingPolicy::LONG_LIFE;
    EXPECT_TRUE(ShouldPauseCharging(policy, 80, false));
    EXPECT_TRUE(ShouldPauseCharging(policy, 79, true));
    EXPECT_FALSE(ShouldPauseCharging(policy, 79, false));
    EXPECT_FALSE(ShouldPauseCharging(policy, 78, true));
    EXPECT_FALSE(ShouldPauseCharging(BatteryChargingPolicy::DEFAULT, 100, true));
}

TEST(ChargeLimitTest, RepeatsAPauseTheFirmwareLost) {
    // charge_disable reads 0: every time.
    EXPECT_TRUE(ShouldRepeatPause(false, false, false));
    EXPECT_TRUE(ShouldRepeatPause(false, true, true));
    // It reads 1 or cannot be read, and the battery starts charging: once.
    EXPECT_TRUE(ShouldRepeatPause(true, true, false));
    EXPECT_TRUE(ShouldRepeatPause(std::nullopt, true, false));
    EXPECT_FALSE(ShouldRepeatPause(true, true, true));
    // Held.
    EXPECT_FALSE(ShouldRepeatPause(true, false, false));
    EXPECT_FALSE(ShouldRepeatPause(std::nullopt, false, true));
}

}  // namespace aidl::android::hardware::health
