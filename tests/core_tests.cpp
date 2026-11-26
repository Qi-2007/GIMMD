#include "core/root_controls.h"
#include "core/camera_control.h"
#include "core/secondary_physics.h"
#include "core/body_collision.h"
#include "core/physics_controls.h"
#include "core/cloth_links.h"
#include "core/cloth_surface.h"
#include "core/skinned_cloth.h"
#include "core/collei_shoulder.h"
#include <sstream>
#include "vmd.h"
#include "core/timeline.h"
#include "core/player_transport.h"
#include "core/rotation_preview.h"
#include "core/fk_retarget.h"
#include "core/root_translation.h"
#include "core/leg_pose.h"
#include "core/camera_pose.h"
#include <bit>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>

namespace vmd = BetterEndfieldNext::Vmd;
static int checks = 0;
static void check(bool condition, const char* message) {
    ++checks;
    if (!condition) throw std::runtime_error(message);
}
static bool near(double a, double b) { return std::abs(a-b) < 1e-5; }
struct Bytes {
    std::vector<uint8_t> data;
    void u32(uint32_t n) { for(int i=0;i<4;++i) data.push_back(uint8_t(n >> (i*8))); }
    void real(float f) { u32(std::bit_cast<uint32_t>(f)); }
    void name(const std::string& text, size_t n) {
        for(size_t i=0;i<n;++i) data.push_back(i < text.size() ? uint8_t(text[i]) : 0);
    }
    void bone(const std::string& name_, uint32_t frame, float x) {
        name(name_,15); u32(frame);
        real(x);real(0);real(0);
        real(0);real(0);real(0);real(1);
        for(int i=0;i<64;++i) data.push_back(0); // diagonal curve is linear
    }
    void camera(uint32_t frame, float x, uint32_t fov) {
        u32(frame);real(-10);real(x);real(1);real(0);
        real(0);real(0);real(0);
        for(int i=0;i<24;++i) data.push_back(0);
        u32(fov);data.push_back(0);
    }
};
static Bytes fixture() {
    Bytes b;
    b.name("Vocaloid Motion Data 0002",30);b.name("synthetic",20);
    b.u32(3);
    b.bone("center",30,10);b.bone("center",0,0);b.bone("center",30,12);
    b.u32(2);
    b.name("blink",15);b.u32(0);b.real(0);
    b.name("blink",15);b.u32(30);b.real(1);
    b.u32(2);b.camera(0,0,30);b.camera(30,10,60);
    b.u32(0);b.u32(0); // lights, shadows
    b.u32(2);
    b.u32(0);b.data.push_back(1);b.u32(1);b.name("left_ik",20);b.data.push_back(1);
    b.u32(15);b.data.push_back(1);b.u32(1);b.name("left_ik",20);b.data.push_back(0);
    return b;
}
int wmain(int argc, wchar_t** argv) {
    try {
        namespace rp = gimmd::rotation_preview;
        bool limited = true;
        auto identity_delta = rp::delta({0,0,0.70710678f,0.70710678f}, {0,0,0.70710678f,0.70710678f}, &limited);
        check(!limited && near(identity_delta.w, 1), "source reference was not removed");
        auto capped = rp::delta({}, {0,1,0,0}, &limited);
        check(limited && near(capped.w, std::cos(2.5 * 3.141592653589793 / 180)), "rotation preview cap failed");
        auto equivalent = rp::delta({}, {0,0,0,-1}, &limited);
        check(!limited && near(equivalent.w, 1), "quaternion sign changed reference delta");
        check(std::size(gimmd::motion_profile::Mappings) == 52 && std::size(gimmd::motion_profile::PreviewIndices) == 10,
            "motion mapping coverage changed");
        auto bytes = fixture();
        vmd::Motion m;
        std::string error;
        check(vmd::Parse(bytes.data,m,error),"valid fixture rejected");
        check(m.last_frame==30 && m.duplicate_keys==1,"sort/duplicate metadata wrong");
        vmd::BoneSample bone;
        check(vmd::SampleBone(m.bones.at("center"),15,bone) && near(bone.position.x,6),"bone interpolation wrong");
        vmd::SampleBone(m.bones.at("center"),999,bone);
        check(near(bone.position.x,12),"end clamp wrong");
        vmd::SampleBone(m.bones.at("center"),std::numeric_limits<double>::quiet_NaN(),bone);
        check(near(bone.position.x,0),"NaN sampling not normalized");
        check(near(vmd::SampleMorph(m.morphs.at("blink"),15),0.5),"morph interpolation wrong");
        check(vmd::SampleSwitch(m.ik.at("left_ik"),14),"early IK switch wrong");
        check(!vmd::SampleSwitch(m.ik.at("left_ik"),15),"exact IK switch wrong");
        vmd::CameraSample camera;
        check(vmd::SampleCamera(m.cameras,15,camera) && near(camera.interest.x,5) && near(camera.fov,45),"camera interpolation wrong");
        for(size_t count=0;count<bytes.data.size();++count) {
            // A file may legally end at an optional-section boundary. Every
            // rejected prefix must leave the previously parsed output intact.
            vmd::Motion candidate=m;
            if(!vmd::Parse(std::span(bytes.data).first(count),candidate,error))
                check(candidate.bones.at("center").back().position.x==12,"parse is not transactional");
        }
        auto invalid=bytes.data;
        invalid[0]='X';
        check(!vmd::Parse(invalid,m,error),"invalid signature accepted");
        invalid=bytes.data;invalid.push_back(99);
        check(!vmd::Parse(invalid,m,error),"trailing data accepted");
        auto unnamed=bytes;
        std::fill(unnamed.data.begin()+54,unnamed.data.begin()+69,uint8_t(0));
        vmd::Motion unnamed_motion;
        check(vmd::Parse(unnamed.data,unnamed_motion,error)&&unnamed_motion.ignored_empty_bone_keys==1,
            "valid unnamed bone record rejected rather than counted/skipped");
        const auto unnamed_nan=std::bit_cast<uint32_t>(std::numeric_limits<float>::quiet_NaN());
        for (unsigned n=0;n<4;++n) unnamed.data[73+n]=uint8_t(unnamed_nan>>(n*8));
        check(!vmd::Parse(unnamed.data,unnamed_motion,error),"unnamed bone bypassed numeric validation");
        vmd::Limits limits;limits.bone_keys=1;
        check(!vmd::Parse(bytes.data,m,error,limits),"key count limit ignored");
        invalid=bytes.data;
        // First bone X at header(50) + count(4) + name(15) + frame(4).
        const auto nan=std::bit_cast<uint32_t>(std::numeric_limits<float>::quiet_NaN());
        for(int i=0;i<4;++i) invalid[73+i]=uint8_t(nan >> (i*8));
        check(!vmd::Parse(invalid,m,error),"nonfinite bone accepted");
        vmd::CameraKey cut_a,cut_b;cut_b.frame=1;cut_b.interest.x=10;
        vmd::SampleCamera({cut_a,cut_b},0.5,camera);
        check(near(camera.interest.x,0),"adjacent camera cut interpolated");
        check(near(vmd::Bezier(20,20,107,107,0.25),0.25),"linear curve wrong");
        const auto q=vmd::Slerp({0,0,0,1},{0,0,0,-1},0.5);
        check(near(std::abs(q.w),1),"quaternion sign discontinuity");

        {
            namespace physics=gimmd::secondary_physics;
            physics::Clock slow,fast;unsigned slow_steps=0,fast_steps=0;
            check(slow.sample(0,0,true).reset&&fast.sample(0,0,true).reset,"physics did not reset on start");
            for(unsigned i=1;i<=30;++i)slow_steps+=slow.sample(double(i)/30,0,true).steps;
            for(unsigned i=1;i<=120;++i)fast_steps+=fast.sample(double(i)/120,0,true).steps;
            check(slow_steps==60&&fast_steps==60,"physics depends on callback frequency");
            check(fast.sample(1,0,false).steps==0&&fast.sample(1,0,false).steps==0,"paused physics advanced");
            check(!fast.sample(1,0,true).reset&&fast.sample(1+physics::Clock::Step,0,true).steps==1,"resume lost simulation state");
            auto seek=fast.sample(5,1,false);check(seek.reset&&!seek.steps,"paused seek simulated stale velocity");
            auto loop=slow.sample(0,1,true);check(loop.reset&&!loop.steps,"loop failed to reset physics");
            auto stall=slow.sample(.5,1,true);check(stall.reset&&!stall.steps,"long stall triggered unbounded catch-up");
            check(!slow.sample(std::numeric_limits<double>::infinity(),1,true).valid,"nonfinite physics time accepted");
            gimmd::Timeline ending(1);physics::Clock end_physics;ending.play(0);end_physics.sample(0,0,true);
            for(unsigned i=1;i<60;++i){const auto f=ending.sample(double(i)/60);end_physics.sample(f.seconds,f.epoch,true);}
            const auto before_hold=ending.sample(1);ending.hold_end(1);const auto held=ending.sample(1);
            const auto final_step=end_physics.sample(held.seconds,held.epoch,false);
            check(held.epoch==before_hold.epoch&&held.state==gimmd::PlaybackState::Paused&&!final_step.reset&&final_step.steps==1,"natural end hold discarded the last physics step or reset the skirt");
            for(unsigned i=0;i<10;++i){ending.hold_end(2+i);const auto f=ending.sample(2+i);const auto hold=end_physics.sample(f.seconds,f.epoch,false);
                check(f.epoch==held.epoch&&!hold.reset&&!hold.steps,"held end changed physics epoch or advanced simulation");}
            ending.seek(0,12);check(end_physics.sample(0,ending.sample(12).epoch,false).reset,"explicit replay no longer resets physics");
        }
        {
            namespace cloth=gimmd::cloth_links;namespace bc=gimmd::body_collision;
            {
                // Barbara and Lumine have this fitted ring; their front
                // strips have different owners but the same orientations.
                const float weights[]={1,1,1,.54f,.54f,0,0};
                unsigned fronts=0,backs=0;
                for(float weight:weights){
                    const auto region=cloth::ring_region(true,weight);
                    if(region==cloth::RingRegion::Front)++fronts;
                    if(region==cloth::RingRegion::Rear)++backs;
                }
                check(fronts==3&&backs==2,"closed skirt regions did not follow the fitted branches");
                check(cloth::ring_region(true,.89f)==cloth::RingRegion::Front&&
                      cloth::ring_region(true,.12f)==cloth::RingRegion::Rear&&
                      cloth::ring_region(false,.90f)==cloth::RingRegion::Other&&
                      cloth::ring_region(false,0)==cloth::RingRegion::Other,"uncalibrated or open skirt acquired a ring profile");
                check(near(cloth::panel_half_width({0,0,1},{0,.01f,.9f},{0,.02f,1.2f},{0,0,1}),.075f),
                      "rotated skirt row measured width on the wrong axis");
            }
            // Lumine's last frame: left rear waist expands by 7.3 cm.
            check(near(cloth::rear_support_radius(.166265f,.073f,95),.17528925f)&&
                  near(cloth::rear_support_radius(.166265f,.073f,0),0),"rear waist forced the hem outward or ignored disabled support");
            {
                // Lumine at 23.467 s. Probe four cm radially inward from
                // the measured left rear joint, holding its height fixed.
                const bc::Vec joint{-.119787f,.083993f,-.147884f},outward{-.62941f,0,-.77707f};
                const auto probe=bc::sub(joint,bc::scale(outward,.04f));
                const bc::Capsule thigh{{-.076054f,.002189f,0},{.026261f,-.158883f,.346323f},.079083f};
                const bc::Capsule guard{{-.088331f,.021511f,-.041558f},{-.040243f,-.054194f,.121213f},.110728f};
                check(bc::distance(thigh,probe)>thigh.radius+.004f&&bc::distance(guard,probe)<guard.radius,
                      "end-pose fixture no longer isolates the upper-thigh guard");
                check(near(cloth::upper_neighbor_lift(0,.10f,true,.004f),.004f)&&
                      near(cloth::upper_neighbor_lift(.03f,.10f,true,.004f),.03f)&&
                      near(cloth::upper_neighbor_lift(0,.10f,false,.004f),.055f),
                      "rear neighbor displaced a clear waist or suppressed a direct contact");
                // A small overlap at the stitch should release; the same
                // bend still needs to clear a second capsule mid-panel.
                const bc::Vec rest{0,-.12f,0},proposed{.048f,-.10998182f,0};
                bc::Capsule bodies[2]={{{-.0095f,-.006f,0},{-.0095f,-.006f,0},.011f},
                                       {{-.003f,-.09f,0},{-.003f,-.09f,0},.024f}};
                const float tips[2]={.011f,.024f};
                float segments[2]={.011f,0};
                const auto released=bc::relax_rear_bend({},rest,proposed,bodies,tips,segments);
                check(bc::length(bc::sub(rest,released))<1e-6f,"pinned contact held the free rear hem outward");
                segments[1]=.024f;
                const auto held=bc::relax_rear_bend({},rest,proposed,bodies,tips,segments);
                const auto mid=bc::closest_segment({},held,bodies[1],1.f/3);
                check(held.x>0&&held.x<proposed.x&&near(bc::length(held),bc::length(rest))&&
                      bc::length(bc::sub(mid.point,mid.axis))>=segments[1]-.00011f,"rear retraction lost mid-panel clearance or changed length");
            }
            {
                // Lumine's recorded front: three waist attachments, each
                // with two descendants, all absent from the native arrays.
                struct Path {const wchar_t* name;int parent_node;};
                const Path paths[]={{L"Bip001 Spine",-1},{L"+SkirtF CF B01",0},{L"+SkirtF L B01",0},{L"+SkirtF R B01",0},
                    {L"+SkirtF CF B02",1},{L"+SkirtF L B02",2},{L"+SkirtF R B02",3},
                    {L"+SkirtF CF B03",4},{L"+SkirtF L B03",5},{L"+SkirtF R B03",6},{L"+SkirtS L B02",0}};
                struct Owned {bool real;unsigned path;};
                const Owned owned[]={{true,10},{true,5},{true,6}};
                unsigned chain[12]{},strips=0;
                for(unsigned i=0;i<11;++i){
                    const auto length=cloth::missing_skirt_chain(paths,11,i,owned,1,chain);
                    if(length){check(length==3&&chain[0]==i&&chain[1]==i+3&&chain[2]==i+6,"front strip mixed bone rows");++strips;}
                }
                check(strips==3,"Lumine did not acquire all three missing front strips");
                check(cloth::missing_skirt_chain(paths,11,1,owned,3,chain)==3&&
                      cloth::missing_skirt_chain(paths,11,2,owned,3,chain)==0&&cloth::missing_skirt_chain(paths,11,3,owned,3,chain)==0,
                      "native lateral strips were bound twice");
                const Path fork[]={{L"+SkirtF L B01",-1},{L"+SkirtF L B02",0},{L"+SkirtF L B03",1},{L"decoration",1}};
                check(cloth::missing_skirt_chain(fork,4,0,owned,0,chain)==0,"branched decoration acquired skirt physics");
                // Collei's six waist panels end at their second joint.
                const Path collei[]={{L"Bip001 Spine",-1},
                    {L"+SkirtF L F01",0},{L"+SkirtF R F01",0},{L"+SkirtS L I01",0},
                    {L"+SkirtS R I01",0},{L"+SkirtB L F01",0},{L"+SkirtB R F01",0},
                    {L"+SkirtF L F02",1},{L"+SkirtF R F02",2},{L"+SkirtS L I02",3},
                    {L"+SkirtS R I02",4},{L"+SkirtB L F02",5},{L"+SkirtB R F02",6},
                    {L"+LegBagS L E01",0},{L"+LegBagS L E02",13}};
                for(unsigned i=0;i<15;++i){
                    const auto length=cloth::missing_skirt_chain(collei,15,i,owned,0,chain);
                    if(i>=1&&i<=6)check(length==2&&chain[0]==i&&chain[1]==i+6,"missing or crossed Collei skirt panel");
                    else check(length==0,"duplicate or unrelated Collei panel");
                }
                const Owned native_tip[]={{true,8}};
                check(cloth::missing_skirt_chain(collei,15,2,native_tip,1,chain)==0,"native short skirt tip was acquired twice");
                const Path shoulder[]={{L"Bip001 L Clavicle",-1},{L"+SkirtS L I01",0},{L"+SkirtS L I02",1}};
                check(cloth::missing_skirt_chain(shoulder,3,1,owned,0,chain)==0,"shoulder accessory acquired waist physics");
            }
            cloth::Vertex ring[7]{};for(unsigned i=0;i<7;++i)ring[i]={i,float(i)*6.283185307f/7,0};
            check(cloth::closed_ring(ring,7,.1f),"closed seven-panel skirt was not recognized");
            check(!cloth::closed_ring(ring,5,.1f),"open sparse petals were joined");
            ring[6].angle=ring[5].angle;check(!cloth::closed_ring(ring,7,.1f),"duplicate directions formed a skirt link");
            bc::Vec a{0,.3f,0},old_a=a,b{1,0,0},old_b=b;
            check(cloth::distance_constraint(a,old_a,b,old_b,1,1)&&b.y>0&&near(a.y+b.y,.3)&&near(bc::length(bc::sub(a,b)),1),"neighbor stretch was not distributed symmetrically");
            check(near(bc::length(bc::sub(a,old_a)),0)&&near(bc::length(bc::sub(b,old_b)),0),"cloth link injected velocity");
            a={-1,0,.05f};b={1,0,.05f};old_a=a;old_b=b;
            check(cloth::project_pair({{0,-1,0},{0,1,0},.2f},.2f,a,old_a,b,old_b,{0,0,1})&&a.z>=.2f&&near(a.z,b.z),"cloth panel contact between noncontact endpoints was missed");
            check(near(a.z,old_a.z)&&near(b.z,old_b.z),"panel contact injected velocity");
            bc::Vec center{0,0,.2f},old_center=center; a={-.2f,0,0};b={.2f,0,0};old_a=a;old_b=b;
            check(cloth::arch_constraint(center,old_center,a,old_a,b,old_b,{0,0,1},.02f)&&center.z<.2f&&a.z>0&&near(a.z,b.z),"central skirt peak was not shared with both neighbors");
            check(near(center.z+a.z+b.z,.2f)&&near(center.z,old_center.z)&&near(a.z,old_a.z)&&near(b.z,old_b.z),"arch smoothing injected momentum or velocity");
            center={0,0,.02f};old_center=center;a={-.2f,0,0};b={.2f,0,0};old_a=a;old_b=b;
            check(!cloth::arch_constraint(center,old_center,a,old_a,b,old_b,{0,0,1},.02f),"fitted skirt arch was flattened");
        }
        {
            namespace bc=gimmd::body_collision;
            bc::Capsule capsule{{0,-1,0},{0,1,0},.2f};bc::Vec point{.1f,0,0},previous{.15f,0,0};
            check(bc::project(capsule,.2f,point,previous,{1,0,0})&&near(point.x,.2)&&near(previous.x,.2),"capsule contact retained inward velocity");
            point={0,0,0};previous=point;
            check(bc::project(capsule,.2f,point,previous,{0,1,0})&&bc::finite(point)&&near(bc::distance(capsule,point),.2),"capsule axis produced invalid contact normal");
            bc::Capsule sphere{{0,0,0},{0,0,0},.2f};point={0,.1f,0};previous=point;
            check(bc::project(sphere,.2f,point,previous,{0,1,0})&&near(point.y,.2),"degenerate capsule did not behave as a sphere");
            point={.4f,0,0};previous={.3f,0,0};
            check(!bc::project(capsule,.2f,point,previous,{1,0,0})&&near(point.x,.4),"noncontact particle was moved");
            point={0,0,0};check(bc::length_constraint({0,0,0},.3f,point,{0,0,1})&&near(point.z,.3),"collapsed chain segment did not recover its length");
            point={-1,0,0};check(bc::limit_direction({1,0,0},point,.2f)&&near(bc::length(point),1)&&near(point.x,std::cos(.2f)),"collision correction flipped a strand or changed its length");
            auto pair=bc::closest_segment({-1,0,0},{1,0,0},capsule);
            check(near(pair.fraction,.5)&&near(bc::length(bc::sub(pair.point,pair.axis)),0),"crossing segments were missed");
            pair=bc::closest_segment({.4f,-2,0},{.4f,2,0},capsule);
            check(near(bc::length(bc::sub(pair.point,pair.axis)),.4),"parallel segment distance is wrong");
            pair=bc::closest_segment({1,0,0},{1,0,0},sphere);
            check(near(bc::length(bc::sub(pair.point,pair.axis)),1),"two degenerate segments are invalid");
            pair=bc::closest_segment({0,2,0},{0,3,0},capsule);
            check(near(pair.point.y,2)&&near(pair.axis.y,1),"segment endpoints were not clamped");
            point={1,0,.1f};previous=point;
            check(bc::distance(capsule,point)>.2&&bc::project_segment(capsule,.2f,{-1,0,.1f},{-1,0,.1f},point,previous,{0,0,1}),"interior cloth contact with noncontact endpoints was missed");
            check(point.z>.29f&&near(point.z,previous.z),"segment contact injected velocity");
            point={1,0,.4f};previous=point;
            check(!bc::project_segment(capsule,.2f,{-1,0,.4f},{-1,0,.4f},point,previous,{0,0,1})&&near(point.z,.4),"noncontact segment was moved");
            point={-.1f,0,0};previous=point;
            check(bc::project_outward(capsule,.2f,point,previous,{1,0,0})&&point.x>=.2f&&point.x<.201f&&near(point.x,previous.x),"skirt contact exited toward the inner side or injected velocity");
            point={.1f,-.4f,0};previous={.15f,-.4f,0};
            check(bc::support_radius({}, {0,1,0},.3f,{1,0,0},point,previous)&&near(point.x,.3)&&near(point.y,-.4)&&near(point.x,previous.x),"skirt support changed height or retained inward velocity");
            point={.4f,-.4f,0};previous=point;
            check(!bc::support_radius({}, {0,1,0},.3f,{1,0,0},point,previous)&&near(point.x,.4),"skirt support blocked outward swing");
            point={0,-.4f,0};previous=point;
            check(bc::support_radius({}, {0,1,0},.3f,{1,0,0},point,previous)&&near(point.x,.3)&&bc::finite(point),"collapsed skirt support failed");
            bc::Capsule hip{{0,0,0},{0,0,0},.08f};point={.9f,0,.05f};previous=point;
            check(!bc::project_segment_outward(hip,.08f,{-.1f,0,.05f},{-.1f,0,.05f},point,previous,{0,0,1}),"lower skirt exclusion unexpectedly covered its fixed end");
            check(bc::project_segment_outward(hip,.08f,{-.1f,0,.05f},{-.1f,0,.05f},point,previous,{0,0,1},.05f)&&point.z>.2f&&near(point.z,previous.z),"upper skirt contact near the anchor was skipped or injected velocity");
            const auto transported=bc::transport_direction({0,-1,0},{.5f,-.8660254f,0},{0,-.2f,0});
            check(near(transported.x,.1)&&near(transported.y,-.17320508)&&near(bc::length(transported),.2),"cloth end did not follow its parent correction or changed length");
            const auto reversed=bc::transport_direction({0,-1,0},{0,1,0},{0,-.2f,0});
            check(bc::finite(reversed)&&near(reversed.y,.2),"opposite direction transport became singular");
        }
        {
            namespace bc=gimmd::body_collision;
            const bc::Vec outward{0,0,1},up{0,1,0};
            const bc::Capsule raised{{0,0,0},{0,.048f,.312f},.063f};
            const auto push=bc::flap_contact_direction(outward,up,raised);
            bc::Vec flap{0,.025f,.18f},history=flap;
            check(bc::project_outward(raised,raised.radius,flap,history,push)&&flap.y>.08f&&bc::distance(raised,flap)>=raised.radius,"front flap failed to lift above a horizontal thigh");
            check(bc::flap_contact_direction(outward,up,{{},{0,-1,0},.06f}).y==0,"standing thigh lifted the flap");
            gimmd::physics_controls::Settings settings;
            std::istringstream extended("GIMMD_PHYSICS_V1 180 180 3 3 20 20 95");
            check(gimmd::physics_controls::read(extended,settings)&&near(settings.front_angle,180)&&near(settings.rear_angle,180),"full skirt angle range refused");
            std::istringstream valid("GIMMD_PHYSICS_V1\n25 9 1.6 .3 80 220 94\n");
            check(gimmd::physics_controls::read(valid,settings)&&near(settings.front_angle,25)&&near(settings.rear_response_ms,220)&&near(settings.support_percent,94),"custom physics controls were not read");
            for(const char* input:{"GIMMD_PHYSICS_V2 24 10 1.5 .4 60 160 95", "GIMMD_PHYSICS_V1 24 10", "GIMMD_PHYSICS_V1 181 10 1.5 .4 60 160 95","GIMMD_PHYSICS_V1 24 181 1.5 .4 60 160 95",
                "GIMMD_PHYSICS_V1 24 10 -1 .4 60 160 95", "GIMMD_PHYSICS_V1 24 10 1.5 .4 0 160 95", "GIMMD_PHYSICS_V1 24 10 1.5 .4 60 160 101",
                "GIMMD_PHYSICS_V1 24 10 nan .4 60 160 95", "GIMMD_PHYSICS_V1 24 10 1.5 .4 60 160 95 extra"}){
                std::istringstream invalid_controls(input);check(!gimmd::physics_controls::read(invalid_controls,settings)&&near(settings.front_angle,25)&&near(settings.support_percent,94),"invalid physics controls partially replaced settings");}
        }
        {
            namespace surface=gimmd::cloth_surface;namespace bc=gimmd::body_collision;
            {
                // Recorded right upper arm at 23.466 s. Place guide strips
                // either side of it so only the fabric connecting them hits.
                const bc::Capsule arm{{.091444f,.379601f,-.209846f},{.259732f,.455731f,-.105431f},.033948f};
                const bc::Vec up{0,1,0};
                const auto axis=bc::sub(arm.b,arm.a),along=bc::scale(axis,.45f);
                const auto lateral=bc::cross(up,axis);
                const auto edge=bc::scale(lateral,.065f/bc::length(lateral));
                const auto seam=bc::add(arm.a,bc::scale(axis,.12f));
                const auto a=bc::sub(seam,edge),b=bc::add(seam,edge);
                const bc::Vec panel[]={a,bc::add(a,along),b,bc::add(b,along)};
                check(surface::cape_panel_depth(a,along,up,.012f,arm)==0&&surface::cape_panel_depth(b,along,up,.012f,arm)==0&&
                      surface::side_depth(panel,arm)>.033f,"upper arm fixture did not isolate the gap between cape strips");
                const auto shift=surface::shoulder_cover_shift(panel,arm,up);
                check(bc::dot(shift,up)>0&&std::abs(bc::dot(shift,axis))<1e-6f&&bc::length(shift)<.035f&&
                      surface::side_depth(panel,arm,shift)<.0002f,"connected cape did not clear the raised upper arm");
                const bc::Capsule distant{bc::add(arm.a,up),bc::add(arm.b,up),arm.radius};
                const bc::Capsule lowered{arm.a,bc::add(arm.a,{0,-.20f,0}),arm.radius};
                check(bc::length(surface::shoulder_cover_shift(panel,distant,up))==0&&
                      bc::length(surface::shoulder_cover_shift(panel,lowered,up))==0,"relaxed shoulder cover lifted without a raised arm contact");
            }
            {
                namespace shoulder=gimmd::collei_shoulder;
                const bc::Capsule arm{{0,0,0},{.212f,0,0},.034f};
                for(unsigned edge=0;edge<2;++edge){
                    const float sign=edge?-1.f:1.f;
                    const bc::Vec facing{0,0,sign},up{0,1,0};
                    const auto& vertices=shoulder::trim[edge];
                    bc::Vec panel[]={{.07f,-.01f,sign*.037f},{.13f,-.01f,sign*.037f},
                                     {.07f,.01f,sign*.037f},{.13f,.01f,sign*.037f}};
                    check(surface::side_depth(panel,arm)==0&&surface::side_depth(panel,shoulder::cuff(arm))>.003f,
                          "cuff fixture did not isolate the front/back trim");
                    const auto shift=shoulder::trim_shift(vertices,panel,arm,up,facing);
                    check(bc::dot(shift,facing)>0&&bc::length(shift)<.025f,"trim pushed through the arm opening");
                    for(unsigned i=0;i<4;++i)panel[i]=bc::add(panel[i],bc::scale(shift,vertices[i].flap_weight));
                    check(surface::side_depth(panel,shoulder::cuff(arm))<.00015f,"weighted trim left inside cuff");
                    const bc::Capsule away{{0,1,0},{.212f,1,0},.034f};
                    check(bc::length(shoulder::trim_shift(vertices,panel,away,up,facing))==0,"distant arm moved trim");
                }
            }
            {
                namespace shoulder=gimmd::collei_shoulder;
                // Left arm from the last reported pose. Probe the two flanks
                // of the forward reach, both at the cuff and near the shoulder.
                const bc::Capsule arm{{-.100991f,.359912f,-.071714f},{-.128192f,.428103f,.127356f},.033948f};
                const bc::Vec up{0,1,0};
                const auto axis=bc::sub(arm.b,arm.a),tangent=bc::cross(up,axis);
                const auto side=bc::scale(tangent,1/bc::length(tangent));
                const auto vertical=bc::scale(bc::cross(axis,side),1/bc::length(axis));
                for(bool proximal:{false,true})for(unsigned edge=0;edge<2;++edge){
                    const float sign=edge?-1.f:1.f;
                    const auto offset=bc::scale(side,sign*(proximal?.030f:.036f));
                    const auto base=bc::add(arm.a,offset);
                    const auto a=bc::add(base,bc::scale(axis,proximal?.01f:.35f));
                    const auto b=bc::add(base,bc::scale(axis,proximal?.07f:.55f));
                    const auto half=bc::scale(vertical,.007f);
                    bc::Vec patch[]={bc::sub(a,half),bc::sub(b,half),bc::add(a,half),bc::add(b,half)};
                    const auto sleeve=shoulder::cuff(arm);
                    check(proximal?(surface::side_depth(patch,arm)>.003f&&surface::side_depth(patch,sleeve)==0):
                                   surface::side_depth(patch,sleeve)>.004f,"forward reach fixture missed its contact");
                    const auto& vertices=shoulder::trim[edge];
                    const auto shift=shoulder::trim_shift(vertices,patch,arm,up,{0,0,sign});
                    check(sign*bc::dot(shift,side)>.003f&&std::abs(bc::dot(shift,axis))<1e-6f,"forward arm lost its lateral escape direction");
                    for(unsigned i=0;i<4;++i)patch[i]=bc::add(patch[i],bc::scale(shift,vertices[i].flap_weight));
                    check(std::max(surface::side_depth(patch,sleeve),surface::side_depth(patch,arm))<.00015f,
                          "forward upper arm remained inside weighted shoulder trim");
                }
            }
            {
                namespace shoulder=gimmd::collei_shoulder;
                // Alternate real contact with one or two missed fixed steps.
                // Exercise the position and angle response in their own units.
                for(bool angular:{false,true}){
                    const float gain=angular?.45f:.35f,limit=angular?.12f:.008f;
                    const bc::Vec target{angular?.8f:.025f,0,0};
                    shoulder::ContactDamping damping{};
                    bc::Vec value{};
                    const auto advance=[&](bc::Vec request){
                        const float response=damping.response(value,request,gain,limit);
                        value=bc::add(value,bc::scale(bc::sub(request,value),response));
                    };
                    advance(target);
                    check(near(value.x,std::min(gain*target.x,limit)),"initial shoulder contact lagged");
                    for(unsigned i=0;i<70;++i)advance(target);
                    float dropout_error=0;
                    for(unsigned i=0;i<30;++i){
                        advance(i%3==0?target:bc::Vec{});
                        dropout_error=std::max(dropout_error,std::abs(value.x-target.x));
                    }
                    check(dropout_error<.00001f,"intermittent shoulder contact chattered");
                    advance({});
                    check(value.x<target.x&&value.x>0,"confirmed shoulder separation did not release");
                    for(unsigned i=0;i<60;++i)advance({});
                    check(value.x<.00001f,"shoulder correction remained after sustained separation");
                    for(unsigned i=0;i<70;++i)advance(target);
                    const float noise=limit*.4f;
                    for(unsigned i=0;i<50;++i)advance({target.x+(i%2?-noise:noise),0,0});
                    const float low=value.x;
                    advance({target.x+noise,0,0});
                    const float previous_ripple=2*noise*gain/(2-gain);
                    check(value.x-low<.75f*previous_ripple,"shoulder damping did not attenuate alternating nonzero contacts");
                }
            }
            {
                // Recorded pose at 10.938 s. These front-piece vertices are
                // fully weighted to AmiceF A21 and its child A31.
                const gimmd::skinned_cloth::Frame frame{{{.756653f,.642135f,-.123043f},{-.038131f,.231211f,.972157f},
                                                        {.652704f,-.730894f,.199431f}},{-.064363f,.460576f,-.049547f}};
                const bc::Capsule arm{{-.104676f,.361793f,-.074870f},{-.148082f,.397240f,.129776f},.033948f};
                const bc::Vec caught[]={{-.118093f,.383485f,-.020316f},{-.118369f,.343426f,-.001348f},{-.139343f,.383380f,-.023551f}};
                const auto turn=gimmd::collei_shoulder::front_piece_turn(frame,false,arm,{-1,0,0},{0,0,1});
                const auto angle=bc::length(turn);
                check(angle>.01f&&angle<=.96f,"front piece ignored the recorded arm crossing");
                const auto axis=bc::scale(turn,1/angle);
                for(auto p:caught){
                    const auto moved=bc::add(frame.position,gimmd::skinned_cloth::axial_turn(bc::sub(p,frame.position),axis,angle));
                    check(bc::distance(arm,p)<arm.radius&&bc::distance(arm,moved)>arm.radius,"front cloth vertex remained inside arm");
                }
                namespace shoulder=gimmd::collei_shoulder;
                const bc::Vec lift{-.008482f,.068238f,-.019974f};
                const auto fit=shoulder::fit_front_drape(frame,false,arm,{-1,0,0},{0,0,1},lift);
                check(fit.release>.99f,"lifted front seam did not settle");
                const auto shift=shoulder::front_seam_shift(lift,fit.release);
                const float target_angle=bc::length(fit.turn);
                const auto target_axis=bc::scale(fit.turn,1/target_angle);
                bc::Vec offsets[6],settled[6];shoulder::front_offsets(frame,false,offsets);
                float old_gap=0,new_gap=0;
                for(unsigned i=0;i<6;++i){
                    const auto old=bc::add(frame.position,gimmd::skinned_cloth::axial_turn(offsets[i],axis,angle));
                    settled[i]=bc::add(bc::add(frame.position,shift),gimmd::skinned_cloth::axial_turn(offsets[i],target_axis,target_angle));
                    old_gap+=bc::distance(arm,old)-arm.radius;
                    new_gap+=bc::distance(arm,settled[i])-arm.radius;
                }
                check(new_gap<old_gap*.65f,"fitted front sheet still stands away from the arm");
                for(unsigned row:{0u,2u}){
                    const bc::Vec panel[]={settled[row],settled[row+1],settled[row+2],settled[row+3]};
                    check(shoulder::patch_depth(panel,arm)<.0003f,"lowered seam crossed the sleeve");
                }
                auto clear=frame;clear.position.y+=.2f;
                check(shoulder::fit_front_drape(clear,false,arm,{-1,0,0},{0,0,1},lift).release==0,"clear front seam moved");
                check(bc::length(gimmd::collei_shoulder::front_piece_turn(clear,false,arm,{-1,0,0},{0,0,1}))==0,
                      "front piece with clearance received a correction");
            }
            {
                // At 10.057 s the left arm points forward. The middle scallop
                // can touch its cuff while the front and back edges are clear.
                namespace shoulder=gimmd::collei_shoulder;
                const bc::Capsule arm{{-.108520f,.355030f,-.061036f},{-.205419f,.411004f,.119233f},.033948f};
                const bc::Vec up{0,1,0},forward{0,0,1};
                const auto axis=bc::sub(arm.b,arm.a),cross=bc::cross(up,axis);
                const auto side=bc::scale(cross,1/bc::length(cross));
                const auto top=bc::scale(bc::cross(axis,side),1/bc::length(axis));
                bc::Vec rim[6];
                for(unsigned row=0;row<3;++row)for(unsigned column=0;column<2;++column){
                    const auto center=bc::add(arm.a,bc::scale(axis,column?.57f:.33f));
                    rim[2*row+column]=bc::add(center,bc::add(bc::scale(side,(float(row)-1)*.055f),bc::scale(top,.034f)));
                }
                const auto shift=shoulder::rim_support(rim,arm,up,forward);
                check(bc::dot(shift,top)>.006f&&bc::length(shift)<.010f,"forward cuff missed central scallop");
                for(unsigned i=0;i<6;++i)rim[i]=bc::add(rim[i],bc::scale(shift,1-shoulder::rim[i].side_front_weight));
                for(unsigned i:{0u,2u}){
                    const bc::Vec patch[]={rim[i],rim[i+1],rim[i+2],rim[i+3]};
                    check(shoulder::patch_depth(patch,arm)<.00015f,"settled central rim still intersects cuff");
                }
                check(bc::length(shoulder::rim_support(rim,arm,up,forward))<.00001f,"clear central rim lifted twice");
            }
            {
                // The palm meets the edge of a cape halfway down a sewn link:
                // both the centerline and the existing tip-only contact miss.
                const bc::Vec direction{0,-.12f,0},normal{0,0,1};
                surface::CapeArm arms[2]{};
                for(unsigned hand=0;hand<2;++hand){
                    const float x=hand?.033f:-.033f;
                    const bc::Capsule palm{{x,-.06f,-.006f},{x,-.06f,.006f},.012f};
                    arms[hand][2]=palm;
                    const auto pair=bc::closest_segment({},direction,palm);
                    check(bc::length(bc::sub(pair.point,pair.axis))>palm.radius&&
                          surface::cape_panel_depth({},direction,normal,.04f,palm)>.008f,"cape edge fixture missed palm overlap");
                    const auto fitted=surface::fit_cape_panel({},direction,direction,normal,.04f,.7f,arms);
                    check(surface::cape_panel_depth({},fitted,normal,.04f,palm)<.0005f&&
                          near(bc::length(fitted),bc::length(direction)),"palm penetrated fitted cape or stretched the seam");
                    arms[hand]={};
                }
                const auto free=surface::fit_cape_panel({},direction,direction,normal,.04f,.7f,arms);
                check(bc::length(bc::sub(free,direction))==0,"clear cape pose changed");
                const bc::Vec wrist{},knuckles[]={{-.045f,0,.035f},{-.03f,0,.07f},{0,0,.08f},{.015f,0,.075f},{.03f,0,.065f}};
                bc::Vec distal[5];for(unsigned i=0;i<5;++i)distal[i]=bc::add(knuckles[i],{0,-.03f,.02f});
                const auto arm=surface::cape_arm({}, {},wrist,knuckles,distal);
                for(const auto& joint:knuckles){
                    bool covered=false;
                    for(unsigned i=2;i<arm.size();++i)covered=covered||bc::distance(arm[i],joint)<=arm[i].radius;
                    check(covered,"finger root on bent hand lacks coverage");
                }
                check(arm[2].radius>0&&bc::distance(arm[2],{0,0,.04f})<arm[2].radius&&
                      bc::distance(arm[8],distal[4])<.0001f,"palm center or bent pinky is uncovered");
            }
            {
                // Collei at 23.467 s: the right front-to-side panel crosses
                // the thigh guard on its fixed edge. Use 2 cm of overlap.
                const bc::Vec panel[3]={{.097368f,.019956f,.115762f},{.151543f,.021851f,-.004378f},{.224935f,-.019714f,.029052f}};
                const bc::Vec outward[3]={{.64f,0,.768375f},{1,0,0},{1,0,0}};
                const bc::Capsule guard={{.075775f,.044844f,-.005988f},{.076888f,-.139515f,.017465f},.110728f};
                const auto hit=surface::nearest_capsule(panel,guard);
                const float radius=bc::length(bc::sub(hit.point,hit.axis))+.02f;
                check(hit.weights[2]<.001f,"Collei fixture missed the pinned edge");
                const float shift=surface::short_panel_shift(panel,outward,guard,radius,.04f);
                bc::Vec moved[3];
                for(unsigned i=0;i<3;++i)moved[i]=bc::add(panel[i],bc::scale(outward[i],shift));
                const auto clear=surface::nearest_capsule(moved,guard);
                check(shift>0&&shift<.04f&&bc::length(bc::sub(clear.point,clear.axis))>=radius-.00001f,
                      "Collei pinned-edge contact persisted after moving both attachments");
                check(surface::short_panel_shift(moved,outward,guard,radius,.04f)==0&&
                      surface::short_panel_shift(panel,outward,guard,radius,.005f)==.005f,
                      "clear panel kept expanding or exceeded its travel limit");
            }
            {
                // Lumine's saved central front B02/B03 and left leg near
                // the 14.73 s seek. Adjacent strips bracket the central row.
                const bc::Vec joint{.005421f,.109494f,.215804f},end{.015566f,.108529f,.348871f};
                const auto link=bc::sub(end,joint);
                const bc::Capsule thigh{{-.076087f,.001023f,-.000012f},{.008430f,.113805f,.369439f},.079083f};
                const bc::Capsule shin{thigh.b,{-.004105f,-.248557f,.170141f},.082746f};
                const auto fitted=surface::fit_over_knee(joint,link,{0,1,0},{1,0,0},{0,0,1},.075f,thigh,shin,.01f);
                const auto before=bc::distance(thigh,end),after=bc::distance(thigh,bc::add(joint,fitted));
                check(before<thigh.radius-.05f&&after>=thigh.radius&&fitted.y>link.y&&near(bc::length(fitted),bc::length(link)),
                      "captured front joint remained inside the raised knee or stretched");
                const auto overlap=[&](bc::Vec v){
                    const auto a=bc::add(joint,bc::scale(v,.35f)),b=bc::add(joint,v);const bc::Vec edge{.075f,0,0};
                    const bc::Vec panel[]{bc::sub(a,edge),bc::sub(b,edge),bc::add(a,edge),bc::add(b,edge)};
                    return std::max(surface::side_depth(panel,thigh),surface::side_depth(panel,shin));
                };
                check(overlap(fitted)+.025f<overlap(link),"raising the front joint did not improve the cloth surface");
                const bc::Capsule standing{thigh.a,bc::add(thigh.a,{0,-.4f,0}),thigh.radius};
                check(bc::length(bc::sub(surface::fit_over_knee(joint,link,{0,1,0},{1,0,0},{0,0,1},.075f,standing,shin,.01f),link))==0,
                      "standing thigh raised the front hem");
            }
            {
                // Nahida at 19.075 s: the extended leg crosses the middle
                // of the left side panel, without touching any corner.
                const bc::Vec side[4]={{-.067893f,-.003904f,.133190f},{-.107714f,-.070624f,.235909f},
                                       {-.135932f,-.033900f,-.079260f},{-.222987f,-.129041f,-.116729f}};
                const bc::Capsule leg{{-.058066f,.004667f,0},{-.264077f,-.204606f,.117011f},.063226f};
                for(const auto p:side)check(bc::distance(leg,p)>leg.radius,"side-contact fixture touches a corner");
                const float before_depth=surface::side_depth(side,leg);
                const auto shift=surface::side_offset(side,leg,{-1,0,0},{0,1,0},.085f);
                check(before_depth>.059f&&surface::side_depth(side,leg,shift)<.018f,"side panel still intersects the extended thigh");
                check(shift.x<0&&shift.y>0&&bc::length(shift)<=.08501f,"side panel correction exceeded travel or moved inward");
                const bc::Capsule forward{{-.058f,0,0},{-.058f,.04f,.313f},.063f};
                check(bc::length(surface::side_offset(side,forward,{-1,0,0},{0,1,0},.085f))==0,"front knee lift activated side correction");
                const bc::Vec far_side[4]={{.118455f,.017079f,.123590f},{.164356f,-.094544f,.168509f},
                                          {.180498f,.000993f,-.070253f},{.251626f,-.112624f,-.078745f}};
                check(surface::side_depth(far_side,leg)==0,"side contact crossed to the far panel");
                bc::Vec mirrored[4];for(unsigned i=0;i<4;++i)mirrored[i]={-side[i].x,side[i].y,side[i].z};
                const bc::Capsule other{{-leg.a.x,leg.a.y,leg.a.z},{-leg.b.x,leg.b.y,leg.b.z},leg.radius};
                const auto opposite=surface::side_offset(mirrored,other,{1,0,0},{0,1,0},.085f);
                check(bc::length(bc::sub(opposite,{-shift.x,shift.y,shift.z}))<.0001f,"mirrored side contact is asymmetric");
            }
            bc::Vec triangle[3]{{-1,-1,.05f},{1,-1,.05f},{0,1,.05f}};
            const bc::Capsule leg{{0,-.3f,0},{0,.3f,0},.2f};
            const auto nearest=surface::nearest_capsule(triangle,leg);
            check(surface::panel_in_range(triangle,leg,.05f)&&!surface::panel_in_range(triangle,leg,.04f),
                  "panel bounds lost a tangent or used the body radius instead of the fitted radius");
            unsigned culled=0,intersections=0;bool all_contacts_kept=true;
            for(int x=-8;x<=8;++x)for(int z=-8;z<=8;++z){
                const bc::Capsule limb{{x*.12f,-.3f,z*.12f},{x*.12f+.2f,.4f,z*.12f-.1f},.2f};
                const auto hit=surface::nearest_capsule(triangle,limb);
                const bool overlaps=bc::length(bc::sub(hit.point,hit.axis))<limb.radius;
                const bool in_range=surface::panel_in_range(triangle,limb,limb.radius);
                if(overlaps)++intersections;
                if(!in_range)++culled;
                if(overlaps&&!in_range)all_contacts_kept=false;
            }
            check(intersections&&culled&&all_contacts_kept,"panel broad phase discarded an exact contact");
            check(near(bc::length(bc::sub(nearest.point,nearest.axis)),.05)&&near(nearest.weights[0]+nearest.weights[1]+nearest.weights[2],1),"triangle interior contact was missed");
            const auto crossing=surface::nearest_capsule(triangle,{{0,0,-1},{0,0,1},.2f});
            check(near(bc::length(bc::sub(crossing.point,crossing.axis)),0),"capsule axis crossing a panel was missed");
            bc::Vec previous[3]{triangle[0],triangle[1],triangle[2]};const bool movable[3]{true,true,true};
            check(surface::project(leg,.2f,triangle,previous,movable,{0,0,1})&&triangle[0].z>.05f&&triangle[1].z>.05f&&triangle[2].z>.05f,"panel contact was not distributed across its bones");
            check(near(triangle[0].z,previous[0].z)&&near(triangle[1].z,previous[1].z)&&near(triangle[2].z,previous[2].z),"panel surface contact injected velocity");
            bc::Vec collapsed[3]{};const auto degenerate=surface::nearest_capsule(collapsed,{{1,0,0},{1,0,0},.2f});
            check(bc::finite(degenerate.point)&&near(bc::length(bc::sub(degenerate.point,degenerate.axis)),1),"degenerate panel generated invalid geometry");
        }
        {
            namespace skin=gimmd::skinned_cloth;namespace bc=gimmd::body_collision;
            {
                // Measured front flap at 19.042 s, in the hip frame.
                const bc::Vec joint{-.129902f,.018478f,.063461f};
                const bc::Vec native_tip{-.177807f,-.045677f,.164343f};
                const bc::Vec cleared_tip{-.193281f,.004706f,.174736f};
                const auto lower=bc::sub(native_tip,joint);
                const bc::Vec upper{-.02f,-.12f,.07f},up{0,1,0};
                const bc::Vec outward{-1,0,0};
                const auto hem=skin::hem_bend(lower,bc::sub(cleared_tip,joint),up,outward,bc::length(lower));
                const auto followed=skin::follow_hem(upper,up,outward,hem,bc::length(lower));
                const auto moved_lower=bc::transport_direction(upper,followed,lower);
                auto aimed=bc::scale(followed,bc::length(upper)/bc::length(followed));
                const auto carried=bc::add(bc::sub(aimed,upper),bc::sub(moved_lower,lower));
                check(carried.y>.005f&&carried.y<cleared_tip.y-native_tip.y,"upper link did not share the measured hem rise");
                const auto released=skin::hem_bend(lower,bc::add(lower,{.02f,-.03f,0}),up,outward,bc::length(lower));
                check(bc::length(bc::sub(skin::follow_hem(upper,up,outward,released,bc::length(lower)),upper))==0,"released hem left a bend at the waist");
                const auto shared=skin::share_hem_bend({},hem);
                check(followed.x<upper.x&&shared.outward>0&&near(shared.rise,.3f*hem.rise),"front flap did not distribute outward and upward contact");
                const auto same=skin::follow_hem(bc::scale(upper,2),up,outward,hem,bc::length(lower)*2);
                check(bc::length(bc::sub(same,bc::scale(followed,2)))<1e-6f,"hem feedback depends on avatar size");
                // A representative 4.95 cm side shift, with the measured
                // thigh and panel corner. The joint must keep its clearance.
                const bc::Vec shift{-.04f,.027f,-.011f};
                const bc::Capsule thigh{{-.101267f,.000159f,-.060732f},{-.285595f,-.213265f,.082146f},.063226f};
                const auto unshifted=aimed;
                const auto residual=skin::share_panel_shift(upper,aimed,shift,.55f);
                const auto placed=bc::add(joint,bc::sub(bc::add(bc::sub(aimed,unshifted),residual),shift));
                check(bc::length(residual)<bc::length(shift)&&bc::length(bc::sub(aimed,unshifted))>.01f,"upper joint stayed parallel during lateral clearance");
                check(bc::length(bc::sub(placed,joint))<1e-6f&&near(bc::length(aimed),bc::length(upper))&&bc::distance(thigh,placed)>thigh.radius,"sharing side correction moved the cleared joint or stretched the link");
                aimed=upper;
                const auto locked_shift=skin::share_panel_shift(upper,aimed,shift,0);
                check(bc::length(bc::sub(locked_shift,shift))<1e-6f&&bc::length(bc::sub(aimed,upper))<1e-6f,"angle limit lost side clearance");
                const auto half_lift=bc::scale(up,.025f);
                aimed=upper;
                const auto rest_shift=skin::share_panel_shift(upper,aimed,half_lift,.55f);
                check(bc::length(bc::sub(aimed,upper))>.001f&&bc::length(bc::sub(bc::add(bc::sub(aimed,upper),bc::add(half_lift,rest_shift)),bc::scale(half_lift,2)))<1e-6f,"sharing front lift changed the cleared joint height");
            }
            {
                struct Node {bool real,skirt;int parent_global;unsigned path;};
                Node nodes[]={{true,true,-1,5},{false,true,0,0},{true,true,-1,9},{true,true,2,10},{true,true,3,11}};
                check(skin::hem_segment(nodes,5,5)==1&&skin::hem_segment(nodes,5,9)==3,"short open flap or long skirt chain missed automatic binding");
                nodes[4].parent_global=2;
                check(skin::hem_segment(nodes,5,9)<0,"branched skirt acquired an arbitrary lower link");
                nodes[1].skirt=false;
                check(skin::hem_segment(nodes,5,5)<0&&skin::hem_segment(nodes,5,99)<0,"non-skirt or missing link acquired hem feedback");
            }
            {
                // Reconstruct front-center samples between the measured flap
                // chains at 14.797 s. Both centerlines clear the raised thigh.
                skin::FrontPose pose{};
                const bc::Vec left_joint{-.13562f,.11004f,.17004f},right_joint{.10973f,.10576f,.18887f};
                const bc::Vec left_tip{-.20979f,.14406f,.26969f},right_tip{.16772f,.16214f,.28908f};
                for(unsigned i=0;i<4;++i){
                    pose.origin[i]=i<2?left_joint:right_joint;
                    auto d=bc::sub(i<2?left_tip:right_tip,pose.origin[i]);pose.axis[i]=bc::scale(d,1/bc::length(d));
                }
                for(unsigned row=0;row<3;++row){
                    const float t=1-.5f*row;
                    const auto mid=bc::scale(bc::add(bc::scale(bc::add(left_tip,right_tip),t),
                                                     bc::scale(bc::add(left_joint,right_joint),1-t)),.5f);
                    for(unsigned i=0;i<4;++i)pose.offset[row][i]=bc::sub(mid,pose.origin[i]);
                }
                const bc::Capsule knee{{-.05824f,.00120f,0},{.00493f,.10711f,.29108f},.06323f},down{{},{0,-.3f,0},.063f};
                float angles[4]{};
                const auto deficit=[&](){float result=0;for(unsigned row=0;row<3;++row){
                    const auto v=pose.sample(row,angles);result=std::max(result,bc::inner_flap_lift(v,v,{0,1,0},knee));}return result;};
                check(deficit()>.02f,"fixture failed to reproduce front seam contact");
                for(unsigned step=0;step<30;++step)skin::support_front(pose,{0,1,0},knee,down,.523599f,angles);
                check(deficit()<.005f,"paired axial support did not clear the centerline");
                for(unsigned i=0;i<4;++i)check(bc::length(bc::sub(skin::axial_turn(pose.axis[i],pose.axis[i],angles[i]),pose.axis[i]))<1e-5f,"seam support displaced a joint endpoint");
                // Enter the same lift from an uneven fold. Previously contact
                // was clear but the adjacent joints stayed 24 and 12 deg apart.
                angles[0]=.35f;angles[1]=-.14f;angles[2]=-.07f;angles[3]=-.28f;
                for(unsigned step=0;step<30;++step)skin::support_front(pose,{0,1,0},knee,down,.523599f,angles);
                check(std::abs(angles[0]-angles[1])<.37f&&std::abs(angles[2]-angles[3])<.07f&&deficit()<.001f,
                      "front flap retained an isolated twist or lost knee clearance");
                for(unsigned step=0;step<30;++step)skin::support_front(pose,{0,1,0},down,down,.523599f,angles);
                for(float angle:angles)check(std::abs(angle)<1e-5f,"front seam twist did not release");
            }
            {
                // Collei's right shoulder ribbon at the recorded paused pose,
                // replayed in upright coordinates with the shoulder stationary.
                const std::array<bc::Vec,6> native={{{.162636f,.313063f,-.101773f},{.279618f,.254980f,-.086521f},
                    {.396018f,.196449f,-.070620f},{.514408f,.133696f,-.051400f},
                    {.641912f,.051382f,-.022105f},{.745530f,-.053682f,.024126f}}};
                auto current=native,previous=native;
                float stretch=0;
                for(unsigned frame=0;frame<180;++frame){
                    const auto old=current;
                    for(unsigned j=1;j<native.size();++j){
                        const auto rest=bc::sub(native[j],native[j-1]);
                        const float span=bc::length(rest);
                        current[j]=skin::predict_drape(old[j],previous[j],current[j-1],rest,span,j,1.f/60);
                        stretch=std::max(stretch,std::abs(bc::length(bc::sub(current[j],current[j-1]))-span));
                    }
                    previous=old;
                }
                check(stretch<1e-5f&&bc::length(bc::sub(current[0],native[0]))==0,"draped shoulder seam moved or ribbon stretched");
                check(current[4].x-current[0].x<.24f&&current[4].y<native[4].y-.15f,"Collei ribbon stayed splayed in the held shoulder pose");
                check(bc::length(bc::sub(current.back(),previous.back()))<.0002f,"draped ribbon failed to settle");
            }
            {
                // E03 to E04 in the saved Collei pose. An accessory mount
                // models a rotating belt or shoulder joint with a fixed seam.
                const bc::Vec rest{-.052097f,-.087110f,-.026027f};
                const bc::Capsule body[]={{{},{},.01f}};
                const float radii[]={0.f},step=1.f/60;
                const float span=bc::length(rest);
                skin::AttachmentSwing swing{};bc::Vec direction{};
                for(unsigned i=0;i<120;++i)direction=swing.advance({},rest,.61f,step,body,radii);
                check(direction.y<rest.y-.006f&&near(bc::length(direction),span),"pouch did not lower around its attachment");
                check(bc::dot(rest,direction)>=span*span*std::cos(.61f)-1e-6f,"accessory passed its seam angle limit");
                const auto settled=direction;
                direction=swing.advance({.025f,0,0},rest,.61f,step,body,radii);
                check(direction.x<settled.x-.005f&&near(bc::length(direction),span),"attachment failed to lag a moving seam");
                swing={};
                const auto reset=swing.advance({2,1,3},rest,.61f,step,body,radii);
                skin::AttachmentSwing fresh{};
                const auto start=fresh.advance({},rest,.61f,step,body,radii);
                check(bc::length(bc::sub(reset,start))<1e-5f,"seek carried the old accessory velocity");
            }
            // Barbara's B04 failure at 6 s: a roughly 13 cm segment predicted
            // more than four metres of travel after a contact correction.
            const bc::Vec tip{.019f,.023f,.428f};
            const auto spike=skin::predict_tip(tip,bc::sub(tip,{0,0,4.17f/.88f}),tip,.13f);
            check(near(spike.z-tip.z,.065f)&&near(spike.y-tip.y,-.0004f),"contact history launched the skirt tip");
            const auto ordinary=skin::predict_tip(tip,bc::sub(tip,{0,0,.01f}),bc::add(tip,{.01f,0,0}),.13f);
            check(near(ordinary.z-tip.z,.0088f)&&near(ordinary.x-tip.x,.0004f),"ordinary skirt inertia or spring changed");
            skin::Frame mirrored{{{-2,0,0},{.1f,1,0},{0,0,.5f}},{3,2,1}};bc::Vec local{},input{.2f,-.4f,.7f};
            check(skin::inverse(mirrored,skin::point(mirrored,input),local)&&bc::length(bc::sub(local,input))<1e-5f,"skinned cloth inverse lost reflection or shear");
            auto aimed=skin::aim({}, {0,-1,0},{0,0,1},{1,2,3});
            check(bc::length(bc::sub(skin::point(aimed,{0,-1,0}),{1,2,4}))<1e-5f,"skinned cloth child aim does not match particle direction");
            const auto gradient=skin::angular_gradient({0,-.2f,.1f},{0,0,1},{0,-1,0},.5f);
            check(bc::dot(bc::cross(gradient,{0,-.2f,.1f}),{0,0,1})>0&&std::abs(gradient.y)<1e-6f,"skin contact gradient moves inward or introduces twist");
            check(bc::length(skin::angular_gradient({0,0,1},{1,0,0},{0,1,0},1))<1e-6f,"unactuated twist entered skin correction");
        }
        gimmd::Timeline clock(2);
        clock.play(100);
        check(near(clock.sample(100.5).seconds,0.5),"clock advance wrong");
        clock.pause(100.5);
        check(near(clock.sample(110).seconds,0.5),"pause advanced");
        unsigned held_batches=0;
        uint64_t last_batch=0;
        for(uint64_t ms=16;ms<=10000;ms+=16){
            if(gimmd::wait_for_held_pose(ms-last_batch,true,false))continue;
            ++held_batches;last_batch=ms;
        }
        check(held_batches==78,"settled pose verification cadence changed");
        check(!gimmd::wait_for_held_pose(16,true,true),"held pose delayed a transport command");
        check(!gimmd::wait_for_held_pose(16,false,false),"held pose delayed physics settlement or playback");
        clock.seek(1.5,110);
        check(clock.sample(110).epoch==1,"seek epoch wrong");
        clock.play(110);
        check(clock.sample(111).state==gimmd::PlaybackState::Ended,"end not detected");
        check(near(clock.sample(120).seconds,2),"end not held");
        clock.play(120);
        check(near(clock.sample(120).seconds,0),"restart not rewound");
        clock.set_loop(true);
        const auto before=clock.sample(120).epoch;
        const auto looped=clock.sample(122.25);
        check(near(looped.seconds,0.25) && looped.epoch==before+1,"loop wrap wrong");
        clock.stop(123);
        check(clock.sample(124).state==gimmd::PlaybackState::Ready && near(clock.sample(124).seconds,0),"stop wrong");
        bool rejected=false;
        try { clock.seek(std::numeric_limits<double>::infinity(),124); }
        catch(const std::invalid_argument&) { rejected=true; }
        check(rejected,"invalid seek accepted");
        gimmd::Timeline zero(0);zero.play(0);
        check(zero.sample(1).state==gimmd::PlaybackState::Ended,"zero-duration playback wrong");
        const auto seek_word = gimmd::parse_player_command("17 seek 1.25");
        check(gimmd::command_sequence(seek_word)==17 && near(gimmd::command_seconds(seek_word),1.25),"command encoding wrong");
        check(!gimmd::parse_player_command("1 seek nan") && !gimmd::parse_player_command("1 seek -1") &&
            !gimmd::parse_player_command("1 pause extra"),"malformed command accepted");
        check(!gimmd::parse_player_command("268435456 play"),"oversized command sequence accepted");
        const auto packed = gimmd::unpack_player_frame(gimmd::pack_player_frame({23.125,57,gimmd::PlaybackState::Paused}));
        check(near(packed.seconds,23.125) && packed.epoch==57 && packed.state==gimmd::PlaybackState::Paused,"player snapshot torn");
        const gimmd::fk_retarget::Binding fk{{0,0,0.70710678f,0.70710678f},{-1,0,0,0,1,0,0,0,-1}};
        const auto fk_rest = gimmd::fk_retarget::evaluate(fk,{});
        check(near(fk_rest.z,fk.rest.z) && near(fk_rest.w,fk.rest.w),"identity source did not preserve target rest");
        const auto fk_turn = gimmd::fk_retarget::evaluate(fk,{0.70710678f,0,0,0.70710678f});
        const auto fk_expected = gimmd::rotation_preview::multiply({-0.70710678f,0,0,0.70710678f},fk.rest);
        check(near(fk_turn.x,fk_expected.x) && near(fk_turn.y,fk_expected.y),"parent-space FK multiplication order wrong");
        const gimmd::fk_retarget::Binding reflected_parent{{0,0,0,1},{-1,0,0,0,-1,0,0,0,1}};
        const auto reflected_turn = gimmd::fk_retarget::evaluate(reflected_parent,{0.70710678f,0,0,0.70710678f});
        check(near(reflected_turn.x,-0.70710678f),"signed-parent pseudovector transport wrong");
        const auto anchored = gimmd::root_translation::offset({3,4,5},{3,4,5},0.1f,{0,0,0,1},{1,1,1});
        check(near(anchored.x,0) && near(anchored.y,0) && near(anchored.z,0),"root reference anchoring wrong");
        const auto moved = gimmd::root_translation::offset({1,2,3},{0,0,0},0.1f,{0,0,0,1},{1,1,1});
        check(near(moved.x,-0.1) && near(moved.y,0.2) && near(moved.z,-0.3),"root units or source basis wrong");
        const auto model_space = gimmd::root_translation::offset({1,2,3},{0,0,0},0.1f,{0,0.70710678f,0,0.70710678f},{2,2,2});
        check(near(model_space.x,-0.6) && near(model_space.y,0.4) && near(model_space.z,0.2),"mesh-to-model root translation wrong");
        namespace ik=gimmd::leg_ik;
        ik::Solution solution{};
        check(ik::solve({0,2,0},{0,1,0},{0,0,0},{0,0.5f,0},{0,0,1},&solution)&&!solution.clamped,
            "reachable two-bone target rejected");
        check(near(ik::length(ik::sub(solution.knee,{0,2,0})),1)&&near(ik::length(ik::sub(solution.ankle,solution.knee)),1)&&solution.knee.z>0,
            "two-bone lengths or forward pole violated");
        check(ik::solve({0,2,0},{0,1,0},{0,0,0},{0,-1,0},{0,0,1},&solution)&&solution.clamped,
            "unreachable IK did not clamp");
        check(!ik::solve({0,0,0},{0,0,0},{0,0,0},{1,0,0},{0,0,1},&solution),"degenerate IK chain accepted");
        const ik::Matrix reflected{{{-1,0,0},{0,1,0},{0,0,1}}}; vmd::Quaternion aimed{};
        check(ik::aim({reflected,{}},{},{-1,0,0},{0,1,0},{},&aimed)&&
            ik::length(ik::sub(ik::vector(ik::multiply(reflected,ik::rotation(aimed)),{1,0,0}),{0,1,0}))<1e-5f,
            "signed parent IK aim wrong");
        namespace lp=gimmd::leg_pose;
        vmd::Quaternion pose[52]{};
        for (unsigned i=0;i<52;++i) pose[i]=gimmd::fk_profile::Bindings[i].rest;
        lp::FootSample feet[2]{{true,{0,0.1f,0.08f},{}},{true,{0,0.1f,-0.05f},{}}}; lp::Result ik_result{};
        check(lp::solve(pose,{0,0.05f,0},{},feet,&ik_result)&&ik_result.solved==2&&ik_result.maximum_residual<1e-4f,
            "target rig foot IK solve failed");
        lp::Rig rig{},reference{}; rig.reset(pose,{0,0.05f,0}); reference.reset(nullptr,{});
        const auto foot_index=gimmd::fk_profile::AvatarIndices[12];
        check(ik::length(ik::sub(rig.world[foot_index].position,ik::add(reference.world[foot_index].position,feet[0].delta)))<1e-4f,
            "Center compensation missed planted foot target");
        check(ik::length(ik::sub(rig.world[foot_index].frame.columns[1],reference.world[foot_index].frame.columns[1]))<1e-4f,
            "ankle orientation changed with solved leg");
        for (unsigned i=0;i<52;++i) pose[i]=gimmd::fk_profile::Bindings[i].rest;
        lp::FootSample support[2]{{true,{}, {},1},{true,{}, {},1}};
        check(lp::solve(pose,{0,0.15f,0},{},support,&ik_result)&&ik_result.root_correction_y< -0.14f&&
            ik_result.root_correction_y>=-0.25f&&ik_result.clamped==0,"support foot reach did not lower visual root");
        check(lp::support_weight(0)==1&&lp::support_weight(0.1f)==0&&lp::support_weight(0.04f)>0.49f,
            "support blend changed lifted foot into ground constraint");
        vmd::Quaternion neutral[52]{},toe_pose[52]{};
        for (unsigned i=0;i<52;++i) neutral[i]=toe_pose[i]=gimmd::fk_profile::Bindings[i].rest;
        lp::FootSample toe_feet[2]{{true,{}, {0,0.198669331f,0,0.980066578f},0,true,{}},{true,{}, {},0,true,{}}};
        auto no_toes=toe_feet[0]; no_toes.toe_enabled=false;
        lp::FootSample base_feet[2]{no_toes,toe_feet[1]}; base_feet[1].toe_enabled=false;
        lp::Result toe_result{};
        check(lp::solve(neutral,{},{},base_feet,&ik_result)&&lp::solve(toe_pose,{},{},toe_feet,&toe_result)&&toe_result.toe_solved==2,
            "neutral toe solve failed");
        check(toe_pose[12].x==neutral[12].x&&toe_pose[12].y==neutral[12].y&&toe_pose[12].z==neutral[12].z&&toe_pose[12].w==neutral[12].w&&
            toe_result.toe_corrected==0&&toe_result.maximum_toe_correction_degrees==0,"zero toe offset changed existing ankle rotation");
        toe_feet[0].toe_offset={0,0.04f,0};
        for (unsigned i=0;i<52;++i) toe_pose[i]=gimmd::fk_profile::Bindings[i].rest;
        check(lp::solve(toe_pose,{},{},toe_feet,&toe_result)&&toe_result.toe_corrected==1&&toe_result.maximum_toe_direction_error<1e-5f,
            "toe aim missed target direction");
        rig.reset(toe_pose,{}); reference.reset(neutral,{});
        check(ik::length(ik::sub(rig.world[foot_index].position,reference.world[foot_index].position))<1e-5f,
            "toe aim displaced solved ankle");
        const auto ti=gimmd::fk_profile::AvatarIndices[13];
        check(near(ik::length(ik::sub(rig.world[ti].position,rig.world[foot_index].position)),
            ik::length(ik::sub(reference.world[ti].position,reference.world[foot_index].position))),"toe aim stretched foot segment");
        lp::Rig rest_toe{}; rest_toe.reset(nullptr,{});
        toe_feet[0].toe_offset=ik::sub(rest_toe.world[foot_index].position,rest_toe.world[ti].position);
        for (unsigned i=0;i<52;++i) toe_pose[i]=gimmd::fk_profile::Bindings[i].rest;
        check(lp::solve(toe_pose,{},{},toe_feet,&toe_result)&&toe_result.toe_degenerate==1,"degenerate toe target did not retain ankle solve");
        lp::Geometry measured{}; const unsigned pelvis=gimmd::fk_profile::AvatarIndices[0]; const auto& p_ref=gimmd::fk_profile::AvatarReference[pelvis];
        measured.active[pelvis]=true; measured.position[pelvis]={p_ref.position[0]+0.00084f,p_ref.position[1],p_ref.position[2]};
        measured.scale[pelvis]={p_ref.scale[0],p_ref.scale[1],p_ref.scale[2]};
        for (unsigned i=0;i<52;++i) toe_pose[i]=gimmd::fk_profile::Bindings[i].rest;
        toe_feet[0].toe_offset={0,0.04f,0};
        check(lp::solve(toe_pose,{},{},toe_feet,&toe_result,nullptr,&measured)&&toe_result.toe_corrected==1,"measured chain geometry solve failed");
        rig.reset(toe_pose,{},nullptr,&measured); rest_toe.reset(nullptr,{},nullptr,&measured);
        check(ik::length(ik::sub(rig.world[foot_index].position,rest_toe.world[foot_index].position))<1e-5f,"measured chain geometry not used for ankle target");
        for (unsigned i=0;i<52;++i) toe_pose[i]=gimmd::fk_profile::Bindings[i].rest;
        lp::FootSample grounded_feet[2]{{true,{}, {},1},{true,{}, {},1}};
        check(lp::solve(toe_pose,{0.2f,-0.04f,0.3f},{0.2f,0,0.3f},grounded_feet,&ik_result),"captured root origin solve failed");
        rig.reset(toe_pose,{0.2f,-0.04f+ik_result.root_correction_y,0.3f}); reference.reset(nullptr,{});
        check(ik::length(ik::sub(rig.world[foot_index].position,ik::add(reference.world[foot_index].position,{0.2f,0,0.3f})))<1e-5f,
            "captured root height offset leaked into ground target");
        gimmd::camera_pose::Pose camera_pose{};
        vmd::CameraSample camera_sample{{0,10,0},{},-20,45,false};
        check(gimmd::camera_pose::evaluate(camera_sample,0.1f,{}, {},&camera_pose)&&near(camera_pose.position.y,1)&&near(camera_pose.position.z,2),
            "camera source basis or signed distance wrong");
        check(near(camera_pose.rotation.y,1)&&near(camera_pose.fov,45),"camera neutral facing/FOV wrong");
        camera_sample.euler.y=1.57079632679f;
        check(gimmd::camera_pose::evaluate(camera_sample,0.1f,{3,4,5}, {},&camera_pose)&&near(camera_pose.position.x,1)&&near(camera_pose.position.y,5),
            "camera Euler composition or anchor placement wrong");
        camera_sample.fov=200; camera_sample.orthographic=true;
        check(gimmd::camera_pose::evaluate(camera_sample,0.1f,{}, {},&camera_pose)&&camera_pose.fov==179&&camera_pose.orthographic,
            "camera projection/FOV boundary wrong");
        check(!gimmd::camera_pose::evaluate(camera_sample,0,{}, {},&camera_pose),"zero camera unit scale accepted");
        gimmd::CameraControl camera_control{};
        check(gimmd::parse_camera_control("1 1 2 3 90 -15 10 60",&camera_control)&&camera_control.yaw==90,"camera control parse failed");
        for (const char* invalid_camera_command:{"0 0 0 0 0 0 0 0","1 101 0 0 0 0 0 0","1 0 0 0 181 0 0 0","1 0 0 0 0 0 0 180",
            "1 nan 0 0 0 0 0 0","1 0 0 0 0 0 0 0 extra","4294967296 0 0 0 0 0 0 0"})
            check(!gimmd::parse_camera_control(invalid_camera_command,&camera_control),"invalid camera control accepted");
        check(gimmd::parse_camera_control("2 0 0 0 0 0 0 0",&camera_control)&&camera_control.fov==0,"camera reset command failed");
        check(camera_control.fov_multiplier==1,"legacy camera command did not default to unit multiplier");
        check(gimmd::parse_camera_control("3 1 2 3 0 0 0 0 0.75",&camera_control)&&near(camera_control.fov_multiplier,.75f),"FOV multiplier command rejected");
        for (const char* invalid_multiplier:{"3 0 0 0 0 0 0 0 0","3 0 0 0 0 0 0 0 4.1","3 0 0 0 0 0 0 0 nan","3 0 0 0 0 0 0 0 1 extra"})
            check(!gimmd::parse_camera_control(invalid_multiplier,&camera_control),"invalid multiplier command accepted");
        check(gimmd::parse_camera_control("4 1 2 3 0 0 0 45 1 1",&camera_control)&&camera_control.preview_override,"live camera preview flag rejected");
        check(!gimmd::parse_camera_control("4 1 2 3 0 0 0 45 1 2",&camera_control),"invalid preview flag accepted");
        for (const char* independent_command:{"5 restore","6 exit","7 camera-on","8 camera-off"})
            check(gimmd::parse_player_command(independent_command)!=0,"dynamic lifecycle command rejected");
        std::vector<vmd::BoneKey> root_controls[3]; root_controls[1].resize(1); root_controls[2].resize(1);
        root_controls[1][0].rotation={0,0.70710678f,0,0.70710678f}; root_controls[2][0].position={1,0,0};
        vmd::Vec3 root_position{}; vmd::Quaternion center_yaw{};
        check(gimmd::root_controls::sample(root_controls,0,&root_position,&center_yaw)&&near(root_position.z,-1)&&near(root_position.x,0),"Center yaw did not rotate child Groove translation");
        check(gimmd::root_controls::supported(1,center_yaw)&&!gimmd::root_controls::supported(0,center_yaw)&&
            gimmd::root_controls::supported(1,{.1f,0,0,.9949874f}),"root rotation capability boundary wrong");
        const auto relative_torso=gimmd::fk_retarget::compensate_parent({},{0,1,0,0},{0,1,0,0});
        check(near(relative_torso.x,0)&&near(relative_torso.y,0)&&near(relative_torso.z,0)&&near(std::abs(relative_torso.w),1),
            "parallel 180-degree torso rotations were doubled");
        const auto torso_lower=vmd::Normalize({.2f,.3f,-.1f,.9f}),torso_upper=vmd::Normalize({-.1f,.6f,.2f,.7f});
        const auto torso_reconstructed=rp::multiply(torso_lower,gimmd::fk_retarget::compensate_parent({},torso_lower,torso_upper));
        check(std::abs(torso_reconstructed.x*torso_upper.x+torso_reconstructed.y*torso_upper.y+torso_reconstructed.z*torso_upper.z+torso_reconstructed.w*torso_upper.w)>.99999f,
            "parent times relative torso did not recover source upper-body rotation");
        if(argc==2) {
            std::ofstream file(std::filesystem::path(argv[1]),std::ios::binary);
            file.write(reinterpret_cast<const char*>(bytes.data.data()),static_cast<std::streamsize>(bytes.data.size()));
            check(bool(file),"cannot write fixture");
        }
        std::cout << checks << " checks passed\n";
        return 0;
    } catch(const std::exception& e) {
        std::cerr << "Test failed: " << e.what() << '\n';
        return 1;
    }
}
