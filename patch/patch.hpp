#ifndef PATCH_HPP
#define PATCH_HPP

#include <cstdint>

struct patch_module_t {
	uint64_t m_base;
	uint32_t m_size;
};

void patch_start_worker();
void patch_shutdown();
void patch_request(int level);
bool patch_is_working();

#endif
