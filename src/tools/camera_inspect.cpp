#include "core/camera_pose.h"
#include "generated/fk_profile.h"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iomanip>
int wmain(int argc,wchar_t** argv) {
    try {
        if (argc!=3) throw std::runtime_error("usage: gimmd-camera camera.vmd seconds");
        size_t used=0; const std::wstring time_text=argv[2]; const double seconds=std::stod(time_text,&used);
        if (used!=time_text.size()||!std::isfinite(seconds)||seconds<0||seconds>86400) throw std::runtime_error("invalid seconds");
        std::ifstream input(std::filesystem::path(argv[1]),std::ios::binary|std::ios::ate);
        const auto size=input ? input.tellg():std::streampos(-1);
        if (size<=0||size>16*1024*1024) throw std::runtime_error("invalid camera motion size");
        std::vector<uint8_t> bytes(static_cast<size_t>(size)); input.seekg(0); input.read(reinterpret_cast<char*>(bytes.data()),size);
        BetterEndfieldNext::Vmd::Motion motion; std::string error;
        if (!input||!BetterEndfieldNext::Vmd::Parse(bytes,motion,error)) throw std::runtime_error("camera parse failed: "+error);
        BetterEndfieldNext::Vmd::CameraSample sample{}; gimmd::camera_pose::Pose pose{};
        if (!BetterEndfieldNext::Vmd::SampleCamera(motion.cameras,seconds*30,sample)||
            !gimmd::camera_pose::evaluate(sample,gimmd::fk_profile::RootMotionScale,{}, {},&pose)) throw std::runtime_error("camera track missing or invalid");
        std::cout<<std::setprecision(9)<<"{\"seconds\":"<<seconds<<",\"cameraKeys\":"<<motion.cameras.size()<<",\"position\":["<<pose.position.x<<','<<pose.position.y<<','<<pose.position.z
            <<"],\"rotation\":["<<pose.rotation.x<<','<<pose.rotation.y<<','<<pose.rotation.z<<','<<pose.rotation.w<<"],\"fov\":"<<pose.fov
            <<",\"orthographic\":"<<(pose.orthographic ? "true":"false")<<",\"clientCameraControlled\":false}\n";
        return 0;
    } catch(const std::exception& e) { std::cerr<<e.what()<<'\n'; return 1; }
}
