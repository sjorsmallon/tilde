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

