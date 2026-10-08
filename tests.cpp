#include "filesystem_repository.hpp"
#include <chrono>
#include <cstddef>
#include <fstream>
#include <iostream>
#include <iterator>

using std::cerr;
using std::cout;
using std::size_t;
using std::string;

namespace domain = safecopy::domain;
namespace app = safecopy::application;
namespace infra = safecopy::infrastructure;
namespace fs = std::filesystem;

namespace {
    int checks = 0;

    void check(bool condition, const char* label) {
        ++checks;
        if (!condition) {
            throw std::runtime_error(label);
        }
    }

    template <class Action>
    void rejects(Action action, const char* label) {
        bool rejected = false;
        try {
            action();
        } catch (const std::exception&) {
            rejected = true;
        }
        check(rejected, label);
    }

    void write(const fs::path& path, const string& value) {
        fs::create_directories(path.parent_path());
        std::ofstream output(path, std::ios::binary);
        output << value;
        output.close();
        if (!output) {
            throw std::runtime_error("Test fixture write failed.");
        }
    }

    string read(const fs::path& path) {
        std::ifstream input(path, std::ios::binary);
        if (!input) {
            throw std::runtime_error("Test fixture read failed.");
        }
        return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
    }

    class FakeRepository final : public app::IBackupRepository {
    public:
        bool copyFails = false;
        bool commitFails = false;
        int reserves = 0;
        int commits = 0;
        domain::BackupFacts facts{true, false};

        domain::BackupFacts inspectBackup(const string&) const override {
            return facts;
        }

        domain::RestoreFacts inspectRestore(const string&) const override {
            return {false, false};
        }

        domain::Backup reserve() override {
            ++reserves;
            return domain::Backup(domain::BackupId("20261008_000000"));
        }

        size_t copyInto(const domain::BackupId&, const string&) override {
            if (copyFails) {
                throw std::runtime_error("Simulated copy failure.");
            }
            return 2;
        }

        void commit(const domain::BackupId&, size_t) override {
            ++commits;
            if (commitFails) {
                throw std::runtime_error("Simulated marker failure.");
            }
        }

        std::vector<domain::Backup> listCompleted() const override {
            return {};
        }

        size_t restoreFiles(const domain::BackupId&, const string&) override {
            return 0;
        }
    };

    void testDomain() {
        // 백업 ID의 유효성과 동등성
        rejects([] { domain::BackupId id(""); }, "empty ID");
        rejects([] { domain::BackupId id("../outside"); }, "path traversal ID");
        check(domain::BackupId("123_4") == domain::BackupId("123_4"), "ID equality");

        // 백업 상태 전이
        domain::Backup backup(domain::BackupId("123"));
        check(backup.state() == domain::BackupState::Preparing, "initial state");
        backup.complete(0);
        check(backup.state() == domain::BackupState::Completed && backup.fileCount() == 0,
              "empty completion");
        rejects([&] { backup.complete(1); }, "repeat completion");
        rejects([&] { backup.fail(); }, "completed to failed");
        domain::Backup failed(domain::BackupId("124"));
        failed.fail();
        check(failed.state() == domain::BackupState::Failed, "failed transition");
        rejects([&] { failed.complete(1); }, "failed to completed");

        // 백업 및 복원 정책
        rejects([] { domain::BackupPolicy::validate(domain::BackupFacts{false, false}); },
                "missing source policy");
        rejects([] { domain::BackupPolicy::validate(domain::BackupFacts{true, true}); },
                "recursive backup policy");
        rejects([] { domain::BackupPolicy::validate(domain::RestoreFacts{true, false}); },
                "overwrite policy");
        rejects([] { domain::BackupPolicy::validate(domain::RestoreFacts{false, true}); },
                "restore inside root policy");
    }

