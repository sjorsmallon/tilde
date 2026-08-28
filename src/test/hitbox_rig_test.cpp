// Guards the `.hitboxes` reader/writer and the bone-span math behind it
// (hitbox_rig.cpp) against the REAL authored rig in resources/models.
//
// The point of the real-file half is the same as model_format_test's: the
// authored file names bones by hand, and this is where those names meet the
// skeleton the exporter actually wrote. It also holds the two numbers §4 turned
// from structural guarantees into checked ones -- hull excursion under the aim
// poses, and skin coverage -- so drift in the model shows up here rather than as
// shots that do not register.
//
// `hitbox_rig_test --dump` additionally prints every bone's bind-pose head and
// axes and the full guesstimated-size table, which is how the authored sizes were
// seeded.
//
// Run from the project root (ctest pins WORKING_DIRECTORY for exactly this).

#include "animation.hpp"
#include "asset.hpp"
#include "hitbox_rig.hpp"
#include "hitscan.hpp"
#include "model_format.hpp"
#include "player_rig.hpp"
#include "player_constants.hpp"
#include "skinning.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

static int32_t g_checks = 0;

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
static const char *RIG_PATH      = "resources/models/rig.hitboxes";

// Keyed by the pose rather than a bare list, so a sixth aim pose resizes this
// and the loop below covers it instead of silently auditing five of six.
static const Enum_Array<entities::Aim_Pose, const char *> AIM_POSE_PATHS = {{
    "resources/models/forward_holding_gun.animation",
    "resources/models/upward_holding_gun.animation",
    "resources/models/downward_holding_gun.animation",
    "resources/models/left_holding_gun.animation",
    "resources/models/right_holding_gun.animation",
}};

// What a volume being outside the hull by THIS much means: the math
// broke, not that a pose leans. A limb 40 units off a 32-wide player is a wrong
// matrix, and that is the only thing this file can usefully assert about
// excursion -- the poses are still being authored, and §4's 6-unit budget is a
// number for the artist to aim at, printed below rather than enforced here.
static const float EXCURSION_SANITY_CEILING = 40.0f;

static bool g_dump = false;

// --- Loading ---------------------------------------------------------------

struct model_t
{
  assets::skeleton_t   skeleton;
  assets::mesh_asset_t mesh;
  assets::hitbox_rig_t rig;
};

static model_t load_model()
{
  model_t model;

  CHECK(models::parse_skeleton(assets::read_asset_bytes(SKELETON_PATH), SKELETON_PATH,
                               model.skeleton),
        "'%s' must parse", SKELETON_PATH);

  models::skeleton_reference_t reference;
  CHECK(models::parse_mesh(assets::read_asset_bytes(MESH_PATH), MESH_PATH, model.mesh, reference),
        "'%s' must parse", MESH_PATH);
  CHECK(model.mesh.is_skinned(), "the preview mesh must carry skin data");

  std::optional<assets::hitbox_rig_file_t> file =
      models::try_parse_hitbox_rig(assets::read_asset_bytes(RIG_PATH), RIG_PATH);
  CHECK(file.has_value(), "'%s' must parse", RIG_PATH);

  CHECK(file->skeleton_name == model.skeleton.name, "rig names skeleton '%s', not '%s'",
        file->skeleton_name.c_str(), model.skeleton.name.c_str());

  std::optional<assets::hitbox_rig_t> rig = assets::try_resolve_hitbox_rig(*file, model.skeleton);
  CHECK(rig.has_value(), "every bone the rig names must exist in '%s'", SKELETON_PATH);
  model.rig = std::move(*rig);

  return model;
}

// --- The authored rig ------------------------------------------------------

