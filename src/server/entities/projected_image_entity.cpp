#include "../../shared/entities/generated/entities/projected_image_entity_generated.hpp"
#include "../../shared/lighting/projected_image.hpp"
#include "../../shared/log.hpp"
#include "../entity_io_context.hpp"

namespace entities
{

void set_character(Projected_Image_Entity &image, const Set_Character_Data &payload, server::input_context_t &)
{
  const int32_t columns = static_cast<int32_t>(shared::PROJECTED_IMAGE_COLUMNS);
  const int32_t rows    = static_cast<int32_t>(shared::PROJECTED_IMAGE_ROWS);
  if (payload.column < 0 || payload.column >= columns || payload.row < 0 || payload.row >= rows)
  {
    log_warning("[image] {} refused Set_Character: ({}, {}) is outside the {} by {} text", image.entity_id,
                payload.column, payload.row, columns, rows);
    return;
  }
  if (payload.character < ' ' || payload.character > '~')
  {
    log_warning("[image] {} refused Set_Character: {} is not printable ASCII (32 to 126)", image.entity_id,
                payload.character);
    return;
  }
  shared::set_projected_image_character(image, static_cast<uint32_t>(payload.column),
                                        static_cast<uint32_t>(payload.row), static_cast<char>(payload.character));
}

} // namespace entities
