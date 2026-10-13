#version 450

// The shadow pass' arm for a locked wall: the keyhole drops its texels here too, or the wall's
// shadow stays whole while the hole opens.

layout(location = 6) in vec3 fragWorldPosition;

#include "keyhole.glsl"

void main() {
    if (keyhole_at(fragWorldPosition) < 0.0)
        discard;
}
