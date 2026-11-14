/**
 * launchiine helper
 *
 * launchiine opens the original Wii U Menu by renaming its own men.rpx for one start of the
 * menu (Aroma only replaces the menu while sd:/wiiu/environments/<env>/men.rpx exists).
 * It leaves sd:/wiiu/launchiine/restore-men.txt with two lines: the renamed file and the
 * original name. This plugin puts the file back as soon as any application starts - the
 * original menu has been loaded by then - so the next start of the menu is launchiine again.
 */
#include <cstdio>
#include <cstring>
#include <string>
#include <sys/stat.h>
#include <unistd.h>
#include <wups.h>

WUPS_PLUGIN_NAME("launchiine helper");
WUPS_PLUGIN_DESCRIPTION("Brings launchiine back after it opened the original Wii U Menu.");
WUPS_PLUGIN_VERSION("v1.0");
WUPS_PLUGIN_AUTHOR("vbarros22");
WUPS_PLUGIN_LICENSE("GPL");

WUPS_USE_WUT_DEVOPTAB();

namespace {
    constexpr const char *MARKER = "fs:/vol/external01/wiiu/launchiine/restore-men.txt";

    bool exists(const std::string &path) {
        struct stat st {};
        return stat(path.c_str(), &st) == 0;
    }

    std::string readLine(FILE *f) {
        char buffer[512] = {};
        if (fgets(buffer, sizeof(buffer), f) == nullptr) {
            return "";
        }
        std::string line(buffer);
        while (!line.empty() && (line.back() == '\n' || line.back() == '\r')) {
            line.pop_back();
        }
        return line;
    }

    void restoreLaunchiine() {
        FILE *f = fopen(MARKER, "r");
        if (f == nullptr) {
            return; // nothing to do, the usual case
        }
        std::string renamed  = readLine(f);
        std::string original = readLine(f);
        fclose(f);

        //! only files of an Aroma environment, nothing else is ever renamed
        bool valid = renamed.rfind("fs:/vol/external01/wiiu/environments/", 0) == 0 &&
                     original.rfind("fs:/vol/external01/wiiu/environments/", 0) == 0;
        if (valid && exists(renamed) && !exists(original)) {
            rename(renamed.c_str(), original.c_str());
        }
        remove(MARKER);
    }
} // namespace

ON_APPLICATION_START() {
    restoreLaunchiine();
}
