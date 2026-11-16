#include "GameDb.h"
#include "fs/FSUtils.h"
#include "utils/logger.h"

#include <atomic>
#include <condition_variable>
#include <coreinit/time.h>
#include <coreinit/userconfig.h>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <curl/curl.h>
#include <algorithm>
#include <deque>
#include <functional>
#include <gd.h>
#include <future>
#include <map>
#include <mutex>
#include <nn/ac.h>
#include <set>
#include <sys/stat.h>
#include <zlib.h>

namespace {
    constexpr const char *DIR_PATH     = "fs:/vol/external01/wiiu/launchiine";
    constexpr const char *COVER_DIR    = "fs:/vol/external01/wiiu/launchiine/covers";
    constexpr const char *CACHE_FILE   = "fs:/vol/external01/wiiu/launchiine/gamedb4.txt";
    constexpr const char *MANUAL_ZIP   = "fs:/vol/external01/wiiu/launchiine/wiiutdb.zip";
    constexpr const char *DB_URL       = "http://www.gametdb.com/wiiutdb.zip?LANG=";
    constexpr const char *COVER_URL    = "http://art.gametdb.com/wiiu/";
    //! Wii and GameCube games, for injects (Virtual Console titles made from a Wii/GC game)
    constexpr const char *WII_DB_URL    = "http://www.gametdb.com/wiitdb.zip?LANG=EN";
    constexpr const char *WII_COVER_URL = "http://art.gametdb.com/wii/";
    //! the database changes slowly; look again for missing games after a week
    constexpr int64_t RECHECK_SECONDS = 7 * 24 * 60 * 60;

    std::mutex lock;
    std::condition_variable wake;
    std::map<std::string, GameDb::Info> infos;
    std::set<std::string> wantedIds;
    std::map<std::string, std::string> wantedNames; // key -> title name (matched when the id is unknown)
    //! covers in memory, the most recently used at the back
    constexpr size_t COVER_CACHE_SIZE = 40;
    std::deque<std::pair<std::string, std::shared_ptr<const GameDb::Cover>>> coverCache;
    std::string coverRequest;           // the TV waits for this one
    std::deque<std::string> prefetch;   // then these
    std::set<std::string> coverMissing; // no cover on GameTDB (this run)
    std::atomic<uint32_t> changeCounter{0};
    std::atomic<bool> stopRequested{false};
    std::future<void> worker;
    bool networkFailed = false;

    std::string coverFile(const std::string &id) {
        return std::string(COVER_DIR) + "/" + id + ".lcv";
    }

    //! JPEG -> RGB565 at most COVER_MAX_W wide. File: "LCV1", width, height (u16), pixels.
    constexpr int COVER_MAX_W = 480;
    std::shared_ptr<GameDb::Cover> jpegToCover(const std::string &jpeg) {
        bool png       = jpeg.size() > 4 && (uint8_t) jpeg[0] == 0x89 && jpeg[1] == 'P';
        gdImagePtr src = png ? gdImageCreateFromPngPtr((int) jpeg.size(), (void *) jpeg.data()) : gdImageCreateFromJpegPtr((int) jpeg.size(), (void *) jpeg.data());
        if (src == nullptr) {
            return nullptr;
        }
        int w = gdImageSX(src), h = gdImageSY(src);
        gdImagePtr img = src;
        if (w > COVER_MAX_W) {
            int nh = (int) ((int64_t) h * COVER_MAX_W / w);
            img    = gdImageCreateTrueColor(COVER_MAX_W, nh);
            if (img == nullptr) {
                gdImageDestroy(src);
                return nullptr;
            }
            gdImageCopyResampled(img, src, 0, 0, 0, 0, COVER_MAX_W, nh, w, h);
            gdImageDestroy(src);
            w = COVER_MAX_W;
            h = nh;
        }
        auto cover    = std::make_shared<GameDb::Cover>();
        cover->width  = w;
        cover->height = h;
        cover->pixels.resize((size_t) w * h);
        uint16_t *px = cover->pixels.data();
        for (int y = 0; y < h; y++) {
            for (int x = 0; x < w; x++) {
                int c = gdImageTrueColorPixel(img, x, y);
                *px++ = (uint16_t) (((gdTrueColorGetRed(c) >> 3) << 11) | ((gdTrueColorGetGreen(c) >> 2) << 5) | (gdTrueColorGetBlue(c) >> 3));
            }
        }
        gdImageDestroy(img);
        return cover;
    }

