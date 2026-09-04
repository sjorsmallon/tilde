#include "asset.hpp"
#include "asset_package.hpp"
#include <cmath>
#include <cassert>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>

// --- Helpers ---

// Fixtures live UNDER THE PROJECT ROOT, in the build directory. They used to go
// in the platform temp dir, and that no longer works: the byte layer is mounted
// at the project root and a path in this system has one spelling, relative to
// it. An absolute path into %TEMP% would still open -- which is exactly why the
// fixtures move rather than the rule bending for the test that checks it.
static const char *FIXTURE_DIRECTORY = "cmake_build/asset_test_fixtures";

static std::string test_file_path(const char *name)
{
  return std::string(FIXTURE_DIRECTORY) + "/" + name;
}

static const std::string g_test_obj_path = test_file_path("test_asset_cube.obj");
static const std::string g_test_tga_path = test_file_path("test_asset_2x2.tga");

// A fixture that silently fails to write turns into a confusing load failure
// three lines later, so the write is checked at the point it happens.
static void require_written(const std::ofstream &stream, const std::string &path)
{
  if (!stream)
  {
    printf("  FAIL: could not write fixture '%s'\n", path.c_str());
    abort();
  }
}

static void write_test_obj()
{
  std::filesystem::create_directories(FIXTURE_DIRECTORY);
  std::ofstream f(g_test_obj_path);
  f << "# test cube (simplified: one face)\n";
  f << "v  0.0  0.0  0.0\n";
  f << "v  1.0  0.0  0.0\n";
  f << "v  1.0  1.0  0.0\n";
  f << "v  0.0  1.0  0.0\n";
  f << "vn 0.0  0.0 -1.0\n";
  f << "vt 0.0  0.0\n";
  f << "vt 1.0  0.0\n";
  f << "vt 1.0  1.0\n";
  f << "vt 0.0  1.0\n";
  f << "f 1/1/1 2/2/1 3/3/1 4/4/1\n";
  require_written(f, g_test_obj_path);
}

static void write_test_tga()
{
  // Minimal 2x2 uncompressed TGA, 24-bit RGB
  uint8_t header[18] = {};
  header[2] = 2;   // uncompressed true-color
  header[12] = 2;  // width low byte
  header[13] = 0;  // width high byte
  header[14] = 2;  // height low byte
  header[15] = 0;  // height high byte
  header[16] = 24; // bits per pixel

  // 4 pixels, BGR order
  uint8_t pixels[12] = {
      255, 0,   0,   // blue
      0,   255, 0,   // green
      0,   0,   255, // red
      255, 255, 255, // white
  };

  std::filesystem::create_directories(FIXTURE_DIRECTORY);
  std::ofstream f(g_test_tga_path, std::ios::binary);
  f.write(reinterpret_cast<char *>(header), sizeof(header));
  f.write(reinterpret_cast<char *>(pixels), sizeof(pixels));
  require_written(f, g_test_tga_path);
}

// --- Tests ---

static int test_load_mesh()
{
  write_test_obj();

  auto handle = assets::load_mesh(g_test_obj_path.c_str());
  assert(handle.valid());

  const auto *mesh = assets::get(handle);
  assert(mesh != nullptr);
  assert(!mesh->vertices.empty());
  assert(!mesh->indices.empty());

  // A quad face should produce 2 triangles = 6 indices
  assert(mesh->indices.size() == 6);
  // 4 unique vertices
  assert(mesh->vertices.size() == 4);

  printf("  PASS: test_load_mesh\n");
  return 0;
}

static int test_load_texture()
{
  write_test_tga();

  auto handle = assets::load_texture(g_test_tga_path.c_str());
  assert(handle.valid());

  const auto *tex = assets::get(handle);
  assert(tex != nullptr);
  assert(tex->width == 2);
  assert(tex->height == 2);
  // The fixture is 24-bit, but load_texture forces STBI_rgb_alpha so every
  // texture has a uniform 4-byte stride -- 4 channels is the invariant the
  // loader promises, not a property of the file.
  assert(tex->channels == 4);
  assert(tex->pixels.size() == 2 * 2 * 4);

  printf("  PASS: test_load_texture\n");
  return 0;
}

static int test_caching()
{
  write_test_obj();

  auto h1 = assets::load_mesh(g_test_obj_path.c_str());
  auto h2 = assets::load_mesh(g_test_obj_path.c_str());
  assert(h1 == h2);

  printf("  PASS: test_caching\n");
  return 0;
}

