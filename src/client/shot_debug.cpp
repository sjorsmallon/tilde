#include "shot_debug.hpp"

#include "../shared/cvars/generated/cvars_generated.hpp"
#include "hitbox_debug_draw.hpp"

#include <algorithm>
#include <format>
#include <string>
#include <cmath>

namespace client
{

namespace
{

constexpr color_t CLIENT_COLOR = colors::blue;
constexpr color_t SERVER_COLOR = colors::red;

linalg::vec3f vector_from_proto(const game::Vec3 &value)
{
  return {value.x(), value.y(), value.z()};
}

// Mirrors bracket_status_t. Kept as a switch over the wire value rather than a
// table so an added status is a compile error here too.
const char *bracket_status_name(uint32_t status)
{
  switch (static_cast<shared::bracket_status_t>(status))
  {
    case shared::bracket_status_t::Ok:        return "Ok";
    case shared::bracket_status_t::Clamped:   return "CLAMPED";
    case shared::bracket_status_t::Absent:    return "ABSENT (no rewind)";
    case shared::bracket_status_t::Malformed: return "MALFORMED (no rewind)";
    case shared::bracket_status_t::Unheld:    return "UNHELD (no rewind)";
  }
  return "unknown";
}

void draw_pose(renderer::debug_draw_list_t &out, const shared::player_pose_t &pose,
               const aim_settings_t &settings, color_t color, float seconds,
               std::vector<assets::posed_hitbox_t> &scratch)
{
  const shared::player_rig_t &rig = shared::player_rig();
  scratch.resize(rig.volume_count());

  // The SAME function both sides pose through, which is what makes shipping a
  // pose instead of a volume set lossless -- see S2C_ShotDebug in game.proto.
  shared::compute_player_hitboxes(rig, pose, settings, scratch);

  const auto line = [&](const linalg::vec3f &start, const linalg::vec3f &end, color_t c)
  { out.line(start, end, c, 0.f, seconds); };

  for (const assets::posed_hitbox_t &hitbox : scratch)
    draw_posed_hitbox(line, hitbox, color);
}

} // namespace

void shot_debug_history_t::record(shot_debug_local_t &&shot)
{
  shots.push_back(std::move(shot));
  while (shots.size() > CAPACITY)
    shots.pop_front();
}

const shot_debug_local_t *shot_debug_history_t::find(uint32_t input_number) const
{
  for (const shot_debug_local_t &shot : shots)
    if (shot.input_number == input_number)
      return &shot;
  return nullptr;
}

void draw_shot_debug_pair(renderer::debug_draw_list_t &out, const shot_debug_local_t* local_recording_of_shot,
                          const game::S2C_ShotDebug &server,
                          const aim_settings_t &settings, float seconds)
{
  if (seconds <= 0.f)
    return;

  std::vector<assets::posed_hitbox_t> scratch;

  // --- The two rays ---
  //
  // Drawn first and always, because they answer a different question from the
  // volumes: two rays apart means the SHOOTER was somewhere else, which is a
  // prediction problem, and no amount of staring at the target's boxes will
  // show it.
  const linalg::vec3f server_eye       = vector_from_proto(server.eye());
  const linalg::vec3f server_direction = vector_from_proto(server.direction());
  const float         ray_length       = 4000.f;
  out.line(server_eye, server_eye + server_direction * ray_length, SERVER_COLOR, 0.f, seconds);

  if (local_recording_of_shot)
    out.line(local_recording_of_shot->eye, local_recording_of_shot->eye + local_recording_of_shot->direction * ray_length, CLIENT_COLOR, 0.f,
             seconds);
 
  // which targets were near enough?
  // reconstruct the pose from the provided protobuf data.
  for (const game::ShotDebugTarget &target : server.targets())
  {
    const shared::player_pose_t pose{.feet_position = vector_from_proto(target.feet_position()),
                                    .body_yaw      = target.body_yaw(),
                                    .view_yaw      = target.view_yaw(),
                                    .view_pitch    = target.view_pitch()};
    draw_pose(out, pose, settings, SERVER_COLOR, seconds, scratch);
  }

 // do we have a local_recording_of_shot copy of that timestamp?
  if (local_recording_of_shot)
  {
    for (const shot_debug_pose_t& drawn : local_recording_of_shot->drawn)
      draw_pose(out, drawn.pose, settings, CLIENT_COLOR, seconds, scratch);

    // The separation itself, as a line you can read a length off. Drawn only
    // between the same player's two silhouettes -- a line between two different
    // players would be a number with no meaning.
    for (const shot_debug_pose_t& drawn : local_recording_of_shot->drawn)
    {
      for (const game::ShotDebugTarget& target : server.targets())
      {
        if (target.player_uid() != drawn.uid)
          continue;

        const linalg::vec3f server_feet = vector_from_proto(target.feet_position());
        if (linalg::length(server_feet - drawn.pose.feet_position) < 0.01f)
          continue; // agreement; a zero-length line draws nothing useful

        out.line(drawn.pose.feet_position, server_feet, colors::yellow, 0.f, seconds);
      }
    }
  }


  const bool  hit    = server.hit_uid() != shared::null_entity_uid;
  const char* status = bracket_status_name(server.bracket_status());

  const linalg::vec3f caption_at =
      hit ? vector_from_proto(server.impact_point()) : server_eye + server_direction * 200.f;

  const std::string caption =
      std::format("{} | rewind {} | bracket {} | asked {}->{}@{:.2f} used {}->{}@{:.2f}{}",
                       hit ? "HIT" : "MISS", server.used_rewind() ? "used" : "NOT USED", status,
                       server.requested_from_tick(), server.requested_towards_tick(),
                       server.requested_fraction(), server.used_from_tick(),
                       server.used_towards_tick(), server.used_fraction(),
                  hit ? std::string{}
                      : std::format(" | nearest miss {:.1f}u", server.nearest_miss_distance()));

  // locally this could have aged out (most likely, it isn't, but still.)
  const std::string client_half =
      local_recording_of_shot ? std::string{}
            : std::string{"  [no client half: input aged out of the ring]"};

  out.text(caption_at, (caption + client_half).c_str(), colors::white, seconds);
}

} // namespace client
