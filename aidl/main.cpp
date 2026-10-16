/*
 * Copyright (c) 2022-2025, Qualcomm Innovation Center, Inc. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#define LOG_TAG "android.hardware.health-service.qti"

#include <android-base/logging.h>
#include <android/binder_interface_utils.h>
#include <health/utils.h>
#include <health-impl/ChargerUtils.h>
#include <health-impl/Health.h>
#include <cutils/klog.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>

#ifndef __ANDROID_RECOVERY__
#include "charge_limit.h"
#endif

using aidl::android::hardware::health::HalHealthLoop;
using aidl::android::hardware::health::Health;

#if !CHARGER_FORCE_NO_UI
using aidl::android::hardware::health::charger::ChargerCallback;
using aidl::android::hardware::health::charger::ChargerModeMain;
namespace aidl::android::hardware::health {
class ChargerCallbackImpl : public ChargerCallback {
  public:
    ChargerCallbackImpl(const std::shared_ptr<Health>& service) : ChargerCallback(service) {}
    bool ChargerEnableSuspend() override { return true; }
};
} //namespace aidl::android::hardware::health
#endif

static constexpr const char* gInstanceName = "default";
static constexpr std::string_view gChargerArg{"--charger"};

constexpr char *ucsiPSYName[]{
	(char *const)"ucsi-source-psy-soc:qcom,pmic_glink:qcom,ucsi1",
	(char *const)"ucsi-source-psy-soc:qcom,pmic_glink:qcom,ucsi2"
};

#define RETRY_COUNT    100

void qti_healthd_board_init(struct healthd_config *hc)
{
    int fd;
    unsigned char retries = RETRY_COUNT;
    int ret = 0;
    unsigned char buf;
    struct stat st;

    hc->ignorePowerSupplyNames.push_back(android::String8(ucsiPSYName[0]));
    hc->ignorePowerSupplyNames.push_back(android::String8(ucsiPSYName[1]));

    /* 3rd party charger check*/
    if (stat("/sys/class/power_supply/bq27421-0/capacity", &st) >= 0)
        return;

retry:
    if (!retries) {
        KLOG_ERROR(LOG_TAG, "Cannot open battery/capacity, fd=%d\n", fd);
        return;
    }

    fd = open("/sys/class/power_supply/battery/capacity", 0440);
    if (fd >= 0) {
        KLOG_INFO(LOG_TAG, "opened battery/capacity after %d retries\n", RETRY_COUNT - retries);
        while (retries) {
            ret = read(fd, &buf, 1);
            if(ret >= 0) {
                KLOG_INFO(LOG_TAG, "Read Batt Capacity after %d retries ret : %d\n", RETRY_COUNT - retries, ret);
                close(fd);
                return;
            }

            retries--;
            usleep(100000);
        }

        KLOG_ERROR(LOG_TAG, "Failed to read Battery Capacity ret=%d\n", ret);
        close(fd);
        return;
    }

    retries--;
    usleep(100000);
    goto retry;
}

int main(int argc, char** argv) {
#ifdef __ANDROID_RECOVERY__
    android::base::InitLogging(argv, android::base::KernelLogger);
#endif
    auto config = std::make_unique<healthd_config>();
    qti_healthd_board_init(config.get());
    ::android::hardware::health::InitHealthdConfig(config.get());
    // DiamaneOS: the FP6 kernel has the state of health here, not under power_supply. Set after
    // InitHealthdConfig(), which resets the config.
    config->batteryStateOfHealthPath = android::String8("/sys/class/qcom-battery/soh");
    bool charger_mode = argc >= 2 && argv[1] == gChargerArg;
    std::shared_ptr<Health> binder;
#ifndef __ANDROID_RECOVERY__
    // DiamaneOS: the charge limit, while Android runs. Off-mode charging charges fully.
    if (!charger_mode)
        binder = ndk::SharedRefBase::make<aidl::android::hardware::health::ChargeLimitHealth>(
                gInstanceName, std::move(config));
#endif
    if (!binder) binder = ndk::SharedRefBase::make<Health>(gInstanceName, std::move(config));

    if (charger_mode) {
#if !CHARGER_FORCE_NO_UI
        KLOG_INFO(LOG_TAG, "Starting charger mode with UI.");
        auto charger_callback = std::make_shared<aidl::android::hardware::health::ChargerCallbackImpl>(binder);
        return ChargerModeMain(binder, charger_callback);
#endif
        KLOG_INFO(LOG_TAG, "Starting charger mode without UI.");
    } else {
        KLOG_INFO(LOG_TAG, "Starting health HAL.");
    }

    auto hal_health_loop = std::make_shared<HalHealthLoop>(binder, binder);
    return hal_health_loop->StartLoop();
}
