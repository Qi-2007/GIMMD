#pragma once
// Included within multi; all solving is numerical, before any engine pose writes.
namespace leg_playback {
static std::vector<vmd::BoneKey> foot_keys[2];
static std::vector<vmd::SwitchKey> switches[2];
static std::vector<vmd::BoneKey> toe_keys[2];
static std::vector<vmd::SwitchKey> toe_switches[2];
static unsigned toe_solved=0,toe_corrected=0,toe_degenerate=0,toe_disabled=0;
static float toe_direction_error=0,toe_radial_miss=0,toe_degrees=0;
static gimmd::leg_pose::Geometry geometry{};
static bool geometry_captured=false;
static unsigned adjusted_joints=0;
static unsigned last_geometry_mapping=0;
static float last_position_error=0,last_scale_error=0;
static float last_reference_length=0,last_measured_length=0;
static vmd::Vec3 root_origin_offset{};
static unsigned solved=0,clamped=0,disabled=0;
static float residual=0,requested_miss=0,geometry_error=0,maximum_root_lowering=0;
static const char* status="disabled";
static void reset() {
    for (auto& track:foot_keys) track.clear();
    for (auto& track:switches) track.clear();
    for (auto& track:toe_keys) track.clear();
    for (auto& track:toe_switches) track.clear();
    toe_solved=toe_corrected=toe_degenerate=toe_disabled=0; toe_direction_error=toe_radial_miss=toe_degrees=0;
    geometry={}; geometry_captured=false; adjusted_joints=0;
    last_geometry_mapping=0; last_position_error=last_scale_error=0;
    last_reference_length=last_measured_length=0;
    root_origin_offset={};
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
        if (toe_mode) for (const auto name:gimmd::fk_profile::ToeTrackNames[side]) {
            const auto found=motion.bones.find(name);
            if (found!=motion.bones.end()&&!found->second.empty()) {
                if (!toe_keys[side].empty()) { status="ambiguous-toe-ik-aliases"; return false; }
                toe_keys[side]=found->second;
            }
            const auto toggle=motion.ik.find(name);
            if (toggle!=motion.ik.end()&&!toggle->second.empty()) {
                if (!toe_switches[side].empty()) { status="ambiguous-toe-ik-switches"; return false; }
                toe_switches[side]=toggle->second;
            }
        }
    }
    if (!present) return false;
    status="foot-ik-prepared"; return true;
}
static bool check_geometry(unsigned mapping,void* bone) {
    namespace rm=root_motion;
    const unsigned index=gimmd::fk_profile::AvatarIndices[mapping];
    const auto& reference=gimmd::fk_profile::AvatarReference[index]; vmd::Vec3 position{},scale{};
    if (!rm::read(rm::get,bone,&position)||!rm::read(rm::get_scale,bone,&scale)) return false;
    const float p_error=rm::error(position,{reference.position[0],reference.position[1],reference.position[2]});
    const float s_error=rm::error(scale,{reference.scale[0],reference.scale[1],reference.scale[2]});
    last_geometry_mapping=mapping; last_position_error=p_error; last_scale_error=s_error;
    geometry_error=std::max(geometry_error,std::max(p_error,s_error));
    namespace li=gimmd::leg_ik;
    const vmd::Vec3 reference_position{reference.position[0],reference.position[1],reference.position[2]};
    last_reference_length=li::length(reference_position); last_measured_length=li::length(position);
    // Animated segment offsets are captured rather than replaced by the bind pose.
    // Validate their length and direction against the same verified Avatar before using them.
    const bool segment_valid=mapping==0 || (last_reference_length>1e-6f &&
        last_measured_length>=last_reference_length*0.95f && last_measured_length<=last_reference_length*1.05f &&
        li::dot(li::unit(position),li::unit(reference_position))>0.998f);
    if (p_error>(mapping==0 ? 0.15f:std::max(0.002f,last_reference_length*0.05f))||s_error>0.003f||!segment_valid) {
        status="live-leg-reference-geometry-mismatch"; return false;
    }
    if (geometry_captured) {
        if (!geometry.active[index]||rm::error(position,geometry.position[index])>0.00002f||rm::error(scale,geometry.scale[index])>0.00002f) {
            status="live-leg-captured-geometry-changed"; return false;
        }
    } else {
        geometry.active[index]=true; geometry.position[index]=position; geometry.scale[index]=scale;
        if (std::max(p_error,s_error)>0.00002f) ++adjusted_joints;
    }
    return true;
}
static bool evaluate(double frame,vmd::Quaternion* rotations,void* const* bones,double blend) {
    if (!leg_mode) return true;
    namespace lp=gimmd::leg_pose;
    namespace li=gimmd::leg_ik;
    namespace rm=root_motion;
    status="foot-ik-solve-refused";
    // Capture small live offsets/scales after acquiring the Animator; keep that geometry fixed.
    for (const unsigned mapping:{0u,10u,11u,12u,33u,34u,35u}) if (!check_geometry(mapping,bones[mapping])) return false;
    if (toe_mode) for (const unsigned mapping:{13u,36u}) if (!check_geometry(mapping,bones[mapping])) return false;
    geometry_captured=true;
    // Current model's mesh frame is rigid and unit scale. Other scale layouts need explicit calibration.
    if (!rm::captured || li::length(li::sub(rm::body_scale,{1,1,1}))>0.002f) return false;
    vmd::Vec3 source{};
    if (!rm::sample(frame,&source)) return false;
    const auto& body=rm::body_rotation;
    auto mesh_frame=li::rotation({body.x,body.y,body.z,body.w});
    mesh_frame.columns[0]=li::scale(mesh_frame.columns[0],rm::body_scale.x);
    mesh_frame.columns[1]=li::scale(mesh_frame.columns[1],rm::body_scale.y);
    mesh_frame.columns[2]=li::scale(mesh_frame.columns[2],rm::body_scale.z);
    vmd::Vec3 actual_root{};
    if (!li::inverse_vector(mesh_frame,li::sub(rm::baseline,rm::body_position),&actual_root)) return false;
    const auto& root_reference=gimmd::fk_profile::AvatarReference[1];
    root_origin_offset=li::sub(actual_root,{root_reference.position[0],root_reference.position[1],root_reference.position[2]});
    const auto root_delta=li::add(root_origin_offset,gimmd::root_translation::offset(source,rm::source_zero,gimmd::fk_profile::RootMotionScale,{}, {1,1,1}));
    vmd::BoneSample master{},master_zero{};
    if (!rm::controls[0].empty() && (!vmd::SampleBone(rm::controls[0],frame,master)||!vmd::SampleBone(rm::controls[0],0,master_zero))) return false;
    // Preserve the captured stage's horizontal placement; ground height uses the mesh reference plane.
    const auto master_delta=li::add({root_origin_offset.x,0,root_origin_offset.z},
        gimmd::root_translation::offset(master.position,master_zero.position,gimmd::fk_profile::RootMotionScale,{}, {1,1,1}));
    lp::FootSample feet[2]{};
    for (unsigned side=0;side<2;++side) {
        if (foot_keys[side].empty()||!vmd::SampleSwitch(switches[side],frame,true)) { ++disabled; if (toe_mode) ++toe_disabled; continue; }
        vmd::BoneSample sample{}; if (!vmd::SampleBone(foot_keys[side],frame,sample)) return false;
        // All channels share the root's first-frame origin. Preserve each foot's initial stance offset.
        const auto reference=li::sub(rm::source_zero,master_zero.position);
        feet[side]={true,gimmd::root_translation::offset(sample.position,reference,gimmd::fk_profile::RootMotionScale,{}, {1,1,1}),sample.rotation,
            lp::support_weight(sample.position.y*gimmd::fk_profile::RootMotionScale)};
        if (toe_mode) {
            if (toe_keys[side].empty()||!vmd::SampleSwitch(toe_switches[side],frame,true)) ++toe_disabled;
            else {
                vmd::BoneSample toe{}; if (!vmd::SampleBone(toe_keys[side],frame,toe)) return false;
                feet[side].toe_enabled=true;
                feet[side].toe_offset=gimmd::root_translation::offset(toe.position,{},gimmd::fk_profile::RootMotionScale,{}, {1,1,1});
            }
        }
    }
    const auto& b=rm::body_rotation; const auto& r=rm::root_rotation;
    const auto root_override=gimmd::rotation_preview::multiply({-b.x,-b.y,-b.z,b.w},{r.x,r.y,r.z,r.w});
    lp::Result result_ik{};
    if (!lp::solve(rotations,root_delta,master_delta,feet,&result_ik,&root_override,&geometry)) return false;
    solved+=result_ik.solved; clamped+=result_ik.clamped; residual=std::max(residual,result_ik.maximum_residual);
    requested_miss=std::max(requested_miss,result_ik.maximum_requested_miss);
    maximum_root_lowering=std::max(maximum_root_lowering,-result_ik.root_correction_y);
    toe_solved+=result_ik.toe_solved; toe_corrected+=result_ik.toe_corrected; toe_degenerate+=result_ik.toe_degenerate;
    toe_direction_error=std::max(toe_direction_error,result_ik.maximum_toe_direction_error);
    toe_radial_miss=std::max(toe_radial_miss,result_ik.maximum_toe_radial_miss);
    toe_degrees=std::max(toe_degrees,result_ik.maximum_toe_correction_degrees);
    const auto correction=li::vector(li::rotation({b.x,b.y,b.z,b.w}),{0,result_ik.root_correction_y*rm::body_scale.y,0});
    rm::pending=li::add(rm::pending,li::scale(correction,static_cast<float>(blend)));
    status="playing-two-bone-foot-ik"; return true;
}
static void report(std::ostream& out) {
    out << "{\"enabled\":" << (leg_mode ? "true":"false") << ",\"status\":\"" << status << "\",\"solvedLegSamples\":" << solved
        << ",\"reachClampedSamples\":" << clamped << ",\"disabledOrMissingSamples\":" << disabled << ",\"maximumReachableTargetResidual\":" << residual
        << ",\"maximumRequestedTargetMiss\":" << requested_miss << ",\"maximumLiveReferenceGeometryError\":" << geometry_error
        << ",\"measuredGeometryCaptured\":" << (geometry_captured ? "true":"false") << ",\"adjustedGeometryJoints\":" << adjusted_joints
        << ",\"lastGeometryMapping\":" << last_geometry_mapping << ",\"lastGeometryPositionError\":" << last_position_error << ",\"lastGeometryScaleError\":" << last_scale_error
        << ",\"lastGeometryReferenceLength\":" << last_reference_length << ",\"lastGeometryMeasuredLength\":" << last_measured_length
        << ",\"capturedRootOriginOffsetMesh\":[" << root_origin_offset.x << ',' << root_origin_offset.y << ',' << root_origin_offset.z << ']'
        << ",\"maximumVisualRootLowering\":" << maximum_root_lowering << ",\"footPositionFrameZeroAnchored\":false,\"commonRootFrameZeroOrigin\":true"
        << ",\"flatSupportReachCompensation\":true,\"terrainRaycast\":false,\"footRotationApplied\":true,\"toeIkApplied\":" << (toe_mode ? "true":"false")
        << ",\"sourcePmxCalibrated\":false,\"toeIk\":{\"enabled\":" << (toe_mode ? "true":"false") << ",\"solvedSamples\":" << toe_solved
        << ",\"correctedSamples\":" << toe_corrected << ",\"degenerateSamples\":" << toe_degenerate << ",\"disabledOrMissingSamples\":" << toe_disabled
        << ",\"maximumDirectionError\":" << toe_direction_error << ",\"maximumRadialMiss\":" << toe_radial_miss << ",\"maximumCorrectionDegrees\":" << toe_degrees
        << ",\"projectedAnkleParent\":true,\"toeControllerRotationApplied\":false}}";
}
}
