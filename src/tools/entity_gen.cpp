//
// entity_gen.cpp -- parser for the entity definition DSL (.def files).
//
// A host tool. Reads a .def file, tokenizes it, parses it into a flat IR and
// resolves names, then dumps the result. Code generation will be appended to
// this file later; the IR below is the contract between the two halves.
//
// Design notes live in entity_def.md at the repo root.
//
// ---------------------------------------------------------------------------
// Grammar
// ---------------------------------------------------------------------------
//
//   program           -> declaration*
//
//   declaration       -> IDENTIFIER '::' declaration_body
//
//   declaration_body  -> 'base'      '{' field* '}'
//                      | 'component' '{' field* '}'
//                      | 'entity'    annotation* '{' field* '}'
//                      | 'enum'      '{' enum_value_list '}'
//                      | 'assets'    '{' asset_entry* '}'
//                      | flagset
//
//   flagset           -> '[' annotation (',' annotation)* ']'
//
//   enum_value_list   -> IDENTIFIER (',' IDENTIFIER)* ','?
//
//   asset_entry       -> 'placeholder' STRING_LITERAL
//                      | 'scan'        STRING_LITERAL STRING_LITERAL
//                      | 'procedural'  IDENTIFIER STRING_LITERAL
//
//   field             -> IDENTIFIER ':' type ('=' default_value)? annotation*
//
//   type              -> 'string' '<' NUMBER '>'
//                      | IDENTIFIER
//
//   default_value     -> NUMBER
//                      | '{' NUMBER (',' NUMBER)* '}'
//                      | '.' IDENTIFIER
//                      | 'true' | 'false'
//                      | STRING_LITERAL
//
//   annotation        -> '@' IDENTIFIER
//
// A field is newline terminated: its annotations must sit on the same line as
// its name. There is no newline token -- every token carries its line number
// and the annotation loop stops when the line changes.
//
// ---------------------------------------------------------------------------
// Memory
// ---------------------------------------------------------------------------
//
// Every array is allocated once, up front, from a proven upper bound, and
// never grows. A token needs at least one source byte, so token_count <=
// source_length; every declaration, field, annotation and enum value needs at
// least one token, so each of those is bounded by token_count. Nothing ever
// reallocates, which is what makes it safe to hand out interior pointers
// (declaration_t*, field_t*) and to store names as views into the source
// buffer instead of copying them.
//
// If these ever have to grow, they must grow as a chunked arena that leaves
// existing chunks in place. A vector-style "realloc and move" would silently
// dangle every pointer handed out so far.
//
// The asset manifest is the one exception to "names are views into the source
// buffer": a scanned filename comes from the filesystem, not from the .def, so
// there is nothing in the source to point at. Those strings are copied into a
// bump arena (program_t::string_arena) and handed out as NUL terminated char*.
// The arena is fixed size and never grows, for the same pointer-stability
// reason. Its capacity is NOT a proven bound -- it depends on what is on disk --
// so exhausting it is a real diagnostic, not an assert.
//

#define _CRT_SECURE_NO_WARNINGS // fopen

#include <cassert>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem> // only for the asset directory scan

// ---------------------------------------------------------------------------
// Strings
// ---------------------------------------------------------------------------

// A slice of the source buffer. Never owns, never allocates, never NUL
// terminated -- always compare with the length first.
struct string_view_t
{
  const char* data;
  int32_t     length;
};

static bool string_view_matches(string_view_t view, const char* text)
{
  int32_t text_length = (int32_t)strlen(text);
  if (view.length != text_length)
    return false;
  return memcmp(view.data, text, (size_t)text_length) == 0;
}

static bool string_views_match(string_view_t left, string_view_t right)
{
  if (left.length != right.length)
    return false;
  return memcmp(left.data, right.data, (size_t)left.length) == 0;
}

// ---------------------------------------------------------------------------
// Character classification
// ---------------------------------------------------------------------------

enum char_class_t : uint8_t
{
  CHAR_CLASS_NONE             = 0,
  CHAR_CLASS_WHITESPACE       = 1 << 0,
  CHAR_CLASS_NEWLINE          = 1 << 1,
  CHAR_CLASS_IDENTIFIER_START = 1 << 2,
  CHAR_CLASS_IDENTIFIER_BODY  = 1 << 3,
  CHAR_CLASS_DIGIT            = 1 << 4,
};

struct char_class_table_t
{
  uint8_t entries[256];
};

static constexpr char_class_table_t make_char_class_table()
{
  char_class_table_t table = {};
  for (int32_t index = 0; index < 256; ++index)
  {
    uint8_t classes = CHAR_CLASS_NONE;

    if (index == ' ' || index == '\t' || index == '\r' || index == '\f' || index == '\v')
      classes |= CHAR_CLASS_WHITESPACE;
    if (index == '\n')
      classes |= CHAR_CLASS_NEWLINE;
    if ((index >= 'a' && index <= 'z') || (index >= 'A' && index <= 'Z') || index == '_')
      classes |= CHAR_CLASS_IDENTIFIER_START | CHAR_CLASS_IDENTIFIER_BODY;
    if (index >= '0' && index <= '9')
      classes |= CHAR_CLASS_DIGIT | CHAR_CLASS_IDENTIFIER_BODY;

    table.entries[index] = classes;
  }
  return table;
}

static constexpr char_class_table_t CHAR_CLASSES = make_char_class_table();

static bool character_has_class(char character, uint8_t classes)
{
  return (CHAR_CLASSES.entries[(uint8_t)character] & classes) != 0;
}

// ---------------------------------------------------------------------------
// Tokens
// ---------------------------------------------------------------------------

enum token_kind_t : uint8_t
{
  TOKEN_END_OF_FILE = 0,
  TOKEN_IDENTIFIER,
  TOKEN_NUMBER,
  TOKEN_STRING_LITERAL,
  TOKEN_COLON_COLON,
  TOKEN_COLON,
  TOKEN_EQUALS,
  TOKEN_AT,
  TOKEN_DOT,
  TOKEN_COMMA,
  TOKEN_OPEN_BRACE,
  TOKEN_CLOSE_BRACE,
  TOKEN_OPEN_BRACKET,
  TOKEN_CLOSE_BRACKET,
  TOKEN_LESS,
  TOKEN_GREATER,
};

struct token_t
{
  token_kind_t kind;
  int32_t      offset; // byte offset of the first character in the source buffer
  int32_t      length;
  int32_t      line;   // 1 based
};

static const char* token_kind_name(token_kind_t kind)
{
  switch (kind)
  {
    case TOKEN_END_OF_FILE:    return "end of file";
    case TOKEN_IDENTIFIER:     return "an identifier";
    case TOKEN_NUMBER:         return "a number";
    case TOKEN_STRING_LITERAL: return "a string literal";
    case TOKEN_COLON_COLON:    return "'::'";
    case TOKEN_COLON:          return "':'";
    case TOKEN_EQUALS:         return "'='";
    case TOKEN_AT:             return "'@'";
    case TOKEN_DOT:            return "'.'";
    case TOKEN_COMMA:          return "','";
    case TOKEN_OPEN_BRACE:     return "'{'";
    case TOKEN_CLOSE_BRACE:    return "'}'";
    case TOKEN_OPEN_BRACKET:   return "'['";
    case TOKEN_CLOSE_BRACKET:  return "']'";
    case TOKEN_LESS:           return "'<'";
    case TOKEN_GREATER:        return "'>'";
  }
  return "an unknown token";
}

// ---------------------------------------------------------------------------
// Intermediate representation
// ---------------------------------------------------------------------------

enum field_flags_t : uint32_t
{
  FIELD_FLAG_NONE      = 0,
  FIELD_FLAG_NETWORKED = 1 << 0,
  FIELD_FLAG_EDITABLE  = 1 << 1,
  FIELD_FLAG_SAVEABLE  = 1 << 2,
};

enum class_flags_t : uint32_t
{
  CLASS_FLAG_NONE         = 0,
  CLASS_FLAG_RUNTIME_ONLY = 1 << 0,
};

enum type_kind_t : uint8_t
{
  TYPE_UNRESOLVED = 0, // a name the resolve pass has not linked yet
  TYPE_F32,
  TYPE_F64,
  TYPE_U8,
  TYPE_U16,
  TYPE_U32,
  TYPE_U64,
  TYPE_I8,
  TYPE_I16,
  TYPE_I32,
  TYPE_I64,
  TYPE_BOOL,
  TYPE_V3,
  TYPE_V4,
  TYPE_V4I,
  TYPE_STRING,    // capacity lives in type_reference_t::capacity
  TYPE_ASSET,     // mesh_asset / sprite_asset, closed sets from the asset manifest
  TYPE_ENUM,      // resolved: declaration_index points at a DECLARATION_ENUM
  TYPE_COMPONENT, // resolved: declaration_index points at a DECLARATION_COMPONENT
};

static const char* type_kind_name(type_kind_t kind)
{
  switch (kind)
  {
    case TYPE_UNRESOLVED: return "unresolved";
    case TYPE_F32:        return "f32";
    case TYPE_F64:        return "f64";
    case TYPE_U8:         return "u8";
    case TYPE_U16:        return "u16";
    case TYPE_U32:        return "u32";
    case TYPE_U64:        return "u64";
    case TYPE_I8:         return "i8";
    case TYPE_I16:        return "i16";
    case TYPE_I32:        return "i32";
    case TYPE_I64:        return "i64";
    case TYPE_BOOL:       return "bool";
    case TYPE_V3:         return "v3";
    case TYPE_V4:         return "v4";
    case TYPE_V4I:        return "v4i";
    case TYPE_STRING:     return "string";
    case TYPE_ASSET:      return "asset";
    case TYPE_ENUM:       return "enum";
    case TYPE_COMPONENT:  return "component";
  }
  return "unknown";
}

struct type_reference_t
{
  type_kind_t   kind;
  string_view_t name;              // exactly as written, for errors and codegen
  int32_t       capacity;          // string<N>, otherwise 0
  int32_t       declaration_index; // resolved target, -1 until then
};

enum default_kind_t : uint8_t
{
  DEFAULT_NONE = 0,
  DEFAULT_NUMBER,
  DEFAULT_VECTOR,
  DEFAULT_ENUM_LITERAL,
  DEFAULT_BOOL,
  DEFAULT_STRING,
};

struct default_value_t
{
  default_kind_t kind;
  double         numbers[4];
  int32_t        number_count;
  bool           boolean;
  string_view_t  text; // enum value name, or the contents of a string literal
};

// Where an asset's bytes come from. This is the ONLY place the distinction
// exists: it is a column in the generated manifest, read by the asset system's
// init and by nothing else. No consumer of an asset id ever asks which kind it
// is -- that is the whole point of the manifest modelling identity rather than
// paths.
enum asset_source_kind_t : uint8_t
{
  ASSET_SOURCE_MISSING = 0, // slot 0 of every class: no asset assigned
  ASSET_SOURCE_FILE,        // `source` is a path, relative to the working directory
  ASSET_SOURCE_PROCEDURAL,  // `source` is a generator key
};

// Names here are NUL terminated and arena owned rather than views into the
// source, because a scanned entry's name comes from the filesystem.
struct asset_entry_t
{
  const char*         name;
  const char*         source;
  asset_source_kind_t source_kind;
  int32_t             offset; // declaration site, for diagnostics; -1 if scanned
  int32_t             line;
};

// Every node keeps the source offset and line of the token it started at, so
// the resolve pass can point at real file positions long after the tokens are
// behind it.
struct annotation_t
{
  string_view_t name;
  int32_t       offset;
  int32_t       line;
};

struct field_t
{
  string_view_t    name;
  type_reference_t type;
  default_value_t  default_value;
  int32_t          first_annotation; // into program_t::annotations
  int32_t          annotation_count;
  uint32_t         flags;            // filled by the resolve pass, not the parser
  int32_t          offset;
  int32_t          line;
};

enum declaration_kind_t : uint8_t
{
  DECLARATION_BASE = 0,
  DECLARATION_COMPONENT,
  DECLARATION_ENTITY,
  DECLARATION_ENUM,
  DECLARATION_FLAGSET,
  DECLARATION_ASSETS,
};

static const char* declaration_kind_name(declaration_kind_t kind)
{
  switch (kind)
  {
    case DECLARATION_BASE:      return "base";
    case DECLARATION_COMPONENT: return "component";
    case DECLARATION_ENTITY:    return "entity";
    case DECLARATION_ENUM:      return "enum";
    case DECLARATION_FLAGSET:   return "flagset";
    case DECLARATION_ASSETS:    return "assets";
  }
  return "unknown";
}

struct declaration_t
{
  declaration_kind_t kind;
  string_view_t      name;
  uint32_t           class_flags; // CLASS_FLAG_*, or the resolved mask of a flagset

  int32_t first_field; // into program_t::fields
  int32_t field_count;

  int32_t first_enum_value; // into program_t::enum_values
  int32_t enum_value_count;

  int32_t first_annotation; // into program_t::annotations, the class level ones
  int32_t annotation_count;

  // DECLARATION_ASSETS only. The scan directive is kept unexpanded until the
  // resolve pass, so parsing stays a pure function of the .def text and only
  // one clearly marked pass ever touches the filesystem.
  int32_t     first_asset_entry; // into program_t::asset_entries
  int32_t     asset_entry_count;
  const char* scan_directory; // nullptr if the class declares no scan
  const char* scan_extension;
  const char* placeholder_path; // nullptr if the class declares no placeholder
  int32_t     scan_offset;      // diagnostics for the scan directive itself
  int32_t     scan_line;

  int32_t offset;
  int32_t line;
};

struct program_t
{
  const char* filename;
  char*       source;
  int32_t     source_length;

  token_t* tokens;
  int32_t  token_count;
  int32_t  token_capacity;

  declaration_t* declarations;
  int32_t        declaration_count;
  int32_t        declaration_capacity;

  field_t* fields;
  int32_t  field_count;
  int32_t  field_capacity;

  annotation_t* annotations;
  int32_t       annotation_count;
  int32_t       annotation_capacity;

  string_view_t* enum_values;
  int32_t        enum_value_count;
  int32_t        enum_value_capacity;

  asset_entry_t* asset_entries;
  int32_t        asset_entry_count;
  int32_t        asset_entry_capacity;

  // Backing store for strings that do not exist in the source buffer: scanned
  // filenames, and the NUL terminated copies of declared asset names.
  char*   string_arena;
  int32_t string_arena_used;
  int32_t string_arena_capacity;

  // Asset scan paths in the .def are relative to this, so that the .def can say
  // "resources/obj" -- what the game says at runtime -- rather than something
  // relative to wherever the build happens to invoke the generator from.
  const char* asset_root;

  int32_t error_count;
};

// A rewind point. A parse function that fails resets the counts it was given,
// so a half built declaration never reaches the IR and its slots are reused by
// the next attempt.
struct program_mark_t
{
  int32_t declaration_count;
  int32_t field_count;
  int32_t annotation_count;
  int32_t enum_value_count;
  int32_t asset_entry_count;
};

static program_mark_t mark_program(const program_t* program)
{
  program_mark_t mark;
  mark.declaration_count = program->declaration_count;
  mark.field_count       = program->field_count;
  mark.annotation_count  = program->annotation_count;
  mark.enum_value_count  = program->enum_value_count;
  mark.asset_entry_count = program->asset_entry_count;
  return mark;
}

static void rewind_program(program_t* program, program_mark_t mark)
{
  program->declaration_count = mark.declaration_count;
  program->field_count       = mark.field_count;
  program->annotation_count  = mark.annotation_count;
  program->enum_value_count  = mark.enum_value_count;
  program->asset_entry_count = mark.asset_entry_count;
  // The string arena is deliberately NOT rewound: it is a bump allocator shared
  // by every declaration, and a failed parse leaks a few bytes of it at most.
}

// The capacities are proven upper bounds, so exhausting one is a bug in the
// bound, not bad input. Fail loudly rather than truncating the program.
static declaration_t* push_declaration(program_t* program)
{
  assert(program->declaration_count < program->declaration_capacity);
  declaration_t* declaration = &program->declarations[program->declaration_count++];
  *declaration                    = {};
  declaration->first_field        = program->field_count;
  declaration->first_enum_value   = program->enum_value_count;
  declaration->first_annotation   = program->annotation_count;
  return declaration;
}

