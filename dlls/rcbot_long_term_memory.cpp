#include "rcbot_long_term_memory.h"
#include "extdll.h" // For gpGlobals if needed
#include "util.h"   // For UTIL_LogPrintf / UTIL_ServerPrintf or other logging
#include <stdio.h>  // For fprintf, snprintf for SQLite errors
#include <sstream>  // For std::stringstream used in deserialization

// Constructor: Opens the database and initializes the schema.
RCBotLongTermMemory::RCBotLongTermMemory() : m_db(nullptr) {
    int rc = sqlite3_open("rcbot_ltm.sqlite", &m_db);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "RCBotLTM Error: Can't open database: %s\n", sqlite3_errmsg(m_db));
        sqlite3_close(m_db); // sqlite3_close can handle a NULL m_db if open failed partway
        m_db = nullptr;
        return;
    } else {
        // fprintf(stdout, "RCBotLTM: Opened database successfully\n"); // Or use UTIL_LogPrintf
        UTIL_ServerPrintf("RCBotLTM: Opened database successfully: rcbot_ltm.sqlite\n");
    }
    initializeDatabase();
}

// Destructor: Closes the database connection.
RCBotLongTermMemory::~RCBotLongTermMemory() {
    if (m_db) {
        sqlite3_close(m_db);
        m_db = nullptr;
        // fprintf(stdout, "RCBotLTM: Closed database connection.\n");
        UTIL_ServerPrintf("RCBotLTM: Closed database connection.\n");
    }
}

// Helper to execute simple SQL statements (CREATE, INSERT, UPDATE, DELETE).
bool RCBotLongTermMemory::executeSQL(const std::string& sql_statement) {
    if (!m_db) return false;

    char* pErrMsg = nullptr;
    int rc = sqlite3_exec(m_db, sql_statement.c_str(), nullptr, nullptr, &pErrMsg);

    if (rc != SQLITE_OK) {
        fprintf(stderr, "RCBotLTM SQL error: %s (Query: %s)\n", pErrMsg, sql_statement.c_str());
        sqlite3_free(pErrMsg);
        return false;
    }
    return true;
}

// Initializes the database schema if tables don't exist.
void RCBotLongTermMemory::initializeDatabase() {
    if (!m_db) return;

    const std::string create_episodes_table_sql =
        "CREATE TABLE IF NOT EXISTS Episodes ("
        "episode_id INTEGER PRIMARY KEY AUTOINCREMENT, "
        "map_name TEXT NOT NULL, "
        "gametype_cvar TEXT, " // Store as JSON string or concatenated string
        "mod_flags TEXT, "     // Store as JSON string or concatenated string
        "timestamp REAL NOT NULL, "
        "outcome TEXT"
        ");";

    if (!executeSQL(create_episodes_table_sql)) {
        fprintf(stderr, "RCBotLTM Error: Failed to create Episodes table.\n");
    } else {
        // UTIL_ServerPrintf("RCBotLTM: Episodes table ensured.\n");
    }

    const std::string create_game_events_table_sql =
        "CREATE TABLE IF NOT EXISTS GameEvents ("
        "event_id INTEGER PRIMARY KEY AUTOINCREMENT, "
        "episode_id INTEGER NOT NULL, "
        "timestamp REAL NOT NULL, "
        "event_type INTEGER NOT NULL, " // Enum GameEventType cast to int
        "damage_amount REAL, "          // Specific to damage events
        "attacker_info TEXT, "          // Placeholder for more complex data (e.g., player name/ID, entity class)
        "target_info TEXT, "            // Placeholder
        "FOREIGN KEY(episode_id) REFERENCES Episodes(episode_id) ON DELETE CASCADE"
        ");";

    if (!executeSQL(create_game_events_table_sql)) {
        fprintf(stderr, "RCBotLTM Error: Failed to create GameEvents table.\n");
    } else {
        // UTIL_ServerPrintf("RCBotLTM: GameEvents table ensured.\n");
    }
}

