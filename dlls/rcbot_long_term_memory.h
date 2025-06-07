#ifndef RCBOT_LONG_TERM_MEMORY_H
#define RCBOT_LONG_TERM_MEMORY_H

#include <string>
#include <vector>
#include <map> // For gametype CVARs or mod flags
#include "rcbot_short_term_memory.h" // For GameEvent
#include "sqlite/sqlite3.h"      // For SQLite integration

// Forward declaration - no longer needed as RCBotLongTermMemory is defined below.
// class RCBotLongTermMemory;

// Structure to hold metadata for an episode
struct EpisodeMetadata {
    std::string mapName;
    std::map<std::string, std::string> gameCvars; // e.g., mp_timelimit, mp_fraglimit
    std::vector<std::string> modFlags; // e.g., "low_gravity", "instagib"
    long timestamp; // Unix timestamp or similar
    std::string outcome; // "win", "loss", "draw", or game-specific result

    // Default constructor
    EpisodeMetadata() : timestamp(0) {}
};

// Structure to represent a complete episode
struct Episode {
    EpisodeMetadata metadata;
    std::vector<GameEvent> events; // Sequence of game events from the episode
};

// Class to manage long-term memory (episodes) for the bot
class RCBotLongTermMemory {
public:
    RCBotLongTermMemory();
    ~RCBotLongTermMemory(); // Added destructor to close DB

    // Archives an episode by serializing it to the database
    void archiveEpisode(const Episode& episode);

    // Retrieves episodes based on query parameters from the database
    std::vector<Episode> retrieveEpisodes(const std::string& mapName, const std::string& gametypeFilter = "");

    // (File-based helpers loadEpisodeFromFile, saveEpisodeToFile, generateEpisodeFilename will be removed or commented out in .cpp)

private:
    sqlite3* m_db; // SQLite database connection
    // std::string episodeStoragePath; // No longer needed for primary storage if using DB
    // std::vector<Episode> loadedEpisodes; // Cache might still be useful, but not part of this DB setup step

    // Helper to execute simple SQL statements (CREATE, INSERT, UPDATE, DELETE without results)
    bool executeSQL(const std::string& sql_statement);

    // Helper to set up database schema (create tables)
    void initializeDatabase();
};

#endif // RCBOT_LONG_TERM_MEMORY_H
