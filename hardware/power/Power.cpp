/*
 * Copyright (C) 2020 The LineageOS Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#define LOG_TAG "android.hardware.power@1.0-service.universal8890"

#include "Power.h"
#include <android-base/logging.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include "samsung_lights.h"
#include "samsung_power.h"

namespace android {
namespace hardware {
namespace power {
namespace V1_0 {
namespace implementation {

/*
 * Write value to path and close file.
 */
template <typename T>
static void set(const std::string& path, const T& value) {
    std::ofstream file(path);
    file << value << std::endl;
}

template <typename T>
static T get(const std::string& path, const T& def) {
    std::ifstream file(path);
    T result;

    file >> result;
    return file.fail() ? def : result;
}

Return<void> Power::setInteractive(bool interactive) {
    if (!initialized) {
        initialize();
    }

    if (!interactive) {
        int32_t panel_brightness = get(PANEL_BRIGHTNESS_NODE, -1);

        if (panel_brightness > 0) {
            LOG(VERBOSE) << "Moving to non-interactive state, but screen is still on,"
                         << "not disabling input devices";
            goto out;
        }
    }

out:
    for (const std::string& interactivePath : cpuInteractivePaths) {
        set(interactivePath + "/io_is_busy", interactive ? "1" : "0");
    }

    set("/sys/power/cpuhotplug/max_online_cpu", interactive ? "8" : "6");

    return Void();
}

Return<void> Power::powerHint(PowerHint hint, int32_t data) {
    if (!initialized) {
        initialize();
    }

    switch (hint) {
        case PowerHint::INTERACTION:
        case PowerHint::LAUNCH:
            sendBoostpulse();
            break;
        case PowerHint::LOW_POWER:
            setProfile(data ? PowerProfile::POWER_SAVE : PowerProfile::BALANCED);
            break;
        default:
            break;
    }
    return Void();
}

Return<void> Power::setFeature(Feature feature, bool activate) {
    if (!initialized) {
        initialize();
    }

    if (feature == Feature::POWER_FEATURE_DOUBLE_TAP_TO_WAKE) {
        if (activate) {
            std::string screen_size = get<std::string>("/sys/class/graphics/fb0/virtual_size", "");
            if (!screen_size.empty()) {
                set("/sys/class/sec/tsp/cmd", "aod_enable,1");
                set("/sys/class/sec/tsp/cmd", "set_aod_rect," + screen_size + ",0,0");
            }
        } else {
            set("/sys/class/sec/tsp/cmd", "aod_enable,0");
            set("/sys/class/sec/tsp/cmd", "set_aod_rect,0,0,0,0");
        }
    }

    return Void();
}

Return<void> Power::getPlatformLowPowerStats(getPlatformLowPowerStats_cb _hidl_cb) {
    _hidl_cb({}, Status::SUCCESS);
    return Void();
}

void Power::initialize() {
    findInputNodes();

    setProfile(PowerProfile::BALANCED);

    for (const std::string& interactivePath : cpuInteractivePaths) {
        hispeed_freqs.emplace_back(get<std::string>(interactivePath + "/hispeed_freq", ""));
    }

    for (const std::string& sysfsPath : cpuSysfsPaths) {
        max_freqs.emplace_back(get<std::string>(sysfsPath + "/cpufreq/scaling_max_freq", ""));
    }

    set(cpuInteractivePaths.at(0) + "/timer_rate", "20000");
    set(cpuInteractivePaths.at(0) + "/timer_slack", "20000");
    set(cpuInteractivePaths.at(0) + "/min_sample_time", "40000");
    set(cpuInteractivePaths.at(0) + "/boostpulse_duration", "40000");
    set(cpuInteractivePaths.at(1) + "/timer_rate", "20000");
    set(cpuInteractivePaths.at(1) + "/timer_slack", "20000");
    set(cpuInteractivePaths.at(1) + "/min_sample_time", "40000");
    set(cpuInteractivePaths.at(1) + "/boostpulse_duration", "40000");

    initialized = true;
}

void Power::findInputNodes() {
    std::error_code ec;
    for (auto& de : std::filesystem::directory_iterator("/sys/class/input/", ec)) {
        /* we are only interested in the input devices that we can access */
        if (ec || de.path().string().find("/sys/class/input/input") == std::string::npos) {
            continue;
        }
    }
}