// Archives an episode. Logic will be updated in a subsequent subtask to use SQLite.
void RCBotLongTermMemory::archiveEpisode(const Episode& episode) {
    if (!m_db) {
        fprintf(stderr, "RCBotLTM Error: Database not open. Cannot archive episode.\n");
        return;
    }
    // TODO: Implement SQLite insertion logic for Episode and its GameEvents.
    // This will involve:
    // 1. INSERT into Episodes table.
    // 2. Get last_insert_rowid() for the episode_id.
    // 3. Loop through episode.events and INSERT them into GameEvents table with the episode_id.
    // All of this should ideally be within a transaction.
    // UTIL_ServerPrintf("RCBotLTM: archiveEpisode called for map %s (Not yet fully implemented for SQLite).\n", episode.metadata.mapName.c_str());

    if (!executeSQL("BEGIN TRANSACTION;")) {
        fprintf(stderr, "RCBotLTM Error: Could not begin transaction for archiveEpisode.\n");
        return;
    }

    sqlite3_stmt* episode_stmt = nullptr;
    const char* episode_sql = "INSERT INTO Episodes (map_name, gametype_cvar, mod_flags, timestamp, outcome) VALUES (?, ?, ?, ?, ?);";

    int rc = sqlite3_prepare_v2(m_db, episode_sql, -1, &episode_stmt, nullptr);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "RCBotLTM SQL error preparing episode insert: %s\n", sqlite3_errmsg(m_db));
        executeSQL("ROLLBACK;");
        return;
    }

    // Serialize gameCvars (map to string)
    std::string gameCvars_str;
    for (const auto& pair : episode.metadata.gameCvars) {
        gameCvars_str += pair.first + "=" + pair.second + ";";
    }
    if (!gameCvars_str.empty()) gameCvars_str.pop_back(); // Remove last ';'

    // Serialize modFlags (vector to string)
    std::string modFlags_str;
    for (const auto& flag : episode.metadata.modFlags) {
        modFlags_str += flag + ";";
    }
    if (!modFlags_str.empty()) modFlags_str.pop_back();

    sqlite3_bind_text(episode_stmt, 1, episode.metadata.mapName.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(episode_stmt, 2, gameCvars_str.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(episode_stmt, 3, modFlags_str.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_double(episode_stmt, 4, static_cast<double>(episode.metadata.timestamp));
    sqlite3_bind_text(episode_stmt, 5, episode.metadata.outcome.c_str(), -1, SQLITE_STATIC);

    if (sqlite3_step(episode_stmt) != SQLITE_DONE) {
        fprintf(stderr, "RCBotLTM SQL error inserting episode: %s\n", sqlite3_errmsg(m_db));
        sqlite3_finalize(episode_stmt);
        executeSQL("ROLLBACK;");
        return;
    }
    sqlite3_finalize(episode_stmt);
    long long new_episode_id = sqlite3_last_insert_rowid(m_db);

    // Insert GameEvents
    sqlite3_stmt* event_stmt = nullptr;
    const char* event_sql = "INSERT INTO GameEvents (episode_id, timestamp, event_type, damage_amount, attacker_info, target_info) VALUES (?, ?, ?, ?, ?, ?);";
    rc = sqlite3_prepare_v2(m_db, event_sql, -1, &event_stmt, nullptr);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "RCBotLTM SQL error preparing event insert: %s\n", sqlite3_errmsg(m_db));
        executeSQL("ROLLBACK;");
        return;
    }

    for (const auto& event_item : episode.events) {
        sqlite3_bind_int64(event_stmt, 1, new_episode_id);
        sqlite3_bind_double(event_stmt, 2, event_item.timestamp);
        sqlite3_bind_int(event_stmt, 3, static_cast<int>(event_item.type));
        sqlite3_bind_double(event_stmt, 4, event_item.damageAmount);
        sqlite3_bind_text(event_stmt, 5, event_item.attacker_info_str.c_str(), -1, SQLITE_STATIC);
        sqlite3_bind_text(event_stmt, 6, event_item.target_info_str.c_str(), -1, SQLITE_STATIC);

        if (sqlite3_step(event_stmt) != SQLITE_DONE) {
            fprintf(stderr, "RCBotLTM SQL error inserting game event: %s\n", sqlite3_errmsg(m_db));
            // Continue to try and insert other events? Or rollback? For now, log and continue.
        }
        sqlite3_reset(event_stmt); // Reset for next bind
    }
    sqlite3_finalize(event_stmt);

    if (!executeSQL("COMMIT;")) {
         fprintf(stderr, "RCBotLTM Error: Could not commit transaction for archiveEpisode.\n");
         // Rollback might have already been called by a failed executeSQL if it supports nested transactions or state.
         // However, sqlite3_exec doesn't support nested BEGIN/COMMIT directly.
         // If COMMIT fails after successful individual steps, the DB state might be as per last successful implicit commit.
    } else {
        // UTIL_ServerPrintf("RCBotLTM: Successfully archived episode ID %lld for map %s.\n", new_episode_id, episode.metadata.mapName.c_str());
    }
}