static void test_authored_rig(const model_t &model)
{
  printf("test_authored_rig\n");

  // Ten volumes over three regions is the §4 table. Not pinned to exactly ten
  // (adding a volume is authoring, not a regression), but every region must be
  // represented -- a rig with no Head volume silently deletes headshots.
  CHECK(model.rig.volumes.size() >= 3, "only %zu volumes", model.rig.volumes.size());

  bool region_present[3] = {false, false, false};
  for (const assets::rigged_hitbox_volume_t &rigged : model.rig.volumes)
  {
    const assets::hitbox_volume_t &volume = rigged.volume;

    region_present[(uint32_t)volume.region] = true;

    if (assets::hitbox_shape_uses_radius(volume.shape))
      CHECK(volume.radius > 0.0f, "volume '%s' has radius %f", volume.name.c_str(), volume.radius);
    else
      CHECK(volume.half_extents.x > 0.0f && volume.half_extents.y > 0.0f &&
                volume.half_extents.z > 0.0f,
            "box volume '%s' has a zero side", volume.name.c_str());

    // A sphere is one bone by construction; the reader fills both fields from
    // the one name, and everything downstream reads a span without asking.
    if (volume.shape == assets::hitbox_shape_t::Sphere)
      CHECK(volume.start_bone == volume.end_bone, "sphere '%s' spans two bones",
            volume.name.c_str());
  }
  for (uint32_t region = 0; region < 3; ++region)
    CHECK(region_present[region], "no volume covers region %s",
          shared::to_string((shared::hit_region_t)region));

  // Volume names are how the tool's table and every log line refer to a volume,
  // so two volumes with one name is two rows you cannot tell apart.
  for (size_t outer = 0; outer < model.rig.volumes.size(); ++outer)
    for (size_t inner = outer + 1; inner < model.rig.volumes.size(); ++inner)
      CHECK(model.rig.volumes[outer].volume.name != model.rig.volumes[inner].volume.name,
            "duplicate volume name '%s'", model.rig.volumes[outer].volume.name.c_str());
}

// The drift check that derivation exists for: the size that ships is authored,
// but a mesh that has moved out from under it should be loud. The band is wide
// on purpose -- the head and hands are documented overrides (todo.md §2e), and a
// test that fails on a deliberate correction is a test people stop reading.
static void test_sizes_against_guesstimated(const model_t &model)
{
  printf("test_sizes_against_guesstimated\n");

  std::vector<assets::guesstimated_hitbox_from_bone_t> guesstimated_sizes(model.rig.volumes.size());
  assets::guesstimate_hitbox_sizes(model.mesh, model.skeleton, model.rig, guesstimated_sizes);

  for (size_t index = 0; index < model.rig.volumes.size(); ++index)
  {
    const assets::hitbox_volume_t &volume = model.rig.volumes[index].volume;
    const assets::guesstimated_hitbox_from_bone_t &guesstimated = guesstimated_sizes[index];

    CHECK(guesstimated.radius > 0.0f, "volume '%s' covers no vertex -- its span_bones own no skin",
          volume.name.c_str());

    if (assets::hitbox_shape_uses_radius(volume.shape))
    {
      CHECK(volume.radius > guesstimated.radius * 0.4f && volume.radius < guesstimated.radius * 2.5f,
            "volume '%s' is authored at r%.2f but guesses r%.2f; the model has moved under it",
            volume.name.c_str(), volume.radius, guesstimated.radius);
    }
    else
    {
      const float authored[3] = {volume.half_extents.x, volume.half_extents.y,
                                 volume.half_extents.z};
      const float guess[3]    = {guesstimated.half_extents.x, guesstimated.half_extents.y,
                                 guesstimated.half_extents.z};
      for (uint32_t axis = 0; axis < 3; ++axis)
        CHECK(authored[axis] > guess[axis] * 0.4f && authored[axis] < guess[axis] * 2.5f,
              "volume '%s' half-extent %u is authored at %.2f but guesses %.2f", volume.name.c_str(),
              axis, authored[axis], guess[axis]);
    }
  }
}

// --- Posed volumes ---------------------------------------------------------

static void compute_hitboxes(const model_t &model, const assets::pose_t &pose,
                             std::vector<assets::posed_hitbox_t> &out)
{
  assets::posed_skeleton_t posed;
  assets::compute_posed_skeleton(model.skeleton, pose, posed);

  out.resize(model.rig.volumes.size());
  assets::compute_posed_hitboxes(model.rig, posed.model_space, out);
}

