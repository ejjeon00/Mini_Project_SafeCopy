#include "filesystem_repository.hpp"

#include <charconv>
#include <cstddef>
#include <fstream>
#include <iostream>
#include <string>

#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#endif

using std::cerr;
using std::cin;
using std::cout;
using std::size_t;
using std::string;

namespace {
    string ask(const char* prompt) {
        cout << prompt;
        string input;
        if (!std::getline(cin, input)) throw std::runtime_error("입력이 종료되었습니다.");
        return input;
    }
    void show(const std::vector<safecopy::domain::Backup>& backups) {
        cout << "\n[완료된 백업 목록]\n";
        if (backups.empty()) cout << "완료된 백업이 없습니다. 메뉴 1번에서 먼저 백업을 만드세요.\n";
        for (size_t i = 0; i < backups.size(); ++i) {
            cout << i + 1 << ". " << backups[i].id().value()
                      << " (파일 " << backups[i].fileCount() << "개)\n";
        }
    }
    size_t selectIndex(const string& text, size_t total) {
        size_t number = 0;
        const auto parsed = std::from_chars(text.data(), text.data() + text.size(), number);
        if (parsed.ec != std::errc{} || parsed.ptr != text.data() + text.size() || number == 0 || number > total) {
            throw std::invalid_argument("백업 목록에 표시된 번호를 입력하세요.");
        }
        return number - 1;
    }
    void logError(const char* message) {
        std::ofstream log("safecopy_ddd_errors.log", std::ios::app);
        log << message << '\n';
        log.close();
        if (!log) cerr << "주의: 오류 기록 파일을 저장하지 못했습니다.\n";
    }
} // namespace

int main() {
#ifdef _WIN32
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);
#endif
    try {
        safecopy::infrastructure::FileBackupRepository repository;
        safecopy::application::BackupService service(repository);
        cout << "백업 저장 위치: "
                  << std::filesystem::absolute("backups_ddd").u8string() << '\n';

        while (cin) {
            cout << "\n============== SafeCopy 폴더 백업 도구 ===============\n"
                      << "아래 번호는 작업 순서가 아니라 선택할 기능입니다.\n\n"
                      << "1. 폴더 백업하기    - 원본 폴더의 파일을 백업으로 저장\n"
                      << "2. 백업 목록 보기   - 저장이 완료된 백업 확인\n"
                      << "3. 백업 복원하기    - 선택한 백업을 새 폴더로 복사\n"
                      << "4. 프로그램 종료\n";
            try {
                const auto choice = ask("\n원하는 기능 번호를 입력하세요 (1~4): ");
                if (choice == "4") {
                    cout << "프로그램을 종료합니다.\n";
                    break;
                }
                if (choice == "1") {
                    cout << "\n[폴더 백업하기]\n"
                              << "이제 메뉴 번호가 아닌 원본 폴더 경로를 입력하세요.\n"
                              << "ex) E:\\내자료 (따옴표 없이 입력)\n";
                    const auto backup = service.create(ask("\n--> 백업할 원본 폴더 경로: "));
                    cout << "백업 완료: " << backup.id().value()
                              << ", 파일 " << backup.fileCount() << "개\n";
                } else if (choice == "2") show(service.list());
                else if (choice == "3") {
                    const auto backups = service.list();
                    show(backups);
                    if (backups.empty()) continue;
                    const auto index = selectIndex(ask("\n--> 복원할 백업의 목록 번호: "), backups.size());
                    cout << "아직 존재하지 않는 새 폴더 경로를 입력하세요.\n"
                              << "예: E:\\복원자료 (따옴표 없이 입력)\n";
                    const auto target = ask("\n--> 복원할 새 폴더 경로: ");
                    const auto count = service.restore(backups[index].id(), target);
                    cout << "복원 완료: 파일 " << count << "개\n";
                } else cout << "메뉴 번호 1, 2, 3, 4 중 하나를 입력하세요.\n";
            } catch (const std::filesystem::filesystem_error& error) {
                cerr << "파일 작업 실패: 경로, 접근 권한, 저장 공간을 확인하세요.\n"
                          << "시스템 오류 상세: " << error.what() << '\n';
                logError(error.what());
            } catch (const std::exception& error) {
                cerr << "작업 실패: " << error.what() << '\n';
                logError(error.what());
            }
        }
    } catch (const std::exception& error) {
        cerr << "프로그램 시작 실패: " << error.what() << '\n';
        logError(error.what());
        
        return 1;
    }

    return 0;
}