// Retrieves episodes.
std::vector<Episode> RCBotLongTermMemory::retrieveEpisodes(const std::string& mapNameFilter, const std::string& gametypeFilter) {
    std::vector<Episode> retrieved_episodes;
    if (!m_db) {
        fprintf(stderr, "RCBotLTM Error: Database not open. Cannot retrieve episodes.\n");
        return retrieved_episodes;
    }

    sqlite3_stmt* episode_stmt = nullptr;
    std::string sql = "SELECT episode_id, map_name, gametype_cvar, mod_flags, timestamp, outcome FROM Episodes";
    std::vector<std::string> params;
    bool whereClauseAdded = false;

    if (!mapNameFilter.empty()) {
        sql += " WHERE map_name = ?";
        params.push_back(mapNameFilter);
        whereClauseAdded = true;
    }
    // TODO: Add gametypeFilter if provided and non-empty
    // if (!gametypeFilter.empty()) {
    //     sql += whereClauseAdded ? " AND " : " WHERE ";
    //     sql += " gametype_cvar LIKE ?"; // Using LIKE for flexibility if gametype_cvar is semi-colon separated
    //     params.push_back("%" + gametypeFilter + "%");
    // }
    sql += ";";

    int rc = sqlite3_prepare_v2(m_db, sql.c_str(), -1, &episode_stmt, nullptr);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "RCBotLTM SQL error preparing episode select: %s\n", sqlite3_errmsg(m_db));
        return retrieved_episodes;
    }

    for (size_t i = 0; i < params.size(); ++i) {
        sqlite3_bind_text(episode_stmt, i + 1, params[i].c_str(), -1, SQLITE_STATIC);
    }

    while (sqlite3_step(episode_stmt) == SQLITE_ROW) {
        Episode current_episode;
        long long current_episode_id = sqlite3_column_int64(episode_stmt, 0);

        current_episode.metadata.mapName = reinterpret_cast<const char*>(sqlite3_column_text(episode_stmt, 1));

        // Deserialize gameCvars
        const char* gameCvars_cstr = reinterpret_cast<const char*>(sqlite3_column_text(episode_stmt, 2));
        if (gameCvars_cstr) {
            std::string gameCvars_str(gameCvars_cstr);
            std::stringstream ss_cvars(gameCvars_str);
            std::string pair_str;
            while(std::getline(ss_cvars, pair_str, ';')) {
                size_t eq_pos = pair_str.find('=');
                if (eq_pos != std::string::npos) {
                    current_episode.metadata.gameCvars[pair_str.substr(0, eq_pos)] = pair_str.substr(eq_pos + 1);
                }
            }
        }

        // Deserialize modFlags
        const char* modFlags_cstr = reinterpret_cast<const char*>(sqlite3_column_text(episode_stmt, 3));
        if (modFlags_cstr) {
            std::string modFlags_str(modFlags_cstr);
            std::stringstream ss_flags(modFlags_str);
            std::string flag;
            while(std::getline(ss_flags, flag, ';')) {
                if(!flag.empty()) current_episode.metadata.modFlags.push_back(flag);
            }
        }

        current_episode.metadata.timestamp = static_cast<long>(sqlite3_column_double(episode_stmt, 4));
        const char* outcome_cstr = reinterpret_cast<const char*>(sqlite3_column_text(episode_stmt, 5));
        current_episode.metadata.outcome = outcome_cstr ? outcome_cstr : "";


        // Fetch associated GameEvents
        sqlite3_stmt* event_stmt = nullptr;
        const char* event_sql = "SELECT timestamp, event_type, damage_amount, attacker_info, target_info FROM GameEvents WHERE episode_id = ? ORDER BY timestamp ASC;";
        rc = sqlite3_prepare_v2(m_db, event_sql, -1, &event_stmt, nullptr);
        if (rc == SQLITE_OK) {
            sqlite3_bind_int64(event_stmt, 1, current_episode_id);
            while (sqlite3_step(event_stmt) == SQLITE_ROW) {
                GameEvent current_event;
                current_event.timestamp = static_cast<float>(sqlite3_column_double(event_stmt, 0));
                current_event.type = static_cast<GameEventType>(sqlite3_column_int(event_stmt, 1));
                current_event.damageAmount = static_cast<float>(sqlite3_column_double(event_stmt, 2));
                const char* attacker_cstr = reinterpret_cast<const char*>(sqlite3_column_text(event_stmt, 3));
                current_event.attacker_info_str = attacker_cstr ? attacker_cstr : "";
                const char* target_cstr = reinterpret_cast<const char*>(sqlite3_column_text(event_stmt, 4));
                current_event.target_info_str = target_cstr ? target_cstr : "";
                current_episode.events.push_back(current_event);
            }
            sqlite3_finalize(event_stmt);
        } else {
            fprintf(stderr, "RCBotLTM SQL error preparing event select for episode %lld: %s\n", current_episode_id, sqlite3_errmsg(m_db));
        }
        retrieved_episodes.push_back(current_episode);
    }
    sqlite3_finalize(episode_stmt);

    // UTIL_ServerPrintf("RCBotLTM: Retrieved %d episodes for map %s.\n", retrieved_episodes.size(), mapNameFilter.c_str());
    return retrieved_episodes;
}