// §4: the absolute "hitboxes never leave the movement hull" invariant becomes a
// bounded, checked one. This REPORTS that bound over the poses that ship today
// and asserts only that the numbers are sane -- the whole-stride version arrives
// with the walk cycle, and the poses themselves are still being authored, so a
// per-pose ceiling here would be a test about content that is about to change.
//
// Deliberately NOT run on the bind pose: a T-pose puts both arms straight out
// through the hull, and it is not a pose any player is ever in.
static void test_hull_excursion(const model_t &model)
{
  printf("test_hull_excursion  (§4 aims at %.1f units; over that is an authoring note)\n",
         assets::HITBOX_MAX_HULL_EXCURSION);

  for (const char *pose_path : AIM_POSE_PATHS)
  {
    assets::animation_asset_t clip;
    CHECK(models::parse_animation(assets::read_asset_bytes(pose_path), pose_path, clip),
          "'%s' must parse", pose_path);
    CHECK(clip.skeleton_hash == model.skeleton.hash, "'%s' was authored against another skeleton",
          pose_path);

    assets::pose_t pose;
    assets::sample_animation_clip_at(pose, clip, 0.0f, false);

    std::vector<assets::posed_hitbox_t> hitboxes;
    compute_hitboxes(model, pose, hitboxes);

    const assets::hull_excursion_t excursion = assets::compute_hull_excursion(
        hitboxes, shared::player_half_width, shared::player_half_height * 2.0f);

    const char *worst = excursion.volume_index < 0
                            ? "(none)"
                            : model.rig.volumes[(size_t)excursion.volume_index].volume.name.c_str();
    printf("  %-46s excursion %5.2f  (%s", pose_path, excursion.distance, worst);
    if (excursion.volume_index >= 0)
    {
      const assets::posed_hitbox_t &hitbox = hitboxes[(size_t)excursion.volume_index];
      printf(": %s (%.1f, %.1f, %.1f) -> (%.1f, %.1f, %.1f)", assets::to_string(hitbox.shape),
             hitbox.start.x, hitbox.start.y, hitbox.start.z, hitbox.end.x, hitbox.end.y,
             hitbox.end.z);
    }
    printf(")%s\n", excursion.distance > assets::HITBOX_MAX_HULL_EXCURSION ? "  OVER BUDGET" : "");

    CHECK(excursion.distance < EXCURSION_SANITY_CEILING,
          "'%s' puts volume '%s' %.2f units outside a 32-wide player -- that is a broken matrix, "
          "not a lean",
          pose_path, worst, excursion.distance);
  }
}