    void saveCover(const std::string &path, const GameDb::Cover &cover) {
        std::vector<uint8_t> data(8 + cover.pixels.size() * 2);
        memcpy(&data[0], "LCV1", 4);
        data[4] = cover.width >> 8;
        data[5] = cover.width & 0xFF;
        data[6] = cover.height >> 8;
        data[7] = cover.height & 0xFF;
        memcpy(&data[8], cover.pixels.data(), cover.pixels.size() * 2); // big endian, like the GPU wants
        FSUtils::CreateSubfolder(COVER_DIR);
        FSUtils::saveBufferToFile(path.c_str(), data.data(), data.size());
    }

    std::shared_ptr<GameDb::Cover> loadCover(const std::string &path) {
        uint8_t *buffer = nullptr;
        uint32_t size   = 0;
        if (FSUtils::LoadFileToMem(path.c_str(), &buffer, &size) <= 0 || buffer == nullptr) {
            return nullptr;
        }
        std::shared_ptr<GameDb::Cover> cover;
        if (size > 8 && memcmp(buffer, "LCV1", 4) == 0) {
            uint16_t w = (buffer[4] << 8) | buffer[5], h = (buffer[6] << 8) | buffer[7];
            if (w > 0 && h > 0 && size >= 8 + (uint32_t) w * h * 2) {
                cover         = std::make_shared<GameDb::Cover>();
                cover->width  = w;
                cover->height = h;
                cover->pixels.resize((size_t) w * h);
                memcpy(cover->pixels.data(), buffer + 8, (size_t) w * h * 2);
            }
        }
        free(buffer);
        return cover;
    }

    //! GameTDB language code of the console language
    const char *languageCode() {
        static const char *codes[12] = {"JA", "EN", "FR", "DE", "IT", "ES", "ZHCN", "KO", "NL", "PT", "RU", "ZHTW"};
        uint32_t language = 1;
        int32_t handle    = UCOpen();
        if (handle >= 0) {
            alignas(0x40) uint32_t value = 1;
            alignas(0x40) UCSysConfig config;
            memset(&config, 0, sizeof(config));
            strncpy(config.name, "cafe.language", sizeof(config.name) - 1);
            config.dataType = UC_DATATYPE_UNSIGNED_INT;
            config.dataSize = sizeof(value);
            config.data     = &value;
            if (UCReadSysConfig(handle, 1, &config) == 0 && value < 12) {
                language = value;
            }
            UCClose(handle);
        }
        return codes[language];
    }

    // ---- cache file: "ID\ttitle\tsynopsis\tdeveloper\tpublisher\tyear\tgenre\tplayers\n" ----

    std::string clean(const std::string &in) {
        std::string out = in;
        for (auto &c : out) {
            if (c == '\t' || c == '\r') {
                c = ' ';
            } else if (c == '\n') {
                c = '\x1F'; // keep line breaks of the synopsis
            }
        }
        return out;
    }

    std::vector<std::string> splitTabs(const std::string &line) {
        std::vector<std::string> fields;
        size_t start = 0;
        while (true) {
            size_t tab = line.find('\t', start);
            fields.push_back(line.substr(start, tab == std::string::npos ? std::string::npos : tab - start));
            if (tab == std::string::npos) {
                break;
            }
            start = tab + 1;
        }
        return fields;
    }

    int64_t loadCache(std::map<std::string, GameDb::Info> &out) {
        uint8_t *buffer = nullptr;
        uint32_t size   = 0;
        if (FSUtils::LoadFileToMem(CACHE_FILE, &buffer, &size) <= 0 || buffer == nullptr) {
            return 0;
        }
        std::string text((const char *) buffer, size);
        free(buffer);
        int64_t checkedAt = 0;
        size_t pos        = 0;
        while (pos < text.size()) {
            size_t end = text.find('\n', pos);
            if (end == std::string::npos) {
                end = text.size();
            }
            std::string line = text.substr(pos, end - pos);
            pos              = end + 1;
            if (line.rfind("checked ", 0) == 0) {
                checkedAt = strtoll(line.c_str() + 8, nullptr, 10);
                continue;
            }
            auto f = splitTabs(line);
            if (f.size() < 8 || f[0].empty()) {
                continue;
            }
            for (auto &c : f[2]) {
                if (c == '\x1F') {
                    c = '\n';
                }
            }
            out[f[0]] = {f[1], f[2], f[3], f[4], f[5], f[6], f[7], f.size() > 8 && !f[8].empty() ? f[8] : f[0]};
        }
        return checkedAt;
    }