Episode RCBotLongTermMemory::fetchFullEpisodeById(long long episode_id) {
    Episode full_episode;
    if (!m_db) {
        fprintf(stderr, "RCBotLTM Error: Database not open in fetchFullEpisodeById.\n");
        return full_episode; // Return empty episode
    }

    sqlite3_stmt* episode_stmt = nullptr;
    const char* episode_sql = "SELECT map_name, gametype_cvar, mod_flags, timestamp, outcome FROM Episodes WHERE episode_id = ?;";

    int rc = sqlite3_prepare_v2(m_db, episode_sql, -1, &episode_stmt, nullptr);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "RCBotLTM SQL error preparing episode select by ID: %s\n", sqlite3_errmsg(m_db));
        return full_episode;
    }
    sqlite3_bind_int64(episode_stmt, 1, episode_id);

    if (sqlite3_step(episode_stmt) == SQLITE_ROW) {
        full_episode.metadata.mapName = reinterpret_cast<const char*>(sqlite3_column_text(episode_stmt, 0));

        const char* gameCvars_cstr = reinterpret_cast<const char*>(sqlite3_column_text(episode_stmt, 1));
        if (gameCvars_cstr) {
            std::string gameCvars_str(gameCvars_cstr);
            std::stringstream ss_cvars(gameCvars_str);
            std::string pair_str;
            while(std::getline(ss_cvars, pair_str, ';')) {
                size_t eq_pos = pair_str.find('=');
                if (eq_pos != std::string::npos) {
                    full_episode.metadata.gameCvars[pair_str.substr(0, eq_pos)] = pair_str.substr(eq_pos + 1);
                }
            }
        }

        const char* modFlags_cstr = reinterpret_cast<const char*>(sqlite3_column_text(episode_stmt, 2));
        if (modFlags_cstr) {
            std::string modFlags_str(modFlags_cstr);
            std::stringstream ss_flags(modFlags_str);
            std::string flag;
            while(std::getline(ss_flags, flag, ';')) {
                if(!flag.empty()) full_episode.metadata.modFlags.push_back(flag);
            }
        }
        full_episode.metadata.timestamp = static_cast<long>(sqlite3_column_double(episode_stmt, 3));
        const char* outcome_cstr = reinterpret_cast<const char*>(sqlite3_column_text(episode_stmt, 4));
        full_episode.metadata.outcome = outcome_cstr ? outcome_cstr : "";

        // Fetch associated GameEvents
        sqlite3_stmt* event_stmt = nullptr;
        const char* event_sql = "SELECT timestamp, event_type, damage_amount, attacker_info, target_info FROM GameEvents WHERE episode_id = ? ORDER BY timestamp ASC;";
        rc = sqlite3_prepare_v2(m_db, event_sql, -1, &event_stmt, nullptr);
        if (rc == SQLITE_OK) {
            sqlite3_bind_int64(event_stmt, 1, episode_id);
            while (sqlite3_step(event_stmt) == SQLITE_ROW) {
                GameEvent current_event;
                current_event.timestamp = static_cast<float>(sqlite3_column_double(event_stmt, 0));
                current_event.type = static_cast<GameEventType>(sqlite3_column_int(event_stmt, 1));
                current_event.damageAmount = static_cast<float>(sqlite3_column_double(event_stmt, 2));
                const char* attacker_cstr = reinterpret_cast<const char*>(sqlite3_column_text(event_stmt, 3));
                current_event.attacker_info_str = attacker_cstr ? attacker_cstr : "";
                const char* target_cstr = reinterpret_cast<const char*>(sqlite3_column_text(event_stmt, 4));
                current_event.target_info_str = target_cstr ? target_cstr : "";
                full_episode.events.push_back(current_event);
            }
            sqlite3_finalize(event_stmt);
        } else {
            fprintf(stderr, "RCBotLTM SQL error preparing event select for episode %lld (in helper): %s\n", episode_id, sqlite3_errmsg(m_db));
        }
    } else {
         fprintf(stderr, "RCBotLTM Error: Episode ID %lld not found.\n", episode_id);
    }
    sqlite3_finalize(episode_stmt);
    return full_episode;
}


