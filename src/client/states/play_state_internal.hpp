#pragma once

// What the two halves of Play_State share: the update (play_state.cpp) reads the server and steps the
// prediction, the draw (play_state_draw.cpp) turns the session into the lists the renderer records. Each
// function below is where the update's answer to "where is this drawn" is read by the draw. Nothing outside
// those two files includes this.

#include "../client_context.hpp"
#include "../renderer.hpp"
#include "../../shared/collision/mover_path.hpp"
#include "../../shared/map/mesh/mesh.hpp"
#include "../../shared/weapons.hpp"

namespace client
{

// The tick the server will run `input_number` in, by the prediction's own count.
[[nodiscard]] uint32_t get_predicted_server_tick_which_this_input_will_be_simulated_on(const client_context_t& ctx,
                                                                                        int input_number);

struct drawn_mover_poses_t
{
  // Where the last predicted step stood its rider: the exact tick, never the eased one.
  shared::path_pose_t at_tick{};
  shared::path_pose_t drawn{};
};

[[nodiscard]] shared::path_pose_t get_rest_pose_for_mover(const client_context_t& ctx, const entities::Mover_Entity& mover);
[[nodiscard]] drawn_mover_poses_t drawn_mover_poses(const client_context_t& ctx, const entities::Mover_Entity& mover);

// Where a Rides entity is drawn this frame: carried by its mover's drawn pose, where it was placed otherwise.
[[nodiscard]] shared::path_pose_t drawn_ridden_pose(const client_context_t& ctx, const entities::Entity& entity);

// Where our own feet are drawn: the prediction plus the error offset, carried by whatever we stand on.
[[nodiscard]] vec3f drawn_local_feet(const client_context_t& ctx);

// Where our own BODY's head points: the mouse, or while a camera swap hands it to another mouse, the head the
// server wrote as the snapshot holds it (shared/camera_swap.hpp). The mouse keeps writing prediction.player_yaw
// underneath either way: swapped, that is the look out of the other body, which our eye sits in.
[[nodiscard]] shared::View drawn_local_head(const client_context_t& ctx);

// The remote body a camera swap seats our eye in: the one our mouse turns. Null in our own body, and null
// while the snapshot has the swap but the body has not reached the interpolation ring yet.
[[nodiscard]] const Remote_Player_State* try_find_remote_body_my_eye_is_in(const client_context_t& ctx);

// A passenger is inside their driver's body; the driver we ride and the body our eye is seated in are
// the ones we look out of.
[[nodiscard]] bool remote_player_is_hidden_from_my_eye(const client_context_t& ctx, const Remote_Player_State& remote_player);

// The wipe an owner's timer puts on its geometry, read on the same clock the movers are drawn on.
[[nodiscard]] renderer::clock_wipe_t compute_drawn_clock_wipe_for_geometry(const client_context_t& ctx,
                                                                           shared::entity_uid_t owner_uid,
                                                                           const shared::mesh_geometry_t& geometry);

// The channels of the lens our prediction holds to our eye, 0 with none or nobody.
[[nodiscard]] uint32_t local_held_lens_channels(const client_context_t& ctx);

[[nodiscard]] const entities::Weapon_Entity* try_find_active_weapon(const client_context_t& ctx,
                                                                   const entities::Player_Entity& player);
[[nodiscard]] const shared::weapon_definition_t*
try_find_weapon_definition_for_active_weapon_held_by_player(const client_context_t& ctx, const entities::Player_Entity* player);

[[nodiscard]] uint8_t clamp_crosshair_color_channel(uint32_t value);

} // namespace client