static field_t* push_field(program_t* program)
{
  assert(program->field_count < program->field_capacity);
  field_t* field = &program->fields[program->field_count++];
  *field                        = {};
  field->type.declaration_index = -1;
  field->first_annotation       = program->annotation_count;
  return field;
}

static annotation_t* push_annotation(program_t* program)
{
  assert(program->annotation_count < program->annotation_capacity);
  annotation_t* annotation = &program->annotations[program->annotation_count++];
  *annotation              = {};
  return annotation;
}

static string_view_t* push_enum_value(program_t* program)
{
  assert(program->enum_value_count < program->enum_value_capacity);
  string_view_t* value = &program->enum_values[program->enum_value_count++];
  *value               = {};
  return value;
}

// Unlike the arrays above, the asset arrays are bounded by what is on disk, not
// by the token count, so exhaustion is possible input and gets a real error.
// Both return nullptr on exhaustion; every caller must check.
static char* arena_copy(program_t* program, const char* text, int32_t length)
{
  if (program->string_arena_used + length + 1 > program->string_arena_capacity)
  {
    fprintf(stderr, "%s: error: the asset string arena (%d bytes) is full\n", program->filename,
            program->string_arena_capacity);
    ++program->error_count;
    return nullptr;
  }

  char* copy = program->string_arena + program->string_arena_used;
  memcpy(copy, text, (size_t)length);
  copy[length] = '\0';
  program->string_arena_used += length + 1;
  return copy;
}

static asset_entry_t* push_asset_entry(program_t* program)
{
  if (program->asset_entry_count >= program->asset_entry_capacity)
  {
    fprintf(stderr, "%s: error: more than %d assets in one manifest\n", program->filename,
            program->asset_entry_capacity);
    ++program->error_count;
    return nullptr;
  }

  asset_entry_t* entry = &program->asset_entries[program->asset_entry_count++];
  *entry               = {};
  entry->offset        = -1;
  entry->line          = -1;
  return entry;
}

// ---------------------------------------------------------------------------
// Diagnostics
// ---------------------------------------------------------------------------

// Columns are only ever needed on the error path, so scanning back to the
// start of the line costs nothing in the common case.
static int32_t column_for_offset(const program_t* program, int32_t offset)
{
  int32_t column = 1;
  for (int32_t index = offset - 1; index >= 0 && program->source[index] != '\n'; --index)
    ++column;
  return column;
}

static void report_error(program_t* program, int32_t offset, int32_t line, const char* format, ...)
{
  ++program->error_count;

  fprintf(stderr, "%s:%d:%d: error: ", program->filename, line, column_for_offset(program, offset));

  va_list arguments;
  va_start(arguments, format);
  vfprintf(stderr, format, arguments);
  va_end(arguments);

  fputc('\n', stderr);
}

// ---------------------------------------------------------------------------
// Tokenizer
// ---------------------------------------------------------------------------

static token_t make_token(token_kind_t kind, int32_t offset, int32_t length, int32_t line)
{
  token_t token;
  token.kind   = kind;
  token.offset = offset;
  token.length = length;
  token.line   = line;
  return token;
}

static void tokenize(program_t* program)
{
  const char* source = program->source;
  int32_t     length = program->source_length;
  int32_t     cursor = 0;
  int32_t     line   = 1;

  while (cursor < length)
  {
    char    character = source[cursor];
    uint8_t classes   = CHAR_CLASSES.entries[(uint8_t)character];

    if (classes & CHAR_CLASS_WHITESPACE)
    {
      ++cursor;
      continue;
    }

    if (classes & CHAR_CLASS_NEWLINE)
    {
      ++cursor;
      ++line;
      continue;
    }

    if (character == '/' && cursor + 1 < length && source[cursor + 1] == '/')
    {
      while (cursor < length && source[cursor] != '\n')
        ++cursor;
      continue;
    }

    if (classes & CHAR_CLASS_IDENTIFIER_START)
    {
      int32_t start = cursor;
      while (cursor < length && character_has_class(source[cursor], CHAR_CLASS_IDENTIFIER_BODY))
        ++cursor;
      program->tokens[program->token_count++] =
          make_token(TOKEN_IDENTIFIER, start, cursor - start, line);
      continue;
    }

    bool starts_negative_number = character == '-' && cursor + 1 < length &&
                                  character_has_class(source[cursor + 1], CHAR_CLASS_DIGIT);
    if ((classes & CHAR_CLASS_DIGIT) || starts_negative_number)
    {
      int32_t start = cursor;
      if (source[cursor] == '-')
        ++cursor;
      while (cursor < length && character_has_class(source[cursor], CHAR_CLASS_DIGIT))
        ++cursor;
      if (cursor < length && source[cursor] == '.')
      {
        ++cursor;
        while (cursor < length && character_has_class(source[cursor], CHAR_CLASS_DIGIT))
          ++cursor;
      }
      program->tokens[program->token_count++] =
          make_token(TOKEN_NUMBER, start, cursor - start, line);
      continue;
    }

    if (character == '"')
    {
      int32_t start = cursor + 1;
      ++cursor;
      while (cursor < length && source[cursor] != '"' && source[cursor] != '\n')
        ++cursor;
      if (cursor >= length || source[cursor] != '"')
      {
        report_error(program, start - 1, line, "unterminated string literal");
        continue;
      }
      program->tokens[program->token_count++] =
          make_token(TOKEN_STRING_LITERAL, start, cursor - start, line);
      ++cursor;
      continue;
    }

    if (character == ':' && cursor + 1 < length && source[cursor + 1] == ':')
    {
      program->tokens[program->token_count++] = make_token(TOKEN_COLON_COLON, cursor, 2, line);
      cursor += 2;
      continue;
    }

    token_kind_t punctuation_kind = TOKEN_END_OF_FILE;
    switch (character)
    {
      case ':': punctuation_kind = TOKEN_COLON;         break;
      case '=': punctuation_kind = TOKEN_EQUALS;        break;
      case '@': punctuation_kind = TOKEN_AT;            break;
      case '.': punctuation_kind = TOKEN_DOT;           break;
      case ',': punctuation_kind = TOKEN_COMMA;         break;
      case '{': punctuation_kind = TOKEN_OPEN_BRACE;    break;
      case '}': punctuation_kind = TOKEN_CLOSE_BRACE;   break;
      case '[': punctuation_kind = TOKEN_OPEN_BRACKET;  break;
      case ']': punctuation_kind = TOKEN_CLOSE_BRACKET; break;
      case '<': punctuation_kind = TOKEN_LESS;          break;
      case '>': punctuation_kind = TOKEN_GREATER;       break;
      default:  break;
    }

    if (punctuation_kind == TOKEN_END_OF_FILE)
    {
      report_error(program, cursor, line, "unexpected character '%c'", character);
      ++cursor;
      continue;
    }

    program->tokens[program->token_count++] = make_token(punctuation_kind, cursor, 1, line);
    ++cursor;
  }

  program->tokens[program->token_count++] = make_token(TOKEN_END_OF_FILE, length, 0, line);
}

// ---------------------------------------------------------------------------
// Parser
// ---------------------------------------------------------------------------

struct parser_t
{
  program_t* program;
  int32_t    token_index;
  bool       panicking; // suppresses cascading errors until the next resync point
};

static string_view_t token_text(const program_t* program, token_t token)
{
  string_view_t view;
  view.data   = program->source + token.offset;
  view.length = token.length;
  return view;
}

static token_t peek(const parser_t* parser)
{
  return parser->program->tokens[parser->token_index];
}

static token_t peek_ahead(const parser_t* parser, int32_t distance)
{
  int32_t index = parser->token_index + distance;
  if (index >= parser->program->token_count)
    index = parser->program->token_count - 1;
  return parser->program->tokens[index];
}

static token_t advance(parser_t* parser)
{
  token_t token = peek(parser);
  if (token.kind != TOKEN_END_OF_FILE)
    ++parser->token_index;
  return token;
}

static bool check(const parser_t* parser, token_kind_t kind)
{
  return peek(parser).kind == kind;
}

static bool accept(parser_t* parser, token_kind_t kind)
{
  if (!check(parser, kind))
    return false;
  advance(parser);
  return true;
}

static void parse_error_at(parser_t* parser, token_t token, const char* format, ...)
{
  if (parser->panicking)
    return;
  parser->panicking = true;

  ++parser->program->error_count;

  fprintf(stderr, "%s:%d:%d: error: ", parser->program->filename, token.line,
          column_for_offset(parser->program, token.offset));

  va_list arguments;
  va_start(arguments, format);
  vfprintf(stderr, format, arguments);
  va_end(arguments);

  fputc('\n', stderr);
}

static void parse_error(parser_t* parser, const char* format, ...)
{
  if (parser->panicking)
    return;
  parser->panicking = true;

  token_t token = peek(parser);
  ++parser->program->error_count;

  fprintf(stderr, "%s:%d:%d: error: ", parser->program->filename, token.line,
          column_for_offset(parser->program, token.offset));

  va_list arguments;
  va_start(arguments, format);
  vfprintf(stderr, format, arguments);
  va_end(arguments);

  fputc('\n', stderr);
}

static bool expect(parser_t* parser, token_kind_t kind, const char* expectation)
{
  if (accept(parser, kind))
    return true;
  parse_error(parser, "expected %s, found %s", expectation, token_kind_name(peek(parser).kind));
  return false;
}

// A declaration always starts with IDENTIFIER '::' at brace depth zero, which
// makes it a reliable thing to resynchronize on.
static void synchronize_to_next_declaration(parser_t* parser)
{
  int32_t brace_depth = 0;
  while (!check(parser, TOKEN_END_OF_FILE))
  {
    if (brace_depth == 0 && check(parser, TOKEN_IDENTIFIER) &&
        peek_ahead(parser, 1).kind == TOKEN_COLON_COLON)
      break;

    token_t token = advance(parser);
    if (token.kind == TOKEN_OPEN_BRACE)
      ++brace_depth;
    else if (token.kind == TOKEN_CLOSE_BRACE && brace_depth > 0)
      --brace_depth;
  }
  parser->panicking = false;
}

// Fields are newline terminated, so skipping to the next line is the natural
// recovery inside a struct body.
static void synchronize_to_next_field(parser_t* parser)
{
  int32_t line = peek(parser).line;
  while (!check(parser, TOKEN_END_OF_FILE) && !check(parser, TOKEN_CLOSE_BRACE) &&
         peek(parser).line == line)
    advance(parser);
  parser->panicking = false;
}

static type_kind_t builtin_type_kind(string_view_t name)
{
  if (string_view_matches(name, "f32"))    return TYPE_F32;
  if (string_view_matches(name, "f64"))    return TYPE_F64;
  if (string_view_matches(name, "u8"))     return TYPE_U8;
  if (string_view_matches(name, "u16"))    return TYPE_U16;
  if (string_view_matches(name, "u32"))    return TYPE_U32;
  if (string_view_matches(name, "u64"))    return TYPE_U64;
  if (string_view_matches(name, "i8"))     return TYPE_I8;
  if (string_view_matches(name, "i16"))    return TYPE_I16;
  if (string_view_matches(name, "i32"))    return TYPE_I32;
  if (string_view_matches(name, "i64"))    return TYPE_I64;
  if (string_view_matches(name, "bool"))   return TYPE_BOOL;
  if (string_view_matches(name, "v3"))     return TYPE_V3;
  if (string_view_matches(name, "v4"))     return TYPE_V4;
  if (string_view_matches(name, "v4i"))    return TYPE_V4I;
  if (string_view_matches(name, "string")) return TYPE_STRING;

  // Asset classes are NOT builtin: `mesh_asset` and `sprite_asset` used to be
  // two magic identifiers baked in here, which meant the generator knew the
  // name of every asset class in the game. They are declared in the .def now
  // and resolve through the name table like an enum or a component.
  return TYPE_UNRESOLVED;
}

static bool parse_number(parser_t* parser, double* out_number)
{
  token_t token = peek(parser);
  if (!expect(parser, TOKEN_NUMBER, "a number"))
    return false;

  char    buffer[64];
  int32_t length = token.length < (int32_t)sizeof(buffer) - 1 ? token.length
                                                              : (int32_t)sizeof(buffer) - 1;
  memcpy(buffer, parser->program->source + token.offset, (size_t)length);
  buffer[length] = '\0';

  *out_number = strtod(buffer, nullptr);
  return true;
}

static bool parse_type(parser_t* parser, type_reference_t* out_type)
{
  if (check(parser, TOKEN_OPEN_BRACKET))
  {
    parse_error(parser, "fixed capacity arrays ('[N]T') are not supported");
    return false;
  }

  token_t name_token = peek(parser);
  if (!expect(parser, TOKEN_IDENTIFIER, "a type name"))
    return false;

  out_type->name              = token_text(parser->program, name_token);
  out_type->kind              = builtin_type_kind(out_type->name);
  out_type->capacity          = 0;
  out_type->declaration_index = -1;

  if (out_type->kind == TYPE_STRING)
  {
    if (!expect(parser, TOKEN_LESS, "'<' after 'string'"))
      return false;

    double capacity = 0.0;
    if (!parse_number(parser, &capacity))
      return false;

    if (!expect(parser, TOKEN_GREATER, "'>' to close the string capacity"))
      return false;

    if (capacity < 1.0 || capacity != (double)(int32_t)capacity)
    {
      parse_error(parser, "string capacity must be a positive whole number");
      return false;
    }
    // pascal_string_t is template <uint8 N>, and its length field is a uint8
    // written as 8 bits on the wire. A capacity above 255 is not merely too
    // large, it is unrepresentable: 256 wraps N to 0 and yields a 1-byte
    // buffer. Reject it here rather than emitting code that silently truncates
    // every value it is ever given.
    if (capacity > 255.0)
    {
      parse_error(parser,
                  "string capacity must be at most 255 (pascal_string_t stores "
                  "its length in a uint8); got %d",
                  (int32_t)capacity);
      return false;
    }
    out_type->capacity = (int32_t)capacity;
  }
  else if (check(parser, TOKEN_LESS))
  {
    parse_error(parser, "only 'string' takes a capacity, '%.*s' does not", out_type->name.length,
                out_type->name.data);
    return false;
  }

  return true;
}

static bool parse_default_value(parser_t* parser, default_value_t* out_value)
{
  *out_value = {};

  if (accept(parser, TOKEN_OPEN_BRACE))
  {
    out_value->kind = DEFAULT_VECTOR;
    while (true)
    {
      if (out_value->number_count >= 4)
      {
        parse_error(parser, "a vector default takes at most 4 components");
        return false;
      }
      if (!parse_number(parser, &out_value->numbers[out_value->number_count]))
        return false;
      ++out_value->number_count;

      if (!accept(parser, TOKEN_COMMA))
        break;
    }
    return expect(parser, TOKEN_CLOSE_BRACE, "'}' to close the default value");
  }

  if (accept(parser, TOKEN_DOT))
  {
    token_t value_token = peek(parser);
    if (!expect(parser, TOKEN_IDENTIFIER, "an enum value name after '.'"))
      return false;
    out_value->kind = DEFAULT_ENUM_LITERAL;
    out_value->text = token_text(parser->program, value_token);
    return true;
  }

  if (check(parser, TOKEN_NUMBER))
  {
    if (!parse_number(parser, &out_value->numbers[0]))
      return false;
    out_value->kind         = DEFAULT_NUMBER;
    out_value->number_count = 1;
    return true;
  }

  if (check(parser, TOKEN_STRING_LITERAL))
  {
    out_value->kind = DEFAULT_STRING;
    out_value->text = token_text(parser->program, advance(parser));
    return true;
  }

  if (check(parser, TOKEN_IDENTIFIER))
  {
    string_view_t text = token_text(parser->program, peek(parser));
    if (string_view_matches(text, "true") || string_view_matches(text, "false"))
    {
      advance(parser);
      out_value->kind    = DEFAULT_BOOL;
      out_value->boolean = string_view_matches(text, "true");
      return true;
    }
    parse_error(parser,
                "'%.*s' is not a default value; enum values are written '.%.*s'", text.length,
                text.data, text.length, text.data);
    return false;
  }

  parse_error(parser, "expected a default value after '='");
  return false;
}

// Annotations bind to the line they were written on, which is what makes a
// field newline terminated without a newline token.
static int32_t parse_annotations(parser_t* parser, int32_t line)
{
  int32_t count = 0;
  while (check(parser, TOKEN_AT) && peek(parser).line == line)
  {
    advance(parser);

    token_t name_token = peek(parser);
    if (!expect(parser, TOKEN_IDENTIFIER, "an annotation name after '@'"))
      break;

    annotation_t* annotation = push_annotation(parser->program);
    annotation->name         = token_text(parser->program, name_token);
    annotation->offset       = name_token.offset;
    annotation->line         = name_token.line;
    ++count;
  }
  return count;
}

