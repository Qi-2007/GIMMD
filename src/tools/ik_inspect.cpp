#include "core/leg_pose.h"
#include "generated/motion_profile.h"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iomanip>
namespace vmd=BetterEndfieldNext::Vmd;
int wmain(int argc,wchar_t** argv) {
    try {
        if (argc!=2) throw std::runtime_error("usage: gimmd-ik-inspect motion.vmd");
        std::ifstream input(std::filesystem::path(argv[1]),std::ios::binary|std::ios::ate);
        const auto size=input ? input.tellg():std::streampos(-1);
        if (size<=0||size>16*1024*1024) throw std::runtime_error("invalid motion size");
        std::vector<uint8_t> bytes(static_cast<size_t>(size)); input.seekg(0); input.read(reinterpret_cast<char*>(bytes.data()),size);
        if (!input) throw std::runtime_error("motion read failed");
        vmd::Motion motion; std::string error;
        if (!vmd::Parse(bytes,motion,error)) throw std::runtime_error(error);
        const auto bone_sample=[&](const char* name,double frame) { vmd::BoneSample value{}; const auto found=motion.bones.find(name);
            if (found!=motion.bones.end()&&!vmd::SampleBone(found->second,frame,value)) throw std::runtime_error("sample failed"); return value; };
        const char* foot_names[2]{};
        for (unsigned side=0;side<2;++side) for (const auto name:gimmd::fk_profile::FootTrackNames[side])
            if (motion.bones.contains(name)) { if (foot_names[side]) throw std::runtime_error("ambiguous foot aliases"); foot_names[side]=name; }
        if (!foot_names[0]||!foot_names[1]) throw std::runtime_error("both foot tracks required for inspection");
        const auto converted=[](vmd::Vec3 a,vmd::Vec3 zero) { return gimmd::root_translation::offset(a,zero,gimmd::fk_profile::RootMotionScale,{}, {1,1,1}); };
        unsigned solved=0,clamped=0; float residual=0,maximum_miss=0,maximum_lowering=0;
        // Half-frame sampling covers interpolation as well as every source frame.
        for (unsigned step=0;step<=motion.last_frame*2;++step) {
            const double frame=double(step)/2;
            vmd::Quaternion rotations[52]{};
            for (unsigned i=0;i<52;++i) {
                // Source map is generated separately; share its exact CP932 names.
                rotations[i]=gimmd::fk_retarget::evaluate(gimmd::fk_profile::Bindings[i],bone_sample(gimmd::motion_profile::Mappings[i].cp932,frame).rotation);
            }
            vmd::Vec3 delta{},master{},root_zero{},master_zero{};
            for (unsigned i=0;i<3;++i) {
                const auto value=converted(bone_sample(gimmd::fk_profile::RootTrackNames[i],frame).position,bone_sample(gimmd::fk_profile::RootTrackNames[i],0).position);
                delta=gimmd::leg_ik::add(delta,value); if (!i) master=value;
                root_zero=gimmd::leg_ik::add(root_zero,bone_sample(gimmd::fk_profile::RootTrackNames[i],0).position);
                if (!i) master_zero=bone_sample(gimmd::fk_profile::RootTrackNames[i],0).position;
            }
            gimmd::leg_pose::FootSample feet[2]{};
            for (unsigned side=0;side<2;++side) {
                const auto value=bone_sample(foot_names[side],frame); const auto toggle=motion.ik.find(foot_names[side]);
                feet[side]={toggle==motion.ik.end()||vmd::SampleSwitch(toggle->second,frame,true),converted(value.position,gimmd::leg_ik::sub(root_zero,master_zero)),value.rotation,
                    gimmd::leg_pose::support_weight(value.position.y*gimmd::fk_profile::RootMotionScale)};
            }
            gimmd::leg_pose::Result result{};
            if (!gimmd::leg_pose::solve(rotations,delta,master,feet,&result)) throw std::runtime_error("IK solve failed at frame "+std::to_string(frame));
            solved+=result.solved; clamped+=result.clamped; residual=std::max(residual,result.maximum_residual);
            delta.y+=result.root_correction_y; maximum_lowering=std::max(maximum_lowering,-result.root_correction_y);
            gimmd::leg_pose::Rig rig{},rest{}; rig.reset(rotations,delta); rest.reset(nullptr,{});
            for (unsigned side=0;side<2;++side) if (feet[side].enabled) {
                const auto f=gimmd::fk_profile::AvatarIndices[gimmd::leg_pose::Foot[side]];
                maximum_miss=std::max(maximum_miss,gimmd::leg_ik::length(gimmd::leg_ik::sub(rig.world[f].position,gimmd::leg_ik::add(rest.world[f].position,gimmd::leg_ik::add(feet[side].delta,master)))));
            }
        }
        std::cout<<std::setprecision(9)<<"{\"halfFrameSamples\":"<<motion.last_frame*2+1<<",\"solvedLegSamples\":"<<solved
            <<",\"reachClampedSamples\":"<<clamped<<",\"maximumReachableTargetResidual\":"<<residual<<",\"maximumRequestedTargetMiss\":"<<maximum_miss
            <<",\"maximumVisualRootLowering\":"<<maximum_lowering<<"}\n";
        return 0;
    } catch(const std::exception& e) { std::cerr<<e.what()<<'\n'; return 1; }
}
