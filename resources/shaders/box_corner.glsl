#ifndef BOX_CORNER_GLSL
#define BOX_CORNER_GLSL

// The box a pass draws around one body (shadow_volume_plan.md ss5, spot_beam_plan.md ss4), as a cube from
// gl_VertexIndex, so the body's fragment shader runs only where the body can be and not over the whole view.
// The pipeline culls the FRONT faces and draws the back ones, which still cover the view from inside the box;
// it tests no depth, the fragment reads the scene's. Each corner is pushed out by the outline's width at its
// own depth, so a line's zero crossing is never at the box's edge, where the derivatives that find it end.
// Includes scene.glsl ahead of this.

const int BOX_VERTEX_COUNT = 36;

// Corner c of the unit cube is (c & 1, c >> 1 & 1, c >> 2 & 1); every face is two triangles wound
// counter-clockwise seen from outside, HOUSE_FRONT_FACE.
const int CUBE_CORNERS[BOX_VERTEX_COUNT] = int[](
    1, 3, 7, 1, 7, 5,
    0, 6, 2, 0, 4, 6,
    2, 6, 7, 2, 7, 3,
    0, 5, 4, 0, 1, 5,
    4, 5, 7, 4, 7, 6,
    0, 3, 1, 0, 2, 3);

// Vertex `vertex_index` of the padded box's clip position; an empty box lands every vertex behind the near plane.
vec4 padded_box_corner_clip_position(vec3 box_min, vec3 box_max, int vertex_index)
{
    if (all(equal(box_min, box_max)))
        return vec4(0.0, 0.0, -2.0, 1.0);

    int   corner          = CUBE_CORNERS[vertex_index];
    vec3  unit            = vec3(corner & 1, (corner >> 1) & 1, (corner >> 2) & 1);
    vec3  position        = mix(box_min, box_max, unit);
    float depth           = max(dot(position - scene.camera_position.xyz, scene.camera_forward.xyz), 1.0);
    float world_per_pixel = 2.0 * length(scene.view_right.xyz) * depth / scene.beam_viewport.z;
    position += (unit * 2.0 - 1.0) * (scene.beam.w + 2.0) * world_per_pixel;
    return scene.view_projection * vec4(position, 1.0);
}

#endif // BOX_CORNER_GLSL