static field_t* parse_field(parser_t* parser)
{
  token_t name_token = peek(parser);
  if (!expect(parser, TOKEN_IDENTIFIER, "a field name"))
    return nullptr;

  field_t* field = push_field(parser->program);
  field->name    = token_text(parser->program, name_token);
  field->offset  = name_token.offset;
  field->line    = name_token.line;

  if (!expect(parser, TOKEN_COLON, "':' after the field name"))
    return nullptr;

  if (!parse_type(parser, &field->type))
    return nullptr;

  if (accept(parser, TOKEN_EQUALS))
  {
    if (!parse_default_value(parser, &field->default_value))
      return nullptr;
  }

  field->annotation_count = parse_annotations(parser, name_token.line);
  return field;
}

static bool parse_struct_body(parser_t* parser, declaration_t* declaration)
{
  if (!expect(parser, TOKEN_OPEN_BRACE, "'{' to open the declaration body"))
    return false;

  declaration->first_field = parser->program->field_count;

  while (!check(parser, TOKEN_CLOSE_BRACE) && !check(parser, TOKEN_END_OF_FILE))
  {
    int32_t field_mark      = parser->program->field_count;
    int32_t annotation_mark = parser->program->annotation_count;

    if (parse_field(parser) == nullptr)
    {
      parser->program->field_count      = field_mark;
      parser->program->annotation_count = annotation_mark;
      synchronize_to_next_field(parser);
    }
  }

  declaration->field_count = parser->program->field_count - declaration->first_field;

  return expect(parser, TOKEN_CLOSE_BRACE, "'}' to close the declaration body");
}

static bool parse_enum_body(parser_t* parser, declaration_t* declaration)
{
  if (!expect(parser, TOKEN_OPEN_BRACE, "'{' to open the enum body"))
    return false;

  declaration->first_enum_value = parser->program->enum_value_count;

  while (!check(parser, TOKEN_CLOSE_BRACE) && !check(parser, TOKEN_END_OF_FILE))
  {
    token_t value_token = peek(parser);
    if (!expect(parser, TOKEN_IDENTIFIER, "an enum value name"))
      return false;

    string_view_t* value = push_enum_value(parser->program);
    *value               = token_text(parser->program, value_token);

    if (!accept(parser, TOKEN_COMMA))
      break;
  }

  declaration->enum_value_count = parser->program->enum_value_count - declaration->first_enum_value;

  if (declaration->enum_value_count == 0)
  {
    parse_error(parser, "an enum must declare at least one value");
    return false;
  }

  return expect(parser, TOKEN_CLOSE_BRACE, "'}' to close the enum body");
}

// An asset class body. Each entry is one line, in the same spirit as a field.
//
//   placeholder "resources/obj/error.obj"
//   scan        "resources/obj" ".obj"
//   procedural  Box "box"
//
// `scan` is recorded, not expanded: the filesystem is not touched until the
// resolve pass, so parsing stays a pure function of the .def text.
static bool parse_assets_body(parser_t* parser, declaration_t* declaration)
{
  if (!expect(parser, TOKEN_OPEN_BRACE, "'{' to open the asset class body"))
    return false;

  declaration->first_asset_entry = parser->program->asset_entry_count;

  while (!check(parser, TOKEN_CLOSE_BRACE) && !check(parser, TOKEN_END_OF_FILE))
  {
    token_t keyword_token = peek(parser);
    if (!expect(parser, TOKEN_IDENTIFIER, "'placeholder', 'scan' or 'procedural'"))
      return false;

    string_view_t keyword = token_text(parser->program, keyword_token);

    if (string_view_matches(keyword, "placeholder"))
    {
      if (declaration->placeholder_path != nullptr)
      {
        parse_error_at(parser, keyword_token, "'%.*s' already declares a placeholder",
                       declaration->name.length, declaration->name.data);
        return false;
      }

      token_t path_token = peek(parser);
      if (!expect(parser, TOKEN_STRING_LITERAL, "a path after 'placeholder'"))
        return false;

      string_view_t path = token_text(parser->program, path_token);
      declaration->placeholder_path = arena_copy(parser->program, path.data, path.length);
      if (declaration->placeholder_path == nullptr)
        return false;
      continue;
    }

    if (string_view_matches(keyword, "scan"))
    {
      if (declaration->scan_directory != nullptr)
      {
        parse_error_at(parser, keyword_token,
                       "'%.*s' already declares a scan directory; one per asset class",
                       declaration->name.length, declaration->name.data);
        return false;
      }

      token_t directory_token = peek(parser);
      if (!expect(parser, TOKEN_STRING_LITERAL, "a directory after 'scan'"))
        return false;

      token_t extension_token = peek(parser);
      if (!expect(parser, TOKEN_STRING_LITERAL, "a file extension after the scan directory"))
        return false;

      string_view_t directory = token_text(parser->program, directory_token);
      string_view_t extension = token_text(parser->program, extension_token);

      declaration->scan_directory = arena_copy(parser->program, directory.data, directory.length);
      declaration->scan_extension = arena_copy(parser->program, extension.data, extension.length);
      declaration->scan_offset    = keyword_token.offset;
      declaration->scan_line      = keyword_token.line;

      if (declaration->scan_directory == nullptr || declaration->scan_extension == nullptr)
        return false;
      continue;
    }

    if (string_view_matches(keyword, "procedural"))
    {
      token_t name_token = peek(parser);
      if (!expect(parser, TOKEN_IDENTIFIER, "a name after 'procedural'"))
        return false;

      token_t key_token = peek(parser);
      if (!expect(parser, TOKEN_STRING_LITERAL,
                  "the generator key, as a string, after the procedural name"))
        return false;

      string_view_t name = token_text(parser->program, name_token);
      string_view_t key  = token_text(parser->program, key_token);

      asset_entry_t* entry = push_asset_entry(parser->program);
      if (entry == nullptr)
        return false;

      entry->name        = arena_copy(parser->program, name.data, name.length);
      entry->source      = arena_copy(parser->program, key.data, key.length);
      entry->source_kind = ASSET_SOURCE_PROCEDURAL;
      entry->offset      = name_token.offset;
      entry->line        = name_token.line;

      if (entry->name == nullptr || entry->source == nullptr)
        return false;
      continue;
    }

    parse_error_at(parser, keyword_token,
                   "'%.*s' is not an asset entry; expected 'placeholder', 'scan' or 'procedural'",
                   keyword.length, keyword.data);
    return false;
  }

  declaration->asset_entry_count =
      parser->program->asset_entry_count - declaration->first_asset_entry;

  return expect(parser, TOKEN_CLOSE_BRACE, "'}' to close the asset class body");
}

static bool parse_flagset_body(parser_t* parser, declaration_t* declaration)
{
  declaration->kind = DECLARATION_FLAGSET;

  if (!expect(parser, TOKEN_OPEN_BRACKET, "'[' to open the flagset"))
    return false;

  declaration->first_annotation = parser->program->annotation_count;

  while (true)
  {
    if (!expect(parser, TOKEN_AT, "'@' before a flag name"))
      return false;

    token_t name_token = peek(parser);
    if (!expect(parser, TOKEN_IDENTIFIER, "a flag name"))
      return false;

    annotation_t* annotation = push_annotation(parser->program);
    annotation->name         = token_text(parser->program, name_token);
    annotation->offset       = name_token.offset;
    annotation->line         = name_token.line;

    if (!accept(parser, TOKEN_COMMA))
      break;
  }

  declaration->annotation_count = parser->program->annotation_count - declaration->first_annotation;

  return expect(parser, TOKEN_CLOSE_BRACKET, "']' to close the flagset");
}

static declaration_t* parse_declaration(parser_t* parser)
{
  program_mark_t mark = mark_program(parser->program);

  token_t name_token = peek(parser);
  if (!expect(parser, TOKEN_IDENTIFIER, "a declaration name"))
  {
    rewind_program(parser->program, mark);
    return nullptr;
  }

  if (!expect(parser, TOKEN_COLON_COLON, "'::' after the declaration name"))
  {
    rewind_program(parser->program, mark);
    return nullptr;
  }

  declaration_t* declaration = push_declaration(parser->program);
  declaration->name          = token_text(parser->program, name_token);
  declaration->offset        = name_token.offset;
  declaration->line          = name_token.line;

  if (check(parser, TOKEN_OPEN_BRACKET))
  {
    if (!parse_flagset_body(parser, declaration))
    {
      rewind_program(parser->program, mark);
      return nullptr;
    }
    return declaration;
  }

  token_t kind_token = peek(parser);
  if (!expect(parser, TOKEN_IDENTIFIER, "a declaration kind"))
  {
    rewind_program(parser->program, mark);
    return nullptr;
  }

  string_view_t kind_text = token_text(parser->program, kind_token);
  bool          parsed    = false;

  if (string_view_matches(kind_text, "enum"))
  {
    declaration->kind = DECLARATION_ENUM;
    parsed            = parse_enum_body(parser, declaration);
  }
  else if (string_view_matches(kind_text, "assets"))
  {
    declaration->kind = DECLARATION_ASSETS;
    parsed            = parse_assets_body(parser, declaration);
  }
  else if (string_view_matches(kind_text, "base") || string_view_matches(kind_text, "component") ||
           string_view_matches(kind_text, "entity"))
  {
    if (string_view_matches(kind_text, "base"))
      declaration->kind = DECLARATION_BASE;
    else if (string_view_matches(kind_text, "component"))
      declaration->kind = DECLARATION_COMPONENT;
    else
      declaration->kind = DECLARATION_ENTITY;

    declaration->first_annotation = parser->program->annotation_count;
    declaration->annotation_count = parse_annotations(parser, kind_token.line);

    parsed = parse_struct_body(parser, declaration);
  }
  else
  {
    parse_error_at(parser, kind_token,
                   "'%.*s' is not a declaration kind; expected 'base', 'component', "
                   "'entity', 'enum' or 'assets'",
                   kind_text.length, kind_text.data);
  }

  if (!parsed)
  {
    rewind_program(parser->program, mark);
    return nullptr;
  }

  return declaration;
}

static void parse_program(parser_t* parser)
{
  while (!check(parser, TOKEN_END_OF_FILE))
  {
    if (parse_declaration(parser) == nullptr)
      synchronize_to_next_declaration(parser);
  }
}

// ---------------------------------------------------------------------------
// Name table
// ---------------------------------------------------------------------------

// Open addressing, power of two capacity, FNV-1a. Slots hold declaration index
// + 1 so that zero means empty.
struct name_table_t
{
  int32_t* slots;
  int32_t  capacity;
};

static uint32_t hash_name(string_view_t name)
{
  uint32_t hash = 2166136261u;
  for (int32_t index = 0; index < name.length; ++index)
  {
    hash ^= (uint8_t)name.data[index];
    hash *= 16777619u;
  }
  return hash;
}

static void build_name_table(name_table_t* table, const program_t* program)
{
  table->capacity = 16;
  while (table->capacity < program->declaration_count * 2)
    table->capacity *= 2;

  table->slots = (int32_t*)calloc((size_t)table->capacity, sizeof(int32_t));

  for (int32_t index = 0; index < program->declaration_count; ++index)
  {
    uint32_t slot = hash_name(program->declarations[index].name) & (uint32_t)(table->capacity - 1);
    while (table->slots[slot] != 0)
      slot = (slot + 1) & (uint32_t)(table->capacity - 1);
    table->slots[slot] = index + 1;
  }
}

static int32_t find_declaration(const name_table_t* table, const program_t* program,
                                string_view_t name)
{
  uint32_t slot = hash_name(name) & (uint32_t)(table->capacity - 1);
  while (table->slots[slot] != 0)
  {
    int32_t index = table->slots[slot] - 1;
    if (string_views_match(program->declarations[index].name, name))
      return index;
    slot = (slot + 1) & (uint32_t)(table->capacity - 1);
  }
  return -1;
}

// ---------------------------------------------------------------------------
// Resolve pass
// ---------------------------------------------------------------------------
//
// Runs after the whole file is parsed so that declaration order in the .def
// file is irrelevant: a field may use a component or a flagset declared below
// it.

static uint32_t builtin_field_flag(string_view_t name)
{
  if (string_view_matches(name, "Networked")) return FIELD_FLAG_NETWORKED;
  if (string_view_matches(name, "Editable"))  return FIELD_FLAG_EDITABLE;
  if (string_view_matches(name, "Saveable"))  return FIELD_FLAG_SAVEABLE;
  return FIELD_FLAG_NONE;
}

static void resolve_flagsets(program_t* program)
{
  for (int32_t index = 0; index < program->declaration_count; ++index)
  {
    declaration_t* declaration = &program->declarations[index];
    if (declaration->kind != DECLARATION_FLAGSET)
      continue;

    uint32_t mask = FIELD_FLAG_NONE;
    for (int32_t offset = 0; offset < declaration->annotation_count; ++offset)
    {
      const annotation_t* annotation = &program->annotations[declaration->first_annotation + offset];

      uint32_t flag = builtin_field_flag(annotation->name);
      if (flag == FIELD_FLAG_NONE)
      {
        report_error(program, annotation->offset, annotation->line,
                     "'@%.*s' is not a field flag; a flagset may only contain @Networked, "
                     "@Editable or @Saveable",
                     annotation->name.length, annotation->name.data);
        continue;
      }
      mask |= flag;
    }
    declaration->class_flags = mask;
  }
}

static void resolve_field_flags(program_t* program, const name_table_t* table)
{
  for (int32_t index = 0; index < program->field_count; ++index)
  {
    field_t* field = &program->fields[index];
    uint32_t flags = FIELD_FLAG_NONE;

    for (int32_t offset = 0; offset < field->annotation_count; ++offset)
    {
      const annotation_t* annotation = &program->annotations[field->first_annotation + offset];

      uint32_t builtin = builtin_field_flag(annotation->name);
      if (builtin != FIELD_FLAG_NONE)
      {
        flags |= builtin;
        continue;
      }

      int32_t flagset_index = find_declaration(table, program, annotation->name);
      if (flagset_index >= 0 &&
          program->declarations[flagset_index].kind == DECLARATION_FLAGSET)
      {
        flags |= program->declarations[flagset_index].class_flags;
        continue;
      }

      report_error(program, annotation->offset, annotation->line, "unknown annotation '@%.*s'",
                   annotation->name.length, annotation->name.data);
    }

    field->flags = flags;
  }
}

static void resolve_class_annotations(program_t* program)
{
  for (int32_t index = 0; index < program->declaration_count; ++index)
  {
    declaration_t* declaration = &program->declarations[index];
    if (declaration->kind == DECLARATION_FLAGSET)
      continue;

    for (int32_t offset = 0; offset < declaration->annotation_count; ++offset)
    {
      const annotation_t* annotation = &program->annotations[declaration->first_annotation + offset];

      if (!string_view_matches(annotation->name, "runtime_only"))
      {
        report_error(program, annotation->offset, annotation->line,
                     "'@%.*s' is not a class annotation; only '@runtime_only' is",
                     annotation->name.length, annotation->name.data);
        continue;
      }

      if (declaration->kind != DECLARATION_ENTITY)
      {
        report_error(program, annotation->offset, annotation->line,
                     "'@runtime_only' may only be applied to an entity, but '%.*s' is a %s",
                     declaration->name.length, declaration->name.data,
                     declaration_kind_name(declaration->kind));
        continue;
      }

      declaration->class_flags |= CLASS_FLAG_RUNTIME_ONLY;
    }
  }
}