    void testApplication() {
        FakeRepository fake;
        app::BackupService service(fake);
        const auto done = service.create("source");
        check(done.state() == domain::BackupState::Completed && done.fileCount() == 2 &&
                  fake.commits == 1,
              "use case completes after commit");

        // 복사 실패 시 완료 처리를 수행하지 않습니다.
        fake.copyFails = true;
        rejects([&] { service.create("source"); }, "copy failure propagates");
        check(fake.commits == 1, "copy failure does not commit");

        // 완료 기록 실패도 호출자에게 전달합니다.
        fake.copyFails = false;
        fake.commitFails = true;
        rejects([&] { service.create("source"); }, "commit failure propagates");

        // 입력 검증은 백업 공간 예약보다 먼저 수행합니다.
        fake.facts = {false, false};
        const auto previous = fake.reserves;
        rejects([&] { service.create("source"); }, "validation before reserve");
        check(fake.reserves == previous, "invalid input has no reserved backup");
    }

    void testFilesystem() {
        // 테스트 데이터는 확인할 수 있도록 build/test-fixtures에 남겨두며, 폴더와 그 안의 파일을
        // 통째로 삭제하지 않습니다.
        const auto unique =
            std::to_string(std::chrono::high_resolution_clock::now().time_since_epoch().count());
        const auto fixture = fs::absolute(fs::path("build/test-fixtures") / unique);
        const auto source = fixture / "source folder";
        const auto store = fixture / "store";
        write(source / "a.txt", "hello\n");
        write(source / fs::u8path(u8"하위 폴더/한글 파일.txt"), "nested\n");
        write(source / "bytes.bin", string("A\0B", 3));
        fs::create_directories(source / "empty");

        // 백업 생성과 목록 조회
        infra::FileBackupRepository disk(store);
        app::BackupService diskService(disk);
        const auto first = diskService.create(source.u8string());
        check(first.fileCount() == 3, "three files copied");
        const auto second = diskService.create(source.u8string());
        check(!(first.id() == second.id()), "backup IDs unique");
        check(diskService.list().size() == 2, "two completed backups");

        // 복원된 파일 내용과 빈 폴더 확인
        const auto restored = fixture / "restored";
        check(diskService.restore(first.id(), restored.u8string()) == 3, "restore count");
        check(read(restored / "a.txt") == read(source / "a.txt"), "text content restored");
        check(read(restored / "bytes.bin") == read(source / "bytes.bin"),
              "binary content restored");
        check(read(restored / fs::u8path(u8"하위 폴더/한글 파일.txt")) == "nested\n",
              "Unicode path restored");
        check(fs::is_directory(restored / "empty"), "empty directory restored");

        // 기존 대상과 잘못된 원본 경로 거부
        rejects([&] { diskService.restore(first.id(), restored.u8string()); },
                "existing restore rejected");
        check(read(restored / "a.txt") == "hello\n", "existing contents unchanged");
        rejects([&] { diskService.restore(first.id(), (store / "restore").u8string()); },
                "restore inside store rejected");
        rejects([&] { diskService.create((fixture / "missing").u8string()); },
                "missing folder rejected");
        rejects([&] { diskService.create(fixture.u8string()); }, "source contains store rejected");

        // 미완료 백업과 잘못된 완료 기록 처리
        const auto unfinished = disk.reserve();
        disk.copyInto(unfinished.id(), source.u8string());
        check(diskService.list().size() == 2, "unfinished backup hidden");
        write(store / unfinished.id().value() / "complete.txt", "SAFECOPY_DDD_V1\nnot-a-number\n");
        check(diskService.list().size() == 2, "invalid marker hidden");
        rejects([&] { diskService.restore(unfinished.id(), (fixture / "bad-restore").u8string()); },
                "invalid marker cannot restore");

        // 파일이 없는 폴더의 백업과 복원
        const auto empty = fixture / "empty-source";
        fs::create_directory(empty);
        const auto emptyBackup = diskService.create(empty.u8string());
        check(emptyBackup.fileCount() == 0, "empty backup count");
        check(diskService.restore(emptyBackup.id(), (fixture / "empty-restored").u8string()) == 0,
              "empty restore");
    }
} // namespace

int main(int argc, char* argv[]) {
    try {
        testDomain();
        testApplication();

        if (argc == 2 && string(argv[1]) == "--unit") {
            cout << "PASS: " << checks << " domain/application checks\n";
            return 0;
        }
        testFilesystem();
        cout << "PASS: " << checks << " checks\n";
        return 0;
    } catch (const std::exception& error) {
        cerr << "FAIL after " << checks << " checks: " << error.what() << '\n';
        return 1;
    }
}
