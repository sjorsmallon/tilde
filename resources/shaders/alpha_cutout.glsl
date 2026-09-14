#ifndef ALPHA_CUTOUT_GLSL
#define ALPHA_CUTOUT_GLSL

// transparency_plan.md ss2. Both are specialization constants, so the discard
// folds away on an opaque pipeline and early-Z survives.
layout(constant_id = 0) const bool  ALPHA_CUTOUT = false;
layout(constant_id = 1) const float ALPHA_CUTOFF = 0.5;

void discard_below_alpha_cutoff(float alpha)
{
    if (ALPHA_CUTOUT && alpha < ALPHA_CUTOFF)
        discard;
}

#endif
