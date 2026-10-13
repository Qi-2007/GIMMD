#include "vmd.h"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iomanip>

namespace vmd = BetterEndfieldNext::Vmd;
int wmain(int argc, wchar_t** argv) {
    try {
        if (argc != 2 && argc != 3) {
            std::cerr << "Usage: gimmd-vmd <motion.vmd> [seconds]\n";
            return 2;
        }
        const std::filesystem::path path(argv[1]);
        const auto size = std::filesystem::file_size(path);
        if (size > vmd::Limits{}.file_bytes) throw std::runtime_error("file too large");
        std::ifstream input(path, std::ios::binary);
        std::vector<uint8_t> bytes(static_cast<size_t>(size));
        if (!input.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size())))
            throw std::runtime_error("cannot read complete file");
        vmd::Motion motion;
        std::string error;
        if (!vmd::Parse(bytes, motion, error)) throw std::runtime_error(error);
        double time = 0;
        if (argc == 3) {
            size_t consumed = 0;
            const std::wstring value(argv[2]);
            time = std::stod(value, &consumed);
            if (consumed != value.size() || !std::isfinite(time) || time < 0)
                throw std::runtime_error("seconds must be finite and nonnegative");
        }
        size_t keys = 0;
        for (const auto& [name, track] : motion.bones) keys += track.size();
        std::cout << "{\n  \"vmdVersion\": " << motion.version
            << ",\n  \"durationSeconds\": " << motion.last_frame / vmd::FramesPerSecond
            << ",\n  \"boneTracks\": " << motion.bones.size()
            << ",\n  \"boneKeys\": " << keys
            << ",\n  \"morphTracks\": " << motion.morphs.size()
            << ",\n  \"cameraKeys\": " << motion.cameras.size()
            << ",\n  \"ikTracks\": " << motion.ik.size()
            << ",\n  \"duplicateKeys\": " << motion.duplicate_keys
            << ",\n  \"zeroQuaternions\": " << motion.zero_quaternions
            << ",\n  \"sampleSeconds\": " << std::setprecision(12) << time
            << ",\n  \"bones\": [";
        bool first = true;
        for (const auto& [name, track] : motion.bones) {
            vmd::BoneSample sample;
            if (!vmd::SampleBone(track, time * vmd::FramesPerSecond, sample)) continue;
            std::cout << (first ? "\n" : ",\n") << "    {\"nameCp932Hex\": \"";
            first = false;
            for (const unsigned char c : name)
                std::cout << std::hex << std::setw(2) << std::setfill('0') << unsigned(c);
            std::cout << std::dec << "\", \"position\": [" << sample.position.x << ", "
                << sample.position.y << ", " << sample.position.z << "], \"rotation\": ["
                << sample.rotation.x << ", " << sample.rotation.y << ", "
                << sample.rotation.z << ", " << sample.rotation.w << "]}";
        }
        std::cout << "\n  ]\n}\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "VMD error: " << e.what() << '\n';
        return 1;
    }
}

