// Guards the `.skeleton` / `.mesh` readers (model_format.cpp) against the REAL
// exporter output in resources/models, plus a set of malformed fixtures.
//
// The real-file half is the point: every invariant the readers check --
// parent-before-child, name hash, weight normalization, index ranges -- is a
// promise made by src/tools/blender_export.py, and this is where the two are
// checked against each other rather than each against its own idea.
//
// Run from the project root (ctest pins WORKING_DIRECTORY for exactly this).

#include "animation.hpp"
#include "asset.hpp"
#include "model_format.hpp"
#include "skinning.hpp"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

static assets::asset_state_t g_asset_state;
static int32_t               g_checks = 0;

#define CHECK(condition, ...)                                                                  \
  do                                                                                           \
  {                                                                                            \
    g_checks += 1;                                                                             \
    if (!(condition))                                                                          \
    {                                                                                          \
      printf("  FAIL (%s:%d): %s\n", __FILE__, __LINE__, #condition);                          \
      printf("        ");                                                                      \
      printf(__VA_ARGS__);                                                                     \
      printf("\n");                                                                            \
      abort();                                                                                 \
    }                                                                                          \
  } while (0)

static const char *SKELETON_PATH = "resources/models/rig.skeleton";
static const char *MESH_PATH     = "resources/models/Leet_Full.mesh";

// --- Fixtures --------------------------------------------------------------

static std::string fixture_path(const char *name)
{
  return (std::filesystem::temp_directory_path() / name).string();
}

static void write_fixture(const std::string &path, const std::string &contents)
{
  std::ofstream file(path);
  file << contents;
  if (!file)
  {
    printf("  FAIL: could not write fixture '%s'\n", path.c_str());
    abort();
  }
}

// --- The real exporter output ----------------------------------------------

static void test_real_skeleton()
{
  printf("test_real_skeleton\n");

  assets::skeleton_t skeleton;
  CHECK(models::parse_skeleton_file(SKELETON_PATH, skeleton), "'%s' must parse", SKELETON_PATH);

  CHECK(skeleton.name == "rig", "name was '%s'", skeleton.name.c_str());
  CHECK(skeleton.bones.size() == 35, "expected 35 deform bones, got %zu", skeleton.bones.size());
  CHECK(skeleton.bones.size() <= assets::MAX_BONES, "over the %u-bone budget", assets::MAX_BONES);

  // Exactly one root, and the DEF- prefix is Rigify bookkeeping the exporter
  // strips -- both are load-bearing (animation_def.md §1), so both are asserted
  // here rather than assumed.
  int32_t root_count = 0;
  for (size_t index = 0; index < skeleton.bones.size(); ++index)
  {
    const assets::bone_t &bone = skeleton.bones[index];
    if (bone.parent_index < 0)
      root_count += 1;
    CHECK(bone.parent_index < (int32_t)index, "bone '%s' parents forward to %d",
          bone.name.c_str(), bone.parent_index);
    CHECK(bone.name.rfind("DEF-", 0) != 0, "bone '%s' kept its Rigify prefix", bone.name.c_str());
  }
  CHECK(root_count == 1, "expected 1 root after parent reconstruction, got %d", root_count);

  // The hierarchy reconstruction's whole reason to exist: without it the arms
  // are not descendants of the spine and aim/torso masking silently break.
  auto index_of = [&skeleton](const char *name) -> int32_t
  {
    for (size_t index = 0; index < skeleton.bones.size(); ++index)
      if (skeleton.bones[index].name == name)
        return (int32_t)index;
    return -1;
  };

  const int32_t spine     = index_of("spine");
  const int32_t upper_arm = index_of("upper_arm.L");
  CHECK(spine >= 0, "no 'spine' bone");
  CHECK(upper_arm >= 0, "no 'upper_arm.L' bone");

  int32_t walker = upper_arm;
  while (walker > 0 && walker != spine)
    walker = skeleton.bones[walker].parent_index;
  CHECK(walker == spine, "'upper_arm.L' does not descend from 'spine'");
}

static void test_real_mesh()
{
  printf("test_real_mesh\n");

  assets::mesh_asset_t         mesh;
  models::skeleton_reference_t reference;
  CHECK(models::parse_mesh_file(MESH_PATH, mesh, reference), "'%s' must parse", MESH_PATH);

  CHECK(reference.skeleton_name == "rig", "skeleton reference was '%s'",
        reference.skeleton_name.c_str());
  CHECK(std::fabs(reference.scale - models::METRES_TO_UNITS) < 0.001f, "scale was %f",
        reference.scale);

  // Deliberately a RANGE, not the exact count. These were pinned to one
  // export's 1216 vertices, which made every legitimate edit to the model a red
  // test -- and a test that cries wolf on authoring is a test people stop
  // reading. What this guards is that a plausible character came through at
  // all; the invariants below are the part worth being exact about.
  CHECK(mesh.vertices.size() > 100 && mesh.vertices.size() < 100000,
        "%zu vertices is not a plausible character mesh", mesh.vertices.size());
  CHECK(mesh.indices.size() % 3 == 0, "%zu indices is not whole triangles", mesh.indices.size());
  CHECK(mesh.is_skinned(), "a mesh naming a skeleton must carry skin data");
  CHECK(mesh.skin.size() == mesh.vertices.size(), "skin is parallel to vertices: %zu vs %zu",
        mesh.skin.size(), mesh.vertices.size());
  CHECK(mesh.materials.size() == 3, "expected 3 materials, got %zu", mesh.materials.size());
  CHECK(mesh.submeshes.size() == 3, "expected 3 submeshes, got %zu", mesh.submeshes.size());

  // Submeshes must tile the index buffer exactly -- a gap draws nothing and an
  // overlap draws twice, both of which look like a modelling problem.
  uint32_t covered = 0;
  for (size_t index = 0; index < mesh.submeshes.size(); ++index)
  {
    CHECK(mesh.submeshes[index].index_offset == covered, "submesh %zu starts at %u, expected %u",
          index, mesh.submeshes[index].index_offset, covered);
    covered += mesh.submeshes[index].index_count;
  }
  CHECK(covered == mesh.indices.size(), "submeshes cover %u of %zu indices", covered,
        mesh.indices.size());

  // --- the axis conversion and the scale, which nothing else proves ---
  //
  // These are the two things animation_def.md flags as INFERRED (from asset.cpp
  // doing no flip on OBJ load) and unproven until something renders. A standing
  // humanoid pins both without a renderer, because the shape of a person is not
  // ambiguous:
  //
  //   - Y is up: the figure is TALL and THIN, so its vertical span dwarfs its
  //     depth. Blender is Z-up; if `(x,y,z) -> (x,z,-y)` were dropped, the two
  //     would swap and this check would be the one that noticed.
  //   - The feet are on the ground plane (y ~ 0), which no coincidence of
  //     scaling produces.
  //   - 1 unit == 1 inch: a person is ~67 units against a 72-unit player hull
  //     (player_half_height 36). Missing the 39.37 metres->units factor would
  //     make the whole model under 2 units tall.
  vec3f minimum = mesh.vertices[0].position;
  vec3f maximum = mesh.vertices[0].position;
  for (const vertex_xnu &vertex : mesh.vertices)
  {
    minimum.x = std::fmin(minimum.x, vertex.position.x);
    minimum.y = std::fmin(minimum.y, vertex.position.y);
    minimum.z = std::fmin(minimum.z, vertex.position.z);
    maximum.x = std::fmax(maximum.x, vertex.position.x);
    maximum.y = std::fmax(maximum.y, vertex.position.y);
    maximum.z = std::fmax(maximum.z, vertex.position.z);
  }

  float vertical_span = maximum.y - minimum.y;
  float depth_span    = maximum.z - minimum.z;

  CHECK(vertical_span > 3.0f * depth_span,
        "vertical span %f is not much larger than depth span %f -- a standing figure is tall and "
        "thin, so the up axis is not Y and the Blender Z-up conversion did not happen",
        vertical_span, depth_span);
  CHECK(std::fabs(minimum.y) < 5.0f, "the model's lowest point is y = %f, not on the ground plane",
        minimum.y);
  CHECK(vertical_span > 50.0f && vertical_span < 90.0f,
        "the figure is %f units tall; a person is ~67 against a 72-unit player hull, so the "
        "metres-to-units scale did not survive the export",
        vertical_span);

  // Normals survive the axis conversion as unit vectors: the 3x3 part is a
  // proper rotation, so no renormalization happens anywhere downstream.
  for (size_t index = 0; index < mesh.vertices.size(); ++index)
  {
    const vec3f &normal = mesh.vertices[index].normal;
    float        length = std::sqrt(dot(normal, normal));
    CHECK(std::fabs(length - 1.0f) < 1e-3f, "vertex %zu normal has length %f", index, length);
  }
}

// Loading through the asset layer is a separate path from the parsers: it is
// where the mesh's skeleton reference is resolved and the two hashes are made to
// agree.
static void test_asset_layer_resolves_skeleton()
{
  printf("test_asset_layer_resolves_skeleton\n");

  assets::asset_handle_t<assets::mesh_asset_t> handle = assets::load_mesh(MESH_PATH);
  CHECK(handle.valid(), "load_mesh('%s') returned an invalid handle", MESH_PATH);

  const assets::mesh_asset_t *mesh = assets::get(handle);
  CHECK(mesh != nullptr, "handle did not resolve");
  CHECK(mesh->skeleton.valid(), "a skinned mesh must resolve its skeleton at load");

  const assets::skeleton_t *skeleton = assets::get(mesh->skeleton);
  CHECK(skeleton != nullptr, "skeleton handle did not resolve");
  CHECK(skeleton->bones.size() == 35, "resolved skeleton has %zu bones", skeleton->bones.size());

  // Every influence must name a bone that exists -- the bound the parser cannot
  // check on its own, since it does not know the skeleton.
  for (const assets::vertex_skin_t &influences : mesh->skin)
    for (uint32_t slot = 0; slot < assets::MAX_BONE_INFLUENCES_PER_VERTEX; ++slot)
      CHECK(influences.bone_indices[slot] < skeleton->bones.size(), "influence names bone %u",
            influences.bone_indices[slot]);

  // Resolved by bare name against the mesh's own directory, and shared: a
  // second load must hand back the same handle, or "bone 7" stops being one
  // bone.
  assets::asset_handle_t<assets::skeleton_t> direct = assets::load_skeleton(SKELETON_PATH);
  CHECK(direct == mesh->skeleton, "the skeleton was loaded twice into two handles");

  // Loading the mesh again is cached, not re-parsed into a second copy.
  CHECK(assets::load_mesh(MESH_PATH) == handle, "load_mesh is not idempotent");
}

// --- Malformed input must be refused, never half-accepted ------------------

static void test_rejects_bad_skeletons()
{
  printf("test_rejects_bad_skeletons\n");
  printf("  (the errors below are expected)\n");

  assets::skeleton_t skeleton;

  std::string forward_parent = fixture_path("bad_forward_parent.skeleton");
  write_fixture(forward_parent,
                "skeleton bad\n"
                "hash 0000000000000000\n"
                "bones 2\n"
                "b 0 root 1 1 0 0 0 0 1 0 0 0 0 1 0 0 0 0 1\n"
                "b 1 child -1 1 0 0 0 0 1 0 0 0 0 1 0 0 0 0 1\n");
  CHECK(!models::parse_skeleton_file(forward_parent.c_str(), skeleton),
        "a bone parented to a later bone must be refused");

  std::string wrong_hash = fixture_path("bad_hash.skeleton");
  write_fixture(wrong_hash,
                "skeleton bad\n"
                "hash deadbeefdeadbeef\n"
                "bones 1\n"
                "b 0 root -1 1 0 0 0 0 1 0 0 0 0 1 0 0 0 0 1\n");
  CHECK(!models::parse_skeleton_file(wrong_hash.c_str(), skeleton),
        "a hash that does not match the bone names must be refused");

  std::string truncated = fixture_path("bad_truncated.skeleton");
  write_fixture(truncated,
                "skeleton bad\n"
                "hash 0000000000000000\n"
                "bones 3\n"
                "b 0 root -1 1 0 0 0 0 1 0 0 0 0 1 0 0 0 0 1\n");
  CHECK(!models::parse_skeleton_file(truncated.c_str(), skeleton),
        "a file with fewer bones than it declares must be refused");

  std::string over_budget = fixture_path("bad_budget.skeleton");
  write_fixture(over_budget, "skeleton bad\nhash 0000000000000000\nbones 129\n");
  CHECK(!models::parse_skeleton_file(over_budget.c_str(), skeleton),
        "more than MAX_BONES must be refused, not truncated");

  CHECK(!models::parse_skeleton_file("resources/models/does_not_exist.skeleton", skeleton),
        "a missing file must be refused");
}

static void test_rejects_bad_meshes()
{
  printf("test_rejects_bad_meshes\n");
  printf("  (the errors below are expected)\n");

  assets::mesh_asset_t         mesh;
  models::skeleton_reference_t reference;

  const char *header = "mesh bad\nskeleton rig 0000000000000000\nscale 39.37000\n";

  std::string unnormalized = fixture_path("bad_weights.mesh");
  write_fixture(unnormalized,
                std::string(header) + "vertices 1\n" +
                    "v 0 0 0 0 1 0 0 0 0 0 0 0 0.5 0.2 0 0\n" + "indices 0\n");
  CHECK(!models::parse_mesh_file(unnormalized.c_str(), mesh, reference),
        "weights that do not sum to 1 must be refused");

  std::string bad_index = fixture_path("bad_index.mesh");
  write_fixture(bad_index,
                std::string(header) + "vertices 1\n" +
                    "v 0 0 0 0 1 0 0 0 0 0 0 0 1 0 0 0\n" + "indices 3\n" + "i 0 0 7\n");
  CHECK(!models::parse_mesh_file(bad_index.c_str(), mesh, reference),
        "a triangle referencing a vertex that does not exist must be refused");

  std::string bad_scale = fixture_path("bad_scale.mesh");
  write_fixture(bad_scale, "mesh bad\nskeleton rig 0000000000000000\nscale 1.0\nvertices 0\nindices 0\n");
  CHECK(!models::parse_mesh_file(bad_scale.c_str(), mesh, reference),
        "an exporter/engine scale disagreement must be refused");

  std::string bad_submesh = fixture_path("bad_submesh.mesh");
  write_fixture(bad_submesh,
                std::string(header) + "mat 0 only -\n" + "vertices 1\n" +
                    "v 0 0 0 0 1 0 0 0 0 0 0 0 1 0 0 0\n" + "indices 3\n" + "i 0 0 0\n" +
                    "sub 0 0 6 0\n");
  CHECK(!models::parse_mesh_file(bad_submesh.c_str(), mesh, reference),
        "a submesh running past the index buffer must be refused");

  // A .mesh whose skeleton hash does not match the sibling .skeleton: the
  // asset layer's check, not the parser's.
  std::string mismatched = "resources/models/hash_mismatch_fixture.mesh";
  write_fixture(mismatched,
                "mesh bad\nskeleton rig 1111111111111111\nscale 39.37000\n"
                "vertices 1\nv 0 0 0 0 1 0 0 0 0 0 0 0 1 0 0 0\nindices 0\n");
  CHECK(!assets::load_mesh(mismatched.c_str()).valid(),
        "a mesh skinned against a different skeleton revision must be refused");
  std::filesystem::remove(mismatched);
}

// A .mesh with no `skeleton` line is a static mesh, and nothing downstream
// learns that skinning exists.
static void test_static_mesh()
{
  printf("test_static_mesh\n");

  std::string path = fixture_path("static.mesh");
  write_fixture(path,
                "mesh flat\n"
                "scale 39.37000\n"
                "mat 0 default -\n"
                "vertices 3\n"
                "v 0 0 0 0 1 0 0 0 0 0 0 0 0 0 0 0\n"
                "v 1 0 0 0 1 0 1 0 0 0 0 0 0 0 0 0\n"
                "v 0 0 1 0 1 0 0 1 0 0 0 0 0 0 0 0\n"
                "indices 3\n"
                "i 0 1 2\n"
                "sub 0 0 3 0\n");

  assets::mesh_asset_t         mesh;
  models::skeleton_reference_t reference;
  CHECK(models::parse_mesh_file(path.c_str(), mesh, reference), "a static .mesh must parse");
  CHECK(reference.skeleton_name.empty(), "a static mesh names no skeleton");
  CHECK(!mesh.is_skinned(), "a static mesh must carry no skin data");
  CHECK(mesh.skin.empty(), "skin.empty() IS the not-skinned test");
  CHECK(mesh.vertices.size() == 3, "expected 3 vertices, got %zu", mesh.vertices.size());
  CHECK(mesh.materials[0].texture_path.empty(), "'-' must read back as no texture");
}

// The GPU-free half of GPU skinning (animation_def.md build order step 2).
//
// A skeleton posed in its BIND pose must produce identity skinning matrices --
// `model_space[i] * inverse_bind[i]` where model_space came from walking the
// hierarchy over locals that were themselves derived from inverse_bind. The two
// cancel by construction, which is the point: it is a self-checking case that
// needs no authored animation and no renderer.
//
// What it catches: a reversed multiply order, a hierarchy walked in the wrong
// direction, a parent read after being overwritten, a broken inverse_affine.
// What it does NOT catch, and this is worth knowing rather than assuming: the
// reader's row-major -> column-major TRANSPOSE of inverse_bind. Every matrix
// here is derived from inverse_bind, so a uniformly transposed skeleton
// telescopes to identity just as cleanly. Only a pose from OUTSIDE the skeleton
// -- a clip or an authored pose, animation_def.md step 5 -- can test that.
static void test_bind_pose_skinning_is_identity()
{
  printf("test_bind_pose_skinning_is_identity\n");

  assets::skeleton_t skeleton;
  CHECK(models::parse_skeleton_file(SKELETON_PATH, skeleton), "'%s' must parse", SKELETON_PATH);

  std::vector<linalg::mat4f> parent_space(skeleton.bones.size());
  std::vector<linalg::mat4f> skinning(skeleton.bones.size());
  assets::compute_parent_space_bind_matrices(skeleton, parent_space);
  assets::compute_skinning_matrices(skeleton, parent_space, skinning);

  // Bone matrices carry translations in the tens of units (a 68-unit figure), so
  // the tolerance is on the scale of the values being cancelled, not on 1.0.
  constexpr float TOLERANCE = 1e-3f;
  const linalg::mat4f identity = linalg::mat4f::identity();

  for (size_t bone = 0; bone < skeleton.bones.size(); ++bone)
  {
    for (int column = 0; column < 4; ++column)
    {
      const float *actual   = &skinning[bone][column].x;
      const float *expected = &identity[column].x;
      for (int row = 0; row < 4; ++row)
        CHECK(std::fabs(actual[row] - expected[row]) < TOLERANCE,
              "bone %zu ('%s') skinning[%d][%d] was %f, expected %f", bone,
              skeleton.bones[bone].name.c_str(), column, row, actual[row], expected[row]);
    }
  }

  // inverse_affine on its own, away from the telescoping: M * M^-1 == I for a
  // real bone matrix, which the cancellation above would hide if it were wrong
  // in a way that cancelled too.
  const linalg::mat4f &bone_matrix = skeleton.bones.back().inverse_bind;
  const linalg::mat4f  round_trip  = bone_matrix * linalg::inverse_affine(bone_matrix);
  for (int column = 0; column < 4; ++column)
    for (int row = 0; row < 4; ++row)
      CHECK(std::fabs((&round_trip[column].x)[row] - (&identity[column].x)[row]) < TOLERANCE,
            "inverse_affine round trip [%d][%d] was %f", column, row,
            (&round_trip[column].x)[row]);
}

// --- Poses, blending and the aim space -------------------------------------

// Keyed by the pose, so a sixth aim pose resizes this and the missing row shows
// up as a null path rather than as a loop that quietly stops at five.
static const Enum_Array<entities::Aim_Pose, const char *> AIM_POSE_PATHS = {{
    "resources/models/forward_holding_gun.animation",
    "resources/models/upward_holding_gun.animation",
    "resources/models/downward_holding_gun.animation",
    "resources/models/left_holding_gun.animation",
    "resources/models/right_holding_gun.animation",
}};

static void test_real_aim_poses()
{
  printf("test_real_aim_poses\n");

  assets::aim_pose_clips_t poses;
  for (uint32_t index = 0; index < entities::Aim_Pose_COUNT; ++index)
  {
    const entities::Aim_Pose pose = (entities::Aim_Pose)index;
    const char              *path = AIM_POSE_PATHS[pose];

    assets::asset_handle_t<assets::animation_clip_t> handle = assets::load_animation(path);
    CHECK(handle.valid(), "load_animation('%s') failed", path);
    poses[pose] = assets::get(handle);
    CHECK(poses[pose] != nullptr, "handle for '%s' did not resolve", path);
    CHECK(poses[pose]->frame_count() == 1, "'%s' is an authored pose and must be ONE frame, got %u",
          path, poses[pose]->frame_count());
    CHECK(poses[pose]->bone_count == 35, "'%s' poses %u bones, the rig has 35", path,
          poses[pose]->bone_count);
    CHECK(poses[pose]->skeleton_name == "rig", "'%s' names skeleton '%s'", path,
          poses[pose]->skeleton_name.c_str());
    // Loading twice must be cached, same as a mesh.
    CHECK(assets::load_animation(path) == handle, "load_animation is not idempotent");
  }

  // The five must be five DIFFERENT poses. This is the check that would have
  // caught the export bug where the rig's pose never evaluated headless and all
  // five files came out identical but for their name line -- a whole set of
  // well-formed files full of plausible numbers. The exporter refuses this too;
  // both ends check it because the failure is silent everywhere else.
  for (uint32_t first_index = 0; first_index < entities::Aim_Pose_COUNT; ++first_index)
  {
    const entities::Aim_Pose first = (entities::Aim_Pose)first_index;
    for (uint32_t second_index = first_index + 1; second_index < entities::Aim_Pose_COUNT;
         ++second_index)
    {
      const entities::Aim_Pose second = (entities::Aim_Pose)second_index;

      bool differs = false;
      for (size_t bone = 0; bone < poses[first]->frames.size() && !differs; ++bone)
      {
        const assets::transform_t &a = poses[first]->frames[bone];
        const assets::transform_t &b = poses[second]->frames[bone];
        differs = std::fabs(a.translation.x - b.translation.x) > 1e-4f ||
                  std::fabs(a.translation.y - b.translation.y) > 1e-4f ||
                  std::fabs(a.translation.z - b.translation.z) > 1e-4f ||
                  std::fabs(a.rotation.x - b.rotation.x) > 1e-4f ||
                  std::fabs(a.rotation.y - b.rotation.y) > 1e-4f ||
                  std::fabs(a.rotation.z - b.rotation.z) > 1e-4f ||
                  std::fabs(a.rotation.w - b.rotation.w) > 1e-4f;
      }
      CHECK(differs, "aim poses '%s' and '%s' are identical", entities::to_string(first),
            entities::to_string(second));
    }
  }
}

// The bounds of the mesh actually skinned by a pose. This is the check
// animation_def.md §7 says NOTHING before an authored pose could make: every
// matrix in the bind-pose test is derived from `inverse_bind`, so a uniformly
// transposed skeleton telescopes to identity just as cleanly. A pose arrives as
// TRS -- translation is translation, with no row/column ambiguity to cancel
// against -- so `model_space * inverse_bind` only lands the vertices back on a
// person if the reader's transpose is right.
static void skinned_bounds(const assets::skeleton_t &skeleton, const assets::mesh_asset_t &mesh,
                           const assets::pose_t &pose, vec3f &out_minimum, vec3f &out_maximum)
{
  std::vector<linalg::mat4f> local(skeleton.bones.size());
  std::vector<linalg::mat4f> skinning(skeleton.bones.size());
  assets::get_local_transforms_of_bones_from_pose(pose, local);
  assets::compute_skinning_matrices(skeleton, local, skinning);

  out_minimum = {1e30f, 1e30f, 1e30f};
  out_maximum = {-1e30f, -1e30f, -1e30f};

  for (size_t index = 0; index < mesh.vertices.size(); ++index)
  {
    const vec3f                 &position   = mesh.vertices[index].position;
    const assets::vertex_skin_t &influences = mesh.skin[index];

    vec4 skinned = {0, 0, 0, 0};
    for (uint32_t slot = 0; slot < assets::MAX_BONE_INFLUENCES_PER_VERTEX; ++slot)
    {
      const float weight = influences.bone_weights[slot];
      if (weight <= 0.0f)
        continue;
      const linalg::mat4f &matrix = skinning[influences.bone_indices[slot]];
      skinned = skinned + (matrix * vec4{position.x, position.y, position.z, 1.0f}) * weight;
    }

    out_minimum.x = std::fmin(out_minimum.x, skinned.x);
    out_minimum.y = std::fmin(out_minimum.y, skinned.y);
    out_minimum.z = std::fmin(out_minimum.z, skinned.z);
    out_maximum.x = std::fmax(out_maximum.x, skinned.x);
    out_maximum.y = std::fmax(out_maximum.y, skinned.y);
    out_maximum.z = std::fmax(out_maximum.z, skinned.z);
  }
}

static void test_posed_skinning_stays_a_person()
{
  printf("test_posed_skinning_stays_a_person\n");

  assets::asset_handle_t<assets::mesh_asset_t> mesh_handle = assets::load_mesh(MESH_PATH);
  const assets::mesh_asset_t                  *mesh        = assets::get(mesh_handle);
  CHECK(mesh != nullptr && mesh->is_skinned(), "the skinned mesh must load");
  const assets::skeleton_t *skeleton = assets::get(mesh->skeleton);
  CHECK(skeleton != nullptr, "the mesh's skeleton must resolve");

  for (uint32_t index = 0; index < entities::Aim_Pose_COUNT; ++index)
  {
    const char *path = AIM_POSE_PATHS[(entities::Aim_Pose)index];

    assets::asset_handle_t<assets::animation_clip_t> handle = assets::load_animation(path);
    const assets::animation_clip_t                  *clip   = assets::get(handle);
    CHECK(clip != nullptr, "'%s' must load", path);

    assets::pose_t pose;
    assets::sample_animation_clip_at(pose, *clip, 0.0f, /*looping*/ false);
    CHECK(pose.local.size() == skeleton->bones.size(), "sampled %zu bones against a %zu-bone rig",
          pose.local.size(), skeleton->bones.size());

    vec3f minimum, maximum;
    skinned_bounds(*skeleton, *mesh, pose, minimum, maximum);

    const float vertical = maximum.y - minimum.y;
    const char *name     = entities::to_string((entities::Aim_Pose)index);

    // Generous, because these poses genuinely move: an arm raised over the head
    // is legitimately taller than the standing bind pose. What is being caught
    // is the failure mode, which is not subtle -- a wrong transpose scatters
    // vertices to thousands of units.
    CHECK(vertical > 40.0f && vertical < 120.0f,
          "pose '%s' skins to a figure %f units tall; a person is ~67. A wrong row-major -> "
          "column-major transpose of inverse_bind lands here and nowhere earlier",
          name, vertical);
    CHECK(std::fabs(minimum.x) < 200.0f && std::fabs(maximum.x) < 200.0f &&
              std::fabs(minimum.z) < 200.0f && std::fabs(maximum.z) < 200.0f,
          "pose '%s' skins to x [%f, %f] z [%f, %f]; the vertices left the model", name, minimum.x,
          maximum.x, minimum.z, maximum.z);
    CHECK(minimum.y > -20.0f && minimum.y < 20.0f, "pose '%s' has its lowest vertex at y = %f", name,
          minimum.y);
  }
}

static void test_blend_and_aim_space()
{
  printf("test_blend_and_aim_space\n");

  // --- the weight formula, which is pure arithmetic and worth pinning ---
  auto sums_to_one = [](const assets::aim_poses_blend_weights_t &blend)
  {
    float total = 0.0f;
    for (float weight : blend.weights)
      total += weight;
    return std::fabs(total - 1.0f) < 1e-4f;
  };

  assets::aim_poses_blend_weights_t centered = assets::compute_aim_blend(0.0f, 0.0f, 45.0f, 45.0f);
  CHECK(centered.weights[entities::Aim_Pose::Forward] > 0.999f,
        "looking straight ahead must be the Forward pose alone, got %f",
        centered.weights[entities::Aim_Pose::Forward]);
  CHECK(sums_to_one(centered), "weights must sum to 1");

  assets::aim_poses_blend_weights_t up = assets::compute_aim_blend(45.0f, 0.0f, 45.0f, 45.0f);
  CHECK(up.weights[entities::Aim_Pose::Upward] > 0.999f, "full pitch up must be Upward");
  CHECK(up.weights[entities::Aim_Pose::Downward] == 0.0f, "Downward must not be mixed in");

  assets::aim_poses_blend_weights_t half_down = assets::compute_aim_blend(-22.5f, 0.0f, 45.0f, 45.0f);
  CHECK(std::fabs(half_down.weights[entities::Aim_Pose::Downward] - 0.5f) < 1e-4f,
        "half pitch down must be half Downward, got %f",
        half_down.weights[entities::Aim_Pose::Downward]);
  CHECK(sums_to_one(half_down), "weights must sum to 1");

  // Past the plus's edge on both axes, all three share out rather than one
  // dropping. This is the case bilinear-over-four-corners cannot express and
  // sequential blending gets wrong.
  assets::aim_poses_blend_weights_t diagonal = assets::compute_aim_blend(45.0f, -45.0f, 45.0f, 45.0f);
  CHECK(sums_to_one(diagonal), "weights must sum to 1 past the edge");
  CHECK(diagonal.weights[entities::Aim_Pose::Upward] > 0.4f &&
            diagonal.weights[entities::Aim_Pose::Left] > 0.4f,
        "a full diagonal must keep BOTH extremes, got up %f left %f",
        diagonal.weights[entities::Aim_Pose::Upward],
        diagonal.weights[entities::Aim_Pose::Left]);

  // Clamped, not extrapolated: aiming further than the pose set covers holds at
  // the extreme rather than overshooting past it.
  assets::aim_poses_blend_weights_t beyond = assets::compute_aim_blend(200.0f, 0.0f, 45.0f, 45.0f);
  CHECK(beyond.weights[entities::Aim_Pose::Upward] > 0.999f,
        "aiming past the pose set must clamp to Upward");

  // --- blend_into against real poses ---
  const assets::animation_clip_t *forward =
      assets::get(assets::load_animation(AIM_POSE_PATHS[entities::Aim_Pose::Forward]));
  const assets::animation_clip_t *upward =
      assets::get(assets::load_animation(AIM_POSE_PATHS[entities::Aim_Pose::Upward]));
  CHECK(forward && upward, "the two poses must load");

  assets::pose_t base, other, blended;
  assets::sample_animation_clip_at(base, *forward, 0.0f, false);
  assets::sample_animation_clip_at(other, *upward, 0.0f, false);

  blended = base;
  assets::blend_into(blended, other, {}, 0.0f);
  for (size_t bone = 0; bone < blended.local.size(); ++bone)
    CHECK(std::fabs(blended.local[bone].translation.y - base.local[bone].translation.y) < 1e-5f,
          "weight 0 must leave the destination untouched at bone %zu", bone);

  blended = base;
  assets::blend_into(blended, other, {}, 1.0f);
  for (size_t bone = 0; bone < blended.local.size(); ++bone)
    CHECK(std::fabs(blended.local[bone].translation.y - other.local[bone].translation.y) < 1e-5f,
          "weight 1 must replace the destination at bone %zu", bone);

  // A mask is per-bone: bone 0 fully replaced, the rest untouched.
  assets::bone_mask_t mask(base.local.size(), 0.0f);
  mask[0] = 1.0f;
  blended = base;
  assets::blend_into(blended, other, mask, 1.0f);
  CHECK(std::fabs(blended.local[0].translation.y - other.local[0].translation.y) < 1e-5f,
        "the masked-in bone must take the source");
  for (size_t bone = 1; bone < blended.local.size(); ++bone)
    CHECK(std::fabs(blended.local[bone].translation.y - base.local[bone].translation.y) < 1e-5f,
          "the masked-out bone %zu must keep the destination", bone);

  // Rotations stay unit through a blend -- nlerp renormalizes, and a
  // non-normalized quaternion would reach the shader as a matrix that scales.
  blended = base;
  assets::blend_into(blended, other, {}, 0.37f);
  for (size_t bone = 0; bone < blended.local.size(); ++bone)
  {
    const linalg::quatf &rotation = blended.local[bone].rotation;
    const float          length   = std::sqrt(linalg::dot(rotation, rotation));
    CHECK(std::fabs(length - 1.0f) < 1e-4f, "bone %zu blended to a rotation of length %f", bone,
          length);
  }

  // --- sample_aim_pose end to end ---
  assets::aim_pose_clips_t poses;
  for (uint32_t index = 0; index < entities::Aim_Pose_COUNT; ++index)
  {
    const entities::Aim_Pose pose = (entities::Aim_Pose)index;
    poses[pose] = assets::get(assets::load_animation(AIM_POSE_PATHS[pose]));
  }

  assets::pose_t aimed;
  assets::sample_aim_pose(aimed, poses, centered);
  for (size_t bone = 0; bone < aimed.local.size(); ++bone)
    CHECK(std::fabs(aimed.local[bone].translation.y - base.local[bone].translation.y) < 1e-4f,
          "at zero angles the aim pose IS the Forward pose; bone %zu differs", bone);

  assets::sample_aim_pose(aimed, poses, up);
  for (size_t bone = 0; bone < aimed.local.size(); ++bone)
    CHECK(std::fabs(aimed.local[bone].translation.y - other.local[bone].translation.y) < 1e-4f,
          "at full pitch up the aim pose IS the Upward pose; bone %zu differs", bone);

  // A missing extreme gives its weight back to Forward rather than sampling
  // nothing -- stiff, not collapsed.
  assets::aim_pose_clips_t incomplete;
  incomplete[entities::Aim_Pose::Forward] = poses[entities::Aim_Pose::Forward];
  assets::sample_aim_pose(aimed, incomplete, up);
  for (size_t bone = 0; bone < aimed.local.size(); ++bone)
    CHECK(std::fabs(aimed.local[bone].translation.y - base.local[bone].translation.y) < 1e-4f,
          "a missing Upward must fall back to Forward; bone %zu differs", bone);
}

static void test_rejects_bad_animations()
{
  printf("test_rejects_bad_animations\n");
  printf("  (the errors below are expected)\n");

  assets::animation_clip_t clip;

  const char *header = "animation bad\nskeleton rig 0000000000000000\nbones 1\nfps 30\n";

  std::string unnormalized = fixture_path("bad_rotation.animation");
  write_fixture(unnormalized,
                std::string(header) + "frames 1\n" + "f 0\n" + "b 0 0 0 0 0 0 0 0.5 1 1 1\n");
  CHECK(!models::parse_animation_file(unnormalized.c_str(), clip),
        "a rotation that is not a unit quaternion must be refused");

  std::string truncated = fixture_path("bad_truncated.animation");
  write_fixture(truncated, std::string(header) + "frames 3\n" + "f 0\n" + "b 0 0 0 0 0 0 0 1 1 1 1\n");
  CHECK(!models::parse_animation_file(truncated.c_str(), clip),
        "a file with fewer frames than it declares must be refused");

  std::string out_of_order = fixture_path("bad_channel_order.animation");
  write_fixture(out_of_order, "animation bad\nskeleton rig 0000000000000000\nbones 2\nfps 30\n"
                              "frames 1\nf 0\nb 1 0 0 0 0 0 0 1 1 1 1\nb 0 0 0 0 0 0 0 1 1 1 1\n");
  CHECK(!models::parse_animation_file(out_of_order.c_str(), clip),
        "channels out of bone order must be refused: the clip's bone index IS the skeleton's");

  std::string zero_frames = fixture_path("bad_zero_frames.animation");
  write_fixture(zero_frames, std::string(header) + "frames 0\n");
  CHECK(!models::parse_animation_file(zero_frames.c_str(), clip),
        "a clip with no frames must be refused, not sampled");

  // The asset layer's checks, not the parser's: a clip is refused against a
  // skeleton it was not authored for, exactly like a mesh.
  std::string mismatched = "resources/models/hash_mismatch_fixture.animation";
  write_fixture(mismatched, "animation bad\nskeleton rig 1111111111111111\nbones 35\nfps 30\n"
                            "frames 1\nf 0\n" +
                                [] {
                                  std::string body;
                                  for (int bone = 0; bone < 35; ++bone)
                                    body += "b " + std::to_string(bone) + " 0 0 0 0 0 0 1 1 1 1\n";
                                  return body;
                                }());
  CHECK(!assets::load_animation(mismatched.c_str()).valid(),
        "a clip authored against a different skeleton revision must be refused");
  std::filesystem::remove(mismatched);

  std::string wrong_bone_count = "resources/models/bone_count_fixture.animation";
  write_fixture(wrong_bone_count, "animation bad\nskeleton rig b1e51a1238f88001\nbones 2\nfps 30\n"
                                  "frames 1\nf 0\nb 0 0 0 0 0 0 0 1 1 1 1\nb 1 0 0 0 0 0 0 1 1 1 1\n");
  CHECK(!assets::load_animation(wrong_bone_count.c_str()).valid(),
        "a clip posing a different number of bones than the skeleton has must be refused");
  std::filesystem::remove(wrong_bone_count);
}

int main()
{
  // Unbuffered: the failure path is abort(), which discards a buffered stdout
  // and leaves a failing test with nothing to say for itself.
  setvbuf(stdout, nullptr, _IONBF, 0);
  printf("=== Model Format Tests ===\n");

  if (!std::filesystem::exists(SKELETON_PATH))
  {
    printf("FAIL: '%s' not found. Run from the project root.\n", SKELETON_PATH);
    return 1;
  }

  assets::set_state(&g_asset_state);

  test_real_skeleton();
  test_real_mesh();
  test_asset_layer_resolves_skeleton();
  test_rejects_bad_skeletons();
  test_rejects_bad_meshes();
  test_static_mesh();
  test_bind_pose_skinning_is_identity();
  test_real_aim_poses();
  test_posed_skinning_stays_a_person();
  test_blend_and_aim_space();
  test_rejects_bad_animations();

  printf("All tests passed (%d checks).\n", g_checks);
  return 0;
}
