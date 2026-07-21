#include "security/SecretsStore.h"

#include <cerrno>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <stdexcept>

#include <fcntl.h>
#include <unistd.h>

#include <openssl/evp.h>
#include <openssl/rand.h>

#include <nlohmann/json.hpp>

namespace {

std::string readFile(const std::string& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) throw std::runtime_error("SecretsStore: cannot open " + path);
    std::ostringstream buffer;
    buffer << file.rdbuf();
    return buffer.str();
}

std::string base64Encode(const std::vector<uint8_t>& data) {
    if (data.empty()) return "";
    std::vector<uint8_t> out((data.size() + 2) / 3 * 4 + 1);
    const int len = EVP_EncodeBlock(out.data(), data.data(), static_cast<int>(data.size()));
    return std::string(reinterpret_cast<char*>(out.data()), static_cast<size_t>(len));
}

// EVP_DecodeBlock doesn't strip padding itself — it decodes as if the input
// had no '=' padding and leaves trailing zero byte(s) in the output that the
// caller must trim based on how many '=' characters were actually present.
std::vector<uint8_t> base64Decode(const std::string& text) {
    if (text.empty()) return {};
    std::vector<uint8_t> out(text.size() / 4 * 3 + 3);
    const int decodedLen = EVP_DecodeBlock(out.data(), reinterpret_cast<const uint8_t*>(text.data()),
                                            static_cast<int>(text.size()));
    if (decodedLen < 0) throw std::runtime_error("SecretsStore: invalid base64 data");
    int padding = 0;
    if (!text.empty() && text.back() == '=') ++padding;
    if (text.size() >= 2 && text[text.size() - 2] == '=') ++padding;
    out.resize(static_cast<size_t>(decodedLen) - padding);
    return out;
}

struct CipherCtxGuard {
    EVP_CIPHER_CTX* ctx;
    ~CipherCtxGuard() { EVP_CIPHER_CTX_free(ctx); }
};

std::vector<uint8_t> aesGcmEncrypt(const std::vector<uint8_t>& key, const std::vector<uint8_t>& nonce,
                                    const std::string& plaintext, std::vector<uint8_t>& tagOut) {
    EVP_CIPHER_CTX* ctx = EVP_CIPHER_CTX_new();
    if (!ctx) throw std::runtime_error("SecretsStore: EVP_CIPHER_CTX_new failed");
    CipherCtxGuard guard{ctx};

    if (EVP_EncryptInit_ex(ctx, EVP_aes_256_gcm(), nullptr, nullptr, nullptr) != 1 ||
        EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_IVLEN, static_cast<int>(nonce.size()), nullptr) != 1 ||
        EVP_EncryptInit_ex(ctx, nullptr, nullptr, key.data(), nonce.data()) != 1) {
        throw std::runtime_error("SecretsStore: failed to initialize AES-256-GCM encryption");
    }

    std::vector<uint8_t> ciphertext(plaintext.size() + static_cast<size_t>(EVP_CIPHER_block_size(EVP_aes_256_gcm())));
    int outLen1 = 0;
    if (EVP_EncryptUpdate(ctx, ciphertext.data(), &outLen1,
                           reinterpret_cast<const uint8_t*>(plaintext.data()),
                           static_cast<int>(plaintext.size())) != 1) {
        throw std::runtime_error("SecretsStore: EVP_EncryptUpdate failed");
    }

    int outLen2 = 0;
    if (EVP_EncryptFinal_ex(ctx, ciphertext.data() + outLen1, &outLen2) != 1) {
        throw std::runtime_error("SecretsStore: EVP_EncryptFinal_ex failed");
    }
    ciphertext.resize(static_cast<size_t>(outLen1 + outLen2));

    tagOut.resize(16);
    if (EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_GET_TAG, 16, tagOut.data()) != 1) {
        throw std::runtime_error("SecretsStore: failed to read GCM tag");
    }
    return ciphertext;
}

std::string aesGcmDecrypt(const std::vector<uint8_t>& key, const std::vector<uint8_t>& nonce,
                           const std::vector<uint8_t>& ciphertext, const std::vector<uint8_t>& tag) {
    EVP_CIPHER_CTX* ctx = EVP_CIPHER_CTX_new();
    if (!ctx) throw std::runtime_error("SecretsStore: EVP_CIPHER_CTX_new failed");
    CipherCtxGuard guard{ctx};

    if (EVP_DecryptInit_ex(ctx, EVP_aes_256_gcm(), nullptr, nullptr, nullptr) != 1 ||
        EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_IVLEN, static_cast<int>(nonce.size()), nullptr) != 1 ||
        EVP_DecryptInit_ex(ctx, nullptr, nullptr, key.data(), nonce.data()) != 1) {
        throw std::runtime_error("SecretsStore: failed to initialize AES-256-GCM decryption");
    }

    std::vector<uint8_t> plaintext(ciphertext.size());
    int outLen1 = 0;
    if (EVP_DecryptUpdate(ctx, plaintext.data(), &outLen1, ciphertext.data(),
                           static_cast<int>(ciphertext.size())) != 1) {
        throw std::runtime_error("SecretsStore: EVP_DecryptUpdate failed");
    }

    if (EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_TAG, static_cast<int>(tag.size()),
                            const_cast<uint8_t*>(tag.data())) != 1) {
        throw std::runtime_error("SecretsStore: failed to set expected GCM tag");
    }

    int outLen2 = 0;
    if (EVP_DecryptFinal_ex(ctx, plaintext.data() + outLen1, &outLen2) != 1) {
        throw std::runtime_error(
            "SecretsStore: GCM authentication failed (wrong master key or tampered ciphertext)");
    }
    plaintext.resize(static_cast<size_t>(outLen1 + outLen2));
    return std::string(reinterpret_cast<char*>(plaintext.data()), plaintext.size());
}

}  // namespace

