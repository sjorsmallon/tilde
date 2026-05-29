// Single translation unit that compiles the miniaudio implementation. Every
// other TU includes "miniaudio.h" for declarations only; this one defines it.
//
// We disable subsystems the game does not use to cut compile time and binary
// size. Re-enable as needed (e.g. MA_NO_FLAC if we never ship .flac assets).
#define MINIAUDIO_IMPLEMENTATION
#define MA_NO_ENCODING    // playback only; we never write audio files
#define MA_NO_GENERATION  // no procedural waveform/noise generators

#include "miniaudio.h"
