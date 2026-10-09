#pragma once

// A solid beam's ripple where a player came to stand on it: an edge on Movement::ground_mover_uid, never networked.

#include "entity_uid.hpp"
#include "linalg.hpp"
#include "span.hpp"

#include <vector>

namespace shared
{

struct Entity_System;

// Past this beam.frag's envelope has faded to nothing.
constexpr float BEAM_RIPPLE_MAX_AGE_SECONDS = 5.0f;

struct beam_ripple_t
{
  linalg::vec3f center      = {0.f, 0.f, 0.f};
  entity_uid_t  beam        = null_entity_uid;
  float         age_seconds = 0.f;
};

struct beam_stander_t
{
  entity_uid_t  uid              = null_entity_uid;
  entity_uid_t  ground_mover_uid = null_entity_uid;
  linalg::vec3f feet             = {0.f, 0.f, 0.f};
};

struct beam_ripple_state_t
{
  std::vector<beam_ripple_t> ripples;

  struct standing_t
  {
    entity_uid_t stander;
    entity_uid_t beam;
  };
  std::vector<standing_t> standing;
};

// Once per frame, with every player in `standers`.
void detect_beam_landings(const Entity_System& system, Span<const beam_stander_t> standers,
                          beam_ripple_state_t& state);

// Once per frame on the world's clock; a ripple past BEAM_RIPPLE_MAX_AGE_SECONDS is dropped.
void age_beam_ripples(beam_ripple_state_t& state, float dt);

} // namespace shared
