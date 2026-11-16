#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

/**
 * Game information from GameTDB (https://www.gametdb.com) for the TV view: official
 * description, publisher, year, genre, players and the cover.
 *
 * A worker thread downloads the Wii U database once (wiiutdb.zip, in the console language),
 * keeps only the installed games in sd:/wiiu/launchiine/gamedb.txt and downloads covers on
 * demand to sd:/wiiu/launchiine/covers/<ID>.lcv (decoded and scaled once to raw RGB565, so
 * showing a cover is a plain copy). A wiiutdb.zip copied to
 * sd:/wiiu/launchiine/ by hand is used as well.
 */
namespace GameDb {
    struct Info {
        std::string title;
        std::string synopsis;
        std::string developer;
        std::string publisher;
        std::string year;
        std::string genre;
        std::string players;
        std::string tdbId; // GameTDB's id: eShop games only have 4 characters ("BMTE")
    };

    //! "WUP-P-AMKE/0001" -> "AMKE01", empty when the product code has no GameTDB id
    std::string idFromProductCode(const std::string &productCode);

    //! Starts the worker for these games: (key, name). The key is the GameTDB id from the
    //! product code (or any unique key); games not found by it are looked up by name, in the
    //! Wii U and then in the Wii / GameCube database (injects). Any thread.
    void start(const std::vector<std::pair<std::string, std::string>> &games);

    //! Asks the worker to stop and waits for it.
    void stop();

    //! false while the info is unknown (not downloaded yet / not in the database)
    bool get(const std::string &id, Info &out);

    //! decoded cover: RGB565 pixels, ready to be copied into a texture
    struct Cover {
        uint16_t width  = 0;
        uint16_t height = 0;
        std::vector<uint16_t> pixels;
    };

    enum class CoverState { Pending, Ready, None };
    //! Ready: the cover is in memory. Pending: the worker loads it (from the SD card or
    //! GameTDB), ask again in the next frames. None: the game has no cover. Render thread.
    CoverState getCover(const std::string &id, std::shared_ptr<const Cover> &out);

    //! Loads these covers into memory in the background (the pages around the selection),
    //! so they show up instantly. Replaces the previous list.
    void prefetchCovers(const std::vector<std::string> &ids);

    //! changes whenever new info or a new cover arrived
    uint32_t generation();
} // namespace GameDb
