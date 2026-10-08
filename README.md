# SafeCopy 적용 수정본

C++17 폴더 백업 및 복원 도구에 DDD의 일부 원칙을 적용한 학습 프로젝트입니다.

**개발 기간**: 2026년 10월 
**개발 형태 및 본인 역할**: 생성형 AI의 도움을 받아 작성한 학습 프로젝트.
**GitHub**: https://github.com/ejjeon00/Mini_Project_SafeCopy


## 한글 메뉴 사용 방법

메뉴의 1~4번은 작업 순서가 아니라 기능 선택 번호입니다.

| 번호 | 기능 | 다음에 입력할 내용 |
|---|---|---|
| 1 | 폴더 백업하기 | 백업할 원본 폴더의 실제 경로. 예: E:\내자료 |
| 2 | 백업 목록 보기 | 추가 입력 없이 완료된 백업 확인 |
| 3 | 백업 복원하기 | 백업 목록 번호를 선택한 뒤, 아직 존재하지 않는 새 폴더 경로 입력 |
| 4 | 프로그램 종료 | 추가 입력 없이 종료 |

폴더 경로는 따옴표 없이 입력합니다. 1번 선택 후 폴더 경로 입력란에 4를 입력하면 종료 명령이 아니라 폴더 이름으로 해석됩니다.
백업 저장 위치는 프로그램 시작 시 표시되며, 현재 실행 폴더의 backups_ddd입니다.
Windows 콘솔의 입력·출력을 UTF-8로 설정하여 한글 메뉴와 경로를 지원합니다.

## 1. 프로젝트 개요

폴더를 날짜·시간별로 전체 백업하고, 완료된 백업을 조회하여 새 폴더에 복원합니다. 메뉴 순서는 백업, 목록, 복원, 종료입니다. 이번 수정에서는 백업의 의미와 규칙을 코드의 이름과 모델로 표현하고, 콘솔과 실제 파일 처리를 분리했습니다.

**DDD 적용 판단**: 규모와 업무 규칙이 작아 전체 DDD 도입이 필수인 프로젝트는 아닙니다. 공통 용어, 값 객체, 엔티티, 규칙의 캡슐화, 저장소 추상화만 적용했습니다. 계층을 나누는 것만으로 DDD가 완성되는 것은 아닙니다. 전문가와의 지속적인 모델링이나 여러 업무 영역의 관계 설계까지 수행한 사례로 주장하지 않습니다.

## 2. 기술 스택 및 핵심 아키텍처

- **언어·라이브러리**: C++17, STL, `std::filesystem`, 파일 스트림, `std::optional`, `std::from_chars`
- **빌드·테스트**: MSVC 19.51, `/std:c++17 /W4`, 자체 테스트 실행 파일
- **설계 구조**: Presentation → Application → Domain. Infrastructure가 저장소 인터페이스를 구현합니다.
- **의존성 주입**: `main`에서 `FileBackupRepository`를 만들고 `BackupService`에 참조로 전달합니다. 서비스는 구체적인 파일 처리 구현 대신 `IBackupRepository`에 의존합니다.

```text
SafeCopy_DDD/
├─ main.cpp                      메뉴, 입력, 출력, 오류 로그, 객체 연결
├─ domain.hpp                    BackupId, Backup, BackupState, BackupPolicy
├─ application.hpp               IBackupRepository, BackupService
├─ filesystem_repository.hpp     파일 저장소 클래스 선언
├─ filesystem_repository.cpp     파일 저장소 구현과 경로·복사 보조 함수
├─ tests.cpp                     도메인/서비스 단위 검사와 파일 통합 검사
├─ build-windows.cmd             Windows 빌드·테스트 명령
├─ README.md
└─ .gitignore
```

| 모델 | 의미 | 보장하는 규칙 |
|---|---|---|
| `BackupId` 값 객체 | 특정 백업을 가리키는 문자열 값 | 비어 있지 않은 80자 이하의 숫자·밑줄, 경로 구분자 금지 |
| `Backup` 엔티티 | ID를 유지하면서 상태가 바뀌는 백업 | 준비 상태에서만 완료 또는 실패로 변경 |
| `BackupPolicy` | 백업·복원의 허용 조건 | 원본 내부 백업과 기존 폴더/백업 내부 복원 거부 |
| `BackupService` | 작업 순서 관리 | 복사와 완료 표식 저장 후 엔티티를 완료 처리 |
| `IBackupRepository` | 백업 저장·조회·복원을 위한 계약 | 가짜 저장소와 파일 저장소를 같은 서비스에 연결 |

## 3. 핵심 기능과 설계 포인트

### 상태와 규칙의 캡슐화

`Backup`의 상태는 private입니다. 준비 중인 백업만 `complete()` 또는 `fail()`을 호출할 수 있고, 완료된 백업을 다시 완료하거나 실패로 바꾸는 요청은 거부합니다. 실패 상태는 생성 작업 안의 메모리에서만 표현하며, 실패한 엔티티를 파일로 저장하거나 반환하지 않습니다. 재실행 시에는 유효한 완료 표식이 있는 백업만 복원용 모델로 다시 만듭니다.

파일 시스템의 실제 상태는 Infrastructure가 확인합니다. Domain은 `BackupFacts`와 `RestoreFacts`를 받아 허용 여부를 결정합니다. 도메인은 콘솔 및 `std::filesystem`에 의존하지 않습니다.

**코드 근거**: [domain.hpp](domain.hpp), [application.hpp](application.hpp)

