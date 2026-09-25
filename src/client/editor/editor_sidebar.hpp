#pragma once

#include "entity_outliner.hpp"

namespace client
{

class Editor_Tool;
struct editor_context_t;

constexpr float EDITOR_SIDEBAR_MAXIMUM_WIDTH = 900.0f;

// The right edge of the editor: the outliner on top, the active tool's
// inspector below, split by a divider the author drags.
struct editor_sidebar_t
{
  float            width         = EDITOR_SIDEBAR_MAXIMUM_WIDTH;
  float            list_fraction = 0.4f;
  outliner_state_t outliner;
};

[[nodiscard]] outliner_result_t draw_editor_sidebar(editor_sidebar_t&                sidebar,
                                                    editor_context_t&                context,
                                                    entity_visibility_t&             visibility,
                                                    Editor_Tool*                     active_tool,
                                                    Span<const shared::entity_uid_t> selection);

} // namespace client
