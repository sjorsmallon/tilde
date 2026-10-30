#pragma once

// A lens: a window the world is seen through. Geometry whose owner is `revealed_by_lens` is drawn only
// where the line from the eye to it passes through a switched-on lens that passes the owner's colour
// (resources/shaders/reveal.glsl's lens_margin); the filters of every lens on one line multiply, so a red
// and a green stacked pass nothing. The geometry is solid whether or not it is seen. A lens held to the eye
// (the Red Lens weapon's view, Player_Entity::held_lens_on) is a lens on every line of sight of that eye, with
// no rim, for its own draw and for what it watches alike. The pane itself is solid, to bodies and to shots
// alike (collision/lens_panes.hpp).

#include "aabb.hpp"
#include "array.hpp"
#include "entities/generated/entities_core_generated.hpp"
#include "entity_uid.hpp"
#include "linalg.hpp"
#include "plane.hpp"
#include "player_sight.hpp"
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

// Every switched-on Lens_Entity's box.
void compute_lenses(const Entity_System& system, std::vector<lens_t>& out);

// A watcher: an eye and the frustum it sees. Geometry whose owner is `solid_only_when_lensed` is solid, for
// everyone, where some watcher sees it through a lens that passes its colour
// (collision_detection.hpp's GEOMETRY_SOLID_WHERE_LENSED): what is watched holds. The frustum is the
// player's own screen (Player_Entity::view_fov_degrees and view_aspect, restated on every input), clamped
// on arrival: the server cannot verify what a client claims to see, only bound it.
inline constexpr uint32_t LENS_WATCH_SIDE_COUNT = 4;

inline constexpr float LENS_WATCH_SCREEN_FOV_DEGREES_MIN = 20.f;
inline constexpr float LENS_WATCH_SCREEN_FOV_DEGREES_MAX = 120.f;
inline constexpr float LENS_WATCH_SCREEN_ASPECT_MIN      = 0.5f;
inline constexpr float LENS_WATCH_SCREEN_ASPECT_MAX      = 3.6f;

[[nodiscard]] lens_watch_screen_t clamp_lens_watch_screen(const lens_watch_screen_t& view);

struct lens_watcher_t
{
  linalg::vec3f                       eye = {0.f, 0.f, 0.f};
  Array<Plane, LENS_WATCH_SIDE_COUNT> sides;
  uint32_t                            held_channels = 0;
};

// The sides face outward, through the eye: left, right, bottom, top. `fov_degrees` is the view's height.
[[nodiscard]] lens_watcher_t compute_lens_watcher(const linalg::vec3f& eye, float yaw_degrees, float pitch_degrees,
                                                  const lens_watch_screen_t& view, uint32_t held_channels);

[[nodiscard]] bool lens_watcher_faces_point(const lens_watcher_t& watcher, const linalg::vec3f& point);

// Some watcher faces `point` and sees it through lenses whose filters, multiplied, pass `filter`.
[[nodiscard]] bool any_lens_watcher_sees_point(Span<const lens_watcher_t> watchers, Span<const lens_t> lenses,
                                               entities::Lens_Filter filter, const linalg::vec3f& point);

// Every alive sight watches, from its eye, through its screen, with the lens it holds.
void compute_lens_watchers(Span<const player_sight_t> sights, std::vector<lens_watcher_t>& out);

} // namespace shared