static void resolve_types(program_t* program, const name_table_t* table)
{
  for (int32_t index = 0; index < program->declaration_count; ++index)
  {
    const declaration_t* declaration = &program->declarations[index];

    for (int32_t offset = 0; offset < declaration->field_count; ++offset)
    {
      field_t* field = &program->fields[declaration->first_field + offset];

      if (field->type.kind != TYPE_UNRESOLVED)
        continue;

      int32_t target = find_declaration(table, program, field->type.name);
      if (target < 0)
      {
        report_error(program, field->offset, field->line, "unknown type '%.*s' on field '%.*s'",
                     field->type.name.length, field->type.name.data, field->name.length,
                     field->name.data);
        continue;
      }

      const declaration_t* target_declaration = &program->declarations[target];
      switch (target_declaration->kind)
      {
        case DECLARATION_ENUM:
          field->type.kind              = TYPE_ENUM;
          field->type.declaration_index = target;
          break;

        case DECLARATION_ASSETS:
          field->type.kind              = TYPE_ASSET;
          field->type.declaration_index = target;
          break;

        case DECLARATION_COMPONENT:
          field->type.kind              = TYPE_COMPONENT;
          field->type.declaration_index = target;
          if (field->default_value.kind != DEFAULT_NONE)
            report_error(program, field->offset, field->line,
                         "field '%.*s' may not have a default value; the defaults declared "
                         "inside component '%.*s' are used instead",
                         field->name.length, field->name.data, target_declaration->name.length,
                         target_declaration->name.data);
          break;

        default:
          report_error(program, field->offset, field->line,
                       "field '%.*s' may not have type '%.*s'; a %s cannot be used as a field type",
                       field->name.length, field->name.data, field->type.name.length,
                       field->type.name.data, declaration_kind_name(target_declaration->kind));
          break;
      }
    }
  }
}

static void check_duplicate_declarations(program_t* program)
{
  for (int32_t index = 0; index < program->declaration_count; ++index)
  {
    for (int32_t other = 0; other < index; ++other)
    {
      if (!string_views_match(program->declarations[index].name, program->declarations[other].name))
        continue;

      report_error(program, program->declarations[index].offset, program->declarations[index].line,
                   "'%.*s' is already declared on line %d",
                   program->declarations[index].name.length, program->declarations[index].name.data,
                   program->declarations[other].line);
      break;
    }
  }
}

static void check_duplicate_fields(program_t* program)
{
  for (int32_t index = 0; index < program->declaration_count; ++index)
  {
    const declaration_t* declaration = &program->declarations[index];

    for (int32_t offset = 0; offset < declaration->field_count; ++offset)
    {
      const field_t* field = &program->fields[declaration->first_field + offset];

      for (int32_t earlier = 0; earlier < offset; ++earlier)
      {
        const field_t* other = &program->fields[declaration->first_field + earlier];
        if (!string_views_match(field->name, other->name))
          continue;

        report_error(program, field->offset, field->line, "'%.*s' already has a field named '%.*s' on line %d",
                     declaration->name.length, declaration->name.data, field->name.length,
                     field->name.data, other->line);
        break;
      }
    }
  }
}

static void check_base_declaration(program_t* program)
{
  int32_t base_count = 0;
  for (int32_t index = 0; index < program->declaration_count; ++index)
  {
    if (program->declarations[index].kind != DECLARATION_BASE)
      continue;

    ++base_count;
    if (base_count > 1)
      report_error(program, program->declarations[index].offset, program->declarations[index].line,
                   "a second 'base' declaration ('%.*s'); exactly one is allowed",
                   program->declarations[index].name.length,
                   program->declarations[index].name.data);
  }

  if (base_count == 0)
    fprintf(stderr, "%s: warning: no 'base' declaration; entities will have no shared prefix\n",
            program->filename);
}

enum visit_state_t : uint8_t
{
  VISIT_UNVISITED = 0,
  VISIT_IN_PROGRESS,
  VISIT_DONE,
};

// Components hold each other by value, so a cycle would be an infinitely large
// struct. Report it rather than letting codegen recurse forever.
static bool visit_component(program_t* program, int32_t index, visit_state_t* states)
{
  if (states[index] == VISIT_DONE)
    return true;

  if (states[index] == VISIT_IN_PROGRESS)
  {
    report_error(program, program->declarations[index].offset, program->declarations[index].line,
                 "component '%.*s' contains itself, directly or through another component",
                 program->declarations[index].name.length, program->declarations[index].name.data);
    return false;
  }

  states[index] = VISIT_IN_PROGRESS;

  const declaration_t* declaration = &program->declarations[index];
  bool                 acyclic     = true;

  for (int32_t offset = 0; offset < declaration->field_count; ++offset)
  {
    const field_t* field = &program->fields[declaration->first_field + offset];
    if (field->type.kind != TYPE_COMPONENT || field->type.declaration_index < 0)
      continue;
    if (!visit_component(program, field->type.declaration_index, states))
      acyclic = false;
  }

  states[index] = VISIT_DONE;
  return acyclic;
}

static void check_component_cycles(program_t* program)
{
  visit_state_t* states =
      (visit_state_t*)calloc((size_t)program->declaration_count, sizeof(visit_state_t));

  for (int32_t index = 0; index < program->declaration_count; ++index)
  {
    if (program->declarations[index].kind == DECLARATION_COMPONENT)
      visit_component(program, index, states);
  }

  free(states);
}

// ---------------------------------------------------------------------------
// Asset manifest expansion
// ---------------------------------------------------------------------------
//
// The only pass that touches the filesystem. It turns each `assets`
// declaration into its final, ordered entry list:
//
//   slot 0    Missing        -- so a zeroed field reads as "no asset assigned"
//   then      scanned files  -- alphabetical, for a deterministic build
//   then      procedural     -- declaration order
//
// Ids are therefore NOT stable across adding a file to a scanned directory.
// That is safe because names are the on-disk identity (a map file stores
// "Cube", never 3), and because the resolved manifest is mixed into
// SCHEMA_HASH -- two builds with different asset sets refuse to connect to
// each other loudly instead of silently disagreeing about what id 3 means.

// "cube" -> "Cube", "smoke_puff" -> "Smoke_Puff". Anything that cannot become a
// C++ identifier is an error rather than a silent mangling: the file is in a
// scanned directory, so somebody meant it to be an asset.
static bool derive_asset_name(program_t* program, const declaration_t* declaration,
                              const char* stem, char* buffer, int32_t buffer_size)
{
  int32_t length = (int32_t)strlen(stem);
  if (length == 0 || length >= buffer_size)
  {
    report_error(program, declaration->scan_offset, declaration->scan_line,
                 "asset file name '%s' is empty or too long to become an identifier", stem);
    return false;
  }

  if (character_has_class(stem[0], CHAR_CLASS_DIGIT))
  {
    report_error(program, declaration->scan_offset, declaration->scan_line,
                 "asset file name '%s' starts with a digit, so it cannot become an identifier; "
                 "rename the file",
                 stem);
    return false;
  }

  bool capitalize_next = true;
  for (int32_t index = 0; index < length; ++index)
  {
    char character = stem[index];

    if (character == '-' || character == '_')
    {
      buffer[index]   = '_';
      capitalize_next = true;
      continue;
    }

    if (!character_has_class(character, CHAR_CLASS_IDENTIFIER_BODY))
    {
      report_error(program, declaration->scan_offset, declaration->scan_line,
                   "asset file name '%s' contains '%c', which cannot appear in an identifier; "
                   "rename the file",
                   stem, character);
      return false;
    }

    if (capitalize_next && character >= 'a' && character <= 'z')
      character = (char)(character - 'a' + 'A');
    buffer[index]   = character;
    capitalize_next = false;
  }

  buffer[length] = '\0';
  return true;
}

static int compare_scanned_names(const void* left, const void* right)
{
  return strcmp(*(const char* const*)left, *(const char* const*)right);
}

static void expand_asset_manifests(program_t* program)
{
  int32_t has_any_asset_class = 0;
  for (int32_t index = 0; index < program->declaration_count; ++index)
    has_any_asset_class += program->declarations[index].kind == DECLARATION_ASSETS ? 1 : 0;
  if (has_any_asset_class == 0)
    return;

  asset_entry_t* combined =
      (asset_entry_t*)malloc((size_t)program->asset_entry_capacity * sizeof(asset_entry_t));
  int32_t combined_count = 0;

  for (int32_t index = 0; index < program->declaration_count; ++index)
  {
    declaration_t* declaration = &program->declarations[index];
    if (declaration->kind != DECLARATION_ASSETS)
      continue;

    int32_t class_first = combined_count;

    // --- slot 0: Missing ---
    //
    // A real domain state ("this field names no asset"), not a decorative
    // sentinel: an unset mesh must render the placeholder, which is loudly
    // wrong, rather than whichever asset happened to sort first, which would
    // look plausible and hide the bug.
    {
      asset_entry_t entry = {};
      entry.name          = "Missing";
      entry.offset        = declaration->offset;
      entry.line          = declaration->line;
      if (declaration->placeholder_path != nullptr)
      {
        entry.source      = declaration->placeholder_path;
        entry.source_kind = ASSET_SOURCE_FILE;
      }
      else
      {
        entry.source      = "";
        entry.source_kind = ASSET_SOURCE_MISSING;
      }
      combined[combined_count++] = entry;
    }

    // --- scanned files ---
    if (declaration->scan_directory != nullptr)
    {
      std::filesystem::path directory =
          std::filesystem::path(program->asset_root) / declaration->scan_directory;

      std::error_code                     error_code;
      std::filesystem::directory_iterator iterator(directory, error_code);
      if (error_code)
      {
        report_error(program, declaration->scan_offset, declaration->scan_line,
                     "cannot scan asset directory '%s': %s",
                     directory.string().c_str(), error_code.message().c_str());
      }
      else
      {
        // Collected and sorted before anything is emitted: directory_iterator
        // order is unspecified, and an unspecified order would make asset ids
        // differ between two builds of the same tree.
        const char** filenames    = (const char**)malloc(4096 * sizeof(const char*));
        int32_t      filename_count = 0;

        for (const std::filesystem::directory_entry& file : iterator)
        {
          if (!file.is_regular_file())
            continue;

          std::string extension = file.path().extension().string();
          if (extension.size() != strlen(declaration->scan_extension))
            continue;
#if defined(_WIN32)
          if (_stricmp(extension.c_str(), declaration->scan_extension) != 0)
            continue;
#else
          if (strcasecmp(extension.c_str(), declaration->scan_extension) != 0)
            continue;
#endif

          if (filename_count >= 4096)
          {
            report_error(program, declaration->scan_offset, declaration->scan_line,
                         "more than 4096 assets in '%s'", directory.string().c_str());
            break;
          }

          std::string filename = file.path().filename().string();
          char*       copy     = arena_copy(program, filename.c_str(), (int32_t)filename.size());
          if (copy == nullptr)
            break;
          filenames[filename_count++] = copy;
        }

        qsort(filenames, (size_t)filename_count, sizeof(const char*), compare_scanned_names);

        for (int32_t which = 0; which < filename_count; ++which)
        {
          const char* filename = filenames[which];

          char    stem[512];
          int32_t stem_length = (int32_t)strlen(filename) - (int32_t)strlen(declaration->scan_extension);
          if (stem_length <= 0 || stem_length >= (int32_t)sizeof(stem))
            continue;
          memcpy(stem, filename, (size_t)stem_length);
          stem[stem_length] = '\0';

          char derived[512];
          if (!derive_asset_name(program, declaration, stem, derived, (int32_t)sizeof(derived)))
            continue;

          char path[1024];
          snprintf(path, sizeof(path), "%s/%s", declaration->scan_directory, filename);

          // The placeholder file is already slot 0 (Missing). Scanning it again
          // would give one file two ids under two names, which is precisely the
          // "two spellings of one concept" the collision check exists to stop --
          // it just happens to be a collision the generator itself created, so
          // the check could never fire on it. Skip it instead.
          if (declaration->placeholder_path != nullptr &&
              strcmp(path, declaration->placeholder_path) == 0)
            continue;

          asset_entry_t entry = {};
          entry.name          = arena_copy(program, derived, (int32_t)strlen(derived));
          entry.source        = arena_copy(program, path, (int32_t)strlen(path));
          entry.source_kind   = ASSET_SOURCE_FILE;
          entry.offset        = declaration->scan_offset;
          entry.line          = declaration->scan_line;

          if (entry.name == nullptr || entry.source == nullptr)
            break;
          combined[combined_count++] = entry;
        }

        free(filenames);
      }
    }

    // --- declared procedural entries, in declaration order ---
    for (int32_t which = 0; which < declaration->asset_entry_count; ++which)
      combined[combined_count++] = program->asset_entries[declaration->first_asset_entry + which];

    declaration->first_asset_entry = class_first;
    declaration->asset_entry_count = combined_count - class_first;

    // --- names must be unique within the class ---
    //
    // This is where a scanned file and a procedural entry claiming the same
    // concept collide, and the collision is information: two spellings of one
    // thing that would otherwise diverge silently.
    for (int32_t left = class_first; left < combined_count; ++left)
    {
      for (int32_t right = class_first; right < left; ++right)
      {
        if (strcmp(combined[left].name, combined[right].name) != 0)
          continue;

        report_error(program, combined[left].offset, combined[left].line,
                     "asset class '%.*s' has two entries named '%s' (%s and %s); rename one",
                     declaration->name.length, declaration->name.data, combined[left].name,
                     combined[right].source_kind == ASSET_SOURCE_PROCEDURAL ? "a procedural entry"
                                                                            : combined[right].source,
                     combined[left].source_kind == ASSET_SOURCE_PROCEDURAL ? "a procedural entry"
                                                                           : combined[left].source);
        break;
      }
    }
  }

  memcpy(program->asset_entries, combined, (size_t)combined_count * sizeof(asset_entry_t));
  program->asset_entry_count = combined_count;
  free(combined);
}

// The three flags become REAL at the cutover -- in the macro system only
// @Editable was ever enforced, so a flag that cannot mean anything was free to
// sit there and read as if it did. These two checks close the cases where a
// flag is not merely unused but self-contradictory, so that "a flag is here"
// and "this flag has an effect" are the same statement.
//
// Both are errors rather than warnings on purpose: each one is a claim about
// the field that is false, and a false claim in the .def is what the whole
// generator exists to stop being possible.
static void check_flag_contradictions(program_t* program)
{
  for (int32_t index = 0; index < program->declaration_count; ++index)
  {
    const declaration_t* declaration = &program->declarations[index];
    if (declaration->kind != DECLARATION_ENTITY && declaration->kind != DECLARATION_COMPONENT &&
        declaration->kind != DECLARATION_BASE)
      continue;

    const bool runtime_only = (declaration->class_flags & CLASS_FLAG_RUNTIME_ONLY) != 0;

    for (int32_t offset = 0; offset < declaration->field_count; ++offset)
    {
      const field_t* field = &program->fields[declaration->first_field + offset];

      // A component-typed field carries no flags of its own: the component's
      // own field flags are the truth, and a recursive walk reads those. A flag
      // written here would be silently ignored by every consumer -- which is
      // how `volume: Box_Volume @Editable` came to sit next to four unflagged
      // component fields meaning exactly the same thing.
      if (field->type.kind == TYPE_COMPONENT && field->flags != FIELD_FLAG_NONE)
      {
        report_error(program, field->offset, field->line,
                     "field '%.*s' is a component, so it cannot carry flags: the flags on "
                     "'%.*s''s own fields are what every consumer reads. Move them there, or "
                     "drop them here",
                     field->name.length, field->name.data, field->type.name.length,
                     field->type.name.data);
        continue;
      }

      // @runtime_only says the type is never placed in the editor and never
      // written to a map. @Editable (the editor inspector only ever sees map
      // entities) and @Saveable (map I/O only ever visits map entities) are
      // therefore unreachable on such a field, not merely unused.
      if (runtime_only && (field->flags & (FIELD_FLAG_EDITABLE | FIELD_FLAG_SAVEABLE)) != 0)
      {
        report_error(program, field->offset, field->line,
                     "field '%.*s' is %s%s%s, but '%.*s' is @runtime_only -- it is never placed "
                     "in the editor and never written to a map, so neither flag can ever be "
                     "read. Drop the flag, or drop @runtime_only",
                     field->name.length, field->name.data,
                     (field->flags & FIELD_FLAG_EDITABLE) ? "@Editable" : "",
                     (field->flags & (FIELD_FLAG_EDITABLE | FIELD_FLAG_SAVEABLE)) ==
                             (FIELD_FLAG_EDITABLE | FIELD_FLAG_SAVEABLE)
                         ? " and "
                         : "",
                     (field->flags & FIELD_FLAG_SAVEABLE) ? "@Saveable" : "",
                     declaration->name.length, declaration->name.data);
      }
    }
  }
}

