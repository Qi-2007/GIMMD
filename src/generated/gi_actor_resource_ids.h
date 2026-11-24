#pragma once
#include <cstdint>
namespace gimmd::actor_resource_ids {
inline constexpr char BuildSha256[] = "7F89938DA606C1281659607D464702CDDB9A09A7D4320A196630F60A811EC38E";
struct Entry { uint32_t hash, package; const char* bundle; const char* model; };
inline constexpr Entry Entries[] = {
    {0x2aaed67b, 1114, "66555/4d09c3889af115cc.asb", "Avatar_Boy_Sword_PlayerBoy_Manekin"},
    {0x14ddb30e, 1179503, "531/49b93d843d36f52a.asb", "Avatar_Girl_Bow_Linnea_Manekin"},
    {0x9f4a5265, 84242, "514/476c7721101a824b.asb", "Avatar_Girl_Bow_Yoimiya_Manekin"},
    {0x495b9e25, 1126875, "808/7036fc2916a17582.asb", "Avatar_Girl_Catalyst_Columbina_Manekin"},
    {0xf2a07319, 1383073, "149/cfa45cac2daa00ed.asb", "Avatar_Girl_Catalyst_Vodyanitsa_Manekin"},
    {0x171df52, 1282629, "822/721e5600a4960632.asb", "Avatar_Girl_Claymore_MarionetteNew_Manekin"},
    {0x55f2c739, 1300049, "127/11b1c4628af8c5bd.asb", "Avatar_Girl_Sword_Odette_Manekin"},
    {0x6976fdb2, 84763, "595/529bdf92c8b1ca1c.asb", "Avatar_Loli_Catalyst_Nahida_Manekin"},
    {0x1727fa83, 206643, "693/6046503989d715e0.asb", "Avatar_Loli_Catalyst_Nahida_Show01_Manekin"},
    {0xf0bc3a96, 1200296, "129/b38b5228e120537e.asb", "Avatar_Loli_Catalyst_Prune_Manekin"},
    {0x8f8c3c87, 717629, "117/a358f4607ef258d1.asb", "Avatar_Loli_Pole_Kachina_Manekin"},
};
}
