#pragma once
// Included within multi; all solving is numerical, before any engine pose writes.
namespace leg_playback {
static std::vector<vmd::BoneKey> foot_keys[2];
static std::vector<vmd::SwitchKey> switches[2];
static unsigned solved=0,clamped=0,disabled=0;
static float residual=0,requested_miss=0,geometry_error=0,maximum_root_lowering=0;
static const char* status="disabled";
static void reset() {
    for (auto& track:foot_keys) track.clear();
    for (auto& track:switches) track.clear();
    solved=clamped=disabled=0; residual=requested_miss=geometry_error=maximum_root_lowering=0; status=leg_mode ? "not-prepared":"disabled";
}
static bool prepare(const vmd::Motion& motion) {
    if (!leg_mode) return true;
    status="foot-ik-tracks-missing"; unsigned present=0;
    for (unsigned side=0;side<2;++side) {
        for (const auto name:gimmd::fk_profile::FootTrackNames[side]) {
            const auto found=motion.bones.find(name);
            if (found!=motion.bones.end()&&!found->second.empty()) {
                if (!foot_keys[side].empty()) { status="ambiguous-foot-ik-aliases"; return false; }
                foot_keys[side]=found->second;
            }
            const auto toggle=motion.ik.find(name);
            if (toggle!=motion.ik.end()&&!toggle->second.empty()) {
                if (!switches[side].empty()) { status="ambiguous-foot-ik-switches"; return false; }
                switches[side]=toggle->second;
            }
        }
        if (!foot_keys[side].empty()) ++present;
    }
    if (!present) return false;
    status="foot-ik-prepared"; return true;
}
static bool evaluate(double frame,vmd::Quaternion* rotations,void* const* bones,double blend) {
    if (!leg_mode) return true;
    namespace lp=gimmd::leg_pose;
    namespace li=gimmd::leg_ik;
    namespace rm=root_motion;
    status="foot-ik-solve-refused";
    // The numerical rig uses reference offsets/scales. Check the actual chain before relying on them.
    for (const unsigned mapping:{0u,10u,11u,12u,33u,34u,35u}) {
        const auto& reference=gimmd::fk_profile::AvatarReference[gimmd::fk_profile::AvatarIndices[mapping]];
        vmd::Vec3 position{},scale{};
        if (!rm::read(rm::get,bones[mapping],&position)||!rm::read(rm::get_scale,bones[mapping],&scale)) return false;
        geometry_error=std::max(geometry_error,std::max(rm::error(position,{reference.position[0],reference.position[1],reference.position[2]}),
            rm::error(scale,{reference.scale[0],reference.scale[1],reference.scale[2]})));
        if (geometry_error>0.00002f) { status="live-leg-reference-geometry-mismatch"; return false; }
    }
    // Current model's mesh frame is rigid and unit scale. Other scale layouts need explicit calibration.
    if (!rm::captured || li::length(li::sub(rm::body_scale,{1,1,1}))>0.002f) return false;
    vmd::Vec3 source{};
    if (!rm::sample(frame,&source)) return false;
    const auto root_delta=gimmd::root_translation::offset(source,rm::source_zero,gimmd::fk_profile::RootMotionScale,{}, {1,1,1});
    vmd::BoneSample master{},master_zero{};
    if (!rm::controls[0].empty() && (!vmd::SampleBone(rm::controls[0],frame,master)||!vmd::SampleBone(rm::controls[0],0,master_zero))) return false;
    const auto master_delta=gimmd::root_translation::offset(master.position,master_zero.position,gimmd::fk_profile::RootMotionScale,{}, {1,1,1});
    lp::FootSample feet[2]{};
    for (unsigned side=0;side<2;++side) {
        if (foot_keys[side].empty()||!vmd::SampleSwitch(switches[side],frame,true)) { ++disabled; continue; }
        vmd::BoneSample sample{}; if (!vmd::SampleBone(foot_keys[side],frame,sample)) return false;
        // All channels share the root's first-frame origin. Preserve each foot's initial stance offset.
        const auto reference=li::sub(rm::source_zero,master_zero.position);
        feet[side]={true,gimmd::root_translation::offset(sample.position,reference,gimmd::fk_profile::RootMotionScale,{}, {1,1,1}),sample.rotation,
            lp::support_weight(sample.position.y*gimmd::fk_profile::RootMotionScale)};
    }
    const auto& b=rm::body_rotation; const auto& r=rm::root_rotation;
    const auto root_override=gimmd::rotation_preview::multiply({-b.x,-b.y,-b.z,b.w},{r.x,r.y,r.z,r.w});
    lp::Result result_ik{};
    if (!lp::solve(rotations,root_delta,master_delta,feet,&result_ik,&root_override)) return false;
    solved+=result_ik.solved; clamped+=result_ik.clamped; residual=std::max(residual,result_ik.maximum_residual);
    requested_miss=std::max(requested_miss,result_ik.maximum_requested_miss);
    maximum_root_lowering=std::max(maximum_root_lowering,-result_ik.root_correction_y);
    const auto correction=li::vector(li::rotation({b.x,b.y,b.z,b.w}),{0,result_ik.root_correction_y*rm::body_scale.y,0});
    rm::pending=li::add(rm::pending,li::scale(correction,static_cast<float>(blend)));
    status="playing-two-bone-foot-ik"; return true;
}
static void report(std::ostream& out) {
    out << "{\"enabled\":" << (leg_mode ? "true":"false") << ",\"status\":\"" << status << "\",\"solvedLegSamples\":" << solved
        << ",\"reachClampedSamples\":" << clamped << ",\"disabledOrMissingSamples\":" << disabled << ",\"maximumReachableTargetResidual\":" << residual
        << ",\"maximumRequestedTargetMiss\":" << requested_miss << ",\"maximumLiveReferenceGeometryError\":" << geometry_error
        << ",\"maximumVisualRootLowering\":" << maximum_root_lowering << ",\"footPositionFrameZeroAnchored\":false,\"commonRootFrameZeroOrigin\":true"
        << ",\"flatSupportReachCompensation\":true,\"terrainRaycast\":false,\"footRotationApplied\":true,\"toeIkApplied\":false,\"sourcePmxCalibrated\":false}";
}
}
