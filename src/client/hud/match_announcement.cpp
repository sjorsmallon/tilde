#include "match_announcement.hpp"

#include <cctype>
#include <format>

namespace client::hud
{

namespace
{

std::string upper(std::string_view text)
{
  std::string result(text);
  for (char& character : result)
    character = static_cast<char>(std::toupper(static_cast<unsigned char>(character)));
  return result;
}

std::string round_end_text(const entities::Match& after)
{
  switch (after.end_reason)
  {
    case entities::Round_End_Reason::Objective:
    case entities::Round_End_Reason::Requested:
    case entities::Round_End_Reason::None:
      return {};
    case entities::Round_End_Reason::Timeout:
      return "ROUND OVER\nTIME";
    case entities::Round_End_Reason::Team_Elimination:
      if (after.winning_team == entities::Team_Allegiance::Free_For_All)
        return "ROUND OVER\nDRAW";
      return std::format("ROUND OVER\n{} TAKES IT", upper(to_string(after.winning_team)));
    case entities::Round_End_Reason::Frag_Limit:
      return "ROUND OVER";
  }
  return {};
}

} // namespace

std::string match_announcement_for(const entities::Match& before, const entities::Match& after,
                                   std::string_view frag_leader_name)
{
  if (before.phase == after.phase && before.round_number == after.round_number)
    return {};

  switch (after.phase)
  {
    case entities::Round_Phase::Warmup:
      return before.phase == entities::Round_Phase::Countdown ? std::string{"COUNTDOWN CANCELLED"}
                                                              : std::string{};
    case entities::Round_Phase::Countdown:
      return {};
    case entities::Round_Phase::Freeze:
      return std::format("ROUND {}", after.round_number);
    case entities::Round_Phase::Live:
      return after.mode == entities::Game_Mode::speedrun ? std::string{} : std::string{"FIGHT"};
    case entities::Round_Phase::Round_End:
      return before.phase == entities::Round_Phase::Live ? round_end_text(after) : std::string{};
    case entities::Round_Phase::Game_Over:
      if (after.end_reason == entities::Round_End_Reason::Frag_Limit && !frag_leader_name.empty())
        return std::format("GAME OVER\n{} WINS", frag_leader_name);
      return "GAME OVER";
  }
  return {};
}

} // namespace client::hud
