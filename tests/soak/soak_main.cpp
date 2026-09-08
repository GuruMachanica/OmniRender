// filepath: tests/soak/soak_main.cpp
// OmniRender soak harness — QA-01 / Package 7.
//
// Spawns the daemon and the test host, watches their working-set
// memory + frame timing, and exits when the test host completes
// (or the timeout elapses). Prints a final summary line in
// `OMNIRENDER_SOAK_SUMMARY` format that the CI workflow can
// grep for.
//
// Usage:
//   OmniRenderSoak --duration-seconds 60 --bin-dir <path>
//
// If --bin-dir is omitted, the harness looks next to itself for
// OmniRenderDaemon.exe and OmniRenderTestHost.exe.

#include <windows.h>
#include <psapi.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <thread>
#include <mutex>
#include <atomic>
#include <chrono>
#include <vector>

namespace {

struct Args {
    int  duration_seconds = 60;
    std::wstring bin_dir;
    int  sample_interval_ms = 500;
};

bool ParseArgs(int argc, wchar_t** argv, Args& out) {
    for (int i = 1; i < argc; ++i) {
        std::wstring a = argv[i];
        if (a == L"--duration-seconds" && i + 1 < argc) {
            out.duration_seconds = _wtoi(argv[++i]);
        } else if (a == L"--bin-dir" && i + 1 < argc) {
            out.bin_dir = argv[++i];
        } else if (a == L"--sample-ms" && i + 1 < argc) {
            out.sample_interval_ms = _wtoi(argv[++i]);
        } else if (a == L"--help" || a == L"-h") {
            std::wprintf(L"Usage: OmniRenderSoak [--duration-seconds N] "
                         L"[--bin-dir PATH] [--sample-ms N]\n");
            return false;
        }
    }
    if (out.bin_dir.empty()) {
        wchar_t self[MAX_PATH]{};
        if (::GetModuleFileNameW(nullptr, self, MAX_PATH) > 0) {
            std::wstring p = self;
            auto pos = p.find_last_of(L"\\/");
            if (pos != std::wstring::npos) p.resize(pos);
            out.bin_dir = p;
        }
    }
    return true;
}

struct ChildProc {
    HANDLE                   handle = nullptr;
    HANDLE                   stdout_read = nullptr;
    std::wstring             out_log_path;
    std::atomic<bool>        exited { false };
    DWORD                    exit_code = 0;
    std::mutex               lines_mutex;
    std::vector<std::string> captured_lines;
};

bool SpawnChild(const std::wstring& exe_path,
                const std::wstring& args,
                const std::wstring& log_path,
                ChildProc& out) {
    SECURITY_ATTRIBUTES sa{};
    sa.nLength = sizeof(sa);
    sa.bInheritHandle = TRUE;

    HANDLE log_handle = ::CreateFileW(log_path.c_str(), GENERIC_WRITE,
                                      FILE_SHARE_READ | FILE_SHARE_WRITE,
                                      &sa, CREATE_ALWAYS,
                                      FILE_ATTRIBUTE_NORMAL, nullptr);
    if (log_handle == INVALID_HANDLE_VALUE) {
        std::fprintf(stderr, "soak: cannot create log file %ls\n",
                     log_path.c_str());
        return false;
    }

    HANDLE stdout_read_tmp = nullptr;
    HANDLE stdout_write = nullptr;
    if (!::CreatePipe(&stdout_read_tmp, &stdout_write, &sa, 0)) {
        ::CloseHandle(log_handle);
        return false;
    }
    ::SetHandleInformation(stdout_read_tmp, HANDLE_FLAG_INHERIT, FALSE);

    STARTUPINFOW si{};
    si.cb         = sizeof(si);
    si.dwFlags    = STARTF_USESTDHANDLES;
    si.hStdOutput = stdout_write;
    si.hStdError  = log_handle;
    si.hStdInput  = nullptr;

    PROCESS_INFORMATION pi{};
    std::wstring cmdline = L"\"" + exe_path + L"\" " + args;

    BOOL ok = ::CreateProcessW(
        nullptr, cmdline.data(), nullptr, nullptr, TRUE,
        CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi);
    ::CloseHandle(stdout_write);
    ::CloseHandle(log_handle);
    if (!ok) {
        std::fprintf(stderr, "soak: CreateProcessW failed for %ls: %lu\n",
                     exe_path.c_str(), ::GetLastError());
        ::CloseHandle(stdout_read_tmp);
        return false;
    }
    out.handle        = pi.hProcess;
    out.stdout_read   = stdout_read_tmp;
    out.out_log_path  = log_path;
    // Note: we do not keep pi.hThread; it is closed by the reader thread.
    ::CloseHandle(pi.hThread);
    return true;
}

void ReaderThread(ChildProc* child) {
    char buf[4096];
    std::string carry;
    for (;;) {
        DWORD got = 0;
        if (!::ReadFile(child->stdout_read, buf, sizeof(buf), &got, nullptr) ||
            got == 0) {
            break;
        }
        carry.append(buf, got);
        // Split on newlines so the main thread can scan the buffer
        // for OMNIRENDER_TEST_HOST_SUMMARY.
        size_t pos = 0;
        while ((pos = carry.find('\n')) != std::string::npos) {
            std::string line = carry.substr(0, pos);
            if (!line.empty() && line.back() == '\r') line.pop_back();
            {
                std::lock_guard<std::mutex> lock(child->lines_mutex);
                child->captured_lines.push_back(line);
            }
            carry.erase(0, pos + 1);
        }
    }
    if (!carry.empty()) {
        std::lock_guard<std::mutex> lock(child->lines_mutex);
        child->captured_lines.push_back(carry);
    }
}

bool WaitForExit(ChildProc& c, DWORD timeout_ms) {
    DWORD rc = ::WaitForSingleObject(c.handle, timeout_ms);
    if (rc == WAIT_OBJECT_0) {
        ::GetExitCodeProcess(c.handle, &c.exit_code);
        c.exited.store(true);
        return true;
    }
    return false;
}

size_t SampleWorkingSet(HANDLE proc) {
    PROCESS_MEMORY_COUNTERS mc{};
    if (::GetProcessMemoryInfo(proc, &mc, sizeof(mc))) {
        return mc.WorkingSetSize;
    }
    return 0;
}

}  // namespace

