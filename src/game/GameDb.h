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
 * demand to sd:/wiiu/launchiine/covers/<ID>.jpg. A wiiutdb.zip copied to
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
    };

    //! "WUP-P-AMKE/0001" -> "AMKE01", empty when the product code has no GameTDB id
    std::string idFromProductCode(const std::string &productCode);

    //! Starts the worker for these GameTDB ids. Any thread.
    void start(const std::vector<std::string> &ids);

    //! Asks the worker to stop and waits for it.
    void stop();

    //! false while the info is unknown (not downloaded yet / not in the database)
    bool get(const std::string &id, Info &out);

    //! path of the downloaded cover, empty when there is none (yet); asks the worker for it
    std::string coverPath(const std::string &id);

    //! changes whenever new info or a new cover arrived
    uint32_t generation();
} // namespace GameDb