    void saveCache(const std::map<std::string, GameDb::Info> &data, int64_t checkedAt) {
        std::string text = "checked " + std::to_string((long long) checkedAt) + "\n";
        for (auto const &x : data) {
            const auto &i = x.second;
            text += x.first + "\t" + clean(i.title) + "\t" + clean(i.synopsis) + "\t" + clean(i.developer) + "\t" + clean(i.publisher) + "\t" +
                    clean(i.year) + "\t" + clean(i.genre) + "\t" + clean(i.players) + "\t" + i.tdbId + "\n";
        }
        FSUtils::CreateSubfolder(DIR_PATH);
        if (FSUtils::saveBufferToFile(CACHE_FILE, (void *) text.data(), text.size()) != (int32_t) text.size()) {
            DEBUG_FUNCTION_LINE("Failed to write %s", CACHE_FILE);
        }
    }

    // ---- network ----

    size_t onData(char *data, size_t size, size_t count, void *user) {
        auto *out = (std::string *) user;
        if (out->size() > 32 * 1024 * 1024) {
            return 0;
        }
        out->append(data, size * count);
        return size * count;
    }

    int onProgress(void *, double, double, double, double) {
        return stopRequested ? 1 : 0;
    }

    bool networkUp() {
        static bool connected = false;
        if (connected) {
            return true;
        }
        if (networkFailed) {
            return false;
        }
        ACInitialize();
        NNResult res = ACConnect();
        if (NNResult_IsFailure(res)) {
            DEBUG_FUNCTION_LINE("ACConnect failed: %08X", res.value);
            networkFailed = true;
            return false;
        }
        static bool curlReady = false;
        if (!curlReady) {
            curl_global_init(CURL_GLOBAL_DEFAULT);
            curlReady = true;
        }
        connected = true;
        return true;
    }

