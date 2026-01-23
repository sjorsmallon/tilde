#pragma once

namespace client {
bool Init();
bool Tick(); // Returns false if should quit
void Shutdown();
} // namespace client
