#ifndef RCBOT_LONG_TERM_MEMORY_H
#define RCBOT_LONG_TERM_MEMORY_H

#include <string>
#include <vector>
#include <map> // For gametype CVARs or mod flags
#include "rcbot_short_term_memory.h" // For GameEvent

// Forward declaration
class RCBotLongTermMemory;

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

    // Archives an episode by serializing it to a file
    // The filename could be generated from metadata (map, timestamp)
    // For now, assumes episodes are stored in a predefined directory e.g., "rcbot/episodes/"
    void archiveEpisode(const Episode& episode);

    // Retrieves episodes based on query parameters
    // This could involve listing files, parsing metadata, and loading matching episodes.
    // For simplicity, this might initially just load all episodes from a map or specific ones.
    std::vector<Episode> retrieveEpisodes(const std::string& mapName, const std::string& gametypeFilter = "");

    // Helper function to load an episode from a file (implementation specific)
    bool loadEpisodeFromFile(const std::string& filePath, Episode& outEpisode);

    // Helper function to save an episode to a file (implementation specific)
    bool saveEpisodeToFile(const std::string& filePath, const Episode& episode);

private:
    std::string episodeStoragePath; // e.g., "rcbot/episodes/"
    std::vector<Episode> loadedEpisodes; // In-memory cache of some loaded episodes, if needed

    // Generates a filename for an episode based on its metadata
    std::string generateEpisodeFilename(const EpisodeMetadata& metadata) const;
};

#endif // RCBOT_LONG_TERM_MEMORY_H
