// A small program that stands in for a project in the process and update tests.
// It has no window and uses no Qt: it only has to be a program whose file lies
// in a project's current/ folder and which stays alive until told to stop.
//
// Behaviour is controlled by files next to the program, so that a test can
// start it without arguments, as the launcher starts projects:
//
//   stop       the program ends as soon as this file exists (it also gives up after a minute)
//   exitcode   the file's text is the exit code the program ends with (default 0)
//   spawn      the file's text is a folder: the program copies itself there as Child.exe and
//              starts the copy, which also waits for the stop file next to the parent
//   started.log  every start appends a line with the size of the program's own file, so a test
//              can tell which version of the program was started
//   mainexits  together with "spawn": the parent ends right after starting the child,
//              the child keeps running (how a launcher stub starts the real program)

#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <shellapi.h>

#include <chrono>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>
#include <thread>

namespace {

std::wstring directoryOf(const std::wstring& path)
{
    const size_t slash = path.find_last_of(L"\\/");
    return slash == std::wstring::npos ? L"." : path.substr(0, slash);
}

bool exists(const std::wstring& path)
{
    return GetFileAttributesW(path.c_str()) != INVALID_FILE_ATTRIBUTES;
}

std::wstring readText(const std::wstring& path)
{
    std::ifstream file(path.c_str(), std::ios::binary);
    std::stringstream buffer;
    buffer << file.rdbuf();
    const std::string bytes = buffer.str();
    // The tests write plain ASCII.
    return std::wstring(bytes.begin(), bytes.end());
}

std::wstring trimmed(std::wstring text)
{
    while (!text.empty() && (text.back() == L'\n' || text.back() == L'\r' || text.back() == L' '))
        text.pop_back();
    return text;
}

} // namespace

int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, int)
{
    wchar_t self[MAX_PATH * 4];
    GetModuleFileNameW(nullptr, self, static_cast<DWORD>(sizeof(self) / sizeof(self[0])));
    const std::wstring home = directoryOf(self);

    // "--stop-file <path>" is how the spawned copy learns where the parent's files are.
    std::wstring controlFolder = home;
    bool isChild = false;
    int argc = 0;
    wchar_t** argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    for (int i = 1; argv != nullptr && i + 1 < argc; ++i) {
        if (std::wstring(argv[i]) == L"--control") {
            controlFolder = argv[i + 1];
            isChild = true;
        }
    }

    if (!isChild) {
        // A trace of this start: the size of the program file tells an old version from a new one.
        WIN32_FILE_ATTRIBUTE_DATA data {};
        if (GetFileAttributesExW(self, GetFileExInfoStandard, &data)) {
            std::ofstream log((home + L"\\started.log").c_str(), std::ios::app);
            log << data.nFileSizeLow << "\n";
        }
    }

    if (!isChild && exists(home + L"\\spawn")) {
        const std::wstring target = trimmed(readText(home + L"\\spawn"));
        const std::wstring child = target + L"\\Child.exe";
        CreateDirectoryW(target.c_str(), nullptr);
        if (CopyFileW(self, child.c_str(), FALSE)) {
            std::wstring command = L"\"" + child + L"\" --control \"" + home + L"\"";
            STARTUPINFOW startup {};
            startup.cb = sizeof(startup);
            PROCESS_INFORMATION info {};
            if (CreateProcessW(child.c_str(), command.data(), nullptr, nullptr, FALSE, 0, nullptr,
                    target.c_str(), &startup, &info)) {
                CloseHandle(info.hThread);
                CloseHandle(info.hProcess);
            }
        }
        if (exists(home + L"\\mainexits")) {
            // Leave time for the child to start before this program disappears.
            std::this_thread::sleep_for(std::chrono::milliseconds(400));
            return 0;
        }
    }

    const auto begin = std::chrono::steady_clock::now();
    while (!exists(controlFolder + L"\\stop")) {
        if (std::chrono::steady_clock::now() - begin > std::chrono::seconds(60))
            break;
        std::this_thread::sleep_for(std::chrono::milliseconds(40));
    }

    int code = 0;
    if (!isChild && exists(home + L"\\exitcode"))
        code = static_cast<int>(std::stoll(trimmed(readText(home + L"\\exitcode"))));
    return code;
}
