#pragma once

#include <cstdint>
#include <string>
#include <vector>

/**
 * Game information from GameTDB (https://www.gametdb.com) for the TV view: official
 * description, publisher, year, genre, players and the cover.
 *
 * A worker thread downloads the Wii U database once (wiiutdb.zip, in the console language),
 * keeps only the installed games in sd:/wiiu/launchiine/gamedb.txt and downloads covers on
 * demand to sd:/wiiu/launchiine/covers/<ID>.tga (decoded and scaled once, so showing a cover
 * does not stall the menu). A wiiutdb.zip copied to
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

    //! Starts the worker for these GameTDB ids. Any thread.
    void start(const std::vector<std::string> &ids);

    //! Asks the worker to stop and waits for it.
    void stop();

    //! false while the info is unknown (not downloaded yet / not in the database)
    bool get(const std::string &id, Info &out);

    //! Asks the worker for the cover of this game (only the latest request counts).
    void requestCover(const std::string &id);

    enum class CoverState { Pending, Ready, None };
    //! Result of requestCover: Ready fills tga (an uncompressed TGA, cheap to turn into a
    //! texture), None when the game has no cover. Render thread.
    CoverState takeCover(const std::string &id, std::vector<uint8_t> &tga);

    //! changes whenever new info or a new cover arrived
    uint32_t generation();
} // namespace GameDb