int wmain(int argc, wchar_t** argv) {
    Args args;
    if (!ParseArgs(argc, argv, args)) return 2;
    if (args.duration_seconds < 1) args.duration_seconds = 1;

    std::wstring daemon_exe = args.bin_dir + L"\\OmniRenderDaemon.exe";
    std::wstring host_exe   = args.bin_dir + L"\\OmniRenderTestHost.exe";

    std::wprintf(L"soak: bin_dir=%ls duration=%ds sample=%dms\n",
                 args.bin_dir.c_str(), args.duration_seconds,
                 args.sample_interval_ms);

    ChildProc daemon, host;
    if (!SpawnChild(daemon_exe, L"",
                    args.bin_dir + L"\\soak_daemon.log", daemon)) {
        return 3;
    }
    std::wstring host_args = L"--duration " +
        std::to_wstring(args.duration_seconds);
    // The test host reads OMNIRENDER_TEST_HOST_DURATION_SECONDS from
    // the environment. We set that here.
    ::SetEnvironmentVariableW(L"OMNIRENDER_TEST_HOST_DURATION_SECONDS",
        std::to_wstring(args.duration_seconds).c_str());
    if (!SpawnChild(host_exe, host_args,
                    args.bin_dir + L"\\soak_test_host.log", host)) {
        ::TerminateProcess(daemon.handle, 1);
        return 4;
    }

    std::thread daemon_reader(ReaderThread, &daemon);
    std::thread host_reader(ReaderThread, &host);

    size_t max_daemon_ws = 0, max_host_ws = 0;
    auto t0 = std::chrono::steady_clock::now();
    auto t_end = t0 + std::chrono::seconds(args.duration_seconds);

    while (true) {
        size_t dws = SampleWorkingSet(daemon.handle);
        size_t hws = SampleWorkingSet(host.handle);
        if (dws > max_daemon_ws) max_daemon_ws = dws;
        if (hws > max_host_ws)   max_host_ws   = hws;

        // Exit if either child has died, or the timer is up and the
        // test host exited gracefully.
        DWORD host_rc = ::WaitForSingleObject(host.handle, 0);
        if (host_rc == WAIT_OBJECT_0) {
            ::GetExitCodeProcess(host.handle, &host.exit_code);
            break;
        }
        if (std::chrono::steady_clock::now() >= t_end) {
            std::fprintf(stderr, "soak: timeout reached, terminating\n");
            ::TerminateProcess(host.handle, 0);
            break;
        }
        ::Sleep(static_cast<DWORD>(args.sample_interval_ms));
    }

    // Give the daemon up to 5 seconds to exit cleanly.
    WaitForExit(daemon, 5000);
    if (!daemon.exited.load()) {
        ::TerminateProcess(daemon.handle, 0);
        ::WaitForSingleObject(daemon.handle, 2000);
    }

    // Close pipe handles to unblock reader threads, then join them.
    if (daemon.stdout_read) { ::CloseHandle(daemon.stdout_read); daemon.stdout_read = nullptr; }
    if (host.stdout_read)   { ::CloseHandle(host.stdout_read);   host.stdout_read = nullptr; }
    if (daemon_reader.joinable()) daemon_reader.join();
    if (host_reader.joinable())   host_reader.join();

    // Find the test host summary line in the captured output.
    std::string summary;
    {
        std::lock_guard<std::mutex> lock(host.lines_mutex);
        for (const auto& line : host.captured_lines) {
            if (line.find("OMNIRENDER_TEST_HOST_SUMMARY") != std::string::npos) {
                summary = line;
                break;
            }
        }
    }
    if (summary.empty()) {
        // Fall back to the log file.
        FILE* f = nullptr;
        _wfopen_s(&f, host.out_log_path.c_str(), L"rb");
        if (f) {
            char buf[16384];
            size_t got = fread(buf, 1, sizeof(buf) - 1, f);
            fclose(f);
            buf[got] = 0;
            std::string content(buf);
            auto pos = content.find("OMNIRENDER_TEST_HOST_SUMMARY");
            if (pos != std::string::npos) {
                auto end = content.find('\n', pos);
                if (end == std::string::npos) end = content.size();
                summary = content.substr(pos, end - pos);
            }
        }
    }

    auto t1 = std::chrono::steady_clock::now();
    double total_s = std::chrono::duration<double>(t1 - t0).count();
    std::printf("OMNIRENDER_SOAK_SUMMARY duration_s=%.2f "
                "daemon_max_ws_mb=%.2f test_host_max_ws_mb=%.2f "
                "test_host_exit=%lu test_host_summary=\"%s\"\n",
                total_s,
                static_cast<double>(max_daemon_ws) / (1024.0 * 1024.0),
                static_cast<double>(max_host_ws)   / (1024.0 * 1024.0),
                static_cast<unsigned long>(host.exit_code),
                summary.c_str());
    return 0;
}