### 작업 순서와 저장소 분리

`create()`는 규칙 검사 → 저장 위치 예약 → 파일 복사 → 완료 표식 저장 → 엔티티 완료 순서로 진행합니다. 복사 실패 시 표식 저장을 호출하지 않습니다. 표식 저장 실패 시에도 성공 결과를 반환하지 않습니다. 서비스는 예외를 다시 던지고, 콘솔이 사용자 안내와 오류 기록을 담당합니다.

저장소 인터페이스는 백업 메타데이터뿐 아니라 파일 복사·복원까지 담당하는 작은 포트입니다. 백업 데이터가 파일인 이번 범위에서는 하나로 유지했습니다. 저장 전략이 다양해지면 복사 기능을 별도 인터페이스로 분리할 수 있습니다.

**코드 근거**: [BackupService와 IBackupRepository](application.hpp)

### 완료 표식과 파일 안전성

백업은 현재 실행 폴더의 `backups_ddd`에 저장되며 원본의 `backups`와 형식·저장 위치를 분리합니다. 임시 표식 파일을 기록·닫은 뒤 `complete.txt`로 이름을 변경합니다. 목록은 버전 문자열, 파일 개수 숫자, 추가 줄 여부를 검사해 깨진 표식을 제외합니다. 데이터의 실제 내용을 검사하는 무결성 검증은 아닙니다.

```text
backups_ddd/YYYYMMDD_HHMMSS/data/    백업 파일
backups_ddd/YYYYMMDD_HHMMSS/complete.txt
```

```text
SAFECOPY_DDD_V1
3
```

복원은 새 폴더에만 수행합니다. 심볼릭 링크와 지원하지 않는 파일 형식을 거부하며 실패한 부분 복사 결과는 남깁니다. 백업 번호는 `from_chars`로 전체 입력을 검사해 음수, 기호, 숫자 뒤 문자를 거부합니다. 로그 저장 실패도 화면에 경고합니다.

**코드 근거**: [filesystem_repository.cpp](filesystem_repository.cpp), [main.cpp](main.cpp)

## 4. 실행 방법과 검증 결과

Visual Studio의 C++ 개발 도구가 설치된 **개발자 명령 프롬프트**에서 이 폴더로 이동합니다.

```bat
build-windows.cmd --unit
build\SafeCopy.exe
```

전체 파일 통합 검사를 함께 실행하려면 다음을 사용합니다.

```bat
build-windows.cmd
```

GCC에서 직접 빌드하는 명령은 다음과 같습니다. GCC 빌드는 이번 환경에서 검증하지 않았습니다.

```sh
g++ -std=c++17 -Wall -Wextra main.cpp filesystem_repository.cpp -o SafeCopy
g++ -std=c++17 -Wall -Wextra tests.cpp filesystem_repository.cpp -o SafeCopyTests
./SafeCopyTests --unit
./SafeCopy
```

### 확인된 결과

- MSVC로 프로그램과 테스트 실행 파일 빌드 성공. `/W4`에서 컴파일 경고 없이 완료했습니다.
- `SafeCopyTests.exe --unit`: **도메인·서비스 검사 19개 통과**. ID 검증, 값 비교, 상태 전이, 복원 정책, 복사 실패 시 commit 미호출, commit 실패 전파, 잘못된 입력 시 예약 미실행을 확인합니다.
- 전체 파일 통합 검사는 시도했지만 `std::filesystem::canonical`의 실제 경로 조회가 현재 실행 환경에서 `Access is denied`로 차단되어 첫 파일 백업 전에 중단됐습니다. **실제 파일 백업·복원 성공을 확인한 결과는 아닙니다.**

통합 검사에는 하위 폴더, 공백·한글 경로, 바이너리 파일 내용, 빈 폴더, 중복 ID, 기존 폴더 보호, 잘못된 완료 표식과 미완료 백업 제외를 포함했습니다. 해당 검사들의 성공 여부는 일반 Windows 실행 환경에서 추가 확인해야 합니다. 검사 데이터는 `build/test-fixtures`에 남깁니다.

## 5. 한계와 다음 개선

수동 전체 백업과 순차 실행만 지원합니다. 증분 백업, 스케줄링, 암호화, 랜섬웨어 방어, 파일 해시 검증, 접근권한 등 모든 메타데이터 보존은 포함하지 않습니다. 완료 표식 이름 변경은 전원 차단 시 저장 보장이나 데이터베이스 트랜잭션을 제공하지 않습니다. 복사 중 원본 변경, 다른 프로세스의 경로 변경, 동시 실행 경쟁, 모든 종류의 Windows 재분석 지점 방어도 보장하지 않습니다.

다음 단계는 일반 Windows 환경에서 전체 파일 검사와 메뉴 실행을 확인하는 것입니다. 이후 복원 내용 비교, 원본 변경 감지, 부분 결과 관리, 저장소 실패 처리 개선을 검토할 수 있습니다. 실제 이점이 생길 때만 새 추상화를 추가합니다.

### 설계 참고 자료

- [Eric Evans DDD Reference](https://www.domainlanguage.com/wp-content/uploads/2016/05/DDD_Reference_2015-03.pdf)
- [Microsoft 도메인 모델 설계](https://learn.microsoft.com/en-us/dotnet/architecture/microservices/microservice-ddd-cqrs-patterns/microservice-domain-model)
- [Microsoft C++ filesystem 함수](https://learn.microsoft.com/en-us/cpp/standard-library/filesystem-functions?view=msvc-170)