// A path that names nothing is a broken install, not a runtime condition, so
// load_mesh dies on it rather than handing back an invalid handle. asset_exists
// is what a caller with a HUMAN-supplied path asks first, and it is the only
// thing left here that can say "no".
static int test_asset_exists()
{
  write_test_obj();

  assert(assets::asset_exists(g_test_obj_path.c_str()));
  assert(!assets::asset_exists(test_file_path("this_does_not_exist.obj").c_str()));
  // A directory is not an asset, however much it exists.
  assert(!assets::asset_exists(FIXTURE_DIRECTORY));

  printf("  PASS: test_asset_exists\n");
  return 0;
}

// --- The byte layer ---
//
// The one place a file is opened. Everything above it -- every decoder, in the
// asset system and out of it -- takes bytes, so this is the seam worth pinning:
// what comes back is the file, and asking twice is one blob rather than two.

static int test_read_asset_bytes()
{
  write_test_obj();

  const Span<const uint8_t> bytes = assets::read_asset_bytes(g_test_obj_path.c_str());
  assert(bytes.size() == std::filesystem::file_size(g_test_obj_path));
  assert(std::memcmp(bytes.data, "# test cube", 11) == 0);

  // Same file, and the spellings the cache key normalises away. One blob, so
  // the pointer is the same one -- which is also what makes a span from here
  // safe to hold for the process lifetime.
  assert(assets::read_asset_bytes(g_test_obj_path.c_str()).data == bytes.data);
  assert(assets::read_asset_bytes(test_file_path("./test_asset_cube.obj").c_str()).data ==
         bytes.data);

  printf("  PASS: test_read_asset_bytes\n");
  return 0;
}

// --- The manifest ---
//
// Everything above tests the layer BELOW the manifest -- load_*, the path
// cache, the decoders. Nothing used to touch init(), the id-to-handle table or
// the Missing fallback, which is precisely the half asset_pipeline_def.md's
// later steps rewrite. These run from the project root (ctest pins the working
// directory), because the manifest's paths are relative to it.

static int test_manifest_registers_every_id()
{
  assets::init();

  // Eager registration, and total: every declared id resolves to a loaded mesh.
  // A hole would otherwise show up as one entity drawing the question mark,
  // which reads as a modelling bug rather than a manifest one.
  for (uint32_t index = 0; index < assets::mesh_asset_COUNT; ++index)
  {
    const assets::mesh_asset id     = (assets::mesh_asset)index;
    const auto               handle = assets::get_mesh(id);
    assert(handle.valid());

    const assets::mesh_asset_t *mesh = assets::get(handle);
    assert(mesh != nullptr);
    assert(!mesh->vertices.empty());
  }

  printf("  PASS: test_manifest_registers_every_id\n");
  return 0;
}

// Every class registers, not just meshes. The four added when the manifest
// replaced assets.def had no coverage at all, and three of them (sound, font,
// hitbox) are decoded by code that nothing else in the suite runs.
static int test_every_class_registers()
{
  for (uint32_t index = 0; index < assets::texture_asset_COUNT; ++index)
    assert(assets::get_texture((assets::texture_asset)index).valid());
  for (uint32_t index = 0; index < assets::sound_asset_COUNT; ++index)
    assert(assets::get_sound((assets::sound_asset)index).valid());
  for (uint32_t index = 0; index < assets::animation_asset_COUNT; ++index)
    assert(assets::get_animation((assets::animation_asset)index).valid());
  for (uint32_t index = 0; index < assets::hitbox_rig_COUNT; ++index)
    assert(assets::get_hitbox_rig((assets::hitbox_rig)index).valid());
  for (uint32_t index = 0; index < assets::font_asset_COUNT; ++index)
    assert(assets::get_font((assets::font_asset)index).valid());
  for (uint32_t index = 0; index < assets::pbr_material_COUNT; ++index)
    assert(assets::get_pbr_material((assets::pbr_material)index).valid());

  // Id 0 is the compiled-in placeholder in every class -- no file behind it, so
  // it cannot be the thing that is missing. The mesh one has real geometry (a
  // question mark) and the texture one real pixels (a magenta checker); the
  // other four are empty on purpose and only have to be VALID.
  const assets::mesh_asset_t *missing_mesh =
      assets::get(assets::get_mesh(assets::mesh_asset::Missing));
  assert(missing_mesh != nullptr && !missing_mesh->vertices.empty());

  const assets::texture_asset_t *missing_texture =
      assets::get(assets::get_texture(assets::texture_asset::Missing));
  assert(missing_texture != nullptr);
  assert(missing_texture->width > 0 && missing_texture->height > 0);
  assert(missing_texture->pixels.size() ==
         (size_t)missing_texture->width * missing_texture->height * 4);

  printf("  PASS: test_every_class_registers\n");
  return 0;
}

