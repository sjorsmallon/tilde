
- sculpting is wrong: after ininitial resize, the face is in the wrong way.
- undo/redo: entity_placement_t position/scale/rotation not tracked by transaction system. Gizmo translate/rotate syncs placement fields but undo only restores entity schema properties. After undo, placement and entity fields may be out of sync until BVH rebuild.
- undo/redo: multi-entity delete creates one transaction per entity. Ctrl+Z only undoes one deletion at a time instead of the whole batch.



- why is AABB a schema? it's not a good decision.
- all components that exist now should define a schema. is that what we want?
- make sure the default mesh is the question mark.
- clean up BVH traversal because we now just iterate over entities in the map editor.