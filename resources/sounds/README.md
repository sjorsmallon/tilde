# Sounds

Drop short `.wav` clips here. Every file is an asset id (`sound_id("twang")`),
numbered at startup; a name no file carries is a build that dies naming it, and
a dropped-in file needs a restart, never a build.

The client audio system (`src/client/audio/audio_system.{hpp,cpp}`, miniaudio
backend) registers every sound at init. `sound_asset::Missing` logs once.

## Movement sounds

Jump, land and footstep play two ways: your **own** player plays them centered
(2D) the instant prediction detects them; **other** players' arrive as
spatialized `Jump`, `Land` and `Footstep` effects from the server, which drop
their own subject. Same files for both.

Land and footstep are PER SURFACE: `SURFACE_SOUNDS` in
`src/client/surface_audio.cpp` has one row per `Surface_Kind` (stone, metal,
wood, glass, fabric, plastic, default), and a material folder names its kind in
a one-word `surface.txt`. No step recording exists yet, so every row's step is
Missing. To add one, drop `step_<kind>_1.wav` .. `step_<kind>_4.wav` here and
name them in the row; `material_sound_plan.md` has the rest.
