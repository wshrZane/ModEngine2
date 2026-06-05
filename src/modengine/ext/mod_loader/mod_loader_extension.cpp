#include "mod_loader_extension.h"
#include "archive_file_overrides.h"
#include "wwise_file_overrides.h"

#include "modengine/util/hex_string.h"
#include "modengine/util/platform.h"

#include <spdlog/spdlog.h>

#include <thread>
#include <chrono>
#include <detours/detours.h>


using namespace spdlog;
namespace fs = std::filesystem;

namespace modengine::ext {

auto loose_params_aob_1 = util::hex_string("74 68 48 8B CF 48 89 5C 24 30 E8");
auto loose_params_aob_2 = util::hex_string("0F 85 C5 00 00 00 48 8D 4C 24 28");
auto virtual_to_archive_path_ds3_aob = util::hex_aob("e8 ?? ?? ?? ?? 48 8b 5c 24 68 48 83 c4 30 41 5c 5e 5d c3 48 8b 4b 10 48 3b ca");
//auto loose_params_aob_3 = util::hex_string("E8 C8 F7 F7 FF 90 E9 73 E3 1F 04");
auto virtual_to_archive_path_ds2_aob = util::hex_aob("41 54 41 56 41 57 48 83 ec 40 48 c7 44 24 20 fe ff ff ff 48 89 5c 24 60 48 89 6c 24 68 48 89 74 24 70 48 89 7c 24 78 48");
//auto virtual_to_archive_path_sekiro_aob = util::hex_aob("40 55 56 41 54 41 55 48 83 ec 28 4d 8b e0");
//auto virtual_to_archive_path_sekiro_aob = util::hex_aob("2f 05 ee 66 53"); //undecryted
auto virtual_to_archive_path_er_aob = util::hex_aob("e8 ?? ?? ?? ?? 48 83 7b 20 08 48 8d 4b 08 72 03 48 8b 09 4c 8b 4b 18 41 b8 05 00 00 00 4d 3b c8");
auto virtual_to_archive_path_ac6_aob = util::hex_aob("cf e8 ?? ?? ?? ?? 48 83 7b 20 08 48 8d 4b 08 72 03 48 8b 09 4c 8b 4b 18 41 b8 05 00 00 00 4d 3b c8");
auto virtual_to_archive_path_nightreign_aob = util::hex_aob("e8 ?? ?? ?? ?? 48 83 7f 18 08 72 03 48 8b 3f 48 83 7b 18 05 75 66 ba 05 00 00 00");
//
auto ak_file_location_resolver_open_aob = util::hex_aob("4c 89 74 24 28 48 8b 84 24 90 00 00 00 48 89 44 24 20 4c 8b ce 45 8b c4 49 8b d7 48 8b cd e8 ?? ?? ?? ?? 8b d8");
auto ak_file_location_resolver_open_aob2 = util::hex_aob("83 c4 e9 ?? ?? ?? ?? 48 89 4c 24 f8 48 8d 64 24 f8 48 8d 0d");




std::optional<fs::path> ModLoaderExtension::resolve_mod_path(const ModInfo& mod)
{
    auto mod_path = fs::path(mod.location);
    if (mod_path.is_absolute()) {
        return mod_path;
    }

    const auto primary_search_path = mod_engine_global->get_settings().modengine_local_path();
    const auto primary_mod_path = primary_search_path / mod_path;

    if (fs::exists(primary_search_path / mod_path)) {
        return primary_mod_path;
    }

    return std::nullopt;
}


void sekiro_deferred_polling_thread()
{
    uintptr_t target_addr = modengine::util::rva2addr(0x1C76D0);
    uint8_t* target_ptr = reinterpret_cast<uint8_t*>(target_addr);

    for (int i = 0; i < 200; ++i) {
        if (*target_ptr == 0x40) {
            hooked_virtual_to_archive_path_sekiro.original = 
                reinterpret_cast<decltype(hooked_virtual_to_archive_path_sekiro.original)>(target_addr);

            DetourTransactionBegin();
            DetourUpdateThread(GetCurrentThread());
            
            DetourAttach(
                reinterpret_cast<PVOID*>(&hooked_virtual_to_archive_path_sekiro.original), 
                reinterpret_cast<PVOID>(&virtual_to_archive_path_sekiro)
            );
            
            LONG error = DetourTransactionCommit();

            if (error == NO_ERROR) {
                info(L"[Sekiro] Successfully hooked virtual_to_archive_path_sekiro!");
            } else {
                warn(L"[Sekiro] Failed to install hook! Detours Error Code: {}", error);
            }
            return;
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }

    warn(L"[Sekiro] Polling thread timed out! Memory was never decrypted.");
}

// ============================================================================

typedef HRESULT(WINAPI* fpCreateDXGIFactory)(REFIID, void**);
Hook<fpCreateDXGIFactory> hooked_CreateDXGIFactory;

HRESULT WINAPI tCreateDXGIFactory(REFIID riid, void** ppFactory) {
    //std::wstring real_mod_path = L"G:/ds2mod/mod/main/dxgi.dll";
   
    auto ds2le_dll_path =  mod_engine_global->get_settings().modengine_install_path().parent_path()/ "DS2LE" / "dxgi.dll";
    std::wstring real_mod_path = ds2le_dll_path.wstring();
    info(L"{}",real_mod_path);
    HMODULE hRealMod = LoadLibraryW(real_mod_path.c_str());

    if (hRealMod) {
        info(L"External dxgi.dll successfully attached at rendering phase.");
    } else {
        warn(L"Failed to load external dxgi.dll. Error: {}", GetLastError());
    }

    return hooked_CreateDXGIFactory.original(riid, ppFactory);
}
// ============================================================================

void ModLoaderExtension::on_attach()
{
    
    // const auto real_dxgi_path = util::system_directory() / "dxgi.dll";
    //register_hook(ALL, &hooked_CreateDXGIFactory, real_dxgi_path.wstring(), "CreateDXGIFactory", tCreateDXGIFactory);
   
    register_patch(DS3, loose_params_aob_1, replace_with<uint8_t>({ 0xEB, 0x68 }));
    register_patch(DS3, loose_params_aob_2, replace_with<uint8_t>({ 0x0F, 0x84, 0xc5, 0x00, 0x00, 0x00 }));
    register_patch(DS3, util::rva2addr(0xEA1B83), replace_with<unsigned char>({ 0x90, 0x90, 0x90, 0x90, 0x90 }));

    const auto kernel32_path = util::system_directory() / "kernel32.dll";
    register_hook(ALL, &hooked_CreateFileW, kernel32_path.wstring(), "CreateFileW", tCreateFileW);
    
    
    //register_hook(DS3, &hooked_virtual_to_archive_path_ds3, util::rva2addr(0x7d660), virtual_to_archive_path_ds3);
    register_hook(DS2, &hooked_virtual_to_archive_path_ds2, virtual_to_archive_path_ds2_aob, 0x0, virtual_to_archive_path_ds2, SCAN_FUNCTION);
    register_hook(DS3, &hooked_virtual_to_archive_path_ds3, virtual_to_archive_path_ds3_aob, 0x0, virtual_to_archive_path_ds3, SCAN_CALL_INST);
    register_hook(ELDEN_RING, &hooked_virtual_to_archive_path_eldenring, virtual_to_archive_path_er_aob, 0x0, virtual_to_archive_path_eldenring, SCAN_CALL_INST);
    register_hook(ARMORED_CORE_6, &hooked_virtual_to_archive_path_eldenring, virtual_to_archive_path_ac6_aob, 0x1, virtual_to_archive_path_eldenring, SCAN_CALL_INST);
    register_hook(NIGHTREIGN, &hooked_virtual_to_archive_path_nightreign, virtual_to_archive_path_nightreign_aob, 0x0, virtual_to_archive_path_nightreign, SCAN_CALL_INST);
    if (GetModuleHandleW(L"sekiro.exe") != nullptr) {std::thread(sekiro_deferred_polling_thread).detach();}
    
    register_hook(ARMORED_CORE_6, &hooked_ak_file_location_resolver_open, ak_file_location_resolver_open_aob, 0x1E, ak_file_location_resolver_open, SCAN_CALL_INST);
    register_hook(ELDEN_RING, &hooked_ak_file_location_resolver_open, ak_file_location_resolver_open_aob, 0x1E, ak_file_location_resolver_open, SCAN_CALL_INST);
    register_hook(NIGHTREIGN, &hooked_ak_file_location_resolver_open, ak_file_location_resolver_open_aob2, 0x2, ak_file_location_resolver_open, SCAN_CALL_INST);
   
   
    auto config = get_config<ModLoaderConfig>();
    for (const auto& mod : config.mods) {
        if (!mod.enabled) {
            continue;
        }

        info(L"Installing mod location {}", mod.location);

        auto mod_path = resolve_mod_path(mod);
        if (mod_path) {
            info(L"Resolved mod path to {}", mod_path->wstring());
            hooked_file_roots.push_back(mod_path->wstring());
            build_mod_file_cache(*mod_path);
            
        } else {
            warn(L"Unable to resolve mod path");
        }
    }
    //


    }


void ModLoaderExtension::on_detach()
{
}


}
