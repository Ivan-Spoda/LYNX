#pragma once

namespace lynx
{
// Registers .lynx project files with Windows for the current user (no admin rights): double-click opens them in this
// LYNX.exe and Explorer shows LYNX's project icon. Rewrites the entries only when they point somewhere else, so a
// moved or updated exe takes the association over. Does nothing on other platforms.
void registerFileTypes();
} // namespace lynx