// `.Something` defaults name a value in a closed set, and the set is only known
// once assets are expanded. Catching a bad name here beats emitting
// `mesh_asset::Erorr` and making the C++ compiler explain it against generated
// code the author never wrote.
static void check_literal_defaults(program_t* program)
{
  for (int32_t index = 0; index < program->field_count; ++index)
  {
    const field_t* field = &program->fields[index];

    if (field->default_value.kind != DEFAULT_ENUM_LITERAL || field->type.declaration_index < 0)
      continue;

    const declaration_t* target = &program->declarations[field->type.declaration_index];
    string_view_t        wanted = field->default_value.text;
    bool                 found  = false;

    if (field->type.kind == TYPE_ENUM)
    {
      for (int32_t which = 0; which < target->enum_value_count && !found; ++which)
        found = string_views_match(program->enum_values[target->first_enum_value + which], wanted);
    }
    else if (field->type.kind == TYPE_ASSET)
    {
      for (int32_t which = 0; which < target->asset_entry_count && !found; ++which)
      {
        const char* name = program->asset_entries[target->first_asset_entry + which].name;
        found = string_view_matches(wanted, name);
      }
    }
    else
    {
      report_error(program, field->offset, field->line,
                   "field '%.*s' has a '.%.*s' default, but its type '%.*s' is not an enum or an "
                   "asset class",
                   field->name.length, field->name.data, wanted.length, wanted.data,
                   field->type.name.length, field->type.name.data);
      continue;
    }

    if (!found)
      report_error(program, field->offset, field->line,
                   "'%.*s' is not a value of '%.*s', so field '%.*s' cannot default to it",
                   wanted.length, wanted.data, target->name.length, target->name.data,
                   field->name.length, field->name.data);
  }
}

static void resolve_program(program_t* program)
{
  name_table_t table = {};
  build_name_table(&table, program);

  check_duplicate_declarations(program);
  check_duplicate_fields(program);
  check_base_declaration(program);

  resolve_flagsets(program);
  resolve_field_flags(program, &table);
  resolve_class_annotations(program);
  resolve_types(program, &table);
  check_component_cycles(program);
  check_flag_contradictions(program);
  expand_asset_manifests(program);
  check_literal_defaults(program);

  free(table.slots);
}

// ---------------------------------------------------------------------------
// Code generation
// ---------------------------------------------------------------------------
//
// Emits the end state directly: plain standard-layout structs with no
// virtuals, no base class and no Class_Schema registration. Reflection is
// constexpr tables, so there is nothing to register at startup and no static
// initializer to be dropped by the linker.
//
// Not emitted yet, deliberately: wire serializers (that is serializer v2, and
// it wants the detection/encoding seam designed first) and the asset manifest
// (no scanner yet, so asset ids are a placeholder typedef).

static bool has_asset_class(const program_t* program)
{
  for (int32_t index = 0; index < program->declaration_count; ++index)
  {
    if (program->declarations[index].kind == DECLARATION_ASSETS)
      return true;
  }
  return false;
}

static int32_t find_base_declaration(const program_t* program)
{
  for (int32_t index = 0; index < program->declaration_count; ++index)
  {
    if (program->declarations[index].kind == DECLARATION_BASE)
      return index;
  }
  return -1;
}

// Component ids are assigned only to components, in declaration order. The
// returned array is indexed by declaration index and holds -1 elsewhere.
static int32_t* build_component_ids(const program_t* program, int32_t* out_component_count)
{
  int32_t* ids = (int32_t*)malloc((size_t)program->declaration_count * sizeof(int32_t));
  int32_t  next = 0;

  for (int32_t index = 0; index < program->declaration_count; ++index)
  {
    if (program->declarations[index].kind == DECLARATION_COMPONENT)
      ids[index] = next++;
    else
      ids[index] = -1;
  }

  *out_component_count = next;
  return ids;
}

// Same shape, for asset classes. A field records which class it draws from so
// that a generic consumer (the editor inspector) can offer the right closed set
// without being told the class by name.
static int32_t* build_asset_class_ids(const program_t* program, int32_t* out_class_count)
{
  int32_t* ids  = (int32_t*)malloc((size_t)program->declaration_count * sizeof(int32_t));
  int32_t  next = 0;

  for (int32_t index = 0; index < program->declaration_count; ++index)
  {
    if (program->declarations[index].kind == DECLARATION_ASSETS)
      ids[index] = next++;
    else
      ids[index] = -1;
  }

  *out_class_count = next;
  return ids;
}

// Same shape again, for enums. A field records which enum it draws from so a
// generic consumer can turn its value into text (map save) and back (map load)
// without knowing the enum by name -- the one thing the type column alone
// cannot tell it.
static int32_t* build_enum_ids(const program_t* program, int32_t* out_enum_count)
{
  int32_t* ids  = (int32_t*)malloc((size_t)program->declaration_count * sizeof(int32_t));
  int32_t  next = 0;

  for (int32_t index = 0; index < program->declaration_count; ++index)
  {
    if (program->declarations[index].kind == DECLARATION_ENUM)
      ids[index] = next++;
    else
      ids[index] = -1;
  }

  *out_enum_count = next;
  return ids;
}

static void write_cpp_type(FILE* out, const type_reference_t* type)
{
  switch (type->kind)
  {
    case TYPE_F32:  fprintf(out, "float");    return;
    case TYPE_F64:  fprintf(out, "double");   return;
    case TYPE_U8:   fprintf(out, "uint8_t");  return;
    case TYPE_U16:  fprintf(out, "uint16_t"); return;
    case TYPE_U32:  fprintf(out, "uint32_t"); return;
    case TYPE_U64:  fprintf(out, "uint64_t"); return;
    case TYPE_I8:   fprintf(out, "int8_t");   return;
    case TYPE_I16:  fprintf(out, "int16_t");  return;
    case TYPE_I32:  fprintf(out, "int32_t");  return;
    case TYPE_I64:  fprintf(out, "int64_t");  return;
    case TYPE_BOOL: fprintf(out, "bool");     return;
    case TYPE_V3:   fprintf(out, "linalg::vec3f"); return;
    case TYPE_V4:   fprintf(out, "linalg::vec4f"); return;
    case TYPE_V4I:  fprintf(out, "linalg::vec4i"); return;

    case TYPE_STRING:
      fprintf(out, "network::pascal_string_t<%d>", type->capacity);
      return;

    case TYPE_ASSET:
    case TYPE_ENUM:
    case TYPE_COMPONENT:
      fprintf(out, "%.*s", type->name.length, type->name.data);
      return;

    case TYPE_UNRESOLVED:
      break;
  }

  // resolve_program rejects unresolved types, so reaching here is a generator bug.
  assert(false && "unresolved type reached code generation");
  fprintf(out, "/* unresolved */ void");
}

static const char* field_type_enum_name(type_kind_t kind)
{
  switch (kind)
  {
    case TYPE_F32:       return "FIELD_TYPE_F32";
    case TYPE_F64:       return "FIELD_TYPE_F64";
    case TYPE_U8:        return "FIELD_TYPE_U8";
    case TYPE_U16:       return "FIELD_TYPE_U16";
    case TYPE_U32:       return "FIELD_TYPE_U32";
    case TYPE_U64:       return "FIELD_TYPE_U64";
    case TYPE_I8:        return "FIELD_TYPE_I8";
    case TYPE_I16:       return "FIELD_TYPE_I16";
    case TYPE_I32:       return "FIELD_TYPE_I32";
    case TYPE_I64:       return "FIELD_TYPE_I64";
    case TYPE_BOOL:      return "FIELD_TYPE_BOOL";
    case TYPE_V3:        return "FIELD_TYPE_V3";
    case TYPE_V4:        return "FIELD_TYPE_V4";
    case TYPE_V4I:       return "FIELD_TYPE_V4I";
    case TYPE_STRING:    return "FIELD_TYPE_STRING";
    case TYPE_ASSET:     return "FIELD_TYPE_ASSET";
    case TYPE_ENUM:      return "FIELD_TYPE_ENUM";
    case TYPE_COMPONENT: return "FIELD_TYPE_COMPONENT";
    case TYPE_UNRESOLVED: break;
  }
  return "FIELD_TYPE_INVALID";
}

// Display names are derived, never declared: strip a trailing "_Entity", then
// turn underscores into spaces. "Trigger_Volume_Entity" -> "Trigger Volume".
static void write_display_name(FILE* out, string_view_t name)
{
  int32_t length = name.length;

  const char* suffix        = "_Entity";
  int32_t     suffix_length = 7;
  if (length > suffix_length &&
      memcmp(name.data + length - suffix_length, suffix, (size_t)suffix_length) == 0)
    length -= suffix_length;

  for (int32_t index = 0; index < length; ++index)
    fputc(name.data[index] == '_' ? ' ' : name.data[index], out);
}

// "16" is not a float literal, so a bare %g would emit `16f` and fail to
// compile. Force a decimal point whenever the shortest representation has none.
static void write_float_literal(FILE* out, double value, bool is_float32)
{
  char buffer[64];
  snprintf(buffer, sizeof(buffer), "%.*g", is_float32 ? 9 : 17, value);

  bool already_floating = strpbrk(buffer, ".eEnN") != nullptr; // nan / inf count
  fprintf(out, "%s%s%s", buffer, already_floating ? "" : ".0", is_float32 ? "f" : "");
}

static bool type_has_integer_components(type_kind_t kind)
{
  return kind == TYPE_V4I;
}

static void write_default_initializer(FILE* out, const field_t* field)
{
  const default_value_t* value = &field->default_value;

  if (value->kind == DEFAULT_NONE)
  {
    fprintf(out, "{}");
    return;
  }

  switch (value->kind)
  {
    case DEFAULT_NUMBER:
      switch (field->type.kind)
      {
        case TYPE_F32: write_float_literal(out, value->numbers[0], true);  return;
        case TYPE_F64: write_float_literal(out, value->numbers[0], false); return;
        default:       fprintf(out, "%lld", (long long)value->numbers[0]); return;
      }

    case DEFAULT_VECTOR:
      fprintf(out, "{");
      for (int32_t index = 0; index < value->number_count; ++index)
      {
        if (index > 0)
          fprintf(out, ", ");
        if (type_has_integer_components(field->type.kind))
          fprintf(out, "%lld", (long long)value->numbers[index]);
        else
          write_float_literal(out, value->numbers[index], true);
      }
      fprintf(out, "}");
      return;

    case DEFAULT_ENUM_LITERAL:
      fprintf(out, "%.*s::%.*s", field->type.name.length, field->type.name.data,
              value->text.length, value->text.data);
      return;

    case DEFAULT_BOOL:
      fprintf(out, "%s", value->boolean ? "true" : "false");
      return;

    case DEFAULT_STRING:
      fprintf(out, "\"%.*s\"", value->text.length, value->text.data);
      return;

    case DEFAULT_NONE:
      break;
  }

  fprintf(out, "{}");
}

// The classname is the on-disk identity, derived from the declared name rather
// than declared separately: Light_Entity -> "light_entity".
static void write_classname(FILE* out, string_view_t name)
{
  for (int32_t index = 0; index < name.length; ++index)
  {
    char character = name.data[index];
    if (character >= 'A' && character <= 'Z')
      character = (char)(character - 'A' + 'a');
    fputc(character, out);
  }
}

static void write_field_members(FILE* out, const program_t* program,
                                const declaration_t* declaration)
{
  for (int32_t offset = 0; offset < declaration->field_count; ++offset)
  {
    const field_t* field = &program->fields[declaration->first_field + offset];

    fprintf(out, "  ");
    write_cpp_type(out, &field->type);
    fprintf(out, " %.*s = ", field->name.length, field->name.data);
    write_default_initializer(out, field);
    fprintf(out, ";\n");
  }
}

// Components may hold other components by value, so a component must be
// emitted after everything it contains. Declaration order does not guarantee
// that, hence the post-order walk. Cycles are already rejected by resolve.
static void emit_component_struct(FILE* out, const program_t* program, int32_t index,
                                  visit_state_t* states)
{
  if (states[index] != VISIT_UNVISITED)
    return;
  states[index] = VISIT_IN_PROGRESS;

  const declaration_t* declaration = &program->declarations[index];

  for (int32_t offset = 0; offset < declaration->field_count; ++offset)
  {
    const field_t* field = &program->fields[declaration->first_field + offset];
    if (field->type.kind == TYPE_COMPONENT && field->type.declaration_index >= 0)
      emit_component_struct(out, program, field->type.declaration_index, states);
  }

  fprintf(out, "struct %.*s\n{\n", declaration->name.length, declaration->name.data);
  write_field_members(out, program, declaration);
  fprintf(out, "};\n\n");

  states[index] = VISIT_DONE;
}

