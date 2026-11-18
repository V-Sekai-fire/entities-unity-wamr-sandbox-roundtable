// usd.wasm: the same OpenUSD guest core as usd.elf (guest/usd/usd_core.cpp, mem_resolver.cpp
// from V-Sekai-fire/interactor-usd-guest), with a plain C ABI in place of godot-sandbox's
// Variant API. The host copies a .usdz into usd_buffer(n), then calls usd_open_staged().
#include <cstdint>
#include <string>

#include "usd_core.h"

static std::string g_pkg;
static std::string g_msg;

#define EXPORT(name) __attribute__((export_name(#name)))

extern "C" {
EXPORT(usd_init) int usd_init() {
	g_msg = usdg::init();
	return g_msg.rfind("FAIL", 0) == 0 || g_msg.rfind("ERR", 0) == 0 ? -1 : 0;
}
// Guest-side staging buffer; a package past 4 GiB cannot exist in wasm32 anyway.
EXPORT(usd_buffer) uint8_t *usd_buffer(uint32_t n) {
	g_pkg.assign(n, '\0');
	return reinterpret_cast<uint8_t *>(&g_pkg[0]);
}
EXPORT(usd_open_staged) int usd_open_staged() {
	g_msg = usdg::open(g_pkg);
	std::string().swap(g_pkg);
	return g_msg.rfind("ok", 0) == 0 ? 0 : -1;
}
EXPORT(usd_message) const char *usd_message() { return g_msg.c_str(); }
EXPORT(usd_close) void usd_close() { usdg::close(); }
EXPORT(usd_mesh_count) int usd_mesh_count() { return usdg::mesh_count(); }
EXPORT(usd_material_count) int usd_material_count() { return usdg::material_count(); }
EXPORT(usd_texture_count) int usd_texture_count() { return usdg::texture_count(); }
}
