#ifndef SHADER_TOOL_COMMON_GLSL
#define SHADER_TOOL_COMMON_GLSL

#define MAX_LIGHTS 8

struct Light {
    vec4 position;          // xyz = world position, w = unused
    vec4 direction;         // xyz = normalized direction, w = unused
    vec4 color_intensity;   // rgb = color, a = intensity
    vec4 spot_params;       // x = cos(inner), y = cos(outer), z = range, w = type (0=point, 1=spot, 2=directional)
};

layout(set = 0, binding = 0) uniform SceneUBO {
    mat4 view;
    mat4 projection;
    mat4 view_projection;
    vec4 camera_position;     // xyz, w unused
    vec4 time;                // x = seconds, y = sin(t), z = cos(t), w = dt
    int  light_count;
    int  debug_flags, _pad1, _pad2;
    Light lights[MAX_LIGHTS];
    vec4  param_color[4];
    vec4  param_vec4[8];
    float param_float[16];
} scene;

// Compute simple lighting contribution from all active lights
vec3 compute_lighting(vec3 world_position, vec3 world_normal) {
    vec3 result = vec3(0.0);

    for (int i = 0; i < scene.light_count && i < MAX_LIGHTS; i++) {
        Light light = scene.lights[i];
        int light_type = int(light.spot_params.w);
        vec3 light_color = light.color_intensity.rgb * light.color_intensity.a;
        float attenuation = 1.0;

        vec3 light_direction;

        if (light_type == 2) {
            // Directional
            light_direction = -normalize(light.direction.xyz);
        } else {
            // Point or spot
            vec3 to_light = light.position.xyz - world_position;
            float distance = length(to_light);
            light_direction = to_light / max(distance, 0.001);

            float light_range = light.spot_params.z;
            if (light_range > 0.0) {
                attenuation = max(1.0 - (distance / light_range), 0.0);
                attenuation *= attenuation;
            }

            if (light_type == 1) {
                // Spot cone
                float cos_angle = dot(-light_direction, normalize(light.direction.xyz));
                float cos_inner = light.spot_params.x;
                float cos_outer = light.spot_params.y;
                float spot_factor = clamp((cos_angle - cos_outer) / max(cos_inner - cos_outer, 0.001), 0.0, 1.0);
                attenuation *= spot_factor;
            }
        }

        float n_dot_l = max(dot(world_normal, light_direction), 0.0);
        result += light_color * n_dot_l * attenuation;
    }

    return result;
}

#endif // SHADER_TOOL_COMMON_GLSL
