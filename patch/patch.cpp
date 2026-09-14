#include "patch/patch.hpp"

#include <windows.h>
#include <tlhelp32.h>
#include <cstdint>
#include <cstdio>
#include <atomic>
#include <thread>
#include <chrono>
#include <vector>
#include <string>

#include "ui/ui.hpp"

struct target_hit_t {
	std::string m_module;
	uint8_t*    m_target;
};

static HANDLE g_hproc = nullptr;
static std::vector<target_hit_t> g_targets;
static std::atomic<int> g_pending_level{ 0 };
static std::atomic<bool> g_request{ false };
static std::atomic<bool> g_working{ false };
static std::atomic<bool> g_stop{ false };
static std::atomic<bool> g_persist{ false };
static std::atomic<int>  g_persist_level{ 0 };
static std::thread g_worker;

static uint8_t* scan_module(HANDLE proc, const patch_module_t& mod, const int* pat, size_t len)
{
	uint8_t* base = (uint8_t*)mod.m_base;
	if (!base || !mod.m_size)
		return nullptr;

	uint8_t* endp = base + mod.m_size;
	MEMORY_BASIC_INFORMATION mbi{};

	while (base < endp) {
		if (!VirtualQueryEx(proc, base, &mbi, sizeof(mbi))
			|| mbi.State != MEM_COMMIT
			|| mbi.Protect == PAGE_NOACCESS
			|| (mbi.Protect & PAGE_GUARD)) {
			base += mbi.RegionSize ? mbi.RegionSize : 0x1000;
			continue;
		}

		SIZE_T regsz = mbi.RegionSize;
		std::vector<uint8_t> buf(regsz);
		SIZE_T got = 0;

		if (ReadProcessMemory(proc, mbi.BaseAddress, buf.data(), regsz, &got) && got >= len) {
			for (size_t i = 0; i + len <= got; ++i) {
				size_t k = 0;

				for (; k < len; ++k) {
					if (pat[k] != -1 && buf[i + k] != (uint8_t)pat[k])
						break;
				}

				if (k == len)
					return base + i;
			}
		}

		base += regsz;
	}

	return nullptr;
}

static uint32_t fnv1a(const char* s)
{
	uint32_t h = 0x811C9DC5u;

	while (*s) {
		h ^= (uint8_t)*s++;
		h *= 0x01000193u;
	}

	return h;
}

static bool is_system_module(const char* path)
{
	if (!path)
		return false;

	for (const char* p = path; *p; ++p) {
		if ((p[0] == 'W' || p[0] == 'w') && (p[1] == 'i' || p[1] == 'I')
			&& (p[2] == 'n' || p[2] == 'N') && (p[3] == 'd' || p[3] == 'D')
			&& (p[4] == 'o' || p[4] == 'O') && (p[5] == 'w' || p[5] == 'W')
			&& (p[6] == 's' || p[6] == 'S') && (p[7] == '\\' || p[7] == '/'))
			return true;
	}

	return false;
}

static void collect_all_targets(HANDLE proc, DWORD pid, const int* pat, size_t len)
{
	g_targets.clear();

	HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, pid);
	if (snap == INVALID_HANDLE_VALUE)
		return;

	MODULEENTRY32 me{};
	me.dwSize = sizeof(me);

	for (BOOL ok = Module32First(snap, &me); ok; ok = Module32Next(snap, &me)) {
		if (is_system_module(me.szExePath))
			continue;

		patch_module_t mod{ (uint64_t)me.modBaseAddr, me.modBaseSize };
		uint8_t* hit = scan_module(proc, mod, pat, len);

		if (!hit)
			continue;

		int32_t disp = 0;
		ReadProcessMemory(proc, hit + 2, &disp, sizeof(disp), nullptr);
		uint8_t* target = hit + 6 + disp;

		g_targets.push_back({ me.szModule, target });
	}

	CloseHandle(snap);
}

static bool resolve_target()
{
	ui_log("Waiting for FiveM...", LOG_INFO);
	HWND wnd = FindWindowA("grcWindow", nullptr);

	while (!wnd) {
		if (g_stop.load())
			return false;

		std::this_thread::sleep_for(std::chrono::milliseconds(500));
		wnd = FindWindowA("grcWindow", nullptr);
	}

	DWORD pid = 0;
	GetWindowThreadProcessId(wnd, &pid);

	if (!pid) {
		ui_log("Failed", LOG_FAIL);
		return false;
	}

	g_hproc = OpenProcess(PROCESS_ALL_ACCESS, FALSE, pid);
	if (!g_hproc) {
		ui_log("Failed", LOG_FAIL);
		return false;
	}

	int pattern[18] = {
		0x8B, 0x05, -1, -1, -1, -1,
		0x83, 0xF8, 0xFF, 0x0F, 0x85, -1, -1, -1, -1,
		0x0F, 0x57, 0xC0
	};

	collect_all_targets(g_hproc, pid, pattern, 18);

	if (g_targets.empty()) {
		ui_log("Failed", LOG_FAIL);
		return false;
	}

	ui_log("Ready", LOG_OK);
	return true;
}

static int write_all_targets(int level)
{
	int ok_count = 0;
	SIZE_T written = 0;

	for (auto& t : g_targets) {
		if (WriteProcessMemory(g_hproc, t.m_target, &level, sizeof(level), &written)
			&& written == sizeof(level))
			++ok_count;
	}

	return ok_count;
}

static void apply_patch(int level)
{
	if (!g_hproc || g_targets.empty()) {
		ui_log("Failed", LOG_FAIL);
		return;
	}

	g_working = true;

	int ok = write_all_targets(level);
	int verified = 0;

	for (auto& t : g_targets) {
		int32_t rb = 0;
		ReadProcessMemory(g_hproc, t.m_target, &rb, sizeof(rb), nullptr);

		if (rb == level)
			++verified;
	}

	if (ok == (int)g_targets.size() && verified == (int)g_targets.size()) {
		g_persist_level = level;
		g_persist = true;
		ui_log("Applied", LOG_OK);
	} else {
		ui_log("Apply failed", LOG_FAIL);
	}

	g_working = false;
}

static void worker_body()
{
	if (!resolve_target())
		return;

	auto last_persist = std::chrono::steady_clock::now();

	while (!g_stop.load()) {
		if (g_request.exchange(false)) {
			int level = g_pending_level.load();
			apply_patch(level);
		}

		if (g_persist.load()) {
			auto now = std::chrono::steady_clock::now();

			if (now - last_persist >= std::chrono::milliseconds(100)) {
				write_all_targets(g_persist_level.load());
				last_persist = now;
			}
		}

		std::this_thread::sleep_for(std::chrono::milliseconds(20));
	}
}

void patch_start_worker()
{
	g_stop = false;
	g_worker = std::thread(worker_body);
}

void patch_shutdown()
{
	g_stop = true;

	if (g_worker.joinable())
		g_worker.join();

	if (g_hproc) {
		CloseHandle(g_hproc);
		g_hproc = nullptr;
	}
}

void patch_request(int level)
{
	g_pending_level = level;
	g_request = true;
}

bool patch_is_working()
{
	return g_working.load();
}
