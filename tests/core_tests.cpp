#include "vmd.h"
#include "core/timeline.h"
#include "core/player_transport.h"
#include "core/rotation_preview.h"
#include "core/fk_retarget.h"
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

        gimmd::Timeline clock(2);
        clock.play(100);
        check(near(clock.sample(100.5).seconds,0.5),"clock advance wrong");
        clock.pause(100.5);
        check(near(clock.sample(110).seconds,0.5),"pause advanced");
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