// The classification rule, pinned from the outside: a material is a DIRECTORY
// that became one id, and the maps inside it are CLAIMED -- packed, never
// enumerated. This replaced a depth rule that could see neither half, and that
// failed silently in both directions: a folder could not be an asset at all, and
// a file one directory too deep dropped out of the id space with no diagnostic.
//
// It asserts against the manifest rather than the filesystem on purpose. The
// manifest is what a packaged build still has, and it is the only half an editor
// can browse once there is no directory to list.
static int test_a_material_is_a_directory_and_claims_its_maps()
{
  // The folder is the entry, minted from the DIRECTORY name.
  const assets::pbr_material_asset_t *material =
      assets::get(assets::get_pbr_material(assets::pbr_material::harsh_bricks));
  assert(material != nullptr);
  assert(material->albedo.valid());

  const Span<const assets::asset_info_t> materials = assets::pbr_material_manifest();
  assert(materials.size() == assets::pbr_material_COUNT);
  assert(materials[(uint32_t)assets::pbr_material::harsh_bricks].path != nullptr);
  assert(std::string_view(materials[(uint32_t)assets::pbr_material::harsh_bricks].path) ==
         "resources/textures/harsh_bricks");

  // Claimed: no map inside a material folder is its own texture id. Six of them
  // share the basename "albedo", so minting them would collide on the first two
  // materials -- the collision IS the reason the folder has to be the unit.
  for (const assets::asset_info_t &texture : assets::texture_asset_manifest())
  {
    if (texture.path == nullptr)
      continue;
    const std::string_view path = texture.path;
    assert(path.find("/harsh_bricks/") == std::string_view::npos);
    assert(path.find("/sloppy_mortar_stone/") == std::string_view::npos);
  }

  // Unclaimed and nested: an id anyway. Under the depth rule this file was
  // packed and silently invisible to the id space.
  bool found_nested = false;
  for (const assets::asset_info_t &texture : assets::texture_asset_manifest())
    if (texture.path != nullptr &&
        std::string_view(texture.path) == "resources/models/textures/leet_skin.png")
      found_nested = true;
  assert(found_nested);

  printf("  PASS: test_a_material_is_a_directory_and_claims_its_maps\n");
  return 0;
}

// Box and Sphere are BAKED .mesh files now, dumped once from the generators
// that used to run at init. physics_body_system.cpp scales both through
// `render.scale` on the assumption that a primitive is UNIT-SIZED, and nothing
// else asserts that -- the failure mode is a physics body 100x too large, and
// it would not be obvious which of the two regimes drifted. A .mesh is in
// engine units and skips load_obj's 100-unit normalization, which is exactly
// why the bake went to .mesh rather than to .obj.
static int test_baked_primitives_are_unit_sized()
{
  auto check_extent = [](assets::mesh_asset id, const char *name)
  {
    const assets::mesh_asset_t *mesh = assets::get(assets::get_mesh(id));
    assert(mesh != nullptr && !mesh->vertices.empty());

    const shared::aabb_bounds_t bounds = assets::compute_mesh_bounds(mesh);
    const linalg::vec3f         size   = bounds.max - bounds.min;

    const float tolerance = 0.001f;
    assert(std::fabs(size.x - 1.0f) < tolerance);
    assert(std::fabs(size.y - 1.0f) < tolerance);
    assert(std::fabs(size.z - 1.0f) < tolerance);
    // Centred on the origin, so scaling about it stays symmetric.
    assert(std::fabs(bounds.min.x + bounds.max.x) < tolerance);
    assert(std::fabs(bounds.min.y + bounds.max.y) < tolerance);
    assert(std::fabs(bounds.min.z + bounds.max.z) < tolerance);
    (void)name;
  };

  check_extent(assets::mesh_asset::Box, "Box");
  check_extent(assets::mesh_asset::Sphere, "Sphere");

  printf("  PASS: test_baked_primitives_are_unit_sized\n");
  return 0;
}

static int test_manifest_ids_are_distinct()
{
  // Two ids resolving to one handle means the manifest named the same file
  // twice, or a load fell back without saying so.
  for (uint32_t index = 1; index < assets::mesh_asset_COUNT; ++index)
    assert(assets::get_mesh((assets::mesh_asset)index) !=
           assets::get_mesh(assets::mesh_asset::Missing));

  printf("  PASS: test_manifest_ids_are_distinct\n");
  return 0;
}

