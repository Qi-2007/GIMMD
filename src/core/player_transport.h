#pragma once
#include "timeline.h"
#include <sstream>
#include <string>
namespace gimmd {
enum class PlayerOp : uint8_t { None, Play, Pause, Seek, LoopOn, LoopOff, Stop, Restore, Exit, CameraOn, CameraOff };
// Single atomic mailbox: operation, milliseconds and monotonic sequence.
inline uint64_t parse_player_command(const std::string& text) {
    std::istringstream in(text); uint64_t sequence = 0; std::string name, extra;
    double seconds = 0;
    if (!(in >> sequence >> name) || !sequence || sequence >= (1ull << 28)) return 0;
    PlayerOp op = PlayerOp::None;
    if (name == "play") op = PlayerOp::Play;
    else if (name == "pause") op = PlayerOp::Pause;
    else if (name == "seek") {
        op = PlayerOp::Seek;
        if (!(in >> seconds) || !std::isfinite(seconds) || seconds < 0 || seconds > 86400) return 0;
    } else if (name == "loop-on") op = PlayerOp::LoopOn;
    else if (name == "loop-off") op = PlayerOp::LoopOff;
    else if (name == "stop") op = PlayerOp::Stop;
    else if (name == "restore") op = PlayerOp::Restore;
    else if (name == "exit") op = PlayerOp::Exit;
    else if (name == "camera-on") op = PlayerOp::CameraOn;
    else if (name == "camera-off") op = PlayerOp::CameraOff;
    else return 0;
    if (in >> extra) return 0;
    return (sequence << 36) | (static_cast<uint64_t>(seconds * 1000) << 4) | static_cast<uint8_t>(op);
}
inline PlayerOp command_op(uint64_t word) { return static_cast<PlayerOp>(word & 15); }
inline double command_seconds(uint64_t word) { return double((word >> 4) & 0xffffffff) / 1000; }
inline uint64_t command_sequence(uint64_t word) { return word >> 36; }
inline void apply_player_command(Timeline& clock, uint64_t word, double now) {
    switch (command_op(word)) {
    case PlayerOp::Play: clock.play(now); break;
    case PlayerOp::Pause: clock.pause(now); break;
    case PlayerOp::Seek: clock.seek(command_seconds(word), now); break;
    case PlayerOp::LoopOn: clock.set_loop(true); break;
    case PlayerOp::LoopOff: clock.set_loop(false); break;
    case PlayerOp::Stop: clock.stop(now); break;
    default: break;
    }
}
// A coherent cross-thread snapshot; position is quantized to 1 ms.
inline uint64_t pack_player_frame(ClockFrame f) {
    return ((f.epoch & 0x3fffffff) << 34) |
        (static_cast<uint64_t>(std::clamp(f.seconds * 1000, 0.0, 4294967295.0)) << 2) |
        static_cast<uint64_t>(f.state);
}
inline ClockFrame unpack_player_frame(uint64_t word) {
    return {double((word >> 2) & 0xffffffff) / 1000, word >> 34, static_cast<PlaybackState>(word & 3)};
}
}