std::vector<uint8_t> SecretsStore::readMasterKey(const std::string& masterKeyPath) {
    std::string content = readFile(masterKeyPath);
    while (!content.empty() &&
           (content.back() == '\n' || content.back() == '\r' || content.back() == ' ')) {
        content.pop_back();
    }
    const auto key = base64Decode(content);
    if (key.size() != 32) {
        throw std::runtime_error("SecretsStore: master key at " + masterKeyPath +
                                  " is not 32 bytes after base64 decode (got " + std::to_string(key.size()) +
                                  ")");
    }
    return key;
}

SecretsStore::SecretsStore(const std::string& masterKeyPath, const std::string& encryptedFilePath) {
    const auto key = readMasterKey(masterKeyPath);

    nlohmann::json blob;
    try {
        blob = nlohmann::json::parse(readFile(encryptedFilePath));
    } catch (const std::exception& ex) {
        throw std::runtime_error("SecretsStore: malformed encrypted file " + encryptedFilePath + ": " +
                                  ex.what());
    }

    const auto nonce = base64Decode(blob.at("nonce").get<std::string>());
    const auto ciphertext = base64Decode(blob.at("ciphertext").get<std::string>());
    const auto tag = base64Decode(blob.at("tag").get<std::string>());

    const std::string plaintext = aesGcmDecrypt(key, nonce, ciphertext, tag);

    nlohmann::json fieldsJson;
    try {
        fieldsJson = nlohmann::json::parse(plaintext);
    } catch (const std::exception& ex) {
        throw std::runtime_error("SecretsStore: decrypted content isn't valid JSON: " +
                                  std::string(ex.what()));
    }
    for (auto it = fieldsJson.begin(); it != fieldsJson.end(); ++it) {
        fields_[it.key()] = it.value().get<std::string>();
    }
}

const std::string& SecretsStore::get(const std::string& field) const { return fields_.at(field); }

void SecretsStore::generateMasterKeyFile(const std::string& masterKeyPath) {
    std::vector<uint8_t> key(32);
    if (RAND_bytes(key.data(), static_cast<int>(key.size())) != 1) {
        throw std::runtime_error("SecretsStore: RAND_bytes failed to generate master key");
    }
    const std::string encoded = base64Encode(key) + "\n";

    const std::filesystem::path path(masterKeyPath);
    if (path.has_parent_path()) std::filesystem::create_directories(path.parent_path());

    const int fd = ::open(masterKeyPath.c_str(), O_CREAT | O_EXCL | O_WRONLY, 0600);
    if (fd < 0) {
        if (errno == EEXIST) {
            throw std::runtime_error("SecretsStore: refusing to overwrite existing master key at " +
                                      masterKeyPath);
        }
        throw std::runtime_error("SecretsStore: failed to create " + masterKeyPath + ": " +
                                  std::strerror(errno));
    }
    const ssize_t written = ::write(fd, encoded.data(), encoded.size());
    ::close(fd);
    if (written != static_cast<ssize_t>(encoded.size())) {
        throw std::runtime_error("SecretsStore: short write to " + masterKeyPath);
    }
}

void SecretsStore::encryptAndWrite(const std::string& masterKeyPath, const std::string& encryptedFilePath,
                                    const std::unordered_map<std::string, std::string>& fields) {
    const auto key = readMasterKey(masterKeyPath);

    nlohmann::json fieldsJson = nlohmann::json::object();
    for (const auto& [k, v] : fields) fieldsJson[k] = v;
    const std::string plaintext = fieldsJson.dump();

    std::vector<uint8_t> nonce(12);
    if (RAND_bytes(nonce.data(), static_cast<int>(nonce.size())) != 1) {
        throw std::runtime_error("SecretsStore: RAND_bytes failed to generate nonce");
    }

    std::vector<uint8_t> tag;
    const auto ciphertext = aesGcmEncrypt(key, nonce, plaintext, tag);

    const nlohmann::json blob{
        {"nonce", base64Encode(nonce)},
        {"ciphertext", base64Encode(ciphertext)},
        {"tag", base64Encode(tag)},
    };

    const std::filesystem::path targetPath(encryptedFilePath);
    if (targetPath.has_parent_path()) std::filesystem::create_directories(targetPath.parent_path());

    const std::string tmpPath = encryptedFilePath + ".tmp";
    {
        std::ofstream out(tmpPath, std::ios::binary | std::ios::trunc);
        if (!out) throw std::runtime_error("SecretsStore: cannot write " + tmpPath);
        out << blob.dump(2);
    }
    if (std::rename(tmpPath.c_str(), encryptedFilePath.c_str()) != 0) {
        throw std::runtime_error("SecretsStore: failed to finalize " + encryptedFilePath + ": " +
                                  std::strerror(errno));
    }
}