// The whole path a shot takes, against the REAL rig: player_rig() loads it,
// compute_player_hitboxes poses and places it, resolve_hitscan tests it. Every
// piece has its own unit test above or in hitscan_test; this is the one place
// they are wired together, so "the volumes exist and pose correctly, but a
// bullet still goes through them" cannot pass.
static void test_hitscan_against_the_real_rig()
{
  printf("test_hitscan_against_the_real_rig\n");

  const shared::player_rig_t &rig = shared::player_rig();
  const aim_settings_t        settings;

  // A target 200 units down +X, facing back at the shooter.
  constexpr float TARGET_X = 200.0f;
  const shared::player_pose_t facing_the_shooter{.feet_position = {TARGET_X, 0.0f, 0.0f},
                                                 .body_yaw      = 180.0f,
                                                 .view_yaw      = 180.0f,
                                                 .view_pitch    = 0.0f};

  std::vector<assets::posed_hitbox_t> volumes(rig.volume_count());
  shared::compute_player_hitboxes(rig, facing_the_shooter, settings, volumes);

  const std::vector<shared::hitscan_target_t> targets{{1, volumes}};

  // Fire horizontally through the centre of one volume per region. Reading the
  // heights off the posed volumes rather than hard-coding them keeps this a
  // test of the path and not of the authored numbers.
  for (uint32_t region_index = 0; region_index < 3; ++region_index)
  {
    const shared::hit_region_t region = (shared::hit_region_t)region_index;

    const assets::posed_hitbox_t *volume = nullptr;
    for (const assets::posed_hitbox_t &candidate : volumes)
      if (candidate.region == region)
      {
        volume = &candidate;
        break;
      }
    CHECK(volume != nullptr, "the rig has no %s volume", shared::to_string(region));

    const linalg::vec3f centre = volume->center();
    const shared::hitscan_result_t hit =
        shared::resolve_hitscan({0.0f, centre.y, centre.z}, {1.0f, 0.0f, 0.0f}, 1000.0f, targets);

    CHECK(hit.hit_uid == 1, "a ray through the centre of a %s volume (%.1f, %.1f, %.1f) missed the "
                            "player entirely",
          shared::to_string(region), centre.x, centre.y, centre.z);
    CHECK(hit.distance > 0.0f && hit.distance < TARGET_X,
          "hit distance %.1f is not between the shooter and the target", hit.distance);
  }

  // Above the crown is a clean miss -- the volumes are a player, not a column
  // to the sky.
  {
    const shared::hitscan_result_t over =
        shared::resolve_hitscan({0.0f, shared::player_half_height * 2.0f + 12.0f, 0.0f},
                                {1.0f, 0.0f, 0.0f}, 1000.0f, targets);
    CHECK(over.hit_uid == 0, "a ray a foot over the player's head hit something");
  }

  // The claim the whole change rests on: the volumes MOVE with the pose. Turn
  // the body 90 degrees and the arms are somewhere else -- if this passes with
  // a zero maximum, hitscan is testing a pose-independent shape again.
  {
    shared::player_pose_t turned = facing_the_shooter;
    turned.body_yaw              = 90.0f;
    turned.view_yaw              = 90.0f;

    std::vector<assets::posed_hitbox_t> turned_volumes(rig.volume_count());
    shared::compute_player_hitboxes(rig, turned, settings, turned_volumes);

    float largest_shift = 0.0f;
    for (uint32_t index = 0; index < rig.volume_count(); ++index)
      largest_shift =
          std::max(largest_shift, linalg::length(turned_volumes[index].start - volumes[index].start));

    printf("  turning the body 90 degrees moves the furthest volume %.1f units\n", largest_shift);
    CHECK(largest_shift > 1.0f, "turning the body moved no volume: the hit volumes are not "
                                "following the pose");
  }

  // And the same for the aim blend, which is the input that has no analogue in
  // the old static table: looking up must move the head volume.
  {
    shared::player_pose_t looking_up = facing_the_shooter;
    looking_up.view_pitch            = settings.max_pitch_degrees;

    std::vector<assets::posed_hitbox_t> raised(rig.volume_count());
    shared::compute_player_hitboxes(rig, looking_up, settings, raised);

    float largest_shift = 0.0f;
    for (uint32_t index = 0; index < rig.volume_count(); ++index)
      largest_shift =
          std::max(largest_shift, linalg::length(raised[index].start - volumes[index].start));

    printf("  aiming %.0f degrees up moves the furthest volume %.1f units\n",
           settings.max_pitch_degrees, largest_shift);
    CHECK(largest_shift > 1.0f, "the aim pitch pose moved no volume");
  }
}

// Skin the volumes do not reach. Measured in the BIND pose, because skinning
// moves the skin and the volumes together -- a gap here is a property of the
// rig rather than of one frame.
static void test_coverage(const model_t &model)
{
  printf("test_coverage\n");

  assets::pose_t bind_pose;
  assets::compute_bind_pose(model.skeleton, bind_pose);

  std::vector<assets::posed_hitbox_t> hitboxes;
  compute_hitboxes(model, bind_pose, hitboxes);

  const assets::hitbox_coverage_t coverage = assets::compute_hitbox_coverage(
      model.mesh, model.skeleton, hitboxes, assets::HITBOX_COVERAGE_TOLERANCE);

  const char *worst_bone = coverage.worst_bone < 0
                               ? "(none)"
                               : model.skeleton.bones[(size_t)coverage.worst_bone].name.c_str();
  printf("  %u/%u vertices uncovered, worst %.2f units out (dominated by '%s')\n",
         coverage.uncovered_vertex_count, coverage.vertex_count, coverage.worst_distance,
         worst_bone);

  for (size_t bone = 0; bone < coverage.uncovered_by_bone.size(); ++bone)
    if (coverage.uncovered_by_bone[bone] > 0)
      printf("    %-18s %u\n", model.skeleton.bones[bone].name.c_str(),
             coverage.uncovered_by_bone[bone]);

  // Loose, and a fraction rather than a count. §4's volume list has no hand
  // volume, so both hands -- 296 of 1216 vertices on this model, because hands
  // are where the polygons are -- are outside everything by design. Pinning
  // that number would make every model edit red and would be a test about
  // authoring rather than about the code. What this catches is the whole set
  // collapsing: a wrong matrix, a rig that resolved to nothing, volumes at the
  // origin.
  const float uncovered_fraction =
      (float)coverage.uncovered_vertex_count / (float)coverage.vertex_count;
  CHECK(uncovered_fraction < 0.5f, "%.1f%% of the skin is outside every volume",
        uncovered_fraction * 100.0f);
}

