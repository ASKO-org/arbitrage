#pragma once
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

// Reads/writes a small set of secrets (currently: exchange API keys) as an
// AES-256-GCM encrypted JSON blob on disk, decrypted only in memory. The
// blob (ciphertext) and the master key are deliberately kept in separate
// files — the ciphertext alone is harmless, so it's fine to sit inside the
// project directory (gitignored, but a backup/accidental-add of it leaks
// nothing); the master key is meant to live somewhere outside the repo
// entirely (e.g. ~/.secrets/), so no single accidental copy/commit of the
// project can expose real trading credentials.
//
// This protects against the ciphertext file being casually exposed
// (backups, wrong permissions, an accidental commit). It does NOT protect
// against a full compromise of this machine/user account — whoever can
// read the master key file can decrypt everything. That fuller protection
// needs an external vault/HSM, which is deliberately out of scope here.
class SecretsStore {
public:
    // Reads the master key (base64, 32 bytes) from masterKeyPath and
    // decrypts encryptedFilePath. Throws std::runtime_error on any failure:
    // missing/malformed key file, missing/malformed encrypted file, or a
    // GCM authentication tag mismatch (wrong key or tampered ciphertext) —
    // fails loudly rather than silently running with an empty/wrong secret.
    SecretsStore(const std::string& masterKeyPath, const std::string& encryptedFilePath);

    // Throws std::out_of_range if the field wasn't present in the decrypted
    // blob.
    const std::string& get(const std::string& field) const;

    bool has(const std::string& field) const { return fields_.count(field) > 0; }

    // --- Writer-side helpers, used by secrets_cli ---

    // Generates 32 random bytes (AES-256 key) and writes them base64-encoded
    // to masterKeyPath with 0600 permissions, set before any data is
    // written. Throws std::runtime_error if masterKeyPath already exists
    // (refuses to silently overwrite/rotate a key that existing ciphertext
    // depends on) or if the write fails.
    static void generateMasterKeyFile(const std::string& masterKeyPath);

    // Encrypts fields (as a JSON object) with a fresh random nonce and
    // writes {nonce, ciphertext, tag} (all base64) as JSON to
    // encryptedFilePath, replacing any existing file atomically (write to a
    // temp file, then rename).
    static void encryptAndWrite(const std::string& masterKeyPath, const std::string& encryptedFilePath,
                                 const std::unordered_map<std::string, std::string>& fields);

    // --- Rotation metadata: separate, UNENCRYPTED file next to the
    // ciphertext (e.g. secrets/exchange_keys.meta.json for secrets/
    // exchange_keys.enc.json) holding only {field: {set_at_ms}}. Contains
    // no secret material, so anything (a web dashboard, execution_service's
    // startup check) can read it directly without the master key. ---

    static std::string metadataPathFor(const std::string& encryptedFilePath);

    // Updates just this one field's set_at_ms to now, preserving every
    // other field's existing entry. Creates the metadata file if absent.
    static void stampMetadata(const std::string& encryptedFilePath, const std::string& field);

    // Returns 0 if the metadata file or that field's entry doesn't exist —
    // callers should treat 0 as "unknown," not "just set."
    static int64_t readFieldSetAtMs(const std::string& encryptedFilePath, const std::string& field);

private:
    static std::vector<uint8_t> readMasterKey(const std::string& masterKeyPath);

    std::unordered_map<std::string, std::string> fields_;
};