    bool download(const std::string &url, std::string &out) {
        if (!networkUp()) {
            return false;
        }
        CURL *curl = curl_easy_init();
        if (curl == nullptr) {
            return false;
        }
        out.clear();
        curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, onData);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &out);
        curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);
        curl_easy_setopt(curl, CURLOPT_PROGRESSFUNCTION, onProgress);
        curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 10L);
        curl_easy_setopt(curl, CURLOPT_TIMEOUT, 120L);
        curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
        curl_easy_setopt(curl, CURLOPT_USERAGENT, "launchiine (Wii U)");
        CURLcode res  = curl_easy_perform(curl);
        long httpCode = 0;
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &httpCode);
        curl_easy_cleanup(curl);
        DEBUG_FUNCTION_LINE("GET %s -> curl %d, HTTP %ld, %d bytes", url.c_str(), (int) res, httpCode, (int) out.size());
        return res == CURLE_OK && httpCode == 200 && !out.empty();
    }

    // ---- zip: the first file of the archive, stored or deflated ----

    bool unzipFirst(const std::string &zip, std::string &out) {
        auto u16 = [&](size_t p) { return (uint32_t) (uint8_t) zip[p] | ((uint32_t) (uint8_t) zip[p + 1] << 8); };
        auto u32 = [&](size_t p) { return u16(p) | (u16(p + 2) << 16); };
        if (zip.size() < 30 || u32(0) != 0x04034b50) {
            return false;
        }
        uint32_t method = u16(8);
        uint32_t flags  = u16(6);
        uint32_t csize  = u32(18);
        uint32_t usize  = u32(22);
        size_t data     = 30 + u16(26) + u16(28);
        if (flags & 0x08) {
            //! sizes are in the central directory
            size_t eocd = zip.rfind(std::string("PK\x05\x06", 4));
            if (eocd == std::string::npos) {
                return false;
            }
            size_t cd = u32(eocd + 16);
            if (cd + 46 > zip.size() || u32(cd) != 0x02014b50) {
                return false;
            }
            csize = u32(cd + 20);
            usize = u32(cd + 24);
        }
        if (data + csize > zip.size()) {
            return false;
        }
        if (method == 0) {
            out = zip.substr(data, csize);
            return true;
        }
        if (method != 8) {
            return false;
        }
        out.resize(usize);
        z_stream stream{};
        if (inflateInit2(&stream, -MAX_WBITS) != Z_OK) {
            return false;
        }
        stream.next_in   = (Bytef *) zip.data() + data;
        stream.avail_in  = csize;
        stream.next_out  = (Bytef *) &out[0];
        stream.avail_out = usize;
        int res          = inflate(&stream, Z_FINISH);
        inflateEnd(&stream);
        return res == Z_STREAM_END;
    }

    // ---- xml ----

    std::string decodeEntities(const std::string &in) {
        std::string out;
        out.reserve(in.size());
        for (size_t i = 0; i < in.size(); i++) {
            if (in[i] != '&') {
                out += in[i];
                continue;
            }
            size_t semi = in.find(';', i);
            if (semi == std::string::npos || semi - i > 10) {
                out += in[i];
                continue;
            }
            std::string e = in.substr(i + 1, semi - i - 1);
            if (e == "amp") {
                out += '&';
            } else if (e == "lt") {
                out += '<';
            } else if (e == "gt") {
                out += '>';
            } else if (e == "quot") {
                out += '"';
            } else if (e == "apos") {
                out += '\'';
            } else if (!e.empty() && e[0] == '#') {
                uint32_t cp = e.size() > 1 && (e[1] == 'x' || e[1] == 'X') ? strtoul(e.c_str() + 2, nullptr, 16) : strtoul(e.c_str() + 1, nullptr, 10);
                //! UTF-8
                if (cp < 0x80) {
                    out += (char) cp;
                } else if (cp < 0x800) {
                    out += (char) (0xC0 | (cp >> 6));
                    out += (char) (0x80 | (cp & 0x3F));
                } else {
                    out += (char) (0xE0 | (cp >> 12));
                    out += (char) (0x80 | ((cp >> 6) & 0x3F));
                    out += (char) (0x80 | (cp & 0x3F));
                }
            } else {
                out += in.substr(i, semi - i + 1);
            }
            i = semi;
        }
        return out;
    }

    //! text of <tag>..</tag> inside [from, to)
    std::string tagText(const std::string &xml, const char *tag, size_t from, size_t to) {
        std::string open = std::string("<") + tag + ">";
        size_t start     = xml.find(open, from);
        if (start == std::string::npos || start >= to) {
            return "";
        }
        start += open.size();
        size_t end = xml.find(std::string("</") + tag + ">", start);
        if (end == std::string::npos || end > to) {
            return "";
        }
        return decodeEntities(xml.substr(start, end - start));
    }

    //! value of attr="..." of the first <tag ...> inside [from, to)
    std::string tagAttribute(const std::string &xml, const char *tag, const char *attr, size_t from, size_t to) {
        size_t start = xml.find(std::string("<") + tag + " ", from);
        if (start == std::string::npos || start >= to) {
            return "";
        }
        size_t close = xml.find('>', start);
        std::string key = std::string(attr) + "=\"";
        size_t a        = xml.find(key, start);
        if (a == std::string::npos || a > close) {
            return "";
        }
        a += key.size();
        size_t b = xml.find('"', a);
        return b == std::string::npos ? "" : xml.substr(a, b - a);
    }

    //! "Resident Evil™ 2" -> "residentevil2": names compared without case, symbols and spaces
    std::string normalizeName(const std::string &in) {
        std::string out;
        for (unsigned char c : in) {
            if (isalnum(c)) {
                out += (char) tolower(c);
            }
        }
        if (out.rfind("the", 0) == 0 && out.size() > 6) {
            out = out.substr(3);
        }
        return out;
    }

    //! words of a name, lower case, letters and digits only
    std::vector<std::string> nameWords(const std::string &in) {
        std::vector<std::string> words;
        std::string word;
        for (unsigned char c : in + " ") {
            if (isalnum(c)) {
                word += (char) tolower(c);
            } else if (!word.empty()) {
                if (word.size() >= 2 || isdigit((unsigned char) word[0])) {
                    words.push_back(word);
                }
                word.clear();
            }
        }
        return words;
    }

    //! Does the GameTDB title fit our name? Injects carry the product code of the Wii U title
    //! they were built from, so a match by code can be a completely different game.
    bool namesFit(const std::string &ours, const std::string &theirs) {
        auto a = nameWords(ours), b = nameWords(theirs);
        if (a.empty() || b.empty()) {
            return true; // nothing to compare
        }
        int common = 0;
        for (auto &w : a) {
            common += std::find(b.begin(), b.end(), w) != b.end();
        }
        return common * 2 >= (int) a.size();
    }

    //! when a name matches several releases: US, then Europe, then the rest
    int regionRank(const std::string &tdbId) {
        char r = tdbId.size() >= 4 ? tdbId[3] : '?';
        return r == 'E' ? 3 : r == 'P' ? 2 : r == 'J' ? 1 : 0;
    }

    //! name -> key for the games still without info
    std::map<std::string, std::string> namesToMatch(const std::map<std::string, GameDb::Info> &known) {
        std::map<std::string, std::string> names;
        for (auto &x : wantedNames) {
            std::string n = normalizeName(x.second);
            if (known.count(x.first) == 0 && n.size() >= 4) {
                names.emplace(n, x.first);
            }
        }
        return names;
    }

    //! Unpacks the first file of a zip piece by piece and hands every <game>..</game> block
    //! over; the Wii database is too large to unpack into memory at once.
    bool forEachGameInZip(const std::string &zip, const std::function<void(const std::string &)> &onGame) {
        auto u16 = [&](size_t p) { return (uint32_t) (uint8_t) zip[p] | ((uint32_t) (uint8_t) zip[p + 1] << 8); };
        auto u32 = [&](size_t p) { return u16(p) | (u16(p + 2) << 16); };
        if (zip.size() < 30 || u32(0) != 0x04034b50 || u16(8) != 8) {
            return false;
        }
        size_t data = 30 + u16(26) + u16(28);
        if (data >= zip.size()) {
            return false;
        }
        z_stream stream{};
        if (inflateInit2(&stream, -MAX_WBITS) != Z_OK) {
            return false;
        }
        stream.next_in  = (Bytef *) zip.data() + data;
        stream.avail_in = zip.size() - data;
        std::vector<char> chunk(256 * 1024);
        std::string pending;
        int res = Z_OK;
        while (res == Z_OK && !stopRequested) {
            stream.next_out  = (Bytef *) chunk.data();
            stream.avail_out = chunk.size();
            res              = inflate(&stream, Z_NO_FLUSH);
            if (res != Z_OK && res != Z_STREAM_END) {
                break;
            }
            pending.append(chunk.data(), chunk.size() - stream.avail_out);
            size_t pos = 0;
            while (true) {
                size_t start = pending.find("<game ", pos);
                if (start == std::string::npos) {
                    break;
                }
                size_t end = pending.find("</game>", start);
                if (end == std::string::npos) {
                    pos = start;
                    break;
                }
                onGame(pending.substr(start, end + 7 - start));
                pos = end + 7;
            }
            pending.erase(0, pos);
        }
        inflateEnd(&stream);
        return res == Z_STREAM_END;
    }

    GameDb::Info readGame(const std::string &xml, size_t pos, size_t end, const std::string &localeTag) {
        GameDb::Info info;
        size_t locale = xml.find(localeTag, pos);
        if (locale != std::string::npos && locale < end) {
            size_t localeEnd = xml.find("</locale>", locale);
            info.title       = tagText(xml, "title", locale, localeEnd);
            info.synopsis    = tagText(xml, "synopsis", locale, localeEnd);
        }
        info.developer = tagText(xml, "developer", pos, end);
        info.publisher = tagText(xml, "publisher", pos, end);
        info.year      = tagAttribute(xml, "date", "year", pos, end);
        info.genre     = tagText(xml, "genre", pos, end);
        info.players   = tagAttribute(xml, "input", "players", pos, end);
        return info;
    }

    //! injects: their product code is the one of the Wii U title they were built from, so
    //! they are found by name in the Wii / GameCube database
    void matchWiiByName(std::map<std::string, GameDb::Info> &out) {
        auto names = namesToMatch(out);
        if (names.empty() || stopRequested) {
            return;
        }
        for (auto &n : names) {
            DEBUG_FUNCTION_LINE("Looking for \"%s\" (%s) in the Wii/GameCube database", wantedNames[n.second].c_str(), n.first.c_str());
        }
        std::string zip;
        if (!download(WII_DB_URL, zip)) {
            return;
        }
        std::map<std::string, int> rank;
        int found = 0;
        bool ok   = forEachGameInZip(zip, [&](const std::string &game) {
            size_t end       = game.size();
            size_t en        = game.find("<locale lang=\"EN\">");
            std::string name = en == std::string::npos ? "" : tagText(game, "title", en, game.find("</locale>", en));
            auto match       = names.find(normalizeName(name));
            if (match == names.end()) {
                return;
            }
            std::string tdbId = tagText(game, "id", 0, end);
            int r             = regionRank(tdbId);
            auto previous     = rank.find(match->second);
            if (previous != rank.end() && previous->second >= r) {
                return;
            }
            rank[match->second] = r;
            GameDb::Info info   = readGame(game, 0, end, "<locale lang=\"EN\">");
            info.tdbId          = "wii/" + tdbId;
            if (previous == rank.end()) {
                found++;
            }
            out[match->second] = info;
        });
        DEBUG_FUNCTION_LINE("GameTDB Wii/GameCube: %d of %d found by name%s", found, (int) names.size(), ok ? "" : " (database incomplete)");
    }

    //! Reads the games we want from one database download. The locale is the language of the
    //! download (with LANG=PT the English synopsis is left out, so English is a second pass).
    void parseDatabase(const std::string &xml, const std::set<std::string> &ids, const char *lang, std::map<std::string, GameDb::Info> &out,
                       const std::map<std::string, std::string> &names = {}) {
        //! eShop games are listed with 4 characters only ("BMTE" for our "BMTETL")
        std::map<std::string, std::string> shortIds;
        for (auto &id : ids) {
            shortIds.emplace(id.substr(0, 4), id);
        }
        size_t pos               = 0;
        int found                = 0;
        std::set<std::string> rejected;
        std::string localeWanted = std::string("<locale lang=\"") + lang + "\">";
        while ((pos = xml.find("<game ", pos)) != std::string::npos) {
            size_t end = xml.find("</game>", pos);
            if (end == std::string::npos) {
                break;
            }
            std::string tdbId = tagText(xml, "id", pos, end);
            std::string ours;
            if (ids.count(tdbId) != 0) {
                ours = tdbId;
            } else if (tdbId.size() == 4 && shortIds.count(tdbId) != 0) {
                ours = shortIds[tdbId];
            }
            if (!ours.empty() && !names.empty()) {
                //! English pass: the code must also fit the name, or it is an inject
                size_t en = xml.find("<locale lang=\"EN\">", pos);
                std::string title = en != std::string::npos && en < end ? tagText(xml, "title", en, xml.find("</locale>", en)) : "";
                auto ourName      = wantedNames.find(ours);
                if (ourName != wantedNames.end() && !namesFit(ourName->second, title)) {
                    DEBUG_FUNCTION_LINE("%s: \"%s\" is not \"%s\", looking it up by name", ours.c_str(), ourName->second.c_str(), title.c_str());
                    rejected.insert(ours);
                    ours.clear();
                }
            }
            if (ours.empty() && !names.empty()) {
                //! not found by its code (e.g. an inject): by its English name
                size_t en = xml.find("<locale lang=\"EN\">", pos);
                if (en != std::string::npos && en < end) {
                    auto match = names.find(normalizeName(tagText(xml, "title", en, xml.find("</locale>", en))));
                    if (match != names.end() && out.count(match->second) == 0) {
                        ours = match->second;
                    }
                }
            }
            if (!ours.empty()) {
                GameDb::Info info;
                info.tdbId    = tdbId;
                size_t locale = xml.find(localeWanted, pos);
                if (locale != std::string::npos && locale < end) {
                    size_t localeEnd = xml.find("</locale>", locale);
                    info.title       = tagText(xml, "title", locale, localeEnd);
                    info.synopsis    = tagText(xml, "synopsis", locale, localeEnd);
                }
                info.developer = tagText(xml, "developer", pos, end);
                info.publisher = tagText(xml, "publisher", pos, end);
                info.year      = tagAttribute(xml, "date", "year", pos, end);
                info.genre     = tagText(xml, "genre", pos, end);
                info.players   = tagAttribute(xml, "input", "players", pos, end);
                out[ours]      = info;
                found++;
            }
            pos = end + 7;
        }
        DEBUG_FUNCTION_LINE("GameTDB (%s): %d of %d games found", lang, found, (int) ids.size());
    }

    bool readDatabase(const char *lang, const std::set<std::string> &ids, std::map<std::string, GameDb::Info> &out) {
        std::string zip;
        uint8_t *buffer = nullptr;
        uint32_t size   = 0;
        if (FSUtils::LoadFileToMem(MANUAL_ZIP, &buffer, &size) > 0 && buffer != nullptr) {
            DEBUG_FUNCTION_LINE("Using %s", MANUAL_ZIP);
            zip.assign((const char *) buffer, size);
            free(buffer);
        } else if (!download(std::string(DB_URL) + lang, zip)) {
            return false;
        }
        std::string xml;
        if (!unzipFirst(zip, xml)) {
            DEBUG_FUNCTION_LINE("Could not unpack the GameTDB database (%d bytes)", (int) zip.size());
            return false;
        }
        zip.clear();
        zip.shrink_to_fit();
        //! by name only in the English pass (the names of the titles are compared with it)
        std::map<std::string, std::string> names;
        if (strcmp(lang, "EN") == 0) {
            std::map<std::string, GameDb::Info> none;
            names = namesToMatch(none); // a match by id later in the file still wins
        }
        parseDatabase(xml, ids, lang, out, names);
        return true;
    }

    //! console language first, every empty field filled from English
    bool loadDatabase(const std::set<std::string> &ids, std::map<std::string, GameDb::Info> &out) {
        std::string lang = languageCode();
        std::map<std::string, GameDb::Info> english;
        if (!readDatabase("EN", ids, english)) {
            return false;
        }
        if (lang == "EN" || stopRequested) {
            out.swap(english);
            matchWiiByName(out);
            return true;
        }
        std::map<std::string, GameDb::Info> local;
        readDatabase(lang.c_str(), ids, local); // without it English is still fine
        out.swap(english);
        matchWiiByName(out);
        for (auto &x : local) {
            if (out.count(x.first) == 0) {
                continue; // e.g. an inject: its code points at another game
            }
            GameDb::Info &target = out[x.first];
            const GameDb::Info &l = x.second;
            for (auto field : {&GameDb::Info::title, &GameDb::Info::synopsis, &GameDb::Info::developer, &GameDb::Info::publisher,
                               &GameDb::Info::year, &GameDb::Info::genre, &GameDb::Info::players, &GameDb::Info::tdbId}) {
                if (!(l.*field).empty()) {
                    target.*field = l.*field;
                }
            }
        }
        return true;
    }

    //! front cover, by region of the id (4th letter), larger first
    std::shared_ptr<GameDb::Cover> downloadCover(const std::string &fullId, const std::string &savePath) {
        //! "wii/G2RE08": a Wii / GameCube cover (PNG), otherwise a Wii U one (JPEG)
        bool wii       = fullId.rfind("wii/", 0) == 0;
        std::string id = wii ? fullId.substr(4) : fullId;
        char region = id.size() >= 4 ? id[3] : 'E';
        std::vector<std::string> regions;
        if (region == 'J') {
            regions = {"JA", "US"};
        } else if (region == 'P') {
            regions = {"EN", "PT", "FR", "DE", "ES", "IT", "US"};
        } else {
            regions = {"US", "EN"};
        }
        for (const char *kind : {"coverHQ", "coverM", "cover"}) {
            for (auto &r : regions) {
                if (stopRequested) {
                    return nullptr;
                }
                std::string data;
                if (download(std::string(wii ? WII_COVER_URL : COVER_URL) + kind + "/" + r + "/" + id + (wii ? ".png" : ".jpg"), data)) {
                    auto cover = jpegToCover(data);
                    if (cover == nullptr) {
                        DEBUG_FUNCTION_LINE("Could not decode the cover of %s", id.c_str());
                        return nullptr;
                    }
                    saveCover(savePath, *cover);
                    return cover;
                }
                if (networkFailed) {
                    return nullptr;
                }
            }
        }
        return nullptr;
    }

    void run() {
        std::set<std::string> ids;
        {
            std::lock_guard<std::mutex> guard(lock);
            ids = wantedIds;
        }
        std::map<std::string, GameDb::Info> cached;
        int64_t checkedAt = loadCache(cached);
        {
            std::lock_guard<std::mutex> guard(lock);
            infos = cached;
        }
        changeCounter++;

        bool missing = false;
        for (auto &id : ids) {
            missing |= cached.count(id) == 0;
        }
        int64_t now = (int64_t) time(nullptr);
        if (missing && (checkedAt == 0 || now - checkedAt > RECHECK_SECONDS || now < checkedAt) && !stopRequested) {
            std::map<std::string, GameDb::Info> fresh;
            if (loadDatabase(ids, fresh) && !stopRequested) {
                for (auto &x : fresh) {
                    cached[x.first] = x.second;
                }
                saveCache(cached, now);
                std::lock_guard<std::mutex> guard(lock);
                infos = cached;
                changeCounter++;
            }
        }

        //! covers: first what the TV waits for, then the pages around the selection
        while (!stopRequested) {
            std::string id;
            {
                std::unique_lock<std::mutex> guard(lock);
                auto cached = [](const std::string &key) {
                    for (auto &entry : coverCache) {
                        if (entry.first == key) {
                            return true;
                        }
                    }
                    return coverMissing.count(key) != 0;
                };
                while (!prefetch.empty() && cached(prefetch.front())) {
                    prefetch.pop_front();
                }
                wake.wait(guard, [&] { return stopRequested || (!coverRequest.empty() && !cached(coverRequest)) || !prefetch.empty(); });
                if (stopRequested) {
                    break;
                }
                if (!coverRequest.empty() && !cached(coverRequest)) {
                    id = coverRequest;
                } else if (!prefetch.empty()) {
                    id = prefetch.front();
                    prefetch.pop_front();
                    if (cached(id)) {
                        continue;
                    }
                } else {
                    continue;
                }
            }

            OSTime loadStart                     = OSGetTime();
            std::shared_ptr<GameDb::Cover> cover = loadCover(coverFile(id));
            if (cover != nullptr) {
                DEBUG_FUNCTION_LINE("Cover %s from the SD card in %lld ms", id.c_str(), OSTicksToMilliseconds(OSGetTime() - loadStart));
            }
            if (cover == nullptr && !networkFailed) {
                std::string tdbId = id;
                {
                    std::lock_guard<std::mutex> guard(lock);
                    auto info = infos.find(id);
                    if (info != infos.end() && !info->second.tdbId.empty()) {
                        tdbId = info->second.tdbId;
                    }
                }
                cover = downloadCover(tdbId, coverFile(id));
            }
            std::lock_guard<std::mutex> guard(lock);
            if (cover == nullptr) {
                coverMissing.insert(id);
            } else {
                coverCache.emplace_back(id, cover);
                while (coverCache.size() > COVER_CACHE_SIZE) {
                    coverCache.pop_front();
                }
            }
            changeCounter++;
        }
    }
} // namespace

