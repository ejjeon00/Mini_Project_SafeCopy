#include "filesystem_repository.hpp"

#include <algorithm>
#include <charconv>
#include <chrono>
#include <ctime>
#include <fstream>
#include <iomanip>
#include <optional>
#include <sstream>
#include <system_error>

using std::optional;
using std::size_t;
using std::string;
using std::vector;

namespace safecopy::infrastructure {
    namespace fs = std::filesystem;

    namespace {
        bool inside(fs::path child, fs::path parent) {
            child = fs::weakly_canonical(child);
            parent = fs::weakly_canonical(parent);
            while (!child.empty()) {
                if (child == parent) {
                    return true;
                }
                std::error_code ec;
                if (fs::equivalent(child, parent, ec) && !ec) {
                    return true;
                }
                auto next = child.parent_path();
                if (next == child) {
                    break;
                }
                child = next;
            }
            return false;
        }

        void rejectLink(const fs::path& path) {
            if (fs::is_symlink(fs::symlink_status(path))) {
                throw std::runtime_error("바로 연결되는 심볼릭 링크는 지원하지 않습니다: " +
                                         path.u8string());
            }
        }

        size_t copyTree(const fs::path& source, const fs::path& destination) {
            rejectLink(source);
            if (!fs::is_directory(source)) {
                throw std::runtime_error("백업 데이터 폴더가 없습니다.");
            }
            fs::create_directories(destination.parent_path());
            if (!fs::create_directory(destination)) {
                throw std::runtime_error("대상 폴더가 이미 존재합니다.");
            }
            size_t count = 0;
            for (const auto& entry : fs::recursive_directory_iterator(source)) {
                rejectLink(entry.path());
                const auto status = entry.symlink_status();
                const auto target = destination / entry.path().lexically_relative(source);
                if (fs::is_directory(status)) {
                    fs::create_directories(target);
                } else if (fs::is_regular_file(status)) {
                    fs::copy_file(entry.path(), target); // 덮어쓰기 옵션을 사용하지 않습니다.
                    ++count;
                } else {
                    throw std::runtime_error("지원하지 않는 파일 형식입니다.");
                }
            }
            return count;
        }

        optional<size_t> readMarker(const fs::path& folder) {
            rejectLink(folder);
            const auto marker = folder / "complete.txt";
            if (!fs::is_regular_file(marker) || !fs::is_directory(folder / "data")) {
                return std::nullopt;
            }
            rejectLink(marker);
            rejectLink(folder / "data");
            std::ifstream stream(marker);
            string version, countLine, extra;
            if (!std::getline(stream, version) || version != "SAFECOPY_DDD_V1" ||
                !std::getline(stream, countLine) || countLine.empty()) {
                return std::nullopt;
            }
            size_t count = 0;
            const auto parsed =
                std::from_chars(countLine.data(), countLine.data() + countLine.size(), count);
            if (parsed.ec != std::errc{} || parsed.ptr != countLine.data() + countLine.size()) {
                return std::nullopt;
            }
            if (std::getline(stream, extra) || stream.bad()) {
                return std::nullopt;
            }
            return count;
        }

        string timestamp() {
            const auto raw = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
            std::tm local{};
#ifdef _WIN32
            if (localtime_s(&local, &raw) != 0) {
                throw std::runtime_error("현재 시간을 확인하지 못했습니다.");
            }
#else
            const auto* converted = std::localtime(&raw);
            if (!converted) {
                throw std::runtime_error("현재 시간을 확인하지 못했습니다.");
            }
            local = *converted;
#endif
            std::ostringstream text;
            text << std::put_time(&local, "%Y%m%d_%H%M%S");
            return text.str();
        }
    } // namespace

    FileBackupRepository::FileBackupRepository(fs::path root)
        : root_(fs::absolute(std::move(root))) {
        validateRoot();
    }

    void FileBackupRepository::validateRoot() const {
        rejectLink(root_);
        if (fs::exists(root_) && !fs::is_directory(root_)) {
            throw std::runtime_error("백업 저장 경로가 폴더가 아닙니다.");
        }
    }

