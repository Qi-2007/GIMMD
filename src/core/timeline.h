#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <stdexcept>

namespace gimmd {
enum class PlaybackState { Ready, Playing, Paused, Ended };
struct ClockFrame {
    double seconds = 0;
    uint64_t epoch = 0;
    PlaybackState state = PlaybackState::Ready;
};

// Transport commands wake a held pose at normal cadence. Keep checking the
// applied pose periodically even after its secondary motion has settled.
inline bool wait_for_held_pose(uint64_t since_batch_ms,bool settled,bool command_pending) {
    return settled&&!command_pending&&since_batch_ms<125;
}

// Caller supplies monotonic seconds. All consumers use the returned snapshot.
class Timeline {
public:
    explicit Timeline(double duration) : duration_(duration) {
        if (!std::isfinite(duration) || duration < 0)
            throw std::invalid_argument("invalid duration");
    }
    ClockFrame sample(double now) {
        validate(now);
        double t = anchor_seconds_;
        if (state_ == PlaybackState::Playing) {
            t += std::max(0.0, now - anchor_clock_);
            if (duration_ > 0 && t >= duration_) {
                if (loop_) {
                    t = std::fmod(t, duration_);
                    ++epoch_;
                    anchor_seconds_ = t;
                    anchor_clock_ = now;
                } else {
                    t = duration_;
                    anchor_seconds_ = t;
                    state_ = PlaybackState::Ended;
                }
            }
        }
        return {t, epoch_, state_};
    }
    void play(double now) {
        const auto f = sample(now);
        if (f.state == PlaybackState::Playing) return;
        if (duration_ == 0) { state_ = PlaybackState::Ended; return; }
        anchor_seconds_ = f.state == PlaybackState::Ended ? 0 : f.seconds;
        if (f.state == PlaybackState::Ended) ++epoch_;
        anchor_clock_ = now;
        state_ = PlaybackState::Playing;
    }
    void pause(double now) {
        const auto f = sample(now);
        anchor_seconds_ = f.seconds;
        if (f.state == PlaybackState::Playing) state_ = PlaybackState::Paused;
    }
    // Holding a naturally reached end is continuous, unlike an explicit seek.
    void hold_end(double now) {
        const auto f=sample(now);
        if(f.state==PlaybackState::Ended){anchor_seconds_=f.seconds;anchor_clock_=now;state_=PlaybackState::Paused;}
    }
    void seek(double seconds, double now) {
        validate(now);
        if (!std::isfinite(seconds)) throw std::invalid_argument("invalid seek");
        anchor_seconds_ = std::clamp(seconds, 0.0, duration_);
        anchor_clock_ = now;
        ++epoch_;
        if (state_ == PlaybackState::Ended) state_ = PlaybackState::Paused;
    }
    void stop(double now) {
        seek(0, now);
        state_ = PlaybackState::Ready;
    }
    void set_loop(bool enabled) { loop_ = enabled; }
private:
    static void validate(double now) {
        if (!std::isfinite(now)) throw std::invalid_argument("invalid clock");
    }
    double duration_, anchor_seconds_ = 0, anchor_clock_ = 0;
    uint64_t epoch_ = 0;
    PlaybackState state_ = PlaybackState::Ready;
    bool loop_ = false;
};
}

