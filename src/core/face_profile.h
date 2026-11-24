#pragma once
#include <algorithm>
#include <cmath>
namespace gimmd::face_profile {
struct Mapping {const char* source;const wchar_t* target;unsigned group;};
inline constexpr Mapping Mappings[]={
    {"\x82\xdc\x82\xce\x82\xbd\x82\xab",L"Eye_WinkA_L",0},
    {"\x82\xdc\x82\xce\x82\xbd\x82\xab",L"Eye_WinkA_R",0},
    {"\x83\x45\x83\x42\x83\x93\x83\x4e",L"Eye_WinkA_L",0},
    {"\x83\x45\x83\x42\x83\x93\x83\x4e\x89\x45",L"Eye_WinkA_R",0},
    {"\x8f\xce\x82\xa2",L"Eye_WinkB_L",0},
    {"\x8f\xce\x82\xa2",L"Eye_WinkB_R",0},
    {"\x83\x45\x83\x42\x83\x93\x83\x4e\x82\x51",L"Eye_WinkB_L",0},
    {"\x83\x45\x83\x42\x83\x93\x83\x4e\x82\x51\x89\x45",L"Eye_WinkB_R",0},
    {"\x82\xa0",L"Mouth_A01",1},
    {"\x82\xa8",L"Mouth_Open01",1},
    {"\x82\xc9\x82\xe2\x82\xe8",L"Mouth_Smile01",1},
    {"\x83\xd6",L"Mouth_Neko01",1},
    {"\x8d\xa2\x82\xe9",L"Brow_Trouble_L",2},
    {"\x8d\xa2\x82\xe9",L"Brow_Trouble_R",2},
    {"\x93\x7b\x82\xe8",L"Brow_Angry_L",2},
    {"\x93\x7b\x82\xe8",L"Brow_Angry_R",2},
    {"\x82\xc9\x82\xb1\x82\xe8",L"Brow_Smily_L",2},
    {"\x82\xc9\x82\xb1\x82\xe8",L"Brow_Smily_R",2},
    {"\x8f\xe3",L"Brow_Up_L",2},
    {"\x8f\xe3",L"Brow_Up_R",2},
    {"\x89\xba",L"Brow_Down_L",2},
    {"\x89\xba",L"Brow_Down_R",2},
};
inline float bounded(float value){return std::isfinite(value)?std::clamp(value,0.0f,1.0f):0.0f;}
}
