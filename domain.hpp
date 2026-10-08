#pragma once

#include <cstddef>
#include <stdexcept>
#include <string>
#include <utility>

namespace safecopy::domain {

// Value object: the value identifies a backup; path separators are forbidden.
class BackupId {
    std::string value_;
public:
    explicit BackupId(std::string value) : value_(std::move(value)) {
        if (value_.empty() || value_.size() > 80) {
            throw std::invalid_argument("백업 식별자의 길이가 올바르지 않습니다.");
        }
        for (char c : value_) {
            if (!((c >= '0' && c <= '9') || c == '_')) {
                throw std::invalid_argument("백업 식별자에는 숫자와 밑줄만 사용할 수 있습니다.");
            }
        }
    }
    const std::string& value() const noexcept { return value_; }
    bool operator==(const BackupId& other) const noexcept { return value_ == other.value_; }
};

enum class BackupState { Preparing, Completed, Failed };

// Entity: identity stays the same while its state changes.
class Backup {
    BackupId id_;
    BackupState state_ = BackupState::Preparing;
    std::size_t fileCount_ = 0;
public:
    explicit Backup(BackupId id) : id_(std::move(id)) {}
    const BackupId& id() const noexcept { return id_; }
    BackupState state() const noexcept { return state_; }
    std::size_t fileCount() const noexcept { return fileCount_; }
    void complete(std::size_t count) {
        if (state_ != BackupState::Preparing) {
            throw std::logic_error("준비 중인 백업만 완료 처리할 수 있습니다.");
        }
        fileCount_ = count;
        state_ = BackupState::Completed;
    }
    void fail() {
        if (state_ != BackupState::Preparing) {
            throw std::logic_error("준비 중인 백업만 실패 처리할 수 있습니다.");
        }
        state_ = BackupState::Failed;
    }
};

// Facts are observed by infrastructure. Decisions belong to the domain.
struct BackupFacts {
    bool sourceIsDirectory;
    bool backupRootInsideSource;
};
struct RestoreFacts {
    bool destinationExists;
    bool destinationInsideBackupRoot;
};
class BackupPolicy {
public:
    static void validate(const BackupFacts& facts) {
        if (!facts.sourceIsDirectory) throw std::invalid_argument("원본 폴더를 찾을 수 없습니다. 메뉴 번호가 아닌 실제 폴더 경로를 입력하세요.");
        if (facts.backupRootInsideSource) throw std::invalid_argument("백업 저장 폴더가 원본 폴더 안에 있습니다. 백업 저장 위치를 포함하지 않는 원본 폴더를 선택하세요.");
    }
    static void validate(const RestoreFacts& facts) {
        if (facts.destinationExists) throw std::invalid_argument("이미 존재하는 폴더로는 복원할 수 없습니다. 아직 없는 새 폴더 경로를 입력하세요.");
        if (facts.destinationInsideBackupRoot) throw std::invalid_argument("백업 저장 폴더 밖의 경로로 복원하세요.");
    }
};
} // namespace safecopy::domain
