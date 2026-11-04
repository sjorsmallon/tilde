// The fill of a drawn body in the air, a beam's or a shadow volume's: one for a flat wash; for dots, the
// dot's own alpha is raised by its share of the cell so the average tint over the body is the wash's.
// Reads scene.beam: x one of BEAM_FILL_*, z the dot spacing in pixels.

// A dot's radius as a share of the spacing, and the 45 degree turn a printed screentone sits at.
const float BEAM_DOT_RADIUS  = 0.18;
const float BEAM_DOT_ANGLE   = 0.7853981634;
const float BEAM_FILL_PI     = 3.14159265;

float beam_fill_factor()
{
    if (int(scene.beam.x) == BEAM_FILL_TINT)
        return 1.0;
    float spacing     = scene.beam.z;
    mat2  turn        = mat2(cos(BEAM_DOT_ANGLE), sin(BEAM_DOT_ANGLE), -sin(BEAM_DOT_ANGLE), cos(BEAM_DOT_ANGLE));
    vec2  cell        = fract(turn * gl_FragCoord.xy / spacing) - 0.5;
    float from_centre = length(cell);
    float edge        = fwidth(from_centre);
    float inside_dot  = 1.0 - smoothstep(BEAM_DOT_RADIUS - edge, BEAM_DOT_RADIUS + edge, from_centre);
    float coverage    = BEAM_FILL_PI * BEAM_DOT_RADIUS * BEAM_DOT_RADIUS;
    return inside_dot / coverage;
}