std::string GameDb::idFromProductCode(const std::string &productCode) {
    //! "WUP-P-AMKE/0001": the last 4 characters of the product code and of the company code
    size_t slash = productCode.find('/');
    if (slash == std::string::npos || slash < 4 || productCode.size() < slash + 3) {
        return "";
    }
    std::string product = productCode.substr(slash - 4, 4);
    std::string company = productCode.substr(productCode.size() - 2);
    for (char c : product + company) {
        if (!isalnum((unsigned char) c)) {
            return "";
        }
    }
    return product + company;
}

void GameDb::start(const std::vector<std::pair<std::string, std::string>> &games) {
    if (worker.valid()) {
        return;
    }
    {
        std::lock_guard<std::mutex> guard(lock);
        wantedIds.clear();
        wantedNames.clear();
        for (auto &game : games) {
            if (!game.first.empty()) {
                wantedIds.insert(game.first);
                wantedNames[game.first] = game.second;
            }
        }
    }
    stopRequested = false;
    worker        = std::async(std::launch::async, run);
}

void GameDb::stop() {
    {
        std::lock_guard<std::mutex> guard(lock);
        stopRequested = true;
    }
    wake.notify_all();
    if (worker.valid()) {
        worker.wait();
    }
}

bool GameDb::get(const std::string &id, Info &out) {
    std::lock_guard<std::mutex> guard(lock);
    auto it = infos.find(id);
    if (it == infos.end()) {
        return false;
    }
    out = it->second;
    return true;
}

GameDb::CoverState GameDb::getCover(const std::string &id, std::shared_ptr<const Cover> &out) {
    if (id.empty()) {
        return CoverState::None;
    }
    std::lock_guard<std::mutex> guard(lock);
    for (auto it = coverCache.begin(); it != coverCache.end(); ++it) {
        if (it->first == id) {
            out = it->second;
            //! most recently used to the back
            auto entry = *it;
            coverCache.erase(it);
            coverCache.push_back(entry);
            return CoverState::Ready;
        }
    }
    if (coverMissing.count(id) != 0) {
        return CoverState::None;
    }
    if (coverRequest != id) {
        coverRequest = id;
        wake.notify_all();
    }
    return CoverState::Pending;
}

void GameDb::prefetchCovers(const std::vector<std::string> &ids) {
    std::lock_guard<std::mutex> guard(lock);
    prefetch.assign(ids.begin(), ids.end());
    wake.notify_all();
}

uint32_t GameDb::generation() {
    return changeCounter;
}
