#include "buddy_brain/emotion_trigger.hpp"

#include <algorithm>

EmotionTrigger::EmotionTrigger(const Config& cfg) : cfg_(cfg) {}

bool EmotionTrigger::update(const std::string& emotion, float confidence) {
    bool is_negative = std::find(cfg_.negative_emotions.begin(), cfg_.negative_emotions.end(), emotion) !=
                       cfg_.negative_emotions.end();

    auto now = std::chrono::steady_clock::now();

    if (is_negative && confidence >= cfg_.confidence_threshold) {
        if (!tracking_) {
            tracking_ = true;
            negative_since_ = now;
        }
        auto elapsed = std::chrono::duration<double>(now - negative_since_).count();
        auto cooldown = std::chrono::duration<double>(now - last_trigger_).count();

        if (elapsed >= cfg_.duration_seconds && cooldown >= cfg_.cooldown_seconds) {
            last_trigger_ = now;
            tracking_ = false;
            return true;
        }
    } else {
        tracking_ = false;
    }

    return false;
}

void EmotionTrigger::reset() {
    tracking_ = false;
}
