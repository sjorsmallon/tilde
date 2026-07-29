// Generated from C:/Users/sjors/Desktop/Projects/tilde/tilde/src/shared/cvars/cvars.def by def_gen. Do not edit.
//
// Binds every @Client command's handler. This TU is compiled into the module
// that owns those handlers, so it references each commands::<name> symbol
// directly: a missing or misspelled handler fails at LINK time, naming the
// symbol. That link step is the assert -- there is no runtime "did
// everyone register?" check because there is nothing to register.
#include "cvars_generated.hpp"

namespace cvars
{

void bind_client_commands(command_table_t& table)
{
  table.handlers[(uint32_t)command_id::bind] = &commands::bind;
}

} // namespace cvars
