// client/hud/match_announcement.cpp: the banner for every row of match_def.md's
// wording table, with no device and no connection.

#include "client/hud/match_announcement.hpp"

#include <cstdio>
#include <string>

namespace
{

int failure_count = 0;

void check_text(const std::string& actual, const std::string& expected, const char* what)
{
  if (actual == expected)
    return;
  std::printf("  FAILED: %s (got \"%s\", expected \"%s\")\n", what, actual.c_str(),
              expected.c_str());
  ++failure_count;
}

entities::Match match_in(entities::Round_Phase phase, uint32_t round_number,
                         entities::Game_Mode mode = entities::Game_Mode::deathmatch)
{
  entities::Match match;
  match.phase        = phase;
  match.round_number = round_number;
  match.mode         = mode;
  return match;
}

entities::Match round_over(entities::Round_End_Reason reason,
                           entities::Team_Allegiance winner = entities::Team_Allegiance::Free_For_All)
{
  entities::Match match = match_in(entities::Round_Phase::Round_End, 1, entities::Game_Mode::rounds);
  match.end_reason      = reason;
  match.winning_team    = winner;
  return match;
}

} // namespace

int main()
{
  using client::hud::match_announcement_for;
  using entities::Game_Mode;
  using entities::Round_End_Reason;
  using entities::Round_Phase;
  using entities::Team_Allegiance;

  std::printf("=== match_announcement_test ===\n");

  const entities::Match rounds_live = match_in(Round_Phase::Live, 1, Game_Mode::rounds);

  check_text(match_announcement_for(match_in(Round_Phase::Live, 1), match_in(Round_Phase::Live, 1), ""),
             "", "no edge, no banner");
  check_text(match_announcement_for(match_in(Round_Phase::Warmup, 0),
                                    match_in(Round_Phase::Freeze, 3, Game_Mode::rounds), ""),
             "ROUND 3", "-> Freeze names the round");
  check_text(match_announcement_for(match_in(Round_Phase::Warmup, 0),
                                    match_in(Round_Phase::Countdown, 0), ""),
             "", "-> Countdown leaves the words to the HUD's count");
  check_text(match_announcement_for(match_in(Round_Phase::Countdown, 0),
                                    match_in(Round_Phase::Warmup, 0), ""),
             "COUNTDOWN CANCELLED", "Countdown -> Warmup says it was cancelled");
  check_text(match_announcement_for(match_in(Round_Phase::Freeze, 1, Game_Mode::rounds),
                                    rounds_live, ""),
             "FIGHT", "-> Live");
  check_text(match_announcement_for(match_in(Round_Phase::Warmup, 0, Game_Mode::speedrun),
                                    match_in(Round_Phase::Live, 1, Game_Mode::speedrun), ""),
             "", "-> Live says nothing for a speedrun");
  check_text(match_announcement_for(match_in(Round_Phase::Live, 1),
                                    match_in(Round_Phase::Live, 2), ""),
             "FIGHT", "a restart is an edge on the round number");

  check_text(match_announcement_for(rounds_live,
                                    round_over(Round_End_Reason::Team_Elimination, Team_Allegiance::Blu),
                                    ""),
             "ROUND OVER\nBLU TAKES IT", "an elimination names the winner");
  check_text(match_announcement_for(rounds_live, round_over(Round_End_Reason::Team_Elimination), ""),
             "ROUND OVER\nDRAW", "a mutual elimination is a draw");
  check_text(match_announcement_for(rounds_live, round_over(Round_End_Reason::Timeout), ""),
             "ROUND OVER\nTIME", "a timeout");
  check_text(match_announcement_for(rounds_live, round_over(Round_End_Reason::Objective), ""), "",
             "the objective's own banner is not overwritten");
  check_text(match_announcement_for(rounds_live, round_over(Round_End_Reason::Requested), ""), "",
             "a requested end says nothing");

  entities::Match frag_over = match_in(Round_Phase::Game_Over, 1);
  frag_over.end_reason      = Round_End_Reason::Frag_Limit;
  check_text(match_announcement_for(match_in(Round_Phase::Live, 1), frag_over, "sjors"),
             "GAME OVER\nsjors WINS", "the frag limit names the winner");

  entities::Match other_over = match_in(Round_Phase::Game_Over, 15, Game_Mode::rounds);
  other_over.end_reason      = Round_End_Reason::Timeout;
  check_text(match_announcement_for(round_over(Round_End_Reason::Timeout), other_over, "sjors"),
             "GAME OVER", "any other end of the match");

  if (failure_count != 0)
  {
    std::printf("\nmatch_announcement_test FAILED with %d failure(s)\n", failure_count);
    return 1;
  }
  std::printf("match_announcement_test: all checks passed\n");
  return 0;
}
