#version 450

// https://www.shadertoy.com/view/MdVXzw

layout(push_constant) uniform Background
{
    vec2  resolution;
    float time;
} background;

layout(location = 0) out vec4 fragment_color;

const vec3 bgColor = vec3(0.01, 0.16, 0.42);
const vec3 rectColor = vec3(0.01, 0.26, 0.57);

//noise background
const float noiseIntensity = 2.8;
const float noiseDefinition = 0.6;
const vec2 glowPos = vec2(-2., 0.);

//rectangles
const float total = 60.;//number of rectangles
const float minSize = 0.03;//rectangle min size
const float maxSize = 0.08-minSize;//rectangle max size
const float yDistribution = 0.5;


float random(vec2 co){
    return fract(sin(dot(co.xy ,vec2(12.9898,78.233))) * 43758.5453);
}

// pcg2d (Jarzynski and Olano): a shared lattice corner hashes the same bits from either cell.
float lattice_random(ivec2 cell)
{
    uvec2 v = uvec2(cell) * 1664525u + 1013904223u;
    v.x += v.y * 1664525u;
    v.y += v.x * 1664525u;
    v = v ^ (v >> 16u);
    v.x += v.y * 1664525u;
    v.y += v.x * 1664525u;
    v = v ^ (v >> 16u);
    return float(v.x) * (1.0 / 4294967296.0);
}

float noise( in vec2 p )
{
    p*=noiseIntensity;
    ivec2 i = ivec2(floor( p ));
    vec2 f = fract( p );
	vec2 u = f*f*(3.0-2.0*f);
    return mix( mix( lattice_random( i + ivec2(0,0) ),
                     lattice_random( i + ivec2(1,0) ), u.x),
                mix( lattice_random( i + ivec2(0,1) ),
                     lattice_random( i + ivec2(1,1) ), u.x), u.y);
}

float fbm( in vec2 uv )
{
	uv *= 5.0;
    mat2 m = mat2( 1.6,  1.2, -1.2,  1.6 );
    float f  = 0.5000*noise( uv ); uv = m*uv;
    f += 0.2500*noise( uv ); uv = m*uv;
    f += 0.1250*noise( uv ); uv = m*uv;
    f += 0.0625*noise( uv ); uv = m*uv;

	f = 0.5 + 0.5*f;
    return f;
}

vec3 bg(vec2 uv )
{
    float velocity = background.time/1.6;
    float intensity = sin(uv.x*3.+velocity*2.)*1.1+1.5;
    uv.y -= 2.;
    vec2 bp = uv+glowPos;
    uv *= noiseDefinition;

    //ripple
    float rb = fbm(vec2(uv.x*.5-velocity*.03, uv.y))*.1;
    uv += rb;

    //coloring
    float rz = fbm(uv*.9+vec2(velocity*.35, 0.0));
    rz *= dot(bp*intensity,bp)+1.2;

    vec3 col = bgColor/(.1-rz);
    return sqrt(abs(col));
}


float rectangle(vec2 uv, vec2 pos, float width, float height, float blur) {

    pos = (vec2(width, height) + .01)/2. - abs(uv - pos);
    pos = smoothstep(0., blur , pos);
    return pos.x * pos.y;

}

mat2 rotate2d(float _angle){
    return mat2(cos(_angle),-sin(_angle),
                sin(_angle),cos(_angle));
}

vec3 srgb_to_linear(vec3 display)
{
    return mix(display / 12.92, pow((display + 0.055) / 1.055, vec3(2.4)),
               step(vec3(0.04045), display));
}

void main()
{
    vec2 fragCoord = vec2(gl_FragCoord.x, background.resolution.y - gl_FragCoord.y);
	vec2 uv = fragCoord.xy / background.resolution.xy * 2. - 1.;
    uv.x *= background.resolution.x/background.resolution.y;

    //bg
    vec3 color = bg(uv)*(2.-abs(uv.y*2.));

    //rectangles
    float pixel_width = 2. / background.resolution.y;
    float velX = -background.time/8.;
    float velY = background.time/10.;
    for(float i=0.; i<total; i++){
        float index = i/total;
        float rnd = random(vec2(index));
        vec3 pos = vec3(0, 0., 0.);
        pos.x = fract(velX*rnd+index)*4.-2.0;
        pos.y = sin(index*rnd*1000.+velY) * yDistribution;
        pos.z = maxSize*rnd+minSize;
        vec2 uvRot = uv - pos.xy + pos.z/2.;
    	uvRot = rotate2d( i+background.time/2. ) * uvRot;
        uvRot += pos.xy+pos.z/2.;
        float rect = rectangle(uvRot, pos.xy, pos.z, pos.z, max((maxSize+minSize-pos.z)/2., pixel_width));
	    color += rectColor * rect * pos.z/maxSize;
    }

    fragment_color = vec4(srgb_to_linear(clamp(color, 0.0, 1.0)), 1.0);
}