std::vector<Episode> RCBotLongTermMemory::retrieveEpisodesByOutcome(
    const std::string& mapName,
    const std::string& outcomeFilter,
    int limit) {

    std::vector<Episode> retrieved_episodes;
    if (!m_db) {
        fprintf(stderr, "RCBotLTM Error: Database not open.\n");
        return retrieved_episodes;
    }

    sqlite3_stmt* stmt = nullptr;
    std::string sql = "SELECT episode_id FROM Episodes WHERE 1=1"; // Start with a tautology
    std::vector<std::string> params_text;
    std::vector<int> params_int; // Not used here, but for consistency if adding int params

    int param_idx = 1;

    if (!mapName.empty()) {
        sql += " AND map_name = ?";
        params_text.push_back(mapName);
    }
    if (!outcomeFilter.empty()) {
        sql += " AND outcome = ?";
        params_text.push_back(outcomeFilter);
    }
    sql += " ORDER BY timestamp DESC LIMIT ?;";

    int rc = sqlite3_prepare_v2(m_db, sql.c_str(), -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "RCBotLTM SQL error preparing retrieveEpisodesByOutcome: %s\n", sqlite3_errmsg(m_db));
        return retrieved_episodes;
    }

    for(const auto& txt_param : params_text) {
        sqlite3_bind_text(stmt, param_idx++, txt_param.c_str(), -1, SQLITE_STATIC);
    }
    sqlite3_bind_int(stmt, param_idx++, limit);

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        long long episode_id = sqlite3_column_int64(stmt, 0);
        retrieved_episodes.push_back(fetchFullEpisodeById(episode_id));
    }
    sqlite3_finalize(stmt);
    return retrieved_episodes;
}

std::vector<Episode> RCBotLongTermMemory::retrieveEpisodesWithEventType(
    const std::string& mapName,
    int eventTypeFilter,
    int limit) {

    std::vector<Episode> retrieved_episodes;
    if (!m_db) {
        fprintf(stderr, "RCBotLTM Error: Database not open.\n");
        return retrieved_episodes;
    }

    sqlite3_stmt* stmt = nullptr;
    std::string sql =
        "SELECT DISTINCT E.episode_id FROM Episodes E "
        "JOIN GameEvents GE ON E.episode_id = GE.episode_id "
        "WHERE GE.event_type = ?";

    std::vector<std::string> params_text;
    int param_idx = 1;

    // sqlite3_bind_int(stmt, param_idx++, eventTypeFilter); // Binding must be done AFTER prepare

    if (!mapName.empty()) {
        sql += " AND E.map_name = ?";
        params_text.push_back(mapName);
    }
    sql += " ORDER BY E.timestamp DESC LIMIT ?;";

    int rc = sqlite3_prepare_v2(m_db, sql.c_str(), -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "RCBotLTM SQL error preparing retrieveEpisodesWithEventType: %s (SQL: %s)\n", sqlite3_errmsg(m_db), sql.c_str());
        return retrieved_episodes;
    }

    // Reset param_idx for binding
    param_idx = 1;
    sqlite3_bind_int(stmt, param_idx++, eventTypeFilter);

    for(const auto& txt_param : params_text) {
        sqlite3_bind_text(stmt, param_idx++, txt_param.c_str(), -1, SQLITE_STATIC);
    }
    sqlite3_bind_int(stmt, param_idx++, limit);

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        long long episode_id = sqlite3_column_int64(stmt, 0);
        retrieved_episodes.push_back(fetchFullEpisodeById(episode_id));
    }
    sqlite3_finalize(stmt);
    return retrieved_episodes;
}


/*
// Old file-based methods - to be removed or fully commented.
// For now, just commenting out their bodies as they are not part of the SQLite setup.

std::string RCBotLongTermMemory::generateEpisodeFilename(const EpisodeMetadata& metadata) const {
    // ... old code ...
    return "";
}

bool RCBotLongTermMemory::saveEpisodeToFile(const std::string& filePath, const Episode& episode) {
    // ... old code ...
    return false;
}

bool RCBotLongTermMemory::loadEpisodeFromFile(const std::string& filePath, Episode& outEpisode) {
    // ... old code ...
    return false;
}
*/
