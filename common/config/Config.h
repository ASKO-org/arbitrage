#pragma once
#include <cstdlib>
#include <string>

// Builds the PostgreSQL connection string from environment variables,
// falling back to local defaults for development.
namespace Config {

inline std::string envOr(const char* name, const std::string& fallback) {
    const char* value = std::getenv(name);
    return value ? std::string(value) : fallback;
}

inline std::string postgresConnectionString() {
    return "host=" + envOr("PGHOST", "localhost") +
           " port=" + envOr("PGPORT", "5432") +
           " dbname=" + envOr("PGDATABASE", "instruments") +
           " user=" + envOr("PGUSER", "postgres") +
           " password=" + envOr("PGPASSWORD", "postgres");
}

}  // namespace Config
