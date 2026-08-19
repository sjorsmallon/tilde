# `.def` Families — Unification Notes

Where the four families are needlessly different, what to do about it, and in
what order. Outcome of the discussion on 2026-08-18, which started from "why
isn't `u8` supported in `cvars.def`?" and ended somewhere more interesting.

Read `entity_def.md`, `cvar_def.md` and `events_def.md` first. This document
argues no new design; it is a list of places where the existing four have drifted
apart for no reason a consumer requires.

## The grammar is not the problem

Worth stating up front, because "the `.def` grammars differ" is the natural first
diagnosis and it is wrong. There is ONE lexer, ONE block parser, ONE `program_t`,
and the production rules at the top of `src/tools/def_gen.cpp` cover all four
families in one grammar. `def_family_t` already exists and `check_family` already
infers a file's family from the declaration kinds in it.

What is fragmented is everything *after* the parse: the per-family rules are
hand-rolled predicates and hand-rolled emitters where they should be one table
with a row per family.

## Essential differences — leave these alone

Each of these is a consequence of what the consuming code can do, not an
arbitrary choice:

- **Entity fields allow `component`, channel fields do not.** `entity_reflection`
  has a leaf-flattening pass; a channel's table is flat and has none.
- **Entity fields allow `asset`, channel fields do not.** `import` exists in one
  direction only, and the event family forbids it.
- **Cvars reject `v3`.** No console line spells a vector.

## Accidental differences — the actual list

The same question, answered in three different shapes:

- **Three type allowlists, no shared table.** `cvar_type_is_allowed`,
  `channel_type_is_allowed`, and the entity family's implicit "everything".
- **Flag vocabularies checked per family in separate functions** rather than
  declared once as a per-family mask.
- **Descriptions are mandatory on cvar lines, command lines and channel members,
  and absent on entity fields.** The editor inspector would happily use them.
- **Emission naming is half literal, half stem.** `entities_generated`,
  `assets_generated` and `cvars_generated` are hardcoded; the event family
  derives its names from the `.def`'s filename stem. Those three literals already
  equal their stems, so deleting them and always deriving is close to a no-op
  diff -- and it removes exactly the hazard the event emitter's own comment warns
  about (a literal would have had the effects codec include the gameplay-event
  header). The one real mismatch left is `{server,client}_command_bindings.cpp`
  against the event family's `client_<stem>_bindings.cpp`.

## The one that matters: cvars opted out of the text seam

`field_to_text` / `field_from_text` in `shared/reflection.cpp` is described
everywhere as *the only place field bytes become characters, for entities AND
events*. Cvars are the exclusion. They carry their own type column (`cvar_type`,
a second and smaller parallel to `field_type_t`) and their own emitted conversion
(`emit_cvar_text_conversion`), which is a second and smaller parallel walker.

**That is why `u8` is not supported in `cvars.def`.** It is not a property of
cvars -- `u8` lexes fine, `FIELD_TYPE_U8` is a first-class entity/event field
type, and the walker that already handles it is the one cvars do not use. The
comment above `cvar_type_is_allowed` justifies the restriction with "has to
survive a memcmp against a retained copy" and "has to be one plain member of a
trivially copyable struct", and a `uint8_t` satisfies both -- the memcmp is not
even a whole-struct one, since `collect_changed_mirrored_cvars` compares per-cvar
through the table's offset+size, so a narrow member's padding is never read. That
reasoning genuinely rules out `v3`, `component` and `enum`. It does not rule out
narrow ints, and it should be corrected whether or not the merge below happens.

## Suggested order

1. **Delete `cvar_type`; use `field_type_t`.** Route cvar text through
   `field_to_text` / `field_from_text`. `cvar_info_t` already carries offset+size
   -- that is what the mirror memcmps against -- which is most of a
   `field_info_t` already. This drops an enum, drops an emitter, and hands over
   the narrow ints, `v4` and `u64` in one move. Enum cvars come nearly free with
   it, which is the one type on the currently-excluded list that has a real user
   waiting (`sv_gamemode deathmatch` beats `sv_gamemode 2`, and the machinery --
   `try_from_string<T>`, enum-by-name parsing in the command binder -- is already
   emitted into the same header).

2. **Always derive emitted filenames from the stem.** Delete the three literals.
   Decide what `command_bindings` should be called under the rule.

3. **Then the table.** A `family_traits_t`, one row per family: allowed
   declaration kinds, an allowed-type mask, an allowed-flag mask,
   `descriptions_mandatory`, `may_import`, and the emitted filenames. Adding a
   fifth family becomes a row instead of a tour of the file.

Order matters: step 1 is what makes "allowed types" a mask rather than a
predicate, which is what makes step 3 a table rather than a struct full of
function pointers.
