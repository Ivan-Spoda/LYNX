#include "engine/FlStudio.h"

namespace lynx
{
namespace
{
juce::String registryValue (const juce::String& key)
{
   #if JUCE_WINDOWS
    for (const auto* root : { "HKEY_CURRENT_USER\\", "HKEY_LOCAL_MACHINE\\" })
        if (auto value = juce::WindowsRegistry::getValue (root + key).trim(); value.isNotEmpty())
            return value;
   #else
    juce::ignoreUnused (key);
   #endif
    return {};
}

juce::File existingFolder (const juce::String& path)
{
    if (path.isEmpty() || ! juce::File::isAbsolutePath (path))
        return {};
    const juce::File folder (path);
    return folder.isDirectory() ? folder : juce::File();
}
} // namespace

FlStudioInstall findFlStudio()
{
    FlStudioInstall fl;

    // installation: where FL says it is, else the newest "FL Studio*" under Program Files\Image-Line
    fl.installDir = existingFolder (registryValue ("Software\\Image-Line\\Shared\\Paths\\Install path"));
    if (! fl.installDir.getChildFile ("Data").isDirectory())
    {
        fl.installDir = juce::File();
        const auto imageLine = juce::File::getSpecialLocation (juce::File::globalApplicationsDirectory).getChildFile ("Image-Line");
        for (const auto& dir : imageLine.findChildFiles (juce::File::findDirectories, false, "FL Studio*"))
            if (dir.getChildFile ("Data").isDirectory() && dir.getFileName() > fl.installDir.getFileName())
                fl.installDir = dir;
    }

    // test hook: LYNX_FL_USER_DATA=<folder> stands in for FL's user data folder (synthetic plugin databases)
    if (const auto testFolder = existingFolder (juce::SystemStats::getEnvironmentVariable ("LYNX_FL_USER_DATA", {})); testFolder.exists())
        fl.userData = testFolder;

    // user data: "Shared data" (e.g. ...\Documents\Image-Line\, possibly under OneDrive) + "FL Studio"
    if (! fl.userData.isDirectory())
        if (const auto shared = existingFolder (registryValue ("Software\\Image-Line\\Shared\\Paths\\Shared data")); shared.exists())
            if (const auto user = shared.getChildFile ("FL Studio"); user.isDirectory())
                fl.userData = user;
    if (! fl.userData.isDirectory())
        if (const auto user = juce::File::getSpecialLocation (juce::File::userDocumentsDirectory).getChildFile ("Image-Line/FL Studio"); user.isDirectory())
            fl.userData = user;

    // plugin folders FL was told about
    for (const auto* key : { "Software\\Image-Line\\Shared\\Paths\\VST plugins", "Software\\Image-Line\\Shared\\Plugins\\FL Studio\\VstDir64",
                             "Software\\Image-Line\\Shared\\Plugins\\FL Studio\\VstDir" })
        for (const auto& part : juce::StringArray::fromTokens (registryValue (key), ";", "\""))
            if (const auto folder = existingFolder (part.trim()); folder.exists())
                fl.vstFolders.addIfNotAlreadyThere (folder.getFullPathName(), true);

    return fl;
}

FlPluginDb readFlPluginDatabase (const FlStudioInstall& fl, int* files)
{
    FlPluginDb db;
    int count = 0;
    const auto folder = fl.pluginDatabase();

    if (folder.isDirectory())
    {
        // .fst first (they carry the IDs), then the .nfo text (vendor, role) fills in the rest
        for (const auto* pattern : { "*.fst", "*.nfo" })
            for (const auto& entry : juce::RangedDirectoryIterator (folder, true, pattern, juce::File::findFiles))
            {
                const auto& file = entry.getFile();
                if (file.getFileName().equalsIgnoreCase ("VerifiedIDs.nfo") || entry.getFileSize() > 8 * 1024 * 1024)
                    continue;
                ++count;
                if (file.hasFileExtension ("fst"))
                {
                    juce::MemoryBlock data;
                    FlPluginDbEntry e;
                    e.source = file.getFullPathName().toStdString();
                    const auto path = e.source;
                    e.role = path.find ("\\Generators\\") != std::string::npos ? 2 : path.find ("\\Effects\\") != std::string::npos ? 1 : 0;
                    if (file.loadFileAsData (data)
                        && parseFlPluginFst ({ static_cast<const std::uint8_t*> (data.getData()), data.getSize() }, e))
                        db.add (e);
                }
                else
                {
                    for (const auto& e : parseFlPluginNfo (file.loadFileAsString().toStdString(), file.getFullPathName().toStdString()))
                        db.add (e);
                }
            }
    }

    if (files != nullptr)
        *files = count;
    return db;
}
} // namespace lynx
