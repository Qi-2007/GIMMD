#pragma once
#include "core/player_transport.h"
#include "core/camera_control.h"
#include <mmsystem.h>
#pragma comment(lib, "winmm.lib")
namespace gimmd::player_session {
#ifdef GIMMD_PLAYER_SESSION
inline constexpr bool Enabled = true;
#else
inline constexpr bool Enabled = false;
#endif
inline std::atomic<uint64_t> command{0}, frame{0}, acknowledged{0};
inline bool active = false;
inline SRWLOCK camera_lock=SRWLOCK_INIT;
inline CameraControl camera_control{};
inline std::atomic<uint64_t> camera_acknowledged{0};
inline std::atomic<bool> camera_enabled{false};
inline bool camera_snapshot(CameraControl* out) {
    if (!TryAcquireSRWLockShared(&camera_lock)) return false;
    *out=camera_control; ReleaseSRWLockShared(&camera_lock); return true;
}
inline bool closed = true;
inline uint64_t session_token = 0;
inline double duration_seconds = 0;
inline std::atomic<bool> loop_enabled{false};
inline bool legacy_resolver = false;
inline SRWLOCK uid_lock=SRWLOCK_INIT;
inline char uid_value[257]{};
inline bool uid_valid=false,uid_enabled=false;
inline bool uid_snapshot(char (&out)[257],bool* enabled) {
    if (!TryAcquireSRWLockShared(&uid_lock)) return false;
    const bool valid=uid_valid; *enabled=uid_enabled; std::memcpy(out,uid_value,sizeof(out));
    ReleaseSRWLockShared(&uid_lock); return valid;
}
inline bool dynamic_session=false;
inline std::atomic<bool> exit_requested{false},mmd_requested{false},rearm_pending{false},camera_requested{false};
inline std::atomic<unsigned> mmd_phase{0};
inline std::atomic<uint64_t> camera_frame{0};
inline bool hold_at_end=false;
inline std::atomic<bool> holding_end{false};
inline bool stabilize_cloth=false,stabilize_braid=false,pause_bgm=false;
inline std::atomic<unsigned> bgm_state{0},uid_state{0};
inline std::filesystem::path directory;
inline unsigned commands_read = 0, audio_requeues = 0, position_checks = 0;
inline MMRESULT audio_error = 0;
inline double max_audio_offset_ms = 0;
inline bool audio_loaded = false, audio_closed = true;
inline HWAVEOUT device = nullptr;
inline WAVEFORMATEX format{};
inline WAVEHDR header{};
inline std::vector<uint8_t> pcm;
inline uint64_t audio_epoch = UINT64_MAX;
inline uint32_t audio_base = 0;
inline PlaybackState audio_state = PlaybackState::Ready;
inline uint16_t u16(const uint8_t* p) { return uint16_t(p[0] | (unsigned(p[1]) << 8)); }
inline uint32_t u32(const uint8_t* p) { return unsigned(p[0]) | (unsigned(p[1]) << 8) | (unsigned(p[2]) << 16) | (unsigned(p[3]) << 24); }
inline bool load_pcm(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary | std::ios::ate);
    const auto size = in ? in.tellg() : std::streampos(-1);
    if (size < 44 || size > (64 << 20)) return false;
    std::vector<uint8_t> bytes(static_cast<size_t>(size)); in.seekg(0);
    in.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    if (!in || memcmp(bytes.data(), "RIFF", 4) || memcmp(bytes.data()+8, "WAVE", 4) ||
        uint64_t(u32(bytes.data()+4))+8 != bytes.size()) return false;
    bool found_format = false, found_data = false; size_t data_start = 0, data_size = 0;
    for (size_t n = 12; n + 8 <= bytes.size();) {
        const size_t length = u32(bytes.data()+n+4); const size_t start = n+8;
        if (length > bytes.size()-start) return false;
        if (!memcmp(bytes.data()+n, "fmt ", 4)) {
            if (found_format || length < 16) return false;
            format = {}; format.wFormatTag = u16(bytes.data()+start);
            format.nChannels = u16(bytes.data()+start+2); format.nSamplesPerSec = u32(bytes.data()+start+4);
            format.nAvgBytesPerSec = u32(bytes.data()+start+8); format.nBlockAlign = u16(bytes.data()+start+12);
            format.wBitsPerSample = u16(bytes.data()+start+14); found_format = true;
        } else if (!memcmp(bytes.data()+n, "data", 4)) {
            if (found_data) return false;
            data_start = start; data_size = length; found_data = true;
        }
        n = start + length + (length & 1);
    }
    if (!found_format || !found_data || !data_size || format.wFormatTag != WAVE_FORMAT_PCM ||
        (format.nChannels != 1 && format.nChannels != 2) || format.wBitsPerSample != 16 ||
        format.nSamplesPerSec < 8000 || format.nSamplesPerSec > 192000 ||
        format.nBlockAlign != format.nChannels*2 || format.nAvgBytesPerSec != format.nSamplesPerSec*format.nBlockAlign ||
        data_size % format.nBlockAlign) return false;
    pcm.assign(bytes.begin()+data_start, bytes.begin()+data_start+data_size); return true;
}
inline bool clear_buffer() {
    if (!device) return true;
    auto error = waveOutReset(device);
    if (error) { audio_error = error; return false; }
    if (header.dwFlags & WHDR_PREPARED) {
        error = waveOutUnprepareHeader(device, &header, sizeof(header));
        if (error) { audio_error = error; return false; }
    }
    header = {}; return true;
}
inline void close_audio() {
    if (!device) return;
    if (!clear_buffer()) { audio_closed = false; return; }
    const auto error = waveOutClose(device);
    if (error) { audio_error = error; audio_closed = false; return; }
    device = nullptr; audio_closed = true;
}
inline bool reload_audio() {
    close_audio(); if (!audio_closed) return false;
    audio_error=0; audio_state=PlaybackState::Ready; audio_epoch=UINT64_MAX; audio_base=0;
    audio_loaded=load_pcm(directory/L"multi-track.wav");
    if (audio_loaded) { audio_error=waveOutOpen(&device,WAVE_MAPPER,&format,0,0,CALLBACK_NULL); audio_closed=device==nullptr; }
    return !audio_error;
}
// Worker only: no audio API or filesystem access is made on the game thread.
inline void initialize(const std::filesystem::path& path) {
    directory = path; command.store(0); frame.store(0); acknowledged.store(0); commands_read = audio_requeues = position_checks = 0;
    uid_valid=uid_enabled=false; uid_value[0]=0;
    camera_control={}; camera_acknowledged.store(0); camera_enabled.store(false);
    closed = false; duration_seconds = 0; loop_enabled.store(false);
    session_token = (uint64_t(GetCurrentProcessId()) << 32) | (GetTickCount64() & 0xffffffff);
    audio_error = 0; max_audio_offset_ms = 0; audio_epoch = UINT64_MAX; audio_state = PlaybackState::Ready;
    if (!Enabled) return;
    active = true;
    dynamic_session=std::filesystem::exists(directory/L"dynamic-session.flag");
    camera_frame.store(0); exit_requested.store(false); mmd_requested.store(false); rearm_pending.store(false); camera_requested.store(false); mmd_phase.store(0);
    hold_at_end=dynamic_session||std::filesystem::exists(directory/L"hold-end.flag"); holding_end.store(false);
    stabilize_cloth=std::filesystem::exists(directory/L"cloth-static.flag");
    stabilize_braid=std::filesystem::exists(directory/L"braid-static.flag");
    pause_bgm=std::filesystem::exists(directory/L"pause-bgm.flag");
    bgm_state.store(pause_bgm ? 1:0); uid_state.store(0);
    legacy_resolver = std::filesystem::exists(directory / L"legacy-resolver.flag");
    // Refuse stale commands from a prior invocation of this directory.
    std::error_code ignored; std::filesystem::remove(directory / L"player.command", ignored);
    std::filesystem::remove(directory / L"camera.command", ignored);
    std::ifstream initial_camera(directory/L"camera-initial.txt",std::ios::binary|std::ios::ate);
    if (initial_camera&&initial_camera.tellg()>0&&initial_camera.tellg()<=256) {
        initial_camera.seekg(0); std::string text; std::getline(initial_camera,text);
        CameraControl initial; if (parse_camera_control(text,&initial)) camera_control=initial;
    }
    audio_loaded = load_pcm(directory / L"multi-track.wav");
    if (audio_loaded) {
        audio_error = waveOutOpen(&device, WAVE_MAPPER, &format, 0, 0, CALLBACK_NULL);
        audio_closed = device == nullptr;
    }
}
inline void write_status() {
    if (!Enabled) return;
    const auto f = unpack_player_frame(frame.load());
    const auto temporary=directory / L"player.status.tmp";
    std::ofstream out(temporary, std::ios::trunc);
    out << "{\"schemaVersion\":2,\"sessionToken\":" << session_token << ",\"closed\":" << (closed ? "true":"false")
        << ",\"dynamicSession\":" << (dynamic_session ? "true":"false") << ",\"mmdPhase\":" << mmd_phase.load()
        << ",\"cameraActive\":" << (camera_requested.load() ? "true":"false")
        << ",\"holdAtEnd\":" << (hold_at_end ? "true":"false") << ",\"holdingEnd\":" << (holding_end.load() ? "true":"false")
        << ",\"durationSeconds\":" << duration_seconds << ",\"loop\":" << (loop_enabled.load() ? "true":"false")
        << ",\"seconds\":" << f.seconds << ",\"state\":" << static_cast<unsigned>(f.state)
        << ",\"epoch\":" << f.epoch << ",\"commandSequence\":" << command_sequence(command.load())
        << ",\"bgmState\":" << bgm_state.load() << ",\"uidTextState\":" << uid_state.load()
        << ",\"cameraEnabled\":" << (camera_enabled.load() ? "true":"false") << ",\"cameraAcknowledgedSequence\":" << camera_acknowledged.load()
        << ",\"acknowledgedSequence\":" << acknowledged.load() << '}';
    out.close();
    if (out) MoveFileExW(temporary.c_str(),(directory / L"player.status.json").c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH);
}
inline void worker_tick() {
    if (!Enabled) return;
    std::ifstream in(directory / L"player.command", std::ios::binary | std::ios::ate);
    if (in && in.tellg() > 0 && in.tellg() < 128) {
        in.seekg(0); std::string text; std::getline(in, text);
        const uint64_t parsed = parse_player_command(text);
        if (command_sequence(parsed) > command_sequence(command.load())) { command.store(parsed); ++commands_read; }
    }
    std::ifstream camera_in(directory / L"camera.command",std::ios::binary|std::ios::ate);
    if (camera_in&&camera_in.tellg()>0&&camera_in.tellg()<=256) {
        camera_in.seekg(0); std::string text; std::getline(camera_in,text); CameraControl parsed;
        if (parse_camera_control(text,&parsed)) {
            AcquireSRWLockExclusive(&camera_lock);
            if (parsed.sequence>camera_control.sequence) camera_control=parsed;
            ReleaseSRWLockExclusive(&camera_lock);
        }
    }
    std::ifstream uid_in(directory/L"uid-command.txt",std::ios::binary|std::ios::ate);
    if (uid_in&&uid_in.tellg()>=1&&uid_in.tellg()<=258) {
        const auto size=static_cast<size_t>(uid_in.tellg()); char text[259]{}; uid_in.seekg(0); uid_in.read(text,size);
        wchar_t checked[129]{};
        if (uid_in&&(text[0]=='0'||(text[0]=='1'&&text[1]==' '))&&
            (size<=2||MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,text+2,int(size-2),checked,128)>0)) {
            AcquireSRWLockExclusive(&uid_lock); uid_valid=true; uid_enabled=text[0]=='1';
            strcpy_s(uid_value,size>2 ? text+2:""); ReleaseSRWLockExclusive(&uid_lock);
        }
    }
    const auto f = unpack_player_frame(frame.load());
    if (device && !audio_error) {
        if (f.state == PlaybackState::Ready || f.state == PlaybackState::Ended) {
            if (audio_state == PlaybackState::Playing || audio_state == PlaybackState::Paused) clear_buffer();
        } else if (audio_epoch != f.epoch || audio_state == PlaybackState::Ready) {
            if (clear_buffer()) {
                audio_base = static_cast<uint32_t>(f.seconds * format.nSamplesPerSec);
                const size_t offset = std::min(size_t(audio_base)*format.nBlockAlign, pcm.size());
                if (offset < pcm.size()) {
                    audio_error = waveOutPause(device);
                    header.lpData = reinterpret_cast<char*>(pcm.data()+offset);
                    header.dwBufferLength = static_cast<DWORD>(pcm.size()-offset);
                    if (!audio_error) audio_error = waveOutPrepareHeader(device, &header, sizeof(header));
                    if (!audio_error) audio_error = waveOutWrite(device, &header, sizeof(header));
                    if (!audio_error && f.state == PlaybackState::Playing) audio_error = waveOutRestart(device);
                    ++audio_requeues;
                }
            }
        } else if (f.state != audio_state) {
            audio_error = f.state == PlaybackState::Paused ? waveOutPause(device) : waveOutRestart(device);
        }
        if (!audio_error && f.state == PlaybackState::Playing && (header.dwFlags & WHDR_PREPARED)) {
            MMTIME position{}; position.wType = TIME_SAMPLES;
            const auto error = waveOutGetPosition(device, &position, sizeof(position));
            if (!error && position.wType == TIME_SAMPLES) {
                ++position_checks;
                const double seconds = (double(audio_base)+position.u.sample)/format.nSamplesPerSec;
                max_audio_offset_ms = std::max(max_audio_offset_ms, std::abs(seconds-f.seconds)*1000);
            }
        }
        audio_epoch = f.epoch; audio_state = f.state;
    }
    static ULONGLONG last_status = 0;
    const auto now = GetTickCount64();
    if (now-last_status >= 100) {
        last_status = now;
        write_status();
    }
}
inline void report(std::ostream& out) {
    out << ",\n  \"playerSession\":{\"enabled\":" << (Enabled ? "true" : "false")
        << ",\"commandsRead\":" << commands_read << ",\"audioLoaded\":" << (audio_loaded ? "true" : "false")
        << ",\"audioError\":" << audio_error << ",\"audioRequeues\":" << audio_requeues
        << ",\"positionChecks\":" << position_checks << ",\"maxReportedAudioOffsetMs\":" << max_audio_offset_ms
        << ",\"audioClosed\":" << (audio_closed ? "true" : "false") << '}';
}
}