static void emit_generated_header(FILE* out, const program_t* program)
{
  int32_t base_index      = find_base_declaration(program);
  int32_t component_count = 0;
  int32_t* component_ids  = build_component_ids(program, &component_count);

  fprintf(out, "// Generated from %s by entity_gen. Do not edit.\n", program->filename);
  fprintf(out, "#pragma once\n\n");
  // Paths are relative to src/shared, which is game_shared's public include dir.
  fprintf(out, "#include \"linalg.hpp\"\n");
  fprintf(out, "#include \"network/network_types.hpp\"\n");
  fprintf(out, "#include \"span.hpp\"\n");
  fprintf(out, "#include <cstdint>\n\n");
  fprintf(out, "namespace entities\n{\n\n");

  // --- asset classes ---
  //
  // An asset id names an asset; it does NOT say where the bytes come from. That
  // is deliberately absent from this API: a file-backed mesh and a procedurally
  // generated one are the same kind of thing to every consumer, and the one
  // place the difference exists is the manifest table below, which the asset
  // system reads at init and nobody else reads at all.
  for (int32_t index = 0; index < program->declaration_count; ++index)
  {
    const declaration_t* declaration = &program->declarations[index];
    if (declaration->kind != DECLARATION_ASSETS)
      continue;

    fprintf(out, "// Missing is 0: an asset field that was never assigned resolves to the\n");
    fprintf(out, "// placeholder, which is loudly wrong, rather than to whichever asset\n");
    fprintf(out, "// happened to sort first, which would look plausible.\n");
    fprintf(out, "enum class %.*s : uint16_t\n{\n", declaration->name.length,
            declaration->name.data);
    for (int32_t which = 0; which < declaration->asset_entry_count; ++which)
    {
      const asset_entry_t* entry = &program->asset_entries[declaration->first_asset_entry + which];
      fprintf(out, "  %s = %d,\n", entry->name, which);
    }
    fprintf(out, "};\n\n");

    fprintf(out, "constexpr uint32_t %.*s_COUNT = %d;\n\n", declaration->name.length,
            declaration->name.data, declaration->asset_entry_count);

    fprintf(out, "const char* to_string(%.*s value);\n", declaration->name.length,
            declaration->name.data);
    fprintf(out, "bool from_string(const char* text, %.*s* out_value);\n\n",
            declaration->name.length, declaration->name.data);
  }

  if (has_asset_class(program))
  {
    fprintf(out, "// Where an asset's bytes come from. This exists for the asset system's\n");
    fprintf(out, "// init and for nothing else -- if you are reaching for it anywhere\n");
    fprintf(out, "// else, the code wants an asset id, not a source.\n");
    fprintf(out, "enum asset_source_kind_t : uint8_t\n{\n");
    fprintf(out, "  ASSET_SOURCE_MISSING = 0, // no asset assigned; `source` is empty\n");
    fprintf(out, "  ASSET_SOURCE_FILE,        // `source` is a path, relative to the working dir\n");
    fprintf(out, "  ASSET_SOURCE_PROCEDURAL,  // `source` is a generator key\n");
    fprintf(out, "};\n\n");

    fprintf(out, "struct asset_info_t\n{\n");
    fprintf(out, "  const char*         name;\n");
    fprintf(out, "  const char*         source;\n");
    fprintf(out, "  asset_source_kind_t source_kind;\n");
    fprintf(out, "};\n\n");

    for (int32_t index = 0; index < program->declaration_count; ++index)
    {
      const declaration_t* declaration = &program->declarations[index];
      if (declaration->kind != DECLARATION_ASSETS)
        continue;

      fprintf(out, "// The complete %.*s manifest, indexed by id. Populate every entry at\n",
              declaration->name.length, declaration->name.data);
      fprintf(out, "// init: registration must NOT be lazy, or an id resolves to nothing\n");
      fprintf(out, "// depending on what ran first.\n");
      fprintf(out, "Span<const asset_info_t> %.*s_manifest();\n\n",
              declaration->name.length, declaration->name.data);
    }

    // The manifest reached by the id a field carries rather than by the class's
    // name. This is what makes an asset field convertible to and from text by a
    // walker that only has a field_info_t: entry `index` of the returned span is
    // the asset whose numeric value is `index`.
    fprintf(out, "// The manifest a field_info_t::asset_class_id refers to. Empty span for\n");
    fprintf(out, "// an id no asset class owns, which is a caller bug -- check the column\n");
    fprintf(out, "// is not -1 before calling.\n");
    fprintf(out, "Span<const asset_info_t> asset_class_manifest(int32_t asset_class_id);\n\n");
  }

  // --- enums ---
  for (int32_t index = 0; index < program->declaration_count; ++index)
  {
    const declaration_t* declaration = &program->declarations[index];
    if (declaration->kind != DECLARATION_ENUM)
      continue;

    fprintf(out, "enum class %.*s : uint8_t\n{\n", declaration->name.length,
            declaration->name.data);
    for (int32_t offset = 0; offset < declaration->enum_value_count; ++offset)
    {
      const string_view_t* value = &program->enum_values[declaration->first_enum_value + offset];
      fprintf(out, "  %.*s = %d,\n", value->length, value->data, offset);
    }
    fprintf(out, "};\n\n");

    fprintf(out, "const char* to_string(%.*s value);\n", declaration->name.length,
            declaration->name.data);
    fprintf(out, "bool from_string(const char* text, %.*s* out_value);\n\n",
            declaration->name.length, declaration->name.data);
  }

  // --- enum tag + reflection ---
  //
  // The typed to_string/from_string pairs above are what hand-written code
  // calls. This table is for the walkers, which hold a field_info_t and a
  // pointer and know neither enum by name. Value N is names[N], so the two
  // directions are an index and a compare.
  {
    int32_t enum_count = 0;
    int32_t* enum_ids  = build_enum_ids(program, &enum_count);

    fprintf(out, "enum class enum_type : uint16_t\n{\n");
    for (int32_t index = 0; index < program->declaration_count; ++index)
    {
      if (enum_ids[index] < 0)
        continue;
      const declaration_t* declaration = &program->declarations[index];
      fprintf(out, "  %.*s = %d,\n", declaration->name.length, declaration->name.data,
              enum_ids[index]);
    }
    fprintf(out, "};\n\n");
    fprintf(out, "constexpr uint32_t ENUM_TYPE_COUNT = %d;\n\n", enum_count);

    fprintf(out, "struct enum_type_info_t\n{\n");
    fprintf(out, "  const char*                 name;\n");
    fprintf(out, "  // Indexed by the enum's own numeric value; the values are dense and\n");
    fprintf(out, "  // start at 0, so `size()` is also the count of valid values.\n");
    fprintf(out, "  Span<const char* const>     value_names;\n");
    fprintf(out, "};\n\n");

    fprintf(out, "const enum_type_info_t& enum_info(enum_type type);\n\n");

    free(enum_ids);
  }

  // --- entity tag ---
  //
  // Sentinel policy, applied to every enum this generator emits:
  //
  //   Count is NEVER a member. A count inside the enum forces every exhaustive
  //   switch to carry a `case Count:` that can never occur, and adding that case
  //   is what silences -Wswitch -- the warning P5's lifecycle hooks rely on to
  //   catch a genuinely forgotten entity type. It is emitted as a sibling
  //   constant instead, so iteration still works and exhaustiveness still warns.
  //
  //   Invalid = 0 appears only where "none/unknown" is a real domain state:
  //   the two tag enums, where zeroed memory must not read as a valid tag.
  //   Domain enums (Light_Type, Fire_Mode, ...) do not get one -- an
  //   Light_Type::Invalid would be a state every consumer has to answer for and
  //   no light can ever be in.
  fprintf(out, "// Invalid is 0 so that zeroed memory never looks like a valid entity.\n");
  fprintf(out, "enum class entity_type : uint16_t\n{\n  Invalid = 0,\n");
  {
    int32_t next = 1;
    for (int32_t index = 0; index < program->declaration_count; ++index)
    {
      const declaration_t* declaration = &program->declarations[index];
      if (declaration->kind != DECLARATION_ENTITY)
        continue;
      fprintf(out, "  %.*s = %d,\n", declaration->name.length, declaration->name.data, next++);
    }
    fprintf(out, "};\n\n");
    fprintf(out, "// Not a member of the enum above, so `switch` over an\n");
    fprintf(out, "// entity_type still warns on an unhandled case.\n");
    fprintf(out, "constexpr uint32_t ENTITY_TYPE_COUNT = %d;\n\n", next);
  }

  // --- component tag ---
  fprintf(out, "enum class component_type : uint16_t\n{\n");
  for (int32_t index = 0; index < program->declaration_count; ++index)
  {
    if (component_ids[index] < 0)
      continue;
    const declaration_t* declaration = &program->declarations[index];
    fprintf(out, "  %.*s = %d,\n", declaration->name.length, declaration->name.data,
            component_ids[index]);
  }
  fprintf(out, "};\n\n");
  fprintf(out, "constexpr uint32_t COMPONENT_TYPE_COUNT = %d;\n\n", component_count);

  // --- components ---
  {
    visit_state_t* states =
        (visit_state_t*)calloc((size_t)program->declaration_count, sizeof(visit_state_t));
    for (int32_t index = 0; index < program->declaration_count; ++index)
    {
      if (program->declarations[index].kind == DECLARATION_COMPONENT)
        emit_component_struct(out, program, index, states);
    }
    free(states);
  }

  // --- the base struct every entity derives from ---
  //
  // Named after the `base` declaration in the .def -- never a name invented
  // here, so a rename happens in the .def where every other fact lives.
  //
  // Inheritance rather than a flat prefix so that casting an entity to the base
  // is a standard-blessed derived-to-base conversion instead of a
  // strict-aliasing violation. The price is that entities are not
  // standard-layout, so the offsetof in the generated tables is formally UB --
  // the same UB the old schema.hpp already pragma-suppressed, and it is
  // suppressed the same way.
  string_view_t base_name = {};
  if (base_index >= 0)
  {
    const declaration_t* base = &program->declarations[base_index];
    base_name                 = base->name;

    fprintf(out, "struct %.*s\n{\n", base_name.length, base_name.data);
    fprintf(out, "  // Set by each derived type's constructor. entity_as<T> compares it.\n");
    fprintf(out, "  entity_type type = entity_type::Invalid;\n\n");
    write_field_members(out, program, base);
    fprintf(out, "};\n\n");
  }

  // --- entities ---
  for (int32_t index = 0; index < program->declaration_count; ++index)
  {
    const declaration_t* declaration = &program->declarations[index];
    if (declaration->kind != DECLARATION_ENTITY)
      continue;

    if (base_index >= 0)
      fprintf(out, "struct %.*s : %.*s\n{\n", declaration->name.length, declaration->name.data,
              base_name.length, base_name.data);
    else
      fprintf(out, "struct %.*s\n{\n", declaration->name.length, declaration->name.data);

    if (base_index >= 0)
    {
      // The compile-time half of the tag. entity_as<T> and entities_of_type<T>
      // compare it against the runtime `type` member, which is what replaced
      // dynamic_cast: one integer compare, no RTTI walk.
      fprintf(out, "  static constexpr entity_type static_type = entity_type::%.*s;\n\n",
              declaration->name.length, declaration->name.data);
      fprintf(out, "  %.*s() { type = entity_type::%.*s; }\n\n", declaration->name.length,
              declaration->name.data, declaration->name.length, declaration->name.data);
    }

    write_field_members(out, program, declaration);
    fprintf(out, "};\n\n");
  }

  // --- reflection record types ---
  fprintf(out, "enum field_type_t : uint8_t\n{\n");
  fprintf(out, "  FIELD_TYPE_INVALID = 0,\n");
  fprintf(out, "  FIELD_TYPE_F32, FIELD_TYPE_F64,\n");
  fprintf(out, "  FIELD_TYPE_U8, FIELD_TYPE_U16, FIELD_TYPE_U32, FIELD_TYPE_U64,\n");
  fprintf(out, "  FIELD_TYPE_I8, FIELD_TYPE_I16, FIELD_TYPE_I32, FIELD_TYPE_I64,\n");
  fprintf(out, "  FIELD_TYPE_BOOL,\n");
  fprintf(out, "  FIELD_TYPE_V3, FIELD_TYPE_V4, FIELD_TYPE_V4I,\n");
  fprintf(out, "  FIELD_TYPE_STRING, FIELD_TYPE_ASSET, FIELD_TYPE_ENUM, FIELD_TYPE_COMPONENT,\n");
  fprintf(out, "};\n\n");

  fprintf(out, "enum field_flags_t : uint32_t\n{\n");
  fprintf(out, "  FIELD_FLAG_NONE      = 0,\n");
  fprintf(out, "  FIELD_FLAG_NETWORKED = 1 << 0,\n");
  fprintf(out, "  FIELD_FLAG_EDITABLE  = 1 << 1,\n");
  fprintf(out, "  FIELD_FLAG_SAVEABLE  = 1 << 2,\n");
  fprintf(out, "};\n\n");

  // The schema is a tree, but the memory it describes is flat, and the one
  // thing a reader has to know is how those two meet: offsets are relative to
  // the struct the field was declared in, so a recursive walk composes them by
  // addition. Said here rather than left to be rediscovered per consumer.
  fprintf(out, "// A field of one struct. Offsets are relative to THAT struct, so walking\n");
  fprintf(out, "// into a component composes them:\n");
  fprintf(out, "//\n");
  fprintf(out, "//   for (field : entity_info(type).fields)\n");
  fprintf(out, "//     if (field.type == FIELD_TYPE_COMPONENT)\n");
  fprintf(out, "//       for (inner : component_info((component_type)field.component_id).fields)\n");
  fprintf(out, "//         byte_offset = field.offset + inner.offset;\n");
  fprintf(out, "//\n");
  fprintf(out, "// A component-typed field's own size_in_bytes spans the whole nested\n");
  fprintf(out, "// struct, so a consumer that does NOT care about the inside (undo's\n");
  fprintf(out, "// memcmp diffing, a whole-struct copy) can treat it as one opaque blob\n");
  fprintf(out, "// and never recurse at all.\n");
  fprintf(out, "struct field_info_t\n{\n");
  fprintf(out, "  const char*  name;\n");
  fprintf(out, "  field_type_t type;\n");
  fprintf(out, "  uint32_t     offset;\n");
  fprintf(out, "  uint32_t     size_in_bytes;\n");
  fprintf(out, "  uint32_t     flags;\n");
  fprintf(out, "  int32_t      component_id;    // FIELD_TYPE_COMPONENT only, else -1\n");
  fprintf(out, "  uint32_t     string_capacity; // FIELD_TYPE_STRING only, else 0\n");
  fprintf(out, "  int32_t      asset_class_id;  // FIELD_TYPE_ASSET only, else -1\n");
  fprintf(out, "  int32_t      enum_id;         // FIELD_TYPE_ENUM only, else -1\n");
  fprintf(out, "};\n\n");

  // Entities all derive from the base, so a single pointer type covers every
  // factory return. Without a base declaration there is no common type and the
  // type-erased hooks degrade to void*.
  char entity_pointer_type[128];
  if (base_index >= 0)
    snprintf(entity_pointer_type, sizeof(entity_pointer_type), "%.*s*", base_name.length,
             base_name.data);
  else
    snprintf(entity_pointer_type, sizeof(entity_pointer_type), "void*");

  fprintf(out, "struct entity_type_info_t\n{\n");
  fprintf(out, "  const char*         classname;\n");
  fprintf(out, "  const char*         display_name;\n");
  fprintf(out, "  Span<const field_info_t> fields;\n");
  fprintf(out, "  uint32_t            size_in_bytes;\n");
  fprintf(out, "  uint32_t            alignment;\n");
  fprintf(out, "  uint32_t            component_mask;\n");
  fprintf(out, "  bool                runtime_only;\n\n");
  fprintf(out, "  // Writes a default constructed entity of this type into `memory`, which\n");
  fprintf(out, "  // must be at least size_in_bytes wide and `alignment` aligned. Allocates\n");
  fprintf(out, "  // nothing -- this is the type-erased hook for callers that already own\n");
  fprintf(out, "  // their storage: undo snapshots, network baselines, pooled storage.\n");
  fprintf(out, "  %s (*construct_at)(void* memory);\n", entity_pointer_type);
  fprintf(out, "};\n\n");

  fprintf(out, "struct component_type_info_t\n{\n");
  fprintf(out, "  const char*         name;\n");
  fprintf(out, "  Span<const field_info_t> fields;\n");
  fprintf(out, "  uint32_t            size_in_bytes;\n");
  fprintf(out, "};\n\n");

  fprintf(out, "// The tables are an implementation detail of the generated TU. Everything\n");
  fprintf(out, "// callers need goes through these free functions, which assert on bad tags.\n");
  fprintf(out, "const entity_type_info_t&    entity_info(entity_type type);\n");
  fprintf(out, "const component_type_info_t& component_info(component_type component);\n");
  fprintf(out, "entity_type                  entity_type_from_classname(const char* classname);\n");
  fprintf(out, "bool has_component(entity_type type, component_type component);\n");
  fprintf(out, "int32_t component_byte_offset(entity_type type, component_type component);\n\n");

  // --- construction ---
  //
  // Heap factories over the same generated switch entity_info dispatches
  // through. They exist because the editor and the map loader both want an
  // instance, not a tag, and neither knows the concrete type at compile time.
  fprintf(out, "// Heap factory. Asserts on entity_type::Invalid -- reaching it with an\n");
  fprintf(out, "// invalid tag is a caller bug, not a data error.\n");
  fprintf(out, "%s create_entity(entity_type type);\n\n", entity_pointer_type);

  fprintf(out, "// The map loader's entry point: classname off disk to a live instance.\n");
  fprintf(out, "// Returns nullptr for an unknown classname, which IS a data error -- the\n");
  fprintf(out, "// caller must report it rather than skipping the entity quietly.\n");
  fprintf(out, "%s entity_from_classname(const char* classname);\n\n", entity_pointer_type);

  fprintf(out, "// The counterpart to create_entity. Entities have no virtual destructor\n");
  fprintf(out, "// (they have no virtuals at all), so `delete` through a base pointer is\n");
  fprintf(out, "// wrong; this recovers the concrete type from the tag first. Null safe.\n");
  fprintf(out, "void destroy_entity(%s entity);\n\n", entity_pointer_type);

  // --- placeable types ---
  fprintf(out, "// Every entity type the editor may place: the ones the .def did NOT mark\n");
  fprintf(out, "// @runtime_only, in declaration order. Contiguous and stable, so a\n");
  fprintf(out, "// placement menu can index it directly.\n");
  fprintf(out, "Span<const entity_type> placeable_entity_types();\n\n");

  fprintf(out, "// Digest of every declaration in the .def. Exchanged at connect; a\n");
  fprintf(out, "// mismatch means the two sides disagree about the entity layout.\n");
  fprintf(out, "extern const uint32_t SCHEMA_HASH;\n\n");

  fprintf(out, "} // namespace entities\n");

  free(component_ids);
}

static void emit_field_table(FILE* out, const program_t* program, const declaration_t* owner,
                             const declaration_t* base, const int32_t* component_ids,
                             const int32_t* asset_class_ids, const int32_t* enum_ids,
                             const char* struct_name_prefix, int32_t struct_name_length)
{
  // The base fields are physically part of every entity struct, so they belong
  // in the entity's own table with real offsets rather than a separate list.
  for (int32_t pass = 0; pass < 2; ++pass)
  {
    const declaration_t* source = pass == 0 ? base : owner;
    if (source == nullptr)
      continue;

    for (int32_t offset = 0; offset < source->field_count; ++offset)
    {
      const field_t* field = &program->fields[source->first_field + offset];

      int32_t component_id = -1;
      if (field->type.kind == TYPE_COMPONENT && field->type.declaration_index >= 0)
        component_id = component_ids[field->type.declaration_index];

      int32_t asset_class_id = -1;
      if (field->type.kind == TYPE_ASSET && field->type.declaration_index >= 0)
        asset_class_id = asset_class_ids[field->type.declaration_index];

      int32_t enum_id = -1;
      if (field->type.kind == TYPE_ENUM && field->type.declaration_index >= 0)
        enum_id = enum_ids[field->type.declaration_index];

      fprintf(out, "  {\"%.*s\", %s, ", field->name.length, field->name.data,
              field_type_enum_name(field->type.kind));
      fprintf(out, "(uint32_t)offsetof(%.*s, %.*s), ", struct_name_length, struct_name_prefix,
              field->name.length, field->name.data);
      fprintf(out, "(uint32_t)sizeof(%.*s::%.*s), ", struct_name_length, struct_name_prefix,
              field->name.length, field->name.data);
      fprintf(out, "%uu, %d, %u, %d, %d},\n", field->flags, component_id,
              (uint32_t)field->type.capacity, asset_class_id, enum_id);
    }
  }
}

