#include "rcbot_long_term_memory.h"
#include "extdll.h" // For gpGlobals if needed for timestamps or game specific data
#include "util.h" // For UTIL_LogPrintf or other logging if available

#include <fstream>
#include <sstream>
#include <iomanip> // For std::setw, std::setfill with timestamps
#include <algorithm> // For std::replace
#include <filesystem> // For directory creation and file listing (C++17)
                     // If C++17 <filesystem> is not available, platform-specific code or
                     // simpler directory handling would be needed. For this subtask,
                     // we'll assume it's available or that basic file operations in a known dir will work.

// Define a subdirectory for episodes. This path is relative to the game's execution path.
// Half-Life typically runs from the main game directory (e.g., "Half-Life").
// Metamod might allow access to create "rcbot/episodes" there.
const std::string DEFAULT_EPISODE_STORAGE_PATH = "rcbot/episodes/";

// Helper function to ensure a directory exists
// Note: Requires <filesystem>
void EnsureDirectoryExists(const std::string& path) {
    try {
        if (!std::filesystem::exists(path)) {
            std::filesystem::create_directories(path);
        }
    } catch (const std::filesystem::filesystem_error& e) {
        // Log error - using UTIL_LogPrintf if available, otherwise fallback
        // For now, just printing to stderr as an example
        fprintf(stderr, "Filesystem error: %s\n", e.what());
    }
}


RCBotLongTermMemory::RCBotLongTermMemory() : episodeStoragePath(DEFAULT_EPISODE_STORAGE_PATH) {
    EnsureDirectoryExists(episodeStoragePath);
}

std::string RCBotLongTermMemory::generateEpisodeFilename(const EpisodeMetadata& metadata) const {
    std::stringstream ss;
    ss << metadata.mapName << "_";

    // Create a simplified gametype string from cvars for the filename
    std::string gameTypeStr;
    for(const auto& pair : metadata.gameCvars) {
        gameTypeStr += pair.first + "_" + pair.second + "_";
    }
    if (!gameTypeStr.empty()) {
       gameTypeStr.pop_back(); // remove last "_"
    } else {
        gameTypeStr = "default";
    }
    // Sanitize gameTypeStr for filename (replace non-alphanumeric)
    std::replace_if(gameTypeStr.begin(), gameTypeStr.end(), [](char c){ return !isalnum(c) && c != '_'; }, '_');
    ss << gameTypeStr << "_";

    ss << metadata.timestamp << ".episode";

    std::string filename = ss.str();
    // Sanitize the whole filename to be safe
    std::replace_if(filename.begin(), filename.end(), [](char c){ return !isalnum(c) && c != '.' && c != '_'; }, '_');
    return filename;
}

bool RCBotLongTermMemory::saveEpisodeToFile(const std::string& filePath, const Episode& episode) {
    std::ofstream outFile(filePath);
    if (!outFile.is_open()) {
        // Log error
        fprintf(stderr, "Error: Could not open file for writing: %s\n", filePath.c_str());
        return false;
    }

    // --- Save Metadata ---
    outFile << "MapName: " << episode.metadata.mapName << std::endl;
    outFile << "Timestamp: " << episode.metadata.timestamp << std::endl;
    outFile << "Outcome: " << episode.metadata.outcome << std::endl;

    outFile << "GameCvars_Count: " << episode.metadata.gameCvars.size() << std::endl;
    for (const auto& pair : episode.metadata.gameCvars) {
        outFile << pair.first << ": " << pair.second << std::endl;
    }

    outFile << "ModFlags_Count: " << episode.metadata.modFlags.size() << std::endl;
    for (const auto& flag : episode.metadata.modFlags) {
        outFile << flag << std::endl;
    }

    // --- Save Events ---
    outFile << "Events_Count: " << episode.events.size() << std::endl;
    for (const auto& event : episode.events) {
        outFile << static_cast<int>(event.type) << " "
                << event.timestamp << " "
                << event.damageAmount; // Assuming GameEvent structure for now
        // Add other event data members here, separated by spaces
        outFile << std::endl;
    }

    outFile.close();
    return true;
}

