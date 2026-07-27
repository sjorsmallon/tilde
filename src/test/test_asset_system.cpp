#include "asset.hpp"
#include <cassert>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>

// --- Helpers ---

// Fixtures go in the platform temp dir, not a hardcoded "/tmp" -- on Windows
// that path does not exist, so every ofstream below wrote nowhere and the
// loads then failed on a missing file.
static std::string test_file_path(const char *name)
{
  return (std::filesystem::temp_directory_path() / name).string();
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

static int test_invalid_path()
{
  auto h = assets::load_mesh(test_file_path("this_does_not_exist.obj").c_str());
  assert(!h.valid());
  assert(assets::get(h) == nullptr);

  printf("  PASS: test_invalid_path\n");
  return 0;
}

int main()
{
  printf("=== Asset System Tests ===\n");
  test_load_mesh();
  test_load_texture();
  test_caching();
  test_invalid_path();
  printf("All tests passed.\n");
  return 0;
}