// An id off the wire or out of a map file is not range-checked anywhere before
// it gets here, so an out-of-range one has to resolve to the placeholder rather
// than reading past the table. This is Enum_Array::try_get doing its job.
static int test_out_of_range_id_resolves_to_missing()
{
  const auto missing = assets::get_mesh(assets::mesh_asset::Missing);
  assert(missing.valid());
  assert(assets::get_mesh((assets::mesh_asset)assets::mesh_asset_COUNT) == missing);
  assert(assets::get_mesh((assets::mesh_asset)9999) == missing);

  printf("  PASS: test_out_of_range_id_resolves_to_missing\n");
  return 0;
}

// One path, one cache entry, whatever separators and redundant components the
// caller spelled it with. Three pools used to disagree about this, and two
// copies of a skeleton means bone 7 is no longer one bone.
static int test_one_cache_key_per_file()
{
  const auto direct = assets::load_mesh("resources/obj/Pyramid.obj");
  assert(direct.valid());
  assert(assets::load_mesh("resources/obj/../obj/Pyramid.obj") == direct);
  assert(assets::load_mesh("resources\\obj\\Pyramid.obj") == direct);
  assert(assets::load_mesh("./resources/obj/Pyramid.obj") == direct);

  printf("  PASS: test_one_cache_key_per_file\n");
  return 0;
}

// The container `pkg` and `embed` both read, round-tripped through the same two
// functions asset_pack and the byte layer use. The test binary itself runs
// LOOSE -- what is being checked is the format, which is the only thing the two
// packaged modes have that loose does not.
static int test_asset_package_round_trip()
{
  std::vector<assets::asset_package_input_t> files;
  // Deliberately out of order and one of them empty: the builder sorts (the
  // index is binary-searched) and a zero-byte asset is a legal one.
  files.push_back({"resources/z/last.bin", {9, 9, 9}});
  files.push_back({"resources/a/first.bin", {1, 2, 3, 4, 5}});
  files.push_back({"resources/a/empty.bin", {}});

  const std::vector<uint8_t> bytes = assets::build_asset_package(files);

  std::string                                  reason;
  const std::optional<assets::asset_package_t> package =
      assets::try_open_asset_package(bytes, reason);
  assert(package && reason.empty());
  assert(package->entry_count == 3);

  // Sorted by path, which is what makes the lookup a binary search.
  assert(assets::asset_package_path_at(*package, 0) == "resources/a/empty.bin");
  assert(assets::asset_package_path_at(*package, 1) == "resources/a/first.bin");
  assert(assets::asset_package_path_at(*package, 2) == "resources/z/last.bin");

  const std::optional<Span<const uint8_t>> first =
      assets::try_find_asset_in_package(*package, "resources/a/first.bin");
  assert(first && first->size() == 5 && (*first)[0] == 1 && (*first)[4] == 5);

  const std::optional<Span<const uint8_t>> empty =
      assets::try_find_asset_in_package(*package, "resources/a/empty.bin");
  assert(empty && empty->size() == 0);

  // Every data span is aligned, so a decoder handed one out of .rodata sees what
  // it would have seen from a file read.
  assert(((uintptr_t)first->data % assets::ASSET_PACKAGE_DATA_ALIGNMENT) == 0);

  assert(!assets::try_find_asset_in_package(*package, "resources/a/nope.bin"));
  // A prefix of a real path must not match: the entry carries its length, the
  // string table is not NUL terminated.
  assert(!assets::try_find_asset_in_package(*package, "resources/a/first"));

  // Refusals, not crashes: these bytes came off disk or out of somebody else's
  // exe.
  std::vector<uint8_t> corrupt = bytes;
  corrupt[0] = 'X';
  assert(!assets::try_open_asset_package(corrupt, reason));
  corrupt    = bytes;
  corrupt[8] = (uint8_t)(assets::ASSET_PACKAGE_VERSION + 1);
  assert(!assets::try_open_asset_package(corrupt, reason));
  assert(!assets::try_open_asset_package(Span<const uint8_t>(bytes.data(), 4), reason));

  printf("  PASS: test_asset_package_round_trip\n");
  return 0;
}

// This test IS the launcher, so it owns the asset state the way main_*.cpp do.
// game_shared is a static lib and the accessors resolve through a per-module
// pointer, so nothing works until something points it at a state -- which is
// the whole reason the state is explicit rather than a file-scope registry.
static assets::asset_state_t g_asset_state{};

