#pragma once

#include "domain.hpp"
#include <string>
#include <vector>

namespace safecopy::application {

// Storage port: the use cases do not depend on std::filesystem or a console.
class IBackupRepository {
public:
    virtual ~IBackupRepository() = default;
    virtual domain::BackupFacts inspectBackup(const std::string& source) const = 0;
    virtual domain::RestoreFacts inspectRestore(const std::string& destination) const = 0;
    virtual domain::Backup reserve() = 0;
    virtual std::size_t copyInto(const domain::BackupId& id, const std::string& source) = 0;
    virtual void commit(const domain::BackupId& id, std::size_t count) = 0;
    virtual std::vector<domain::Backup> listCompleted() const = 0;
    virtual std::size_t restoreFiles(const domain::BackupId& id, const std::string& destination) = 0;
};

class BackupService {
    IBackupRepository& repository_;
public:
    explicit BackupService(IBackupRepository& repository) : repository_(repository) {}
    domain::Backup create(const std::string& source) {
        domain::BackupPolicy::validate(repository_.inspectBackup(source));
        auto backup = repository_.reserve();
        try {
            const auto count = repository_.copyInto(backup.id(), source);
            repository_.commit(backup.id(), count);
            backup.complete(count); // Only after copy and completion record succeed.
            return backup;
        } catch (...) {
            backup.fail(); // In-memory state; partial files remain without a marker.
            throw;
        }
    }
    std::vector<domain::Backup> list() const { return repository_.listCompleted(); }
    std::size_t restore(const domain::BackupId& id, const std::string& destination) {
        domain::BackupPolicy::validate(repository_.inspectRestore(destination));
        return repository_.restoreFiles(id, destination);
    }
};
} // namespace safecopy::application
