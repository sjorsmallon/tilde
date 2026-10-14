#pragma once

#include "shared/cvars/cvar_console.hpp"
#include "shared/cvars/generated/cvars_generated.hpp"
#include "shared/log.hpp"

#include <cstdlib>
#include <string>
#include <string_view>
#include <vector>

// Quake's split. An option starting with '-' is read by the launcher BEFORE the
// client comes up (the window does not exist yet, so these cannot be console
// lines); an argument starting with '+' begins a console line that runs AFTER
// init, its tokens being everything up to the next '+'.
//
//   arguments    := ( option | console_line )*
//   option       := "-w" NUMBER | "-h" NUMBER | "-x" NUMBER | "-y" NUMBER
//   console_line := "+" NAME TOKEN*
//
//   MyGame_Client -w 0.5 -h 0.5 -x 0.5 -y 0 +cl_auto_join 1 +connect 127.0.0.1
namespace launch_arguments {

struct option_t
{
  const char*                  name;
  float cvars::cvar_state_t::* cvar;
};

constexpr option_t OPTIONS[] = {
    {"-w", &cvars::cvar_state_t::r_window_width},
    {"-h", &cvars::cvar_state_t::r_window_height},
    {"-x", &cvars::cvar_state_t::r_window_x},
    {"-y", &cvars::cvar_state_t::r_window_y},
};

struct launch_arguments_t
{
  std::vector<std::string> console_lines;
};

inline launch_arguments_t parse_launch_arguments(int argc, char* argv[],
                                                 cvars::cvar_state_t& cvars)
{
  launch_arguments_t arguments{};
  for (int index = 1; index < argc; ++index)
  {
    const std::string_view argument = argv[index];
    if (argument.starts_with('+'))
    {
      std::string line(argument.substr(1));
      while (index + 1 < argc && argv[index + 1][0] != '+')
      {
        line += ' ';
        line += argv[++index];
      }
      arguments.console_lines.push_back(line);
      continue;
    }

    const option_t* option = nullptr;
    for (const option_t& candidate : OPTIONS)
      if (argument == candidate.name)
        option = &candidate;

    if (option == nullptr)
      fatal_error("unknown launch argument '{}' (expected -w -h -x -y or +line)", argument);
    if (index + 1 >= argc)
      fatal_error("launch argument '{}' needs a number", argument);
    cvars.*(option->cvar) = static_cast<float>(std::atof(argv[++index]));
  }
  return arguments;
}

inline void execute_launch_console_lines(const launch_arguments_t& arguments,
                                         cvars::cvar_state_t&  cvars,
                                         const cvars::command_table_t& table)
{
  for (const std::string& line : arguments.console_lines)
  {
    std::string reply;
    const cvars::console_result_t result = cvars::execute_console_line(
        cvars, table, line, cvars::command_context_t{}, &reply);
    if (result != cvars::console_result_t::ok &&
        result != cvars::console_result_t::forwarded)
      fatal_error("launch line '+{}' failed: {}", line, reply);
    log_terminal("[launch] +{}", line);
  }
}

} // namespace launch_arguments