static uint32_t compute_schema_hash(const program_t* program)
{
  uint32_t hash = 2166136261u;

  auto mix = [&hash](const char* bytes, int32_t length) {
    for (int32_t index = 0; index < length; ++index)
    {
      hash ^= (uint8_t)bytes[index];
      hash *= 16777619u;
    }
  };

  for (int32_t index = 0; index < program->declaration_count; ++index)
  {
    const declaration_t* declaration = &program->declarations[index];

    mix(declaration->name.data, declaration->name.length);
    mix(declaration_kind_name(declaration->kind),
        (int32_t)strlen(declaration_kind_name(declaration->kind)));

    for (int32_t offset = 0; offset < declaration->enum_value_count; ++offset)
    {
      const string_view_t* value = &program->enum_values[declaration->first_enum_value + offset];
      mix(value->data, value->length);
    }

    // The RESOLVED asset manifest, not just the declaration. Asset ids come
    // from what is on disk, so two builds of the same .def with different
    // asset directories disagree about what id 3 means. Mixing the expanded
    // list in makes that a loud hash mismatch at connect instead of a silent
    // wrong mesh. This is the reason ids are allowed to be unstable at all.
    for (int32_t offset = 0; offset < declaration->asset_entry_count; ++offset)
    {
      const asset_entry_t* entry = &program->asset_entries[declaration->first_asset_entry + offset];
      mix(entry->name, (int32_t)strlen(entry->name));
      mix(entry->source, (int32_t)strlen(entry->source));

      char buffer[8];
      int  written = snprintf(buffer, sizeof(buffer), "%u", (unsigned)entry->source_kind);
      mix(buffer, written);
    }

    for (int32_t offset = 0; offset < declaration->field_count; ++offset)
    {
      const field_t* field = &program->fields[declaration->first_field + offset];
      mix(field->name.data, field->name.length);
      mix(field->type.name.data, field->type.name.length);

      char buffer[32];
      int  written = snprintf(buffer, sizeof(buffer), "%u:%d", field->flags, field->type.capacity);
      mix(buffer, written);
    }
  }

  return hash;
}

static void emit_generated_source(FILE* out, const program_t* program, const char* header_name)
{
  int32_t  base_index       = find_base_declaration(program);
  int32_t  component_count  = 0;
  int32_t* component_ids    = build_component_ids(program, &component_count);
  int32_t  asset_class_count = 0;
  int32_t* asset_class_ids   = build_asset_class_ids(program, &asset_class_count);
  int32_t  enum_count        = 0;
  int32_t* enum_ids          = build_enum_ids(program, &enum_count);

  const declaration_t* base = base_index >= 0 ? &program->declarations[base_index] : nullptr;

  char entity_pointer_type[128];
  if (base != nullptr)
    snprintf(entity_pointer_type, sizeof(entity_pointer_type), "%.*s*", base->name.length,
             base->name.data);
  else
    snprintf(entity_pointer_type, sizeof(entity_pointer_type), "void*");

  fprintf(out, "// Generated from %s by entity_gen. Do not edit.\n", program->filename);
  fprintf(out, "#include \"%s\"\n", header_name);
  fprintf(out, "#include <cassert>\n#include <cstddef>\n#include <cstring>\n#include <new>\n\n");

  // Entities derive from the base struct and both halves carry data, so they
  // are not standard-layout and offsetof is conditionally-supported. Every
  // compiler lays single non-virtual inheritance out predictably; the warning
  // is suppressed here rather than at each of the hundreds of table entries.
  fprintf(out, "#if defined(__clang__) || defined(__GNUC__)\n");
  fprintf(out, "#pragma GCC diagnostic ignored \"-Winvalid-offsetof\"\n");
  fprintf(out, "#elif defined(_MSC_VER)\n");
  fprintf(out, "#pragma warning(disable : 4841)\n");
  fprintf(out, "#endif\n\n");

  fprintf(out, "namespace entities\n{\n\nnamespace\n{\n\n");

  // --- per component field tables ---
  for (int32_t index = 0; index < program->declaration_count; ++index)
  {
    if (component_ids[index] < 0)
      continue;
    const declaration_t* declaration = &program->declarations[index];

    fprintf(out, "constexpr field_info_t %.*s_FIELDS[] = {\n", declaration->name.length,
            declaration->name.data);
    emit_field_table(out, program, declaration, nullptr, component_ids, asset_class_ids, enum_ids,
                     declaration->name.data, declaration->name.length);
    fprintf(out, "};\n\n");
  }

  // --- per entity field tables ---
  for (int32_t index = 0; index < program->declaration_count; ++index)
  {
    const declaration_t* declaration = &program->declarations[index];
    if (declaration->kind != DECLARATION_ENTITY)
      continue;

    fprintf(out, "constexpr field_info_t %.*s_FIELDS[] = {\n", declaration->name.length,
            declaration->name.data);
    emit_field_table(out, program, declaration, base, component_ids, asset_class_ids, enum_ids,
                     declaration->name.data, declaration->name.length);
    fprintf(out, "};\n\n");
  }

  // --- component info table ---
  fprintf(out, "constexpr component_type_info_t COMPONENT_INFOS[] = {\n");
  for (int32_t index = 0; index < program->declaration_count; ++index)
  {
    if (component_ids[index] < 0)
      continue;
    const declaration_t* declaration = &program->declarations[index];
    fprintf(out, "  {\"%.*s\", {%.*s_FIELDS, %d}, (uint32_t)sizeof(%.*s)},\n",
            declaration->name.length, declaration->name.data, declaration->name.length,
            declaration->name.data, declaration->field_count, declaration->name.length,
            declaration->name.data);
  }
  fprintf(out, "};\n\n");

  // --- placement-new thunks, one per entity type ---
  //
  // These are what entity_type_info_t::construct_at points at. A thunk rather
  // than a template so the table can be plain data: the address of a function
  // is a constant expression, so ENTITY_INFOS stays constexpr and there is
  // still no static initializer to run.
  for (int32_t index = 0; index < program->declaration_count; ++index)
  {
    const declaration_t* declaration = &program->declarations[index];
    if (declaration->kind != DECLARATION_ENTITY)
      continue;

    fprintf(out, "%s construct_%.*s(void* memory) { return new (memory) %.*s(); }\n",
            entity_pointer_type, declaration->name.length, declaration->name.data,
            declaration->name.length, declaration->name.data);
  }
  fprintf(out, "\n");

  // --- entity info table, indexed by tag, slot 0 is Invalid ---
  fprintf(out, "constexpr entity_type_info_t ENTITY_INFOS[] = {\n");
  fprintf(out, "  {\"\", \"\", {}, 0, 0, 0, false, nullptr}, // Invalid\n");
  for (int32_t index = 0; index < program->declaration_count; ++index)
  {
    const declaration_t* declaration = &program->declarations[index];
    if (declaration->kind != DECLARATION_ENTITY)
      continue;

    uint32_t component_mask = 0;
    for (int32_t offset = 0; offset < declaration->field_count; ++offset)
    {
      const field_t* field = &program->fields[declaration->first_field + offset];
      if (field->type.kind == TYPE_COMPONENT && field->type.declaration_index >= 0)
        component_mask |= 1u << component_ids[field->type.declaration_index];
    }

    int32_t total_field_count = declaration->field_count + (base != nullptr ? base->field_count : 0);

    fprintf(out, "  {\"");
    write_classname(out, declaration->name);
    fprintf(out, "\", \"");
    write_display_name(out, declaration->name);
    fprintf(out,
            "\", {%.*s_FIELDS, %d}, (uint32_t)sizeof(%.*s), (uint32_t)alignof(%.*s), %uu, %s, "
            "construct_%.*s},\n",
            declaration->name.length, declaration->name.data, total_field_count,
            declaration->name.length, declaration->name.data, declaration->name.length,
            declaration->name.data, component_mask,
            (declaration->class_flags & CLASS_FLAG_RUNTIME_ONLY) ? "true" : "false",
            declaration->name.length, declaration->name.data);
  }
  fprintf(out, "};\n\n");

  // --- component byte offsets, -1 where the entity lacks the component ---
  fprintf(out, "constexpr int32_t COMPONENT_OFFSETS[][%d] = {\n",
          component_count > 0 ? component_count : 1);
  fprintf(out, "  {");
  for (int32_t id = 0; id < (component_count > 0 ? component_count : 1); ++id)
    fprintf(out, "%s-1", id == 0 ? "" : ", ");
  fprintf(out, "}, // Invalid\n");

  for (int32_t index = 0; index < program->declaration_count; ++index)
  {
    const declaration_t* declaration = &program->declarations[index];
    if (declaration->kind != DECLARATION_ENTITY)
      continue;

    fprintf(out, "  {");
    for (int32_t id = 0; id < (component_count > 0 ? component_count : 1); ++id)
    {
      const field_t* found = nullptr;
      for (int32_t offset = 0; offset < declaration->field_count; ++offset)
      {
        const field_t* field = &program->fields[declaration->first_field + offset];
        if (field->type.kind == TYPE_COMPONENT && field->type.declaration_index >= 0 &&
            component_ids[field->type.declaration_index] == id)
        {
          found = field;
          break;
        }
      }

      if (id > 0)
        fprintf(out, ", ");
      if (found == nullptr)
        fprintf(out, "-1");
      else
        fprintf(out, "(int32_t)offsetof(%.*s, %.*s)", declaration->name.length,
                declaration->name.data, found->name.length, found->name.data);
    }
    fprintf(out, "}, // %.*s\n", declaration->name.length, declaration->name.data);
  }
  fprintf(out, "};\n\n");

  // --- placeable types: the filtered, indexable list the editor menu wants ---
  {
    int32_t placeable_count = 0;
    for (int32_t index = 0; index < program->declaration_count; ++index)
    {
      const declaration_t* declaration = &program->declarations[index];
      if (declaration->kind == DECLARATION_ENTITY &&
          (declaration->class_flags & CLASS_FLAG_RUNTIME_ONLY) == 0)
        ++placeable_count;
    }

    fprintf(out, "constexpr uint32_t PLACEABLE_ENTITY_TYPE_COUNT = %d;\n", placeable_count);

    if (placeable_count == 0)
    {
      // A zero length array is ill formed, and the accessor must still have
      // something to return. The count above is what callers loop on.
      fprintf(out, "// Every entity is @runtime_only, so nothing is placeable. The array\n");
      fprintf(out, "// exists only because C++ has no zero length arrays.\n");
      fprintf(out, "constexpr entity_type PLACEABLE_ENTITY_TYPES[1] = {entity_type::Invalid};\n\n");
    }
    else
    {
      fprintf(out, "constexpr entity_type PLACEABLE_ENTITY_TYPES[] = {\n");
      for (int32_t index = 0; index < program->declaration_count; ++index)
      {
        const declaration_t* declaration = &program->declarations[index];
        if (declaration->kind != DECLARATION_ENTITY ||
            (declaration->class_flags & CLASS_FLAG_RUNTIME_ONLY) != 0)
          continue;
        fprintf(out, "  entity_type::%.*s,\n", declaration->name.length, declaration->name.data);
      }
      fprintf(out, "};\n\n");
    }
  }

  // --- asset manifests ---
  //
  // The source column lives here and only here. Every other consumer of an
  // asset id goes through the enum and never learns whether the bytes came off
  // disk or out of a generator.
  for (int32_t index = 0; index < program->declaration_count; ++index)
  {
    const declaration_t* declaration = &program->declarations[index];
    if (declaration->kind != DECLARATION_ASSETS)
      continue;

    fprintf(out, "constexpr asset_info_t %.*s_MANIFEST[] = {\n", declaration->name.length,
            declaration->name.data);
    for (int32_t which = 0; which < declaration->asset_entry_count; ++which)
    {
      const asset_entry_t* entry = &program->asset_entries[declaration->first_asset_entry + which];

      const char* kind_name = "ASSET_SOURCE_MISSING";
      if (entry->source_kind == ASSET_SOURCE_FILE)
        kind_name = "ASSET_SOURCE_FILE";
      else if (entry->source_kind == ASSET_SOURCE_PROCEDURAL)
        kind_name = "ASSET_SOURCE_PROCEDURAL";

      fprintf(out, "  {\"%s\", \"%s\", %s},\n", entry->name, entry->source, kind_name);
    }
    fprintf(out, "};\n\n");
  }

  // --- enum value-name tables ---
  //
  // The same strings the typed to_string returns, in an array a walker can
  // index. Duplicated rather than shared with the switch above on purpose: the
  // switch is what an unhandled enumerator warns on, and a table would silence
  // exactly that.
  for (int32_t index = 0; index < program->declaration_count; ++index)
  {
    if (enum_ids[index] < 0)
      continue;
    const declaration_t* declaration = &program->declarations[index];

    fprintf(out, "constexpr const char* %.*s_VALUE_NAMES[] = {\n", declaration->name.length,
            declaration->name.data);
    for (int32_t offset = 0; offset < declaration->enum_value_count; ++offset)
    {
      const string_view_t* value = &program->enum_values[declaration->first_enum_value + offset];
      fprintf(out, "  \"%.*s\",\n", value->length, value->data);
    }
    fprintf(out, "};\n\n");
  }

  if (enum_count > 0)
  {
    fprintf(out, "constexpr enum_type_info_t ENUM_INFOS[] = {\n");
    for (int32_t index = 0; index < program->declaration_count; ++index)
    {
      if (enum_ids[index] < 0)
        continue;
      const declaration_t* declaration = &program->declarations[index];
      fprintf(out, "  {\"%.*s\", {%.*s_VALUE_NAMES, %d}},\n", declaration->name.length,
              declaration->name.data, declaration->name.length, declaration->name.data,
              declaration->enum_value_count);
    }
    fprintf(out, "};\n\n");
  }

  fprintf(out, "} // namespace\n\n");

  // --- asset accessors ---
  for (int32_t index = 0; index < program->declaration_count; ++index)
  {
    const declaration_t* declaration = &program->declarations[index];
    if (declaration->kind != DECLARATION_ASSETS)
      continue;

    fprintf(out, "Span<const asset_info_t> %.*s_manifest()\n{\n",
            declaration->name.length, declaration->name.data);
    fprintf(out, "  return {%.*s_MANIFEST, %.*s_COUNT};\n}\n\n",
            declaration->name.length, declaration->name.data, declaration->name.length,
            declaration->name.data);

    fprintf(out, "const char* to_string(%.*s value)\n{\n", declaration->name.length,
            declaration->name.data);
    fprintf(out, "  assert((uint32_t)value < %.*s_COUNT);\n", declaration->name.length,
            declaration->name.data);
    fprintf(out, "  return %.*s_MANIFEST[(uint16_t)value].name;\n}\n\n", declaration->name.length,
            declaration->name.data);

    fprintf(out, "bool from_string(const char* text, %.*s* out_value)\n{\n",
            declaration->name.length, declaration->name.data);
    fprintf(out, "  for (uint32_t index = 0; index < %.*s_COUNT; ++index)\n  {\n",
            declaration->name.length, declaration->name.data);
    fprintf(out, "    if (strcmp(%.*s_MANIFEST[index].name, text) != 0)\n      continue;\n",
            declaration->name.length, declaration->name.data);
    fprintf(out, "    *out_value = (%.*s)index;\n    return true;\n  }\n",
            declaration->name.length, declaration->name.data);
    fprintf(out, "  return false;\n}\n\n");
  }

  if (has_asset_class(program))
  {
    fprintf(out, "Span<const asset_info_t> asset_class_manifest(int32_t asset_class_id)\n{\n");
    fprintf(out, "  switch (asset_class_id)\n  {\n");
    for (int32_t index = 0; index < program->declaration_count; ++index)
    {
      if (asset_class_ids[index] < 0)
        continue;
      const declaration_t* declaration = &program->declarations[index];
      fprintf(out, "    case %d: return %.*s_manifest();\n", asset_class_ids[index],
              declaration->name.length, declaration->name.data);
    }
    fprintf(out, "  }\n");
    fprintf(out, "  assert(false && \"asset_class_manifest: no asset class has this id\");\n");
    fprintf(out, "  return {};\n}\n\n");
  }

  // --- enum string conversion ---
  for (int32_t index = 0; index < program->declaration_count; ++index)
  {
    const declaration_t* declaration = &program->declarations[index];
    if (declaration->kind != DECLARATION_ENUM)
      continue;

    fprintf(out, "const char* to_string(%.*s value)\n{\n  switch (value)\n  {\n",
            declaration->name.length, declaration->name.data);
    for (int32_t offset = 0; offset < declaration->enum_value_count; ++offset)
    {
      const string_view_t* v = &program->enum_values[declaration->first_enum_value + offset];
      fprintf(out, "    case %.*s::%.*s: return \"%.*s\";\n", declaration->name.length,
              declaration->name.data, v->length, v->data, v->length, v->data);
    }
    fprintf(out, "  }\n  assert(false && \"invalid %.*s\");\n  return \"\";\n}\n\n",
            declaration->name.length, declaration->name.data);

    fprintf(out, "bool from_string(const char* text, %.*s* out_value)\n{\n",
            declaration->name.length, declaration->name.data);
    for (int32_t offset = 0; offset < declaration->enum_value_count; ++offset)
    {
      const string_view_t* v = &program->enum_values[declaration->first_enum_value + offset];
      fprintf(out, "  if (strcmp(text, \"%.*s\") == 0) { *out_value = %.*s::%.*s; return true; }\n",
              v->length, v->data, declaration->name.length, declaration->name.data, v->length,
              v->data);
    }
    fprintf(out, "  return false;\n}\n\n");
  }

  if (enum_count > 0)
  {
    fprintf(out, "const enum_type_info_t& enum_info(enum_type type)\n{\n");
    fprintf(out, "  assert((uint32_t)type < ENUM_TYPE_COUNT);\n");
    fprintf(out, "  return ENUM_INFOS[(uint16_t)type];\n}\n\n");
  }

  // --- accessors ---
  fprintf(out, "const entity_type_info_t& entity_info(entity_type type)\n{\n");
  fprintf(out, "  assert(type > entity_type::Invalid && (uint32_t)type < ENTITY_TYPE_COUNT);\n");
  fprintf(out, "  return ENTITY_INFOS[(uint16_t)type];\n}\n\n");

  fprintf(out, "const component_type_info_t& component_info(component_type component)\n{\n");
  fprintf(out, "  assert((uint32_t)component < COMPONENT_TYPE_COUNT);\n");
  fprintf(out, "  return COMPONENT_INFOS[(uint16_t)component];\n}\n\n");

  fprintf(out, "entity_type entity_type_from_classname(const char* classname)\n{\n");
  fprintf(out, "  for (uint32_t index = 1; index < ENTITY_TYPE_COUNT; ++index)\n");
  fprintf(out, "  {\n    if (strcmp(ENTITY_INFOS[index].classname, classname) == 0)\n");
  fprintf(out, "      return (entity_type)index;\n  }\n");
  fprintf(out, "  return entity_type::Invalid;\n}\n\n");

  fprintf(out, "bool has_component(entity_type type, component_type component)\n{\n");
  fprintf(out, "  return (entity_info(type).component_mask & (1u << (uint16_t)component)) != 0;\n}\n\n");

  fprintf(out, "int32_t component_byte_offset(entity_type type, component_type component)\n{\n");
  fprintf(out, "  assert(type > entity_type::Invalid && (uint32_t)type < ENTITY_TYPE_COUNT);\n");
  fprintf(out, "  assert((uint32_t)component < COMPONENT_TYPE_COUNT);\n");
  fprintf(out, "  return COMPONENT_OFFSETS[(uint16_t)type][(uint16_t)component];\n}\n\n");

  // --- construction ---
  //
  // Switches rather than a table of `new` thunks: an unhandled entity_type is
  // then a -Wswitch warning at generator-output compile time, which is the same
  // guarantee P5's lifecycle hooks are built on. Invalid is listed explicitly so
  // adding a type can never be absorbed by a default case.
  fprintf(out, "%s create_entity(entity_type type)\n{\n  switch (type)\n  {\n",
          entity_pointer_type);
  fprintf(out, "    case entity_type::Invalid: break;\n");
  for (int32_t index = 0; index < program->declaration_count; ++index)
  {
    const declaration_t* declaration = &program->declarations[index];
    if (declaration->kind != DECLARATION_ENTITY)
      continue;
    fprintf(out, "    case entity_type::%.*s: return new %.*s();\n", declaration->name.length,
            declaration->name.data, declaration->name.length, declaration->name.data);
  }
  fprintf(out, "  }\n");
  fprintf(out, "  assert(false && \"create_entity: not a valid entity_type\");\n");
  fprintf(out, "  return nullptr;\n}\n\n");

  fprintf(out, "%s entity_from_classname(const char* classname)\n{\n", entity_pointer_type);
  fprintf(out, "  entity_type type = entity_type_from_classname(classname);\n");
  fprintf(out, "  if (type == entity_type::Invalid)\n");
  fprintf(out, "    return nullptr; // unknown classname: the caller reports it\n");
  fprintf(out, "  return create_entity(type);\n}\n\n");

  fprintf(out, "void destroy_entity(%s entity)\n{\n", entity_pointer_type);
  fprintf(out, "  if (entity == nullptr)\n    return;\n\n");
  fprintf(out, "  switch (entity->type)\n  {\n");
  fprintf(out, "    case entity_type::Invalid: break;\n");
  for (int32_t index = 0; index < program->declaration_count; ++index)
  {
    const declaration_t* declaration = &program->declarations[index];
    if (declaration->kind != DECLARATION_ENTITY)
      continue;
    fprintf(out, "    case entity_type::%.*s: delete static_cast<%.*s*>(entity); return;\n",
            declaration->name.length, declaration->name.data, declaration->name.length,
            declaration->name.data);
  }
  fprintf(out, "  }\n");
  fprintf(out, "  assert(false && \"destroy_entity: entity carries an invalid tag\");\n}\n\n");

  fprintf(out, "Span<const entity_type> placeable_entity_types()\n{\n");
  fprintf(out, "  return {PLACEABLE_ENTITY_TYPES, PLACEABLE_ENTITY_TYPE_COUNT};\n}\n\n");

  fprintf(out, "const uint32_t SCHEMA_HASH = 0x%08xu;\n\n", compute_schema_hash(program));

  fprintf(out, "} // namespace entities\n");

  free(component_ids);
  free(asset_class_ids);
  free(enum_ids);
}

