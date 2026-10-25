#pragma once

namespace lynx
{
// Registers .lynx project files with Windows for the current user (no admin rights): double-click opens them in this
// LYNX.exe and Explorer shows LYNX's project icon. Rewrites the entries only when they point somewhere else, so a
// moved or updated exe takes the association over. Does nothing on other platforms.
// Linux AppImage: when LYNX's menu entry (made by setAppImageMenuEntry) points at another AppImage file — moved, or
// replaced by a newer version — it is rewritten for this one.
void registerFileTypes();

// Linux (round 161): LYNX runs from an AppImage ($APPIMAGE). AppImages are single files that integrate nothing by
// themselves; on request LYNX adds its applications-menu entry, its icon and the .lynx file type to the user's
// ~/.local/share (no root), pointing at this AppImage, and removes them again.
bool runningAsAppImage();
bool appImageMenuEntryInstalled();
bool setAppImageMenuEntry (bool on);
} // namespace lynx
