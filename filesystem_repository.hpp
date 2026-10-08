#pragma once

#include "application.hpp"
#include <filesystem>

namespace safecopy::infrastructure {
class FileBackupRepository final : public application::IBackupRepository {
    std::filesystem::path root_;
    void validateRoot() const;
    std::filesystem::path location(const domain::BackupId& id) const;
public:
    explicit FileBackupRepository(std::filesystem::path root = "backups_ddd");
    domain::BackupFacts inspectBackup(const std::string& source) const override;
    domain::RestoreFacts inspectRestore(const std::string& destination) const override;
    domain::Backup reserve() override;
    std::size_t copyInto(const domain::BackupId& id, const std::string& source) override;
    void commit(const domain::BackupId& id, std::size_t count) override;
    std::vector<domain::Backup> listCompleted() const override;
    std::size_t restoreFiles(const domain::BackupId& id, const std::string& destination) override;
};
} // namespace safecopy::infrastructure