// The house front face is COUNTER-CLOCKWISE (renderer.hpp's HOUSE_FRONT_FACE)
// and the mesh pipeline culls back faces, so a primitive wound the other way
// draws its far side and culls its near side: inside out. Both baked primitives
// shipped that way once. Every triangle must be counter-clockwise seen from
// outside, and must agree with the normal its own vertices carry -- a winding
// that disagrees with the normal is lit from the wrong side even with culling
// off.
static int test_baked_primitives_are_wound_outward()
{
  auto check_winding = [](assets::mesh_asset id, const char *name)
  {
    const assets::mesh_asset_t *mesh = assets::get(assets::get_mesh(id));
    assert(mesh != nullptr && mesh->indices.size() % 3 == 0);

    linalg::vec3f centroid{0.f, 0.f, 0.f};
    for (const vertex_xnu &vertex : mesh->vertices) centroid = centroid + vertex.position;
    centroid = centroid * (1.0f / (float)mesh->vertices.size());

    for (size_t at = 0; at + 2 < mesh->indices.size(); at += 3)
    {
      const vertex_xnu &a = mesh->vertices[mesh->indices[at + 0]];
      const vertex_xnu &b = mesh->vertices[mesh->indices[at + 1]];
      const vertex_xnu &c = mesh->vertices[mesh->indices[at + 2]];

      const linalg::vec3f face_normal =
          linalg::cross(b.position - a.position, c.position - a.position);
      const linalg::vec3f outward =
          (a.position + b.position + c.position) * (1.0f / 3.0f) - centroid;

      // A UV sphere's pole triangles have two coincident corners and no area, so
      // they have no winding to check.
      if (linalg::length(face_normal) < 1e-9f) continue;

      assert(linalg::dot(face_normal, outward) > 0.f);
      assert(linalg::dot(face_normal, a.normal + b.normal + c.normal) > 0.f);
    }
    (void)name;
  };

  check_winding(assets::mesh_asset::Box, "Box");
  check_winding(assets::mesh_asset::Sphere, "Sphere");

  printf("  PASS: test_baked_primitives_are_wound_outward\n");
  return 0;
}

// An OBJ with no `vn` loads with unit normals that agree with its faces
// (lightmap_unwrap_plan.md step 5), rather than the zeros that made every such
// mesh read as bare texture under any bake.
static int test_an_obj_without_normals_derives_them()
{
  const char* text = "v 0 0 0\nv 1 0 0\nv 1 1 0\nv 0 1 0\nf 1 2 3 4\n";
  const assets::mesh_asset_t mesh = assets::decode_obj(
      Span<const uint8_t>((const uint8_t*)text, (uint32_t)strlen(text)), "no_normals.obj");

  assert(mesh.vertices.size() == 4);
  assert(mesh.indices.size() == 6);
  for (const vertex_xnu& vertex : mesh.vertices)
  {
    assert(std::abs(linalg::length(vertex.normal) - 1.f) < 1e-5f);
    // (1,0,0) x (1,1,0) is +z: the winding's own normal.
    assert(vertex.normal.z > 0.999f);
  }

  printf("  PASS: test_an_obj_without_normals_derives_them\n");
  return 0;
}

int main()
{
  printf("=== Asset System Tests ===\n");
  assets::set_state(&g_asset_state);
  assets::mount_asset_source();

  // The five fixture-backed tests write a file and then load it, which is a
  // loose-mode question by construction: a packaged build's answer to "is this
  // path there" is the index asset_pack wrote, and a file appearing next to the
  // exe afterwards deliberately does not change it. Everything below them is
  // about the manifest and runs in all three modes.
#if !defined(TILDE_ASSET_SOURCE_PKG) && !defined(TILDE_ASSET_SOURCE_EMBED)
  test_load_mesh();
  test_load_texture();
  test_caching();
  test_asset_exists();
  test_read_asset_bytes();
#endif

  test_manifest_registers_every_id();
  test_every_class_registers();
  test_a_material_is_a_directory_and_claims_its_maps();
  test_baked_primitives_are_unit_sized();
  test_baked_primitives_are_wound_outward();
  test_manifest_ids_are_distinct();
  test_out_of_range_id_resolves_to_missing();
  test_one_cache_key_per_file();
  test_asset_package_round_trip();
  test_an_obj_without_normals_derives_them();
  printf("All tests passed.\n");
  return 0;
}
