#pragma once

// A lens: a window the world is seen through. Geometry whose owner is `revealed_by_lens` is drawn only
// where the line from the eye to it passes through a switched-on lens that passes the owner's colour
// (resources/shaders/reveal.glsl's lens_margin); the filters of every lens on one line multiply, so a red
// and a green stacked pass nothing. The geometry is solid whether or not it is seen. A lens held to the eye
// (the Red Lens weapon's view, Player_Entity::held_lens_on) is a lens on every line of sight of that eye, with
// no rim, for its own draw and for what it watches alike. The pane itself is solid, to bodies and to shots
// alike (collision/lens_panes.hpp).

#include "aabb.hpp"
#include "entities/generated/entities_core_generated.hpp"
#include "entity_uid.hpp"
#include "linalg.hpp"
#include "span.hpp"

#include <cstdint>
#include <optional>
#include <vector>

namespace entities
{
struct Lens_Entity;
struct Player_Entity;
}

namespace shared
{

struct Entity_System;

struct lens_t
{
  oriented_box_t        box;
  entities::Lens_Filter filter = entities::Lens_Filter::Red;
};

inline constexpr uint32_t LENS_CHANNEL_RED   = 1u << 0;
inline constexpr uint32_t LENS_CHANNEL_GREEN = 1u << 1;
inline constexpr uint32_t LENS_CHANNEL_BLUE  = 1u << 2;
inline constexpr uint32_t LENS_CHANNEL_ALL   = LENS_CHANNEL_RED | LENS_CHANNEL_GREEN | LENS_CHANNEL_BLUE;

[[nodiscard]] uint32_t      get_channel_bits_for_lens_filter(entities::Lens_Filter filter);
[[nodiscard]] linalg::vec3f get_color_for_lens_filter(entities::Lens_Filter filter);

// Its volume about the entity plus the volume's offset, turned by the entity's orientation.
[[nodiscard]] lens_t compute_lens_box(const entities::Lens_Entity& lens);

// The segment from `eye` to `point` enters the lens and leaves it before reaching `point`: an eye inside
// the lens looks through it, a point inside the lens or in front of it is not seen through it.
[[nodiscard]] bool lens_lies_on_line_of_sight(const lens_t& lens, const linalg::vec3f& eye,
                                              const linalg::vec3f& point);

// The channels `point` is seen in from `eye`: the product of every lens on the line and of `held_channels`, the
// lens held to the eye (0 when none is); 0 when nothing is on the line.
[[nodiscard]] uint32_t compute_channels_seen_through_lenses(Span<const lens_t> lenses, const linalg::vec3f& eye,
                                                            const linalg::vec3f& point, uint32_t held_channels);

// The filter of the Lens_View weapon in the player's hand, if that is what is held.
[[nodiscard]] std::optional<entities::Lens_Filter> try_get_held_lens_filter(const Entity_System&           system,
                                                                             const entities::Player_Entity& player);

// The channel bits a player's held lens passes: those of the Lens_View weapon in hand while `held_lens_on` (the
// server's flag, or the client's prediction of it) and the player watches; 0 otherwise.
[[nodiscard]] uint32_t compute_held_lens_channels(const Entity_System& system, const entities::Player_Entity& player,
                                                  bool held_lens_on);

// Every switched-on Lens_Entity's box.
void collect_lenses(const Entity_System& system, std::vector<lens_t>& out);

// A watcher: an eye and where it faces. Geometry whose owner is `solid_only_when_lensed` is solid, for
// everyone, where some watcher sees it through a lens that passes its colour
// (collision_detection.hpp's GEOMETRY_SOLID_WHERE_LENSED): what is watched holds.
struct lens_watcher_t
{
  linalg::vec3f eye           = {0.f, 0.f, 0.f};
  linalg::vec3f forward       = {0.f, 0.f, -1.f};
  uint32_t      held_channels = 0;
};

// How far off the watcher's facing a point may lie and still count as looked at.
inline constexpr float LENS_WATCH_HALF_ANGLE_DEGREES = 45.f;

[[nodiscard]] lens_watcher_t compute_lens_watcher(const linalg::vec3f& eye, float yaw_degrees, float pitch_degrees,
                                                  uint32_t held_channels);

[[nodiscard]] bool lens_watcher_faces_point(const lens_watcher_t& watcher, const linalg::vec3f& point);

// Some watcher faces `point` and sees it through lenses whose filters, multiplied, pass `filter`.
[[nodiscard]] bool any_lens_watcher_sees_point(Span<const lens_watcher_t> watchers, Span<const lens_t> lenses,
                                               entities::Lens_Filter filter, const linalg::vec3f& point);

// The one rule for who watches: alive. `predicted_by_caller` is the one player left out, whose eye a
// client adds from its own prediction; null on the server.
[[nodiscard]] bool player_watches_lenses(const entities::Player_Entity& player);
void collect_lens_watchers(const Entity_System& system, entity_uid_t predicted_by_caller,
                           std::vector<lens_watcher_t>& out);

} // namespace shared
