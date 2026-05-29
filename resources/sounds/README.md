# Sounds

Drop short SFX clips here. Paths are resolved relative to the working directory
the game is launched from (the repo root for `./cmake_build/bin/MyGame`).

The client audio system (`src/client/audio/audio_system.{hpp,cpp}`, miniaudio
backend) loads these on demand and caches the decoded data by path. WAV / MP3 /
FLAC are supported. A missing file logs an error once (see
`feedback_no_silent_failures`) and is otherwise ignored — the game still runs.

## Files the current cosmetic-effect handlers expect

| Path                              | Played by                                   | Spatialized |
|-----------------------------------|---------------------------------------------|-------------|
| `rocket_explosion.wav`            | `effects/rocket_explosion.cpp` (3D, origin) | yes         |
| `footstep.wav`                    | `effects/footstep.cpp` (3D, foot position)  | yes         |
| `player_jump.wav`                 | local: PlayState (2D); remote: `effects/player_movement.cpp` (3D) | mixed |
| `player_land.wav`                 | local: PlayState (2D); remote: `effects/player_movement.cpp` (3D) | mixed |

`footstep.wav` will only be heard once the server actually dispatches the
`FOOTSTEP` cosmetic effect; the handler is wired and waiting.

Jump/land play two ways: your **own** player plays them centered (2D) the
instant prediction detects them; **other** players' jumps/lands arrive as
spatialized cosmetic effects from the server. Same files for both.
