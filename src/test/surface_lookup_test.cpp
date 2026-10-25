// The surface kind parse, the material.txt parse it rides in (material_def.md
// ss3) and the one resolver from a ground contact to a material index: a brush
// face answers with its own material, a face matching nothing answers with the
// brush default, and anything that is not static geometry answers 0
// (material_sound_plan.md step 3).

#include "../shared/assets/material_file.hpp"
#include "../shared/map/game_session.hpp"
#include "../shared/map/map.hpp"
#include "../shared/map/map_geometry.hpp"
#include "../shared/map/surface_kind.hpp"
#include "../shared/map/surface_lookup.hpp"

#include <cassert>
#include <iostream>

namespace
{

void test_parse_is_one_trimmed_word()
{
  assert(shared::try_parse_surface_kind("stone") == shared::Surface_Kind::Stone);
  assert(shared::try_parse_surface_kind("  plastic\r\n") == shared::Surface_Kind::Plastic);
  assert(shared::try_parse_surface_kind("default") == shared::Surface_Kind::Default);
  assert(!shared::try_parse_surface_kind("Stone"));
  assert(!shared::try_parse_surface_kind(""));
  assert(!shared::try_parse_surface_kind("stone metal"));
  for (const shared::surface_kind_name_t& row : shared::SURFACE_KIND_NAMES)
    assert(shared::try_parse_surface_kind(shared::to_string(row.kind)) == row.kind);
}

void test_material_file_lines_are_key_and_value()
{
  const assets::material_file_t empty = assets::parse_material_file("", "empty");
  assert(empty.shader == assets::material_shader_t::Lit);
  assert(empty.surface == shared::Surface_Kind::Default);

  const assets::material_file_t brick = assets::parse_material_file("surface stone\n", "brick");
  assert(brick.shader == assets::material_shader_t::Lit);
  assert(brick.surface == shared::Surface_Kind::Stone);

  const assets::material_file_t ghost =
      assets::parse_material_file("\r\n  shader\tghost \r\n\nsurface glass", "ghost");
  assert(ghost.shader == assets::material_shader_t::Ghost);
  assert(ghost.surface == shared::Surface_Kind::Glass);

  for (const assets::material_shader_name_t& row : assets::MATERIAL_SHADER_NAMES)
    assert(assets::try_parse_material_shader(assets::to_string(row.shader)) == row.shader);
  assert(!assets::try_parse_material_shader("Lit"));
  assert(!assets::try_parse_material_shader(""));
}

void test_a_face_answers_with_its_material()
{
  shared::map_t map;
  map.name = "surface_lookup";

  shared::mesh_geometry_t box = shared::make_box_mesh({0.f, 0.f, 0.f}, {64.f, 32.f, 64.f});

  const uint16_t brick = map.material_index_for("resources/textures/bricks_mortar");
  assert(brick == 1);

  Plane top;
  top.normal = {0.f, 1.f, 0.f};
  top.point  = {0.f, 32.f, 0.f};
  for (uint32_t face_index = 0; face_index < (uint32_t)box.faces.size(); ++face_index)
    if (shared::compute_mesh_face_plane(box, face_index).normal.y > 0.999f)
      box.faces[face_index].surface.material = brick;

  map.add_geometry(box);
  const shared::game_session_t session = shared::build_session(map);
  assert(session.geometry.size() == 1);

  const shared::ground_contact_t on_top = {
      .id = {Collision_Id::Type::Static_Geometry, 0}, .plane = top};
  assert(shared::material_index_under(session, on_top) == brick);

  Plane side;
  side.normal = {1.f, 0.f, 0.f};
  side.point  = {64.f, 0.f, 0.f};
  const shared::ground_contact_t on_side = {
      .id = {Collision_Id::Type::Static_Geometry, 0}, .plane = side};
  assert(shared::material_index_under(session, on_side) == 0);

  const shared::ground_contact_t on_a_mover = {
      .id = {Collision_Id::Type::Entity, 7}, .mover_uid = 7, .plane = top};
  assert(shared::material_index_under(session, on_a_mover) == 0);

  const shared::ground_contact_t out_of_range = {
      .id = {Collision_Id::Type::Static_Geometry, 5}, .plane = top};
  assert(shared::material_index_under(session, out_of_range) == 0);

  const shared::ground_contact_t nothing = {};
  assert(shared::material_index_under(session, nothing) == 0);
}

} // namespace

int main()
{
  test_parse_is_one_trimmed_word();
  test_material_file_lines_are_key_and_value();
  test_a_face_answers_with_its_material();
  std::cout << "surface_lookup_test: all tests passed\n";
  return 0;
}