void Power::setProfile(PowerProfile profile) {
    if (current_profile == profile) {
        return;
    }

    switch (profile) {
        case PowerProfile::POWER_SAVE:
                set(cpuInteractivePaths.at(0) + "/hispeed_freq", INTERACTIVE_LOW_L_HISPEED_FREQ);
                set(cpuInteractivePaths.at(0) + "/go_hispeed_load", INTERACTIVE_LOW_L_GO_HISPEED_LOAD);
                set(cpuInteractivePaths.at(0) + "/target_loads", INTERACTIVE_LOW_L_TARGET_LOADS);
                set(cpuInteractivePaths.at(0) + "/above_hispeed_delay", INTERACTIVE_LOW_L_ABOVE_HISPEED_DELAY);
                set(cpuInteractivePaths.at(1) + "/hispeed_freq", INTERACTIVE_LOW_B_HISPEED_FREQ);
                set(cpuInteractivePaths.at(1) + "/go_hispeed_load", INTERACTIVE_LOW_B_GO_HISPEED_LOAD);
                set(cpuInteractivePaths.at(1) + "/target_loads", INTERACTIVE_LOW_B_TARGET_LOADS);
                set(cpuInteractivePaths.at(1) + "/above_hispeed_delay", INTERACTIVE_LOW_B_ABOVE_HISPEED_DELAY);
            break;
        case PowerProfile::BALANCED:
                set(cpuInteractivePaths.at(0) + "/hispeed_freq", INTERACTIVE_NORMAL_L_HISPEED_FREQ);
                set(cpuInteractivePaths.at(0) + "/go_hispeed_load", INTERACTIVE_NORMAL_L_GO_HISPEED_LOAD);
                set(cpuInteractivePaths.at(0) + "/target_loads", INTERACTIVE_NORMAL_L_TARGET_LOADS);
                set(cpuInteractivePaths.at(0) + "/above_hispeed_delay", INTERACTIVE_NORMAL_L_ABOVE_HISPEED_DELAY);
                set(cpuInteractivePaths.at(1) + "/hispeed_freq", INTERACTIVE_NORMAL_B_HISPEED_FREQ);
                set(cpuInteractivePaths.at(1) + "/go_hispeed_load", INTERACTIVE_NORMAL_B_GO_HISPEED_LOAD);
                set(cpuInteractivePaths.at(1) + "/target_loads", INTERACTIVE_NORMAL_B_TARGET_LOADS);
                set(cpuInteractivePaths.at(1) + "/above_hispeed_delay", INTERACTIVE_NORMAL_B_ABOVE_HISPEED_DELAY);
                break;
        case PowerProfile::HIGH_PERFORMANCE:
                set(cpuInteractivePaths.at(0) + "/hispeed_freq", INTERACTIVE_HIGH_L_HISPEED_FREQ);
                set(cpuInteractivePaths.at(0) + "/go_hispeed_load", INTERACTIVE_HIGH_L_GO_HISPEED_LOAD);
                set(cpuInteractivePaths.at(0) + "/target_loads", INTERACTIVE_HIGH_L_TARGET_LOADS);
                set(cpuInteractivePaths.at(0) + "/above_hispeed_delay", INTERACTIVE_HIGH_L_ABOVE_HISPEED_DELAY);
                set(cpuInteractivePaths.at(1) + "/hispeed_freq", INTERACTIVE_HIGH_B_HISPEED_FREQ);
                set(cpuInteractivePaths.at(1) + "/go_hispeed_load", INTERACTIVE_HIGH_B_GO_HISPEED_LOAD);
                set(cpuInteractivePaths.at(1) + "/target_loads", INTERACTIVE_HIGH_B_TARGET_LOADS);
                set(cpuInteractivePaths.at(1) + "/above_hispeed_delay", INTERACTIVE_HIGH_B_ABOVE_HISPEED_DELAY);
            break;
        default:
            break;
    }

    current_profile = profile;
}

void Power::sendBoostpulse() {
    // the boostpulse node is only valid for the LITTLE cluster
    set(cpuInteractivePaths.front() + "/boostpulse", "1");
}

void Power::sendBoost(int duration_us) {
    set(cpuInteractivePaths.front() + "/boost", "1");

    usleep(duration_us);

    set(cpuInteractivePaths.front() + "/boost", "0");
}

}  // namespace implementation
}  // namespace V1_0
}  // namespace power
}  // namespace hardware
}  // namespace android