void RCBotLongTermMemory::archiveEpisode(const Episode& episode) {
    EnsureDirectoryExists(episodeStoragePath); // Ensure directory exists before saving
    std::string filename = generateEpisodeFilename(episode.metadata);
    std::string fullPath = episodeStoragePath + filename;

    if (!saveEpisodeToFile(fullPath, episode)) {
        // Log error
        fprintf(stderr, "Error: Failed to archive episode to %s\n", fullPath.c_str());
    } else {
        // Optionally, add to a list of loaded/indexed episodes if keeping some in memory
        // loadedEpisodes.push_back(episode);
        // fprintf(stdout, "Successfully archived episode to %s\n", fullPath.c_str());
    }
}


bool RCBotLongTermMemory::loadEpisodeFromFile(const std::string& filePath, Episode& outEpisode) {
    std::ifstream inFile(filePath);
    if (!inFile.is_open()) {
        fprintf(stderr, "Error: Could not open file for reading: %s\n", filePath.c_str());
        return false;
    }

    std::string line;
    try {
        // --- Load Metadata ---
        std::getline(inFile, line); outEpisode.metadata.mapName = line.substr(line.find(": ") + 2);
        std::getline(inFile, line); outEpisode.metadata.timestamp = std::stol(line.substr(line.find(": ") + 2));
        std::getline(inFile, line); outEpisode.metadata.outcome = line.substr(line.find(": ") + 2);

        std::getline(inFile, line); int cvarsCount = std::stoi(line.substr(line.find(": ") + 2));
        for (int i = 0; i < cvarsCount; ++i) {
            std::getline(inFile, line);
            size_t colonPos = line.find(": ");
            outEpisode.metadata.gameCvars[line.substr(0, colonPos)] = line.substr(colonPos + 2);
        }

        std::getline(inFile, line); int flagsCount = std::stoi(line.substr(line.find(": ") + 2));
        for (int i = 0; i < flagsCount; ++i) {
            std::getline(inFile, line);
            outEpisode.metadata.modFlags.push_back(line);
        }

        // --- Load Events ---
        std::getline(inFile, line); int eventsCount = std::stoi(line.substr(line.find(": ") + 2));
        outEpisode.events.reserve(eventsCount);
        for (int i = 0; i < eventsCount; ++i) {
            int typeInt;
            float timestamp, damageAmount; // Extend as per GameEvent
            inFile >> typeInt >> timestamp >> damageAmount;
            // Add other event data members here

            // Basic GameEvent constructor assumed.
            // This needs to match the GameEvent definition from rcbot_short_term_memory.h
            outEpisode.events.emplace_back(static_cast<GameEventType>(typeInt), timestamp, damageAmount);
            std::getline(inFile, line); // Consume rest of the line
        }
    } catch (const std::exception& e) {
        fprintf(stderr, "Error parsing episode file %s: %s\n", filePath.c_str(), e.what());
        inFile.close();
        return false;
    }

    inFile.close();
    return true;
}


std::vector<Episode> RCBotLongTermMemory::retrieveEpisodes(const std::string& mapNameFilter, const std::string& gametypeFilter) {
    std::vector<Episode> matchedEpisodes;
    EnsureDirectoryExists(episodeStoragePath);

    try {
        for (const auto& entry : std::filesystem::directory_iterator(episodeStoragePath)) {
            if (entry.is_regular_file()) {
                std::string filename = entry.path().filename().string();
                // Basic filtering: check if filename contains mapName
                // More sophisticated parsing of filename or file content would be needed for gametypeFilter
                if (filename.find(mapNameFilter) != std::string::npos) {
                    Episode ep;
                    if (loadEpisodeFromFile(entry.path().string(), ep)) {
                        // Further filter by gametype if filter is provided and if metadata supports it well
                        bool gametypeMatch = true;
                        if (!gametypeFilter.empty()) {
                            // This is a simplified check. A real implementation would parse
                            // gameCvars from ep.metadata more robustly.
                            std::string combinedCvars;
                            for(const auto& pair : ep.metadata.gameCvars) {
                                combinedCvars += pair.first + "_" + pair.second;
                            }
                            if (combinedCvars.find(gametypeFilter) == std::string::npos) {
                                gametypeMatch = false;
                            }
                        }

                        if (gametypeMatch) {
                             matchedEpisodes.push_back(ep);
                        }
                    }
                }
            }
        }
    } catch (const std::filesystem::filesystem_error& e) {
         fprintf(stderr, "Filesystem error while retrieving episodes: %s\n", e.what());
    }

    return matchedEpisodes;
}
