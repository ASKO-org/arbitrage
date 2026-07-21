#include <iostream>
#include <stdexcept>
#include <string>
#include <unordered_map>

#include <termios.h>
#include <unistd.h>

#include "security/SecretsStore.h"

namespace {

// Fields this tool knows how to merge when updating one at a time. Kept as
// a fixed list (rather than something more generic) since there are
// currently exactly four exchange credentials in scope.
const char* kKnownFields[] = {"binance_api_key", "binance_api_secret", "bybit_api_key", "bybit_api_secret"};

std::string promptHidden(const std::string& prompt) {
    std::cout << prompt;
    std::cout.flush();

    termios oldTermios{};
    tcgetattr(STDIN_FILENO, &oldTermios);
    termios noEchoTermios = oldTermios;
    noEchoTermios.c_lflag &= ~static_cast<tcflag_t>(ECHO);
    tcsetattr(STDIN_FILENO, TCSANOW, &noEchoTermios);

    std::string value;
    std::getline(std::cin, value);

    tcsetattr(STDIN_FILENO, TCSANOW, &oldTermios);
    std::cout << "\n";
    return value;
}

void printUsage() {
    std::cerr << "usage:\n"
               << "  secrets_cli init <master-key-path>\n"
               << "  secrets_cli set <master-key-path> <encrypted-file-path> <field>\n"
               << "  secrets_cli get <master-key-path> <encrypted-file-path> <field>\n"
               << "\n"
               << "known fields: binance_api_key, binance_api_secret, bybit_api_key, bybit_api_secret\n";
}

}  // namespace

int main(int argc, char** argv) {
    try {
        if (argc < 2) {
            printUsage();
            return 1;
        }
        const std::string command = argv[1];

        if (command == "init") {
            if (argc != 3) {
                printUsage();
                return 1;
            }
            SecretsStore::generateMasterKeyFile(argv[2]);
            std::cout << "Master key written to " << argv[2] << " (0600)\n";
            return 0;
        }

        if (command == "set") {
            if (argc != 5) {
                printUsage();
                return 1;
            }
            const std::string masterKeyPath = argv[2];
            const std::string encryptedFilePath = argv[3];
            const std::string field = argv[4];

            std::unordered_map<std::string, std::string> fields;
            try {
                SecretsStore existing(masterKeyPath, encryptedFilePath);
                for (const char* knownField : kKnownFields) {
                    try {
                        fields[knownField] = existing.get(knownField);
                    } catch (const std::out_of_range&) {
                        // Not set yet — leave absent rather than writing an empty string.
                    }
                }
            } catch (const std::exception&) {
                // No existing encrypted file yet (or it's unreadable with this
                // master key) — start fresh with just the one field being set.
            }

            fields[field] = promptHidden("Enter value for " + field + ": ");

            SecretsStore::encryptAndWrite(masterKeyPath, encryptedFilePath, fields);
            std::cout << "Updated '" << field << "' in " << encryptedFilePath << "\n";
            return 0;
        }

        if (command == "get") {
            if (argc != 5) {
                printUsage();
                return 1;
            }
            SecretsStore store(argv[2], argv[3]);
            std::cout << store.get(argv[4]) << "\n";
            return 0;
        }

        std::cerr << "unknown command: " << command << "\n";
        printUsage();
        return 1;
    } catch (const std::exception& ex) {
        std::cerr << "Error: " << ex.what() << "\n";
        return 1;
    }
}
