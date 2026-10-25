// A face's grid and paint, which never involved a hull: a level change
// resamples rather than flattens, and a painted weight set sums to one.

#include "shared/map/face_surface.hpp"
#include "shared/vertex.hpp"

#include <cassert>
#include <cmath>
#include <iostream>

namespace
{

void test_the_grid_survives_a_level_change()
{
  shared::face_surface_t face;
  shared::resize_face_grid(face, 4);
  assert(face.offsets.size() == 25 && face.blend.size() == 25);
  face.offsets[2 * 5 + 2] = {0.5f, 32.25f, -0.125f};
  face.blend[2 * 5 + 2]   = 0.375f;

  // Raising the level resamples rather than flattening, so sculpting work is not
  // thrown away by a slider.
  shared::resize_face_grid(face, 8);
  assert(face.offsets.size() == 81);
  assert(face.blend.size() == 81);
  assert(std::abs(face.offsets[4 * 9 + 4].y - 32.25f) < 0.001f);

  // And zero clears both arrays outright, which is what "flat" means.
  shared::resize_face_grid(face, 0);
  assert(face.offsets.empty() && face.blend.empty());
  assert(!shared::face_is_subdivided(face));

  std::cout << "test_the_grid_survives_a_level_change passed" << std::endl;
}

void test_a_painted_weight_set_always_sums_to_one()
{
  shared::face_surface_t face;
  face.subdivision_level = 1;
  face.offsets.assign(4, {0, 0, 0});
  face.blend.assign(4, 0.f);

  // An unpainted vertex is ALL of layer 0, with nothing stored to say so: the
  // implied first layer is what makes a file written before blending existed
  // mean what it always meant.
  assert(std::abs(shared::face_layer_weight(face, 0, 0) - 1.f) < 1e-6f);
  assert(shared::face_layer_weight(face, 0, 1) == 0.f);

  shared::paint_face_layer_weight(face, 0, 1, 0.25f);
  shared::paint_face_layer_weight(face, 0, 1, 0.25f);

  float sum = 0.f;
  for (int layer = 0; layer < BLEND_LAYER_COUNT; ++layer)
    sum += shared::face_layer_weight(face, 0, layer);
  assert(std::abs(sum - 1.f) < 1e-5f);
  assert(shared::face_layer_weight(face, 0, 1) > 0.4f);

  // Painting toward layer 0 IS the eraser -- there is no subtract flag to get
  // out of step with the target.
  for (int stroke = 0; stroke < 20; ++stroke)
    shared::paint_face_layer_weight(face, 0, 0, 0.5f);
  assert(shared::face_layer_weight(face, 0, 1) < 0.001f);
  assert(shared::face_layer_weight(face, 0, 0) > 0.999f);

  std::cout << "test_a_painted_weight_set_always_sums_to_one passed" << std::endl;
}

} // namespace

int main()
{
  test_the_grid_survives_a_level_change();
  test_a_painted_weight_set_always_sums_to_one();

  std::cout << "all face surface tests passed" << std::endl;
  return 0;
}
