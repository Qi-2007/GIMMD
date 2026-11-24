#pragma once
// Included within playback, after checked path/pose helpers.
namespace multi {
constexpr unsigned Capacity = 52;
static unsigned TrackCount = 10;
struct Track {
    std::vector<vmd::BoneKey> keys;
    Step steps[MaxDepth + 1]{};
    unsigned length = 0, writes = 0, retained = 0, overwritten = 0, attempts = 0, limited = 0;
    vmd::Quaternion source_zero{};
    Quat baseline{0,0,0,1}, applied{0,0,0,1};
    bool dirty = false, captured = false, restored = false;
    unsigned mapping_index = 0;
};
static Track tracks[Capacity];
static gimmd::fk_retarget::Binding bindings[Capacity]{};
static bool adaptive_reference=false;
static unsigned source_used = 0;
static bool parallel_torso=false;
#include "root_motion.h"
#include "reference_pose.h"
#include "leg_playback.h"
#include "pelvis_helper.h"
#include "cloth_stabilizer.h"
static size_t source_tracks = 0, source_morphs = 0, source_cameras = 0, source_ik = 0;
static uint32_t last_frame = 0;
static const char* state = "disabled";
static gimmd::Timeline clock(0);
static uint64_t applied_command = 0;
static unsigned commands_applied = 0;
static bool clock_started = false;
static double entry_blend_seconds = 0;
static ULONGLONG blend_last = 0;
static bool blend_was_playing = false;
struct Performance {
    unsigned batches = 0, unique_nodes = 0, reused_prefixes = 0;
    double total_ms = 0, max_ms = 0, resolve_ms = 0, write_ms = 0;
};
static Performance performance;
static double precise_ms() {
    static const double frequency = [] { LARGE_INTEGER f{}; QueryPerformanceFrequency(&f); return double(f.QuadPart); }();
    LARGE_INTEGER now{}; QueryPerformanceCounter(&now); return double(now.QuadPart)*1000/frequency;
}
static void reset() {
    reference_pose::reset();
    cloth_stabilizer::reset();
    pelvis_helper::reset();
    root_motion::reset();
    leg_playback::reset();
    for (auto& track : tracks) track = {};
    parallel_torso=false;adaptive_reference=false;
    for(unsigned i=0;i<Capacity;++i)bindings[i]=gimmd::fk_profile::Bindings[i];
    TrackCount = full_mode ? Capacity : 10; source_used = 0;
    source_tracks = source_morphs = source_cameras = source_ik = 0; last_frame = 0;
    state = MultiEnabled ? "not-loaded" : "disabled";
    clock = gimmd::Timeline(0); clock_started = false; applied_command = 0; commands_applied = 0;
    performance = {};
    entry_blend_seconds = 0; blend_last = 0; blend_was_playing = false;
}
static bool prepare(const vmd::Motion& motion) {
    reset(); state = "source-tracks-missing";
    if(gimmd::face_bridge::prepare&&!gimmd::face_bridge::prepare(false,motion)){state="face-prepare-refused";return false;}
    if (motion.last_frame < 1 || motion.zero_quaternions) return false;
    for (unsigned i = 0; i < TrackCount; ++i) {
        tracks[i].mapping_index = full_mode ? i : gimmd::motion_profile::PreviewIndices[i];
        const auto& mapping = gimmd::motion_profile::Mappings[tracks[i].mapping_index];
        auto found = motion.bones.find(mapping.cp932);
        if (full_mode&&i==0&&found==motion.bones.end()&&!motion.bones.contains(gimmd::fk_profile::RootTrackNames[1]))
            found=motion.bones.find("\x8d\x98"); // Baked FK waist (Japanese koshi).
        if (found == motion.bones.end() || found->second.empty()) {
            if (full_mode) continue;
            return false;
        }
        ++source_used;
        tracks[i].keys = found->second;
        vmd::BoneSample sample{};
        if (!vmd::SampleBone(tracks[i].keys, 0, sample)) return false;
        tracks[i].source_zero = sample.rotation;
    }
    if (full_mode && (source_used < 16 || tracks[0].keys.empty() || tracks[1].keys.empty() ||
        tracks[7].keys.empty() || tracks[8].keys.empty() || tracks[30].keys.empty() || tracks[31].keys.empty())) return false;
    parallel_torso=full_mode&&motion.bones.contains(gimmd::motion_profile::Mappings[0].cp932);
    if (!root_motion::prepare(motion)) { state=root_motion::status; return false; }
    if (root_mode) for (const auto& track:root_motion::controls) source_used+=unsigned(!track.empty());
    if (!leg_playback::prepare(motion)) { state=leg_playback::status; return false; }
    if (leg_mode) for (const auto& track:leg_playback::foot_keys) source_used+=unsigned(!track.empty());
    if (toe_mode) for (const auto& track:leg_playback::toe_keys) source_used+=unsigned(!track.empty());
    source_tracks = motion.bones.size(); source_morphs = motion.morphs.size();
    source_cameras = motion.cameras.size(); source_ik = motion.ik.size(); last_frame = motion.last_frame;
    if (leg_mode) for (const auto& track:leg_playback::switches) if (!track.empty()&&source_ik) --source_ik;
    if (toe_mode) for (const auto& track:leg_playback::toe_switches) if (!track.empty()&&source_ik) --source_ik;
    clock = gimmd::Timeline(double(last_frame)/30);
    gimmd::player_session::duration_seconds=double(last_frame)/30;
    state = "source-prepared"; return true;
}
static bool bind(int actor) {
    const int model_node = animator_observer::owner_node;
    if (model_node < 0 || static_cast<unsigned>(model_node) >= count) return false;
    adaptive_reference=full_mode&&std::wcscmp(nodes[model_node].name,gimmd::fk_profile::ModelName)!=0;
    if (full_mode&&!adaptive_reference) {
        namespace bp = animator_observer::bind_pose;
        state = "full-body-reference-mismatch";
        if (std::wcscmp(nodes[model_node].name,gimmd::fk_profile::ModelName) ||
            std::strcmp(bp::avatar_rest_status,"avatar-local-reference-observed") ||
            bp::avatar_rest_count != std::size(gimmd::fk_profile::AvatarReference)) return false;
        for (unsigned i = 0; i < bp::avatar_rest_count; ++i) {
            const auto& expected = gimmd::fk_profile::AvatarReference[i]; const auto& actual = bp::avatar_local[i];
            if (bp::avatar_parents[i] != expected.parent) return false;
            for (unsigned n = 0; n < 3; ++n) if (std::abs(actual[n]-expected.position[n]) > 0.00002f ||
                std::abs(actual[8+n]-expected.scale[n]) > 0.00002f) return false;
            for (unsigned n = 0; n < 4; ++n) if (std::abs(actual[4+n]-expected.rotation[n]) > 0.00002f) return false;
        }
    }
    if(adaptive_reference) {
        namespace bp=animator_observer::bind_pose;
        state="character-reference-unavailable";
        if(std::strcmp(bp::avatar_rest_status,"avatar-local-reference-observed")||!bp::avatar_rest_count)return false;
        // Fixed male IK geometry must not be used for a different character.
        leg_mode=false;toe_mode=false;
    }
    for (unsigned i = 0; i < TrackCount; ++i) {
        auto& track = tracks[i];
        if (track.keys.empty() && !full_mode) return false;
        const auto& mapping = gimmd::motion_profile::Mappings[track.mapping_index];
        int bone = -1; unsigned matches = 0;
        for (unsigned n = 0; n < count; ++n) {
            if (std::wcscmp(nodes[n].name, mapping.target)) continue;
            bool under_model = false, under_actor = false;
            for (int parent = static_cast<int>(n); parent >= 0; parent = nodes[parent].parent) {
                under_model = under_model || parent == model_node; under_actor = under_actor || parent == actor;
            }
            if (under_model && under_actor) { ++matches; bone = static_cast<int>(n); }
        }
        if (matches != 1) { state = "missing-or-ambiguous-target"; return false; }
        if(adaptive_reference) {
            namespace bp=animator_observer::bind_pose;
            const int ref=bp::avatar_links[bone];
            if(ref<0||unsigned(ref)>=bp::avatar_rest_count){state="character-bone-reference-missing";return false;}
            const auto* q=bp::avatar_local[ref]+4;
            Quat rest{q[0],q[1],q[2],q[3]};
            if(!unit_quat(rest)){state="character-reference-rotation-invalid";return false;}
            bindings[track.mapping_index].rest={rest.x,rest.y,rest.z,rest.w};
        }

        state="character-path-validation";
        Step reverse[MaxDepth + 1]{}; unsigned length = 0;
        for (int n = bone; n >= 0; n = nodes[n].parent) {
            if (length >= MaxDepth + 1 || !nodes[n].processed || !nodes[n].aliases_equal || !nodes[n].parent_valid ||
                !nodes[n].owner_valid || !nodes[n].active || !roundtrip::identity_current(nodes[n]) ||
                (nodes[n].parent >= 0 && !nodes[n].rotation_relation_valid)) return false;
            auto& step = reverse[length++]; step.identity = nodes[n].identity; wcscpy_s(step.name, nodes[n].name);
            if (nodes[n].parent >= 0) {
                step.child = 0;
                for (int previous = 0; previous < n; ++previous) if (nodes[previous].parent == nodes[n].parent) ++step.child;
            }
        }
        track.length = length;
        for (unsigned n = 0; n < length; ++n) track.steps[n] = reverse[length - n - 1];
    }
    if (!root_motion::bind(model_node)) { state = "root-motion-binding-refused"; return false; }
    if (!reference_pose::bind(model_node)) { state="reference-pose-binding-refused";return false; }
    if (!pelvis_helper::bind()) { state="pelvis-helper-binding-refused"; return false; }
    if (!cloth_stabilizer::bind(model_node)) { state="cloth-static-binding-refused"; return false; }
    state = adaptive_reference?"character-reference-targets-bound":"all-targets-bound"; return true;
}
static void restore_all() {
    if (gimmd::player_session::active) gimmd::player_session::frame.store(0);
    bool pending = false;
    ResolutionBatch batch{};
    // Local snapshots restore child-first; each target has its own retry bound.
    for (unsigned reverse = TrackCount; reverse > 0; --reverse) {
        auto& track = tracks[reverse - 1];
        if (!track.dirty) continue;
        bool active = true; void* bone = gimmd::player_session::legacy_resolver ? resolve_steps(&active,track.steps,track.length) :
            resolve_batch_steps(&batch,&active,track.steps,track.length);
        if (!bone || track.attempts >= 8) { pending = true; continue; }
        ++track.attempts; ++result.restore_attempts; Quat observed{};
        if (write_local(bone, &track.baseline, &observed)) {
            track.dirty = false; track.restored = true;
            result.restore_error = std::max(result.restore_error, roundtrip::error(track.baseline, observed));
        } else pending = true;
    }
    if (!cloth_stabilizer::restore(&batch)) pending=true;
    if (!pelvis_helper::restore(&batch)) pending=true;
    if (!root_motion::restore(&batch)) pending = true;
    if (!pending&&!reference_pose::restore(&batch)) pending=true;
    if(gimmd::face_bridge::restore){bool active=true;if(!gimmd::face_bridge::restore(false,resolve_model(&active)))pending=true;}
    dirty.store(pending);
    if (pending) {
        animator_lease::release(); state = "pose-restoration-pending"; result.status = state; stop_requested.store(true); return;
    }
    result.restored = true; state = "all-written-targets-restored";
    finish(result.exception ? "multi-track-exception-restored" : "multi-track-stopped-restored");
}
static void tick(ULONGLONG now) {
    const ULONGLONG cadence = full_mode && !gimmd::player_session::legacy_resolver ? 16 : 33;
    // Check the lease at pose cadence, including paused poses; urgent cleanup is never throttled.
    if (!gimmd::player_session::legacy_resolver && !stop_requested.load() && result.samples && now-result.last < cadence) return;
    if (!stop_requested.load() && !animator_lease::begin()) {
        if (!animator_lease::refused) return;
        stop_requested.store(true);
    }
    if (!result.began) result.began = now;
    result.elapsed = now >= result.began ? now - result.began : 2000;
    double source_seconds = double(result.elapsed)/1000;
    bool timeline_playing = true;
    if (gimmd::player_session::active) {
        const double seconds = double(now)/1000;
        if (!clock_started) { clock.play(seconds); clock_started = true; }
        const auto word = gimmd::player_session::command.load();
        if (gimmd::command_sequence(word) > gimmd::command_sequence(applied_command)) {
            applied_command = word; ++commands_applied;
            if (gimmd::player_session::dynamic_session&&gimmd::player_session::holding_end.load()&&gimmd::command_op(word)==gimmd::PlayerOp::Play) clock.seek(0,seconds);
            gimmd::apply_player_command(clock, word, seconds);
            if (gimmd::command_op(word)==gimmd::PlayerOp::LoopOn) gimmd::player_session::loop_enabled.store(true);
            if (gimmd::command_op(word)==gimmd::PlayerOp::LoopOff) gimmd::player_session::loop_enabled.store(false);
            gimmd::player_session::acknowledged.store(gimmd::command_sequence(word));
            if (gimmd::command_op(word) == gimmd::PlayerOp::Stop) stop_requested.store(true);
        }
        auto f = clock.sample(seconds);
        if (gimmd::player_session::dynamic_session&&f.state==gimmd::PlaybackState::Ended) { clock.seek(f.seconds,seconds); f=clock.sample(seconds); }
        source_seconds = f.seconds;
        timeline_playing = f.state == gimmd::PlaybackState::Playing;
        gimmd::player_session::holding_end.store((f.state==gimmd::PlaybackState::Ended||(gimmd::player_session::dynamic_session&&f.state==gimmd::PlaybackState::Paused&&f.seconds>=double(last_frame)/30))&&gimmd::player_session::hold_at_end&&!stop_requested.load());
        if (gimmd::player_session::dynamic_session) gimmd::player_session::camera_frame.store(gimmd::pack_player_frame(f));
        gimmd::player_session::frame.store(gimmd::pack_player_frame(f));
        if (f.state == gimmd::PlaybackState::Ended&&!gimmd::player_session::hold_at_end) stop_requested.store(true);
    } else if (result.elapsed >= 2000) stop_requested.store(true);
    if (full_mode) {
        if (blend_last && blend_was_playing && now >= blend_last)
            entry_blend_seconds = std::min(0.5,entry_blend_seconds+double(now-blend_last)/1000);
        blend_last = now; blend_was_playing = timeline_playing;
    }
    if (stop_requested.load()) { restore_all(); return; }
    if (result.samples && now - result.last < cadence) return;
    if(!reference_pose::initialize()){stop_requested.store(true);restore_all();return;}
    const double batch_start = precise_ms();
    ResolutionBatch batch{};
    void* bones[Capacity]{}; Quat sampled[Capacity]{}, current[Capacity]{}; vmd::Quaternion desired[Capacity]{};
    // Resolve and read every target before writing any of this batch.
    for (unsigned i = 0; i < TrackCount; ++i) {
        auto& track = tracks[i]; bool active = true;
        bones[i] = gimmd::player_session::legacy_resolver ? resolve_steps(&active,track.steps,track.length) :
            resolve_batch_steps(&batch,&active,track.steps,track.length);
        if (!bones[i] || !active || !read_local(bones[i], &current[i])) {
            ++result.resolve_failures; stop_requested.store(true); restore_all(); return;
        }
        if (track.dirty) {
            if (roundtrip::error(track.applied, current[i]) < 0.00002f) { ++track.retained; ++result.persisted; }
            else { ++track.overwritten; ++result.overwritten; stop_requested.store(true); }
        }
        vmd::BoneSample source{};
        if (!track.keys.empty() && !vmd::SampleBone(track.keys, source_seconds * 30, source)) {
            stop_requested.store(true); restore_all(); return;
        }
        if (!track.captured) { track.baseline = current[i]; track.captured = true; }
        if (full_mode) {
            auto source_rotation=source.rotation;
            if (root_mode&&i==0) {
                vmd::Vec3 position{}; vmd::Quaternion center{};
                if (!gimmd::root_controls::sample(root_motion::controls,source_seconds*30,&position,&center)) {
                    stop_requested.store(true); restore_all(); return;
                }
                source_rotation=gimmd::root_controls::pelvis(center,source.rotation);
                if (std::abs(center.y)>0.00001f) ++root_motion::yaw_samples;
                root_motion::maximum_yaw_degrees=std::max(root_motion::maximum_yaw_degrees,2*std::acos(std::clamp(std::abs(center.w),0.0f,1.0f))*57.295779513f);
            }
            desired[i] = gimmd::fk_retarget::evaluate_at_rest(bindings[track.mapping_index],bindings[track.mapping_index].rest,source_rotation);
            if (parallel_torso&&i==1) {
                vmd::BoneSample lower{};
                if (!vmd::SampleBone(tracks[0].keys,source_seconds*30,lower)) {
                    stop_requested.store(true); restore_all(); return;
                }
                const auto lower_target=gimmd::fk_retarget::evaluate(bindings[0],lower.rotation);
                desired[i]=gimmd::fk_retarget::compensate_parent(bindings[0].rest,lower_target,desired[i]);
            }
        } else {
            bool limited = false;
            const auto delta = gimmd::rotation_preview::delta(track.source_zero, source.rotation, &limited);
            if (limited) ++track.limited;
            sampled[i] = roundtrip::multiply(track.baseline, {delta.x,delta.y,delta.z,delta.w});
        }
    }
    if (stop_requested.load()) { restore_all(); return; }
    if (!cloth_stabilizer::preflight(&batch)) { stop_requested.store(true); restore_all(); return; }
    if (!pelvis_helper::preflight(&batch)) { stop_requested.store(true); restore_all(); return; }
    if (!root_motion::preflight(&batch,source_seconds*30,entry_blend_seconds/0.5)) {
        stop_requested.store(true); restore_all(); return;
    }
    if (!leg_playback::evaluate(source_seconds*30,desired,bones,entry_blend_seconds/0.5)) { stop_requested.store(true); restore_all(); return; }
    if (full_mode) for (unsigned i=0;i<TrackCount;++i) {
        const auto& baseline=tracks[i].baseline;
        const auto blended=vmd::Slerp({baseline.x,baseline.y,baseline.z,baseline.w},desired[i],entry_blend_seconds/0.5);
        sampled[i]={blended.x,blended.y,blended.z,blended.w};
    }
    const double write_start = precise_ms();
    for (unsigned i = 0; i < TrackCount; ++i) {
        auto& track = tracks[i]; track.applied = sampled[i]; track.dirty = true; dirty.store(true);
        Quat observed{};
        if (!write_local(bones[i], &track.applied, &observed)) {
            stop_requested.store(true); restore_all(); return;
        }
        ++track.writes;
    }
    if (!pelvis_helper::apply(&batch,{desired[0].x,desired[0].y,desired[0].z,desired[0].w},entry_blend_seconds/0.5)) { stop_requested.store(true); restore_all(); return; }
    if (!root_motion::apply(&batch)) { dirty.store(root_motion::dirty || dirty.load()); stop_requested.store(true); restore_all(); return; }
    if (!cloth_stabilizer::apply(&batch,entry_blend_seconds/0.5)) { stop_requested.store(true); restore_all(); return; }
    if(gimmd::face_bridge::tick){bool active=true;if(!gimmd::face_bridge::tick(false,resolve_model(&active),source_seconds)){stop_requested.store(true);restore_all();return;}}
    const double batch_end = precise_ms(); ++performance.batches;
    performance.total_ms += batch_end-batch_start; performance.max_ms = std::max(performance.max_ms,batch_end-batch_start);
    performance.resolve_ms += write_start-batch_start; performance.write_ms += batch_end-write_start;
    performance.unique_nodes += batch.count; performance.reused_prefixes += batch.reused;
    ++result.samples; result.last = now; state = full_mode ? "playing-full-body-canonical-fk" : "playing-relative-rotation-preview";
}
static void report(std::ostream& out, bool complete) {
    out << "{\"enabled\":" << (multi_mode ? "true" : "false") << ",\"targetCount\":" << (multi_mode ? TrackCount : 0);
    if (complete && multi_mode) {
        out << ",\"characterReference\":" << (adaptive_reference?"true":"false");
        out << ",\"sessionCommandsApplied\":" << commands_applied;
        out << ",\"status\":\"" << state << "\",\"sourceBoneTracks\":" << source_tracks
            << ",\"sourceLastFrame\":" << last_frame << ",\"sourceTracksUsed\":" << source_used
            << ",\"ignoredBoneTracks\":" << (source_tracks >= source_used ? source_tracks - source_used : 0)
            << ",\"ignoredMorphTracks\":" << (source_morphs-(gimmd::face_bridge::matched?std::min(source_morphs,size_t(gimmd::face_bridge::matched(false))):0)) << ",\"ignoredCameraKeys\":" << source_cameras << ",\"ignoredIkTracks\":" << source_ik
            << ",\"positionsApplied\":" << (root_mode ? "true" : "false") << ",\"targetRestValidated\":" << (full_mode ? "true" : "false")
            << ",\"parallelTorsoCompensated\":" << (parallel_torso ? "true":"false")
            << ",\"fullBodyFk\":" << (full_mode ? "true" : "false") << ",\"sourcePmxCalibrated\":false,\"relativeToSourceFrame\":"
            << (full_mode ? "null" : "0") << ",\"tracks\":[";
        for (unsigned i = 0; i < TrackCount; ++i) {
            if (i) out << ',';
            const auto& t = tracks[i];
            out << "{\"target\":\"" << gimmd::motion_profile::Mappings[t.mapping_index].label
                << "\",\"hasSourceTrack\":" << (!t.keys.empty() ? "true" : "false")
                << ",\"writeCount\":" << t.writes << ",\"poseRetainedCount\":" << t.retained
                << ",\"overwriteCount\":" << t.overwritten << ",\"angleLimitedSamples\":" << t.limited
                << ",\"restoreAttempts\":" << t.attempts << ",\"restored\":" << (t.restored ? "true" : "false")
                << ",\"cleanupPending\":" << (t.dirty ? "true" : "false") << '}';
        }
        out << "],\"rootMotion\":"; root_motion::report(out);
        reference_pose::report(out);
        pelvis_helper::report(out);
        cloth_stabilizer::report(out);
        out << ",\"legIk\":"; leg_playback::report(out);
        out << ",\"performance\":{\"sharedPathResolution\":" << (gimmd::player_session::legacy_resolver ? "false" : "true")
            << ",\"batches\":" << performance.batches << ",\"averageBatchMs\":" << (performance.batches ? performance.total_ms/performance.batches : 0)
            << ",\"maxBatchMs\":" << performance.max_ms << ",\"totalResolveAndSampleMs\":" << performance.resolve_ms
            << ",\"totalWriteMs\":" << performance.write_ms << ",\"uniqueNodesValidated\":" << performance.unique_nodes
            << ",\"sharedPrefixesReused\":" << performance.reused_prefixes << '}';
    }
    out << '}';
}
}
