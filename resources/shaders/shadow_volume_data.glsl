#ifndef SHADOW_VOLUME_DATA_GLSL
#define SHADOW_VOLUME_DATA_GLSL

// The shadow volumes a view pass carries (shadow_volume_plan.md ss5), written per pass by
// renderer/shadow_volumes.cpp into a storage buffer of set 3 and read by reveal.glsl's cuts. A volume is named
// by its index within the pass; scene.shadow_volume_storage.x is where the pass's records start. Include after
// scene.glsl.

// A volume's planes are [0, SHADOW_VOLUME_SIDE_SLOTS) its sides, the rest its light-facing planes: the outward
// normal (xyz) and dot(normal, point) (w); a slot past the live counts is (0, 0, 0, 1e9), inside for every
// point. `counts` holds the sides in the low byte, the front planes in the next and cuts_geometry at bit 16.
struct ShadowVolumeRecord
{
    vec4 planes[MAX_SHADOW_VOLUME_PLANES];
    int  light;
    int  counts;
    int  unused_0;
    int  unused_1;
};

layout(set = 3, binding = 20, std430) readonly buffer ShadowVolumeRecords { ShadowVolumeRecord records[]; } shadow_volume_records;

int shadow_volume_record(int volume)
{
    return int(scene.shadow_volume_storage.x) + volume;
}
vec4 shadow_volume_plane(int volume, int slot)
{
    return shadow_volume_records.records[shadow_volume_record(volume)].planes[slot];
}
// The live plane counts: x sides, y front planes.
ivec2 shadow_volume_plane_counts(int volume)
{
    int packed = shadow_volume_records.records[shadow_volume_record(volume)].counts;
    return ivec2(packed & 0xff, (packed >> 8) & 0xff);
}
bool shadow_volume_cuts_geometry(int volume)
{
    return (shadow_volume_records.records[shadow_volume_record(volume)].counts & (1 << 16)) != 0;
}

#endif // SHADOW_VOLUME_DATA_GLSL
