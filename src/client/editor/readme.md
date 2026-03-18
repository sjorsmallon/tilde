# Editor Ramblings

I ran into issues with an imperative approach to my editor. the long if-else chain of 
```
if (a _pressed)
    if (shift_pressed)
    else if (control_pressed)
....
```
became very onerous and was not the solution. I then moved to a simple dispatch mechanism where we keep a table of function pointers and key maps to know which
combination of keys should dispatch to whatever button. however, the state management
and serialization with the map became very ad-hoc and I did not know how to generalize it properly.


After some digging and investigation, it seems that a "common"(?) pattern is to use a `tool` pattern. one tool for selection, one tool for sculpting, one tool for placement.

The problem here then is how to abstract those different tools in a meaningful way to support all functionality that is needed. This is not something I came up with but stole from somewhere else and defined in `editor_tool.hpp`. the gist of it is that the `tool_editor_state` keeps track of which tool is active and dispatches to the correct tool.

I ran into more issues with selection not being generalizable and also how to keep track of entities and static geometry etc. We had AABBS which are trivial, but I wanted to introduce wedges and then it becomes evident that keeping a linear list of all these shapes in the map is also not very fruitful. The current solution that I picked is that the map contains a `static_geometry_t` variant which is now either an AABB, a wedge or a `mesh` (not directly embedding the vertices, but pointing to an asset but having an up-to-date bounding `AABB`).

this also simplifies picking in the editor!

now the next problem to fix is the undo / redo stack. The previous solution uses a pair of function pointers that do undo / redo, but that to me looked like a very fragile mechanism relying on order of insertion etc. Additionally, it means implementing an undo / redo for every single unique action and that also does not seem to scale well IMO. I want to move to a transaction mechanism which can be easily undone or done.

## Entity Editor Traits

The editor needs per-entity-type behavior (ghost rendering, placement Y-offset) but entities live in `shared/` and must not know about the editor. The solution is `entity_editor_traits.hpp` — a template specialization pattern that keeps all editor-specific knowledge in client code.

```cpp
// Primary template — deliberately unimplemented.
template <typename EntityClass>
struct Entity_Editor_Traits {
  static linalg::vec3 get_half_extents(const EntityClass *e);
  static bool draw_ghost(const EntityClass *e, overlay_renderer_t &renderer,
                         const linalg::vec3 &center);
};
```

Every entity type in `SHARED_ENTITIES_LIST` must have a specialization. If you add a new entity to the X-macro and forget to specialize, the linker will error telling you exactly which instantiation is missing (because the dispatch functions in `entity_editor_traits.cpp` use the X-macro to call every specialization).

### How to add traits for a new entity

1. Add your entity to `SHARED_ENTITIES_LIST` in `entity_list.hpp` (as usual).
2. Open `entity_editor_traits.cpp` and add two template specializations:

```cpp
template <>
linalg::vec3
Entity_Editor_Traits<network::My_New_Entity>::get_half_extents(
    const network::My_New_Entity *e)
{
  // Return the half-extents used for Y-offset placement.
  // {0,0,0} for point entities, actual extents for geometry.
  return {editor::DEFAULT_HALF_EXTENT, editor::DEFAULT_HALF_EXTENT,
          editor::DEFAULT_HALF_EXTENT};
}

template <>
bool Entity_Editor_Traits<network::My_New_Entity>::draw_ghost(
    const network::My_New_Entity *e, overlay_renderer_t &renderer,
    const linalg::vec3 &center)
{
  // Return false to use the default path (render component mesh wireframe,
  // then wire box fallback). Return true if you drew something custom.
  return false;
}
```

### What each function does

- **`get_half_extents`**: Returns the half-size used to compute the Y-offset when placing an entity so it sits on the surface rather than clipping through it. For example, AABB returns its actual `half_extents`, Player_Spawn returns the player hull dimensions, and Particle_Emitter returns `{0,0,0}` because it's a point.

- **`draw_ghost`**: Custom ghost/preview drawing for the placement tool. Return `true` if you handled drawing (e.g. wedge wireframe, emitter cross icon). Return `false` to fall through to the default path which tries the entity's render component mesh as a wireframe, then falls back to a wire box using `get_half_extents`.

### Runtime dispatch

Since placement works with `shared_ptr<Entity>` at runtime, the X-macro generates a dispatch table in `entity_editor_traits.cpp`:

```cpp
linalg::vec3 get_placement_half_extents(const network::Entity *e);
bool draw_entity_ghost(const network::Entity *e, ...);
linalg::vec3 compute_placement_center(const network::Entity *e, const linalg::vec3 &ghost_pos);
```

These use `dynamic_cast` internally (one per entity type, generated by the X-macro) to route to the correct specialization. The placement tool just calls these and never needs to know about specific entity types.

