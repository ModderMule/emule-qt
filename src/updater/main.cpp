// emuleqt-updater — installs a downloaded eMuleQt package once the core and
// GUIs quit, then restarts them. No Qt: it replaces the Qt libs it would use.

#include "Platform.h"
#include "Updater.h"

#include <string>
#include <vector>

#ifdef _WIN32
int wmain(int argc, wchar_t** argv)
{
    std::vector<std::string> args;
    for (int i = 1; i < argc; ++i)
        args.push_back(updater::platform::fromPath(std::filesystem::path(argv[i])));
    return updater::Updater().run(args);
}
#else
int main(int argc, char** argv)
{
    std::vector<std::string> args(argv + 1, argv + argc);
    return updater::Updater().run(args);
}
#endif
