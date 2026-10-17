#pragma once

#include "../shared/color.hpp"
#include "../shared/entities/generated/entities_core_generated.hpp"
#include "../shared/linalg.hpp"
#include "../shared/span.hpp"
#include "entities/generated/entities/player_entity_generated.hpp"
#include "renderer.hpp"
#include "ui/font.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace client::hud
{

// How long a banner stays is how long it takes to READ: a floor for a two-word
// "Saved!", then reading time per word. A flat three seconds was right for the
// short ones and gone before a sentence was half read.
inline constexpr float ANNOUNCEMENT_MINIMUM_SECONDS  = 3.0f;
inline constexpr float ANNOUNCEMENT_SECONDS_PER_WORD = 0.35f;

// A banner wider than this fraction of the screen is word-wrapped.
inline constexpr float ANNOUNCEMENT_MAX_WIDTH_FRACTION = 0.8f;

struct announcement_t
{
  std::string text;
  float remaining_seconds = 0.0f;
};

announcement_t &current_announcement();

// A '\n' in the text starts a new line; lines longer than the screen allows
// are wrapped at word boundaries when drawn.
void set_announcement(std::string_view text);

[[nodiscard]] float announcement_duration_for(std::string_view text);

// The text cut into lines for `max_width` pixels: first at every '\n', then
// each of those wrapped greedily at spaces. A single word wider than the limit
// is a line of its own rather than being cut mid-word. Pure, so it can be pinned.
[[nodiscard]] std::vector<std::string> announcement_lines(const ui::ui_font_t &font,
                                                          ui::font_size_t size,
                                                          std::string_view text,
                                                          float max_width);

void advance_announcement(announcement_t &announcement, float delta_seconds);

void draw_announcement(renderer::ui_draw_list_t &list, const ui::ui_font_t &font,
                       linalg::vec2 screen, float display_scale,
                       const announcement_t &announcement);

// The banner for the match moving from `before` to `after`, empty when there is
// nothing to say. A restart is an edge too: same phase, the next round number.
// `frag_leader_name` names who won a Frag_Limit match.
[[nodiscard]] std::string match_announcement_for(const entities::Match& before,
                                                 const entities::Match& after,
                                                 std::string_view frag_leader_name);

// The three lengths are LOGICAL units: a crosshair the player tuned at 96 DPI
// must keep its apparent size on a denser display, or their aim reference moves
// when they change monitor.
struct crosshair_settings_t
{
  float   arm_length = 7.f;
  float   gap        = 5.f;             // center to where each line starts
  float   thickness  = 2.f;             // line width, and the dot's size
  bool    draw_dot   = true;
  color_t color      = colors::green;
};

// Call from a state's build_frame().
void draw_crosshair(renderer::ui_draw_list_t &list, linalg::vec2 screen, float display_scale,
                    const crosshair_settings_t &settings);

// How long until the weapon being raised is up, drawn as a banner.
//
// A POLLED value, not a pushed one, which is what keeps it out of
// the announcement above. An announcement is a discrete occurrence with a
// lifetime of its own -- it is set once, it counts itself down, and its model
// is what gets drawn. A deploy countdown is a continuous read of a clock that
// already exists (prediction_t::seconds_until_local_deploy_complete), so
// pushing it into a banner would be a second copy of that clock, free to
// disagree with the gate the shot is actually judged against. See the
// "continuous values are polled from the truth" rule in CLAUDE.md.
//
// So there is no state here and nothing to advance: the caller hands over the
// seconds it already holds, and zero draws nothing.
//
// It reads the CLIENT's predicted clock rather than the server's
// Inventory::deploy_complete_time, which is deliberate and is the only honest
// option: that deadline is sub-tick and server-only, and a replicated copy
// would be a round trip behind the keypress the player is watching the number
// for.

// Nothing is drawn while `seconds_remaining` is at or below zero. Debug-shaped
// on purpose -- this is what cl_show_deploy_timer exists to look at, and a
// shipped HUD would show the draw animation instead of a number.
void draw_deploy_timer(renderer::ui_draw_list_t &list, const ui::ui_font_t &font,
                       linalg::vec2 screen, float display_scale, float seconds_remaining);

// Polled off the replicated phase deadline; nothing draws once seconds_remaining reaches zero.
void draw_freeze_countdown(renderer::ui_draw_list_t& list, const ui::ui_font_t& font,
                           linalg::vec2 screen, float display_scale, float seconds_remaining,
                           std::string_view caption);

// Polled off the replicated phase start; nothing draws while seconds_elapsed is negative.
void draw_run_timer(renderer::ui_draw_list_t& list, const ui::ui_font_t& font,
                    linalg::vec2 screen, float display_scale, float seconds_elapsed);

struct warmup_vote_view_t
{
  int32_t              joined        = 0;
  int32_t              ready         = 0;
  bool                 i_have_a_body = false;
  bool                 i_am_ready    = false;
  bool                 starts_when_loaded = false;
  std::optional<float> seconds_until_start;
};

[[nodiscard]] std::string warmup_vote_text(const warmup_vote_view_t& vote);

void draw_ready_status(renderer::ui_draw_list_t& list, const ui::ui_font_t& font,
                       linalg::vec2 screen, float display_scale, const warmup_vote_view_t& vote);

void draw_weapon_name(renderer::ui_draw_list_t& list, const ui::ui_font_t& font,
                      linalg::vec2 screen, float display_scale, std::string_view weapon_name,
                      int32_t ammo, int32_t reserve_ammo);

// One drawn row. Flattened out of the entity on purpose: the draw half then
// needs no entity header, no client_context and no snapshot, which is what lets
// it be exercised with nothing but a baked font.
struct scoreboard_row_t
{
  std::string name;
  int32_t     kills    = 0;
  int32_t     deaths   = 0;
  int32_t     slot     = -1;
  bool        is_local = false;
  bool        is_alive = true;
};

// Fill `storage` from the session's player pool, in DRAW ORDER, and return the
// rows actually written.
//
// The sort is not cosmetic. A pool is swap-and-pop storage, whose order moves
// whenever a player leaves -- so drawing it in traversal order would let rows
// swap places mid-match for no reason the player could see. Ranked by kills, then by fewest deaths, and finally by slot, which
// is what makes the order TOTAL: without that last key two tied players still
// have no defined position relative to each other.
//
// `storage` is CAPACITY, not length, and that is the whole reason it is a Span
// rather than a vector. There is exactly one row per player, so a length is not
// something the caller could usefully pass: it would only be restating
// players.size(), and a restated number is free to disagree with the original.
// Handing over the whole buffer and getting the filled prefix back leaves no
// second copy of the count to be wrong. The one failure left is a real one --
// more players than the buffer holds -- and it is fatal.
[[nodiscard]] Span<scoreboard_row_t> collect_scoreboard_rows(
    Span<const entities::Player_Entity> players,
    int32_t local_slot,
    Span<scoreboard_row_t> storage);

// Draw the panel centered on screen. An empty `rows` still draws the frame and
// its header: a board that vanishes when the last player leaves reads as a
// broken key, not as an empty server.
void draw_scoreboard(renderer::ui_draw_list_t &list, const ui::ui_font_t &font,
                     linalg::vec2 screen, float display_scale,
                     Span<const scoreboard_row_t> rows);

} // namespace client::hud