    fs::path FileBackupRepository::location(const domain::BackupId& id) const {
        return root_ / id.value();
    }

    domain::BackupFacts FileBackupRepository::inspectBackup(const string& source) const {
        validateRoot();
        if (source.empty()) {
            return {false, false};
        }
        const auto path = fs::u8path(source);
        rejectLink(path);
        const bool isDirectory = fs::is_directory(path);
        return {isDirectory, isDirectory && inside(root_, fs::canonical(path))};
    }

    domain::RestoreFacts FileBackupRepository::inspectRestore(const string& destination) const {
        validateRoot();
        if (destination.empty()) {
            throw std::invalid_argument("복원할 새 폴더 경로를 입력하세요.");
        }
        const auto path = fs::u8path(destination);
        rejectLink(path);
        return {fs::exists(path), inside(path, root_)};
    }

    domain::Backup FileBackupRepository::reserve() {
        validateRoot();
        fs::create_directories(root_);
        const auto base = timestamp();
        for (size_t suffix = 0;; ++suffix) {
            domain::BackupId id(suffix == 0 ? base : base + "_" + std::to_string(suffix));
            if (fs::create_directory(location(id))) {
                return domain::Backup(std::move(id));
            }
        }
    }

    size_t FileBackupRepository::copyInto(const domain::BackupId& id, const string& source) {
        validateRoot();
        const auto folder = location(id);
        rejectLink(folder);
        if (!fs::is_directory(folder) || fs::exists(folder / "complete.txt")) {
            throw std::runtime_error("백업 폴더가 준비 상태가 아니거나 이미 완료되었습니다.");
        }
        const auto rawSource = fs::u8path(source);
        rejectLink(rawSource);
        const auto canonicalSource = fs::canonical(rawSource);
        domain::BackupPolicy::validate(
            domain::BackupFacts{fs::is_directory(canonicalSource), inside(root_, canonicalSource)});
        return copyTree(canonicalSource, folder / "data");
    }

    void FileBackupRepository::commit(const domain::BackupId& id, size_t count) {
        const auto folder = location(id);
        validateRoot();
        rejectLink(folder);
        rejectLink(folder / "data");
        if (!fs::is_directory(folder / "data") || fs::exists(folder / "complete.txt") ||
            fs::exists(folder / "complete.tmp")) {
            throw std::runtime_error("이 백업을 완료 처리할 수 없습니다.");
        }
        const auto temporary = folder / "complete.tmp";
        std::ofstream stream(temporary);
        stream << "SAFECOPY_DDD_V1\n" << count << '\n';
        stream.close();
        if (!stream) {
            throw std::runtime_error("백업 완료 기록을 저장하지 못했습니다.");
        }
        fs::rename(temporary, folder / "complete.txt");
    }

    vector<domain::Backup> FileBackupRepository::listCompleted() const {
        validateRoot();
        vector<domain::Backup> result;
        if (!fs::exists(root_)) {
            return result;
        }
        for (const auto& entry : fs::directory_iterator(root_)) {
            if (fs::is_symlink(entry.symlink_status()) || !entry.is_directory()) {
                continue;
            }
            optional<domain::BackupId> id;
            try {
                id.emplace(entry.path().filename().u8string());
            } catch (const std::invalid_argument&) {
                continue;
            }
            const auto count = readMarker(entry.path());
            if (!count) {
                continue;
            }
            domain::Backup backup(*id);
            backup.complete(*count);
            result.push_back(std::move(backup));
        }
        std::sort(result.begin(), result.end(),
                  [](const auto& a, const auto& b) { return a.id().value() < b.id().value(); });
        return result;
    }

    size_t FileBackupRepository::restoreFiles(const domain::BackupId& id,
                                              const string& destination) {
        domain::BackupPolicy::validate(inspectRestore(destination));
        const auto folder = location(id);
        if (!readMarker(folder)) {
            throw std::runtime_error("백업이 완료되지 않았거나 완료 기록이 올바르지 않습니다.");
        }
        return copyTree(folder / "data", fs::weakly_canonical(fs::u8path(destination)));
    }
} // namespace safecopy::infrastructure