// ---------------------------------------------------------------------------
// Dump
// ---------------------------------------------------------------------------

static void print_type(const type_reference_t* type)
{
  if (type->kind == TYPE_STRING)
  {
    printf("string<%d>", type->capacity);
    return;
  }
  printf("%.*s", type->name.length, type->name.data);
}

static void print_default_value(const default_value_t* value)
{
  switch (value->kind)
  {
    case DEFAULT_NONE:
      break;

    case DEFAULT_NUMBER:
      printf(" = %g", value->numbers[0]);
      break;

    case DEFAULT_VECTOR:
      printf(" = {");
      for (int32_t index = 0; index < value->number_count; ++index)
        printf("%s%g", index == 0 ? "" : ", ", value->numbers[index]);
      printf("}");
      break;

    case DEFAULT_ENUM_LITERAL:
      printf(" = .%.*s", value->text.length, value->text.data);
      break;

    case DEFAULT_BOOL:
      printf(" = %s", value->boolean ? "true" : "false");
      break;

    case DEFAULT_STRING:
      printf(" = \"%.*s\"", value->text.length, value->text.data);
      break;
  }
}

static void print_field_flags(uint32_t flags)
{
  if (flags & FIELD_FLAG_NETWORKED) printf(" @Networked");
  if (flags & FIELD_FLAG_EDITABLE)  printf(" @Editable");
  if (flags & FIELD_FLAG_SAVEABLE)  printf(" @Saveable");
}

static void dump_program(const program_t* program)
{
  printf("// %s: %d declarations, %d fields, %d tokens\n", program->filename,
         program->declaration_count, program->field_count, program->token_count);

  for (int32_t index = 0; index < program->declaration_count; ++index)
  {
    const declaration_t* declaration = &program->declarations[index];

    printf("\n%.*s :: %s", declaration->name.length, declaration->name.data,
           declaration_kind_name(declaration->kind));

    if (declaration->kind == DECLARATION_FLAGSET)
    {
      printf(" [");
      print_field_flags(declaration->class_flags);
      printf(" ]\n");
      continue;
    }

    if (declaration->class_flags & CLASS_FLAG_RUNTIME_ONLY)
      printf(" @runtime_only");

    printf("\n{\n");

    if (declaration->kind == DECLARATION_ENUM)
    {
      for (int32_t offset = 0; offset < declaration->enum_value_count; ++offset)
      {
        const string_view_t* value = &program->enum_values[declaration->first_enum_value + offset];
        printf("  %d: %.*s\n", offset, value->length, value->data);
      }
    }
    else if (declaration->kind == DECLARATION_ASSETS)
    {
      for (int32_t offset = 0; offset < declaration->asset_entry_count; ++offset)
      {
        const asset_entry_t* entry = &program->asset_entries[declaration->first_asset_entry + offset];
        const char*          kind  = entry->source_kind == ASSET_SOURCE_FILE       ? "file"
                                     : entry->source_kind == ASSET_SOURCE_PROCEDURAL ? "procedural"
                                                                                     : "missing";
        printf("  %d: %-16s %-12s %s\n", offset, entry->name, kind, entry->source);
      }
    }
    else
    {
      for (int32_t offset = 0; offset < declaration->field_count; ++offset)
      {
        const field_t* field = &program->fields[declaration->first_field + offset];

        printf("  %.*s: ", field->name.length, field->name.data);
        print_type(&field->type);
        print_default_value(&field->default_value);
        print_field_flags(field->flags);
        printf("   // %s\n", type_kind_name(field->type.kind));
      }
    }

    printf("}\n");
  }
}

// ---------------------------------------------------------------------------
// Driver
// ---------------------------------------------------------------------------

static bool read_entire_file(const char* filename, char** out_contents, int32_t* out_length)
{
  FILE* file = fopen(filename, "rb");
  if (file == nullptr)
  {
    fprintf(stderr, "error: cannot open '%s'\n", filename);
    return false;
  }

  fseek(file, 0, SEEK_END);
  long size = ftell(file);
  fseek(file, 0, SEEK_SET);

  if (size < 0)
  {
    fprintf(stderr, "error: cannot determine the size of '%s'\n", filename);
    fclose(file);
    return false;
  }

  char*  contents  = (char*)malloc((size_t)size + 1);
  size_t bytes_read = fread(contents, 1, (size_t)size, file);
  fclose(file);

  if (bytes_read != (size_t)size)
  {
    fprintf(stderr, "error: read %zu of %ld bytes from '%s'\n", bytes_read, size, filename);
    free(contents);
    return false;
  }

  contents[size] = '\0';

  // A UTF-8 BOM is invisible in every editor that writes one, but the tokenizer
  // sees three bytes in no character class and reports three "unexpected
  // character" errors before it has read a single declaration. Drop it here so
  // the diagnostic never happens. Moving the bytes down rather than skipping
  // past them keeps every offset in the buffer equal to the offset an editor
  // shows, which is what the line/column diagnostics are built on.
  if (size >= 3 && (uint8_t)contents[0] == 0xEF && (uint8_t)contents[1] == 0xBB &&
      (uint8_t)contents[2] == 0xBF)
  {
    size -= 3;
    memmove(contents, contents + 3, (size_t)size + 1);
  }

  *out_contents  = contents;
  *out_length    = (int32_t)size;
  return true;
}

// Every capacity here is an upper bound proven by construction, so no array
// ever grows and every pointer into them stays valid for the whole run.
static void allocate_program(program_t* program)
{
  program->token_capacity = program->source_length + 1; // a token needs >= 1 byte, plus the EOF one
  program->tokens = (token_t*)malloc((size_t)program->token_capacity * sizeof(token_t));

  int32_t token_bound = program->token_capacity; // every node needs >= 1 token

  program->declaration_capacity = token_bound;
  program->declarations =
      (declaration_t*)malloc((size_t)program->declaration_capacity * sizeof(declaration_t));

  program->field_capacity = token_bound;
  program->fields         = (field_t*)malloc((size_t)program->field_capacity * sizeof(field_t));

  program->annotation_capacity = token_bound;
  program->annotations =
      (annotation_t*)malloc((size_t)program->annotation_capacity * sizeof(annotation_t));

  program->enum_value_capacity = token_bound;
  program->enum_values =
      (string_view_t*)malloc((size_t)program->enum_value_capacity * sizeof(string_view_t));

  // These two are the exception to "capacity is a proven bound": their contents
  // come from the filesystem, so no bound can be proven from the source. They
  // are generous fixed sizes, and overrunning either is a diagnostic.
  program->asset_entry_capacity = 8192;
  program->asset_entries =
      (asset_entry_t*)malloc((size_t)program->asset_entry_capacity * sizeof(asset_entry_t));

  program->string_arena_capacity = 1 << 20;
  program->string_arena_used     = 0;
  program->string_arena          = (char*)malloc((size_t)program->string_arena_capacity);
}

int main(int argument_count, char** arguments)
{
  const char* filename    = nullptr;
  const char* output_dir  = nullptr;
  // Asset scan paths in the .def are written the way the game writes them at
  // runtime ("resources/obj"), so they resolve against the repo root, not
  // against wherever the build invoked the generator from.
  const char* asset_root  = ".";
  bool        should_dump = false;

  for (int index = 1; index < argument_count; ++index)
  {
    if (strcmp(arguments[index], "--dump") == 0)
    {
      should_dump = true;
      continue;
    }
    if (strcmp(arguments[index], "--output-dir") == 0)
    {
      if (index + 1 >= argument_count)
      {
        fprintf(stderr, "error: --output-dir needs a directory\n");
        return 1;
      }
      output_dir = arguments[++index];
      continue;
    }
    if (strcmp(arguments[index], "--asset-root") == 0)
    {
      if (index + 1 >= argument_count)
      {
        fprintf(stderr, "error: --asset-root needs a directory\n");
        return 1;
      }
      asset_root = arguments[++index];
      continue;
    }
    if (arguments[index][0] == '-')
    {
      fprintf(stderr, "error: unknown option '%s'\n", arguments[index]);
      return 1;
    }
    if (filename != nullptr)
    {
      fprintf(stderr, "error: more than one input file given ('%s' and '%s')\n", filename,
              arguments[index]);
      return 1;
    }
    filename = arguments[index];
  }

  if (filename == nullptr)
  {
    fprintf(stderr,
            "usage: entity_gen <file.def> [--dump] [--output-dir <dir>] [--asset-root <dir>]\n");
    return 1;
  }

  program_t program  = {};
  program.filename   = filename;
  program.asset_root = asset_root;

  if (!read_entire_file(filename, &program.source, &program.source_length))
    return 1;

  allocate_program(&program);

  tokenize(&program);

  parser_t parser = {};
  parser.program  = &program;
  parse_program(&parser);

  resolve_program(&program);

  if (program.error_count > 0)
  {
    fprintf(stderr, "%s: %d error%s\n", filename, program.error_count,
            program.error_count == 1 ? "" : "s");
    return 1;
  }

  if (should_dump)
    dump_program(&program);

  if (output_dir != nullptr)
  {
    const char* header_name = "entities_generated.hpp";
    const char* source_name = "entities_generated.cpp";

    char header_path[1024];
    char source_path[1024];
    snprintf(header_path, sizeof(header_path), "%s/%s", output_dir, header_name);
    snprintf(source_path, sizeof(source_path), "%s/%s", output_dir, source_name);

    FILE* header_file = fopen(header_path, "wb");
    if (header_file == nullptr)
    {
      fprintf(stderr, "error: cannot write '%s'\n", header_path);
      return 1;
    }
    emit_generated_header(header_file, &program);
    fclose(header_file);

    FILE* source_file = fopen(source_path, "wb");
    if (source_file == nullptr)
    {
      fprintf(stderr, "error: cannot write '%s'\n", source_path);
      return 1;
    }
    emit_generated_source(source_file, &program, header_name);
    fclose(source_file);

    fprintf(stderr, "entity_gen: wrote %s and %s\n", header_path, source_path);
  }

  return 0;
}