// --- The format ------------------------------------------------------------

static std::string fixture_path(const char *name)
{
  return (std::filesystem::temp_directory_path() / name).string();
}

// The round-trip tests write a REAL file, because try_write_hitbox_rig_file is
// what the tool's Save button calls and writing is still a filesystem act. The
// reader is not: parse takes bytes, so the test hands them over itself rather
// than through assets::read_asset_bytes, whose paths are project-root-relative
// and whose blobs are cached for the process -- neither of which suits a temp
// file written moments ago.
static std::string read_fixture(const std::string &path)
{
  std::ifstream file(path, std::ios::binary);
  CHECK((bool)file, "could not read fixture '%s'", path.c_str());
  return std::string(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
}

static Span<const uint8_t> fixture(const std::string &text)
{
  return Span<const uint8_t>(reinterpret_cast<const uint8_t *>(text.data()),
                             (uint32_t)text.size());
}

static void test_round_trip(const model_t &model)
{
  printf("test_round_trip\n");

  const std::string path = fixture_path("round_trip.hitboxes");
  CHECK(models::try_write_hitbox_rig_file(path.c_str(), model.rig), "the writer must succeed");

  const std::string                        written = read_fixture(path);
  std::optional<assets::hitbox_rig_file_t> reloaded =
      models::try_parse_hitbox_rig(fixture(written), path.c_str());
  CHECK(reloaded.has_value(), "what the writer wrote must parse");

  CHECK(reloaded->name == model.rig.name, "name changed");
  CHECK(reloaded->skeleton_name == model.rig.skeleton_name, "skeleton name changed");
  CHECK(reloaded->skeleton_hash == model.rig.skeleton_hash, "skeleton hash changed: %016llx",
        (unsigned long long)reloaded->skeleton_hash);
  CHECK(reloaded->volumes.size() == model.rig.volumes.size(), "volume count changed");

  for (size_t index = 0; index < model.rig.volumes.size(); ++index)
  {
    const assets::hitbox_volume_t &original = model.rig.volumes[index].volume;
    const assets::hitbox_volume_t &copy     = reloaded->volumes[index];
    CHECK(copy.name == original.name, "volume %zu renamed", index);
    CHECK(copy.start_bone == original.start_bone, "volume '%s' start bone changed",
          original.name.c_str());
    CHECK(copy.end_bone == original.end_bone, "volume '%s' end bone changed",
          original.name.c_str());
    CHECK(copy.region == original.region, "volume '%s' region changed", original.name.c_str());
    CHECK(copy.shape == original.shape, "volume '%s' shape changed", original.name.c_str());
    // Three decimals in the writer, so this is exact to a thousandth of a unit
    // and not "close enough" -- a size that drifts on every save would make the
    // file's history unreadable.
    if (assets::hitbox_shape_uses_radius(original.shape))
      CHECK(std::fabs(copy.radius - original.radius) < 0.001f, "volume '%s' radius %f -> %f",
            original.name.c_str(), original.radius, copy.radius);
    else
      CHECK(std::fabs(copy.half_extents.x - original.half_extents.x) < 0.001f &&
                std::fabs(copy.half_extents.y - original.half_extents.y) < 0.001f &&
                std::fabs(copy.half_extents.z - original.half_extents.z) < 0.001f,
            "volume '%s' half-extents changed", original.name.c_str());
    CHECK(std::fabs(copy.offset - original.offset) < 0.001f, "volume '%s' offset %f -> %f",
          original.name.c_str(), original.offset, copy.offset);
  }
}

// The authored rig is spheres and capsules, so the other two shapes would go
// through the writer untested. This is the format's own round trip: one volume
// per shape, written and read back.
static void test_every_shape_round_trips(const model_t &model)
{
  printf("test_every_shape_round_trips\n");

  assets::hitbox_rig_file_t authored;
  authored.name          = "shapes";
  authored.skeleton_name = model.skeleton.name;
  authored.skeleton_hash = model.skeleton.hash;
  authored.volumes       = {
      {.name = "ball", .shape = assets::hitbox_shape_t::Sphere, .start_bone = "spine.006",
             .end_bone = "spine.006", .region = shared::hit_region_t::Head, .radius = 6.5f,
             .offset = 3.25f},
      {.name = "limb", .shape = assets::hitbox_shape_t::Capsule, .start_bone = "thigh.L",
             .end_bone = "shin.L", .region = shared::hit_region_t::Legs, .radius = 5.5f},
      {.name = "tube", .shape = assets::hitbox_shape_t::Cylinder, .start_bone = "shin.L",
             .end_bone = "foot.L", .region = shared::hit_region_t::Legs, .radius = 4.25f},
      {.name = "crate", .shape = assets::hitbox_shape_t::Box, .start_bone = "spine",
             .end_bone = "spine.003", .region = shared::hit_region_t::Torso, .radius = 0.0f,
             .half_extents = {9.0f, 7.5f, 6.25f}},
  };

  // The writer takes the bound form, so the trip is authored -> resolve -> write
  // -> read, which is exactly what the tool's Save button does.
  std::optional<assets::hitbox_rig_t> rig = assets::try_resolve_hitbox_rig(authored, model.skeleton);
  CHECK(rig.has_value(), "the four-shape rig must resolve");

  const std::string path = fixture_path("shapes.hitboxes");
  CHECK(models::try_write_hitbox_rig_file(path.c_str(), *rig), "the writer must succeed");

  const std::string                        written = read_fixture(path);
  std::optional<assets::hitbox_rig_file_t> reloaded =
      models::try_parse_hitbox_rig(fixture(written), path.c_str());
  CHECK(reloaded.has_value(), "what the writer wrote must parse");
  CHECK(reloaded->volumes.size() == authored.volumes.size(), "volume count changed: %zu",
        reloaded->volumes.size());

  for (size_t index = 0; index < authored.volumes.size(); ++index)
  {
    const assets::hitbox_volume_t &original = authored.volumes[index];
    const assets::hitbox_volume_t &copy     = reloaded->volumes[index];
    CHECK(copy.shape == original.shape, "volume '%s' came back as %s", original.name.c_str(),
          assets::to_string(copy.shape));
    CHECK(copy.start_bone == original.start_bone && copy.end_bone == original.end_bone,
          "volume '%s' span changed", original.name.c_str());
    CHECK(std::fabs(copy.offset - original.offset) < 0.001f, "volume '%s' offset changed",
          original.name.c_str());
  }

  // And what came back off disk resolves and poses: a shape the format can spell
  // but the math cannot place would be a format that lies.
  std::optional<assets::hitbox_rig_t> reresolved =
      assets::try_resolve_hitbox_rig(*reloaded, model.skeleton);
  CHECK(reresolved.has_value(), "the four-shape rig must resolve after a round trip");

  std::vector<linalg::mat4f> bind_model(model.skeleton.bones.size());
  assets::compute_bind_model_matrices(model.skeleton, bind_model);

  std::vector<assets::posed_hitbox_t> posed(reresolved->volumes.size());
  assets::compute_posed_hitboxes(*reresolved, bind_model, posed);

  // A point at each volume's own centre is inside it, and one a long way off is
  // not -- the cheapest statement that distance_outside_hitbox is oriented the
  // right way round for every shape.
  for (size_t index = 0; index < posed.size(); ++index)
  {
    const assets::posed_hitbox_t &hitbox = posed[index];
    CHECK(assets::distance_outside_hitbox(hitbox, hitbox.center()) == 0.0f,
          "%s '%s' does not contain its own centre", assets::to_string(hitbox.shape),
          reresolved->volumes[index].volume.name.c_str());
    CHECK(assets::distance_outside_hitbox(hitbox, hitbox.center() + linalg::vec3f{1000, 0, 0}) >
              900.0f,
          "%s '%s' claims to reach 1000 units away", assets::to_string(hitbox.shape),
          reresolved->volumes[index].volume.name.c_str());
  }
}

static void test_malformed(const model_t &model)
{
  printf("test_malformed\n");

  auto refuses = [](const char *what, const std::string &contents)
  {
    const std::optional<assets::hitbox_rig_file_t> rig =
        models::try_parse_hitbox_rig(fixture(contents), "bad.hitboxes");
    g_checks += 1;
    if (rig.has_value())
    {
      printf("  FAIL: %s was accepted\n", what);
      abort();
    }
  };

  printf("  (the errors below are expected)\n");
  refuses("an unknown shape", "hitboxes rig\nskeleton rig 0\n"
                              "v torso Lozenge spine spine.003 Torso 10\n");
  refuses("an unknown damage region", "hitboxes rig\nskeleton rig 0\n"
                                      "v torso Capsule spine spine.003 Chest 10\n");
  refuses("a missing radius", "hitboxes rig\nskeleton rig 0\n"
                              "v torso Capsule spine spine.003 Torso\n");
  refuses("a zero radius", "hitboxes rig\nskeleton rig 0\n"
                           "v torso Capsule spine spine.003 Torso 0\n");
  // A box needs three half-extents; two is a line that would otherwise read the
  // offset column as a size.
  refuses("a box with two half-extents", "hitboxes rig\nskeleton rig 0\n"
                                         "v torso Box spine spine.003 Torso 8 6\n");
  refuses("a box with a zero side", "hitboxes rig\nskeleton rig 0\n"
                                    "v torso Box spine spine.003 Torso 8 6 0\n");
  // A sphere takes ONE bone, so the second name here lands in the region column.
  refuses("a sphere with two bones", "hitboxes rig\nskeleton rig 0\n"
                                     "v head Sphere spine.006 spine.006 Head 9\n");
  refuses("no volumes at all", "hitboxes rig\nskeleton rig 0\n");
  refuses("a reordered header", "skeleton rig 0\nhitboxes rig\n"
                                "v torso Capsule spine spine.003 Torso 10\n");

  // Resolution failures are separate from parse failures: the file is
  // well-formed and still cannot be applied to this skeleton.
  auto refuses_resolution = [&model](const char *what, const assets::hitbox_volume_t &volume)
  {
    assets::hitbox_rig_file_t rig;
    rig.name          = "probe";
    rig.skeleton_name = model.skeleton.name;
    rig.volumes.push_back(volume);

    g_checks += 1;
    if (assets::try_resolve_hitbox_rig(rig, model.skeleton).has_value())
    {
      printf("  FAIL: %s resolved\n", what);
      abort();
    }
  };

  refuses_resolution("a bone the skeleton lacks",
                     {.name = "probe", .shape = assets::hitbox_shape_t::Capsule,
                      .start_bone = "spine", .end_bone = "no_such_bone",
                      .region = shared::hit_region_t::Torso, .radius = 5.0f});
  // Two bones on different limbs: the volume would run through the middle of
  // the character, which is a wrong answer that looks plausible.
  refuses_resolution("a span that is not a bone chain",
                     {.name = "probe", .shape = assets::hitbox_shape_t::Capsule,
                      .start_bone = "thigh.L", .end_bone = "forearm.R",
                      .region = shared::hit_region_t::Torso, .radius = 5.0f});
  // Backwards: the parent must be the START, or the walk up from the end never
  // reaches it.
  refuses_resolution("a reversed span",
                     {.name = "probe", .shape = assets::hitbox_shape_t::Capsule,
                      .start_bone = "hand.L", .end_bone = "forearm.L",
                      .region = shared::hit_region_t::Torso, .radius = 5.0f});

  // A rig authored against another skeleton revision is refused whole rather
  // than resolved by name and hoped over -- same shape as the .mesh/.animation
  // hash checks.
  assets::hitbox_rig_file_t stale;
  stale.name          = "stale";
  stale.skeleton_name = model.skeleton.name;
  stale.skeleton_hash = model.skeleton.hash ^ 1ull;
  stale.volumes.push_back({.name       = "torso",
                           .shape      = assets::hitbox_shape_t::Capsule,
                           .start_bone = "spine",
                           .end_bone   = "spine.003",
                           .region     = shared::hit_region_t::Torso,
                           .radius     = 10.0f});
  g_checks += 1;
  if (assets::try_resolve_hitbox_rig(stale, model.skeleton).has_value())
  {
    printf("  FAIL: a stale skeleton hash resolved\n");
    abort();
  }
}

// --- The seeding dump ------------------------------------------------------

static void dump(const model_t &model)
{
  std::vector<linalg::mat4f> bind_model(model.skeleton.bones.size());
  assets::compute_bind_model_matrices(model.skeleton, bind_model);

  // The axis is the bone's own +Y in model space -- the direction a volume's
  // `offset` column slides along, which is the only way to author it.
  printf("\nbind-pose bone heads (model space, feet at y=0) and +Y axes\n");
  for (size_t index = 0; index < model.skeleton.bones.size(); ++index)
  {
    const linalg::vec4 &translation = bind_model[index][3];
    const linalg::vec4 &x_axis      = bind_model[index][0];
    const linalg::vec4 &y_axis      = bind_model[index][1];
    const linalg::vec4 &z_axis      = bind_model[index][2];
    printf("  %2zu %-18s parent %3d  head (%7.2f, %7.2f, %7.2f)  X (%5.2f,%5.2f,%5.2f)  "
           "Y (%5.2f,%5.2f,%5.2f)  Z (%5.2f,%5.2f,%5.2f)\n",
           index, model.skeleton.bones[index].name.c_str(),
           model.skeleton.bones[index].parent_index, translation.x, translation.y, translation.z,
           x_axis.x, x_axis.y, x_axis.z, y_axis.x, y_axis.y, y_axis.z, z_axis.x, z_axis.y,
           z_axis.z);
  }

  printf("\nevery bone as a one-bone volume (the authoring template)\n");
  const assets::hitbox_rig_t template_rig =
      assets::make_hitbox_rig_template(model.mesh, model.skeleton);
  for (const assets::rigged_hitbox_volume_t &rigged : template_rig.volumes)
    printf("  v %-18s %-18s %-6s %6.2f\n", rigged.volume.name.c_str(), rigged.volume.name.c_str(),
           shared::to_string(rigged.volume.region), rigged.volume.radius);
}

int main(int argument_count, char **arguments)
{
  // Unbuffered: every failure path here is abort(), which throws away whatever
  // is still sitting in the buffer -- including the line saying what failed.
  setvbuf(stdout, nullptr, _IONBF, 0);

  for (int index = 1; index < argument_count; ++index)
    if (strcmp(arguments[index], "--dump") == 0)
      g_dump = true;

  printf("=== hitbox_rig_test ===\n");

  // The byte layer hangs off the one launcher-owned state, so a test that reads
  // an asset owns that state and mounts it exactly as a launcher does -- init()
  // included, since player_rig() resolves manifest ids and registration is
  // eager.
  static assets::asset_state_t asset_state;
  assets::set_state(&asset_state);
  assets::mount_asset_source();
  assets::init();

  const model_t model = load_model();

  // Before the checks, not after: --dump exists to seed the numbers a failing
  // check is complaining about.
  if (g_dump)
    dump(model);

  test_authored_rig(model);
  test_sizes_against_guesstimated(model);
  test_hull_excursion(model);
  test_coverage(model);
  test_hitscan_against_the_real_rig();
  test_round_trip(model);
  test_every_shape_round_trips(model);
  test_malformed(model);

  printf("=== hitbox_rig_test: %d checks passed ===\n", g_checks);
  return 0;
}
