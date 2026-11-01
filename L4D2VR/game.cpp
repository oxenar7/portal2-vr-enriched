#include "game.h"
#include <Windows.h>
#include <iostream>
#include "sdk.h"
#include "vr.h"
#include "hooks.h"
#include "offsets.h"
#include "sigscanner.h"
#include "logger.h"
#include <vector>
#include <winver.h>

#pragma comment(lib, "Version.lib")

static std::string ModuleVersion(const char *path)
{
    DWORD ignored = 0;
    const DWORD size = GetFileVersionInfoSizeA(path, &ignored);
    if (!size) return "version unavailable";
    std::vector<char> data(size);
    if (!GetFileVersionInfoA(path, 0, size, data.data())) return "version unavailable";
    VS_FIXEDFILEINFO *info = nullptr;
    UINT infoSize = 0;
    if (!VerQueryValueA(data.data(), "\\", reinterpret_cast<void **>(&info), &infoSize) ||
        !info || infoSize < sizeof(VS_FIXEDFILEINFO)) return "version unavailable";
    return std::to_string(HIWORD(info->dwFileVersionMS)) + "." +
           std::to_string(LOWORD(info->dwFileVersionMS)) + "." +
           std::to_string(HIWORD(info->dwFileVersionLS)) + "." +
           std::to_string(LOWORD(info->dwFileVersionLS));
}

Game::Game() = default;

Game::~Game()
{
    delete m_Hooks;
    delete m_VR;
    delete m_Offsets;
}

bool Game::Initialize()
{
    const struct Module { const char *name; uintptr_t *base; } modules[] = {
        { "client.dll", &m_BaseClient }, { "engine.dll", &m_BaseEngine },
        { "materialsystem.dll", &m_BaseMaterialSystem }, { "server.dll", &m_BaseServer },
        { "vgui2.dll", &m_BaseVgui2 }
    };
    for (const auto &module : modules) {
        const auto start = GetTickCount64();
        while (!(*module.base = reinterpret_cast<uintptr_t>(GetModuleHandleA(module.name)))) {
            if (GetTickCount64() - start > 30000) {
                errorMsg((std::string("Timed out waiting for ") + module.name).c_str());
                return false;
            }
            Sleep(50);
        }
        char path[MAX_PATH]{};
        GetModuleFileNameA(reinterpret_cast<HMODULE>(*module.base), path, MAX_PATH);
        Logger::Write(std::string("Loaded ") + module.name + ": " + path +
                      " (" + ModuleVersion(path) + ")");
    }
    const auto surfaceStart = GetTickCount64();
    while (!GetModuleHandleA("vguimatsurface.dll")) {
        if (GetTickCount64() - surfaceStart > 30000) {
            errorMsg("Timed out waiting for vguimatsurface.dll");
            return false;
        }
        Sleep(50);
    }
    char surfacePath[MAX_PATH]{};
    GetModuleFileNameA(GetModuleHandleA("vguimatsurface.dll"), surfacePath, MAX_PATH);
    Logger::Write(std::string("Loaded vguimatsurface.dll: ") + surfacePath +
                  " (" + ModuleVersion(surfacePath) + ")");

    m_ClientEntityList = (IClientEntityList *)GetInterface("client.dll", "VClientEntityList003");
    m_EngineTrace = (IEngineTrace *)GetInterface("engine.dll", "EngineTraceClient004");
    m_EngineClient = (IEngineClient *)GetInterface("engine.dll", "VEngineClient015");
    m_DebugOverlay = (IVDebugOverlay *)GetInterface("engine.dll", "VDebugOverlay004");
    m_MaterialSystem = (IMaterialSystem *)GetInterface("MaterialSystem.dll", "VMaterialSystem080");
    m_EngineViewRender = (IViewRender *)GetInterface("engine.dll", "VEngineRenderView013");
    m_ModelInfo = (IModelInfo *)GetInterface("engine.dll", "VModelInfoClient004");
    m_ModelRender = (IModelRender *)GetInterface("engine.dll", "VEngineModel016");
    m_VguiInput = (IInput *)GetInterface("vgui2.dll", "VGUI_InputInternal001");
    m_VguiSurface = (ISurface *)GetInterface("vguimatsurface.dll", "VGUI_Surface031");

    if (!m_ClientEntityList || !m_EngineTrace || !m_EngineClient || !m_MaterialSystem ||
        !m_EngineViewRender || !m_ModelInfo || !m_ModelRender ||
        !m_VguiInput || !m_VguiSurface) {
        errorMsg("A required Portal 2 Source interface is unavailable; see portal2vr.log.");
        return false;
    }

    m_Offsets = new Offsets();
    if (!m_Offsets->Validate()) {
        errorMsg("A required Portal 2 symbol was not resolved; see portal2vr.log.");
        return false;
    }

    m_VR = new VR(this);
    if (!m_VR->m_IsInitialized)
        return false;
    // Optional: Options > VR is drawn only with a verified VGUI ABI.
    m_VR->m_OptionsMenu.Initialize(m_VR, m_VguiSurface,
        GetInterface("vgui2.dll", "VGUI_Panel009"), GetInterface("vgui2.dll", "VGUI_Scheme010"));
    m_Hooks = new Hooks(this);
    m_Hooks->Initialize();
    if (!m_Hooks->m_Ready) {
        errorMsg("A required Portal 2 hook could not be installed; see portal2vr.log.");
        return false;
    }

    m_Initialized = true;
    Logger::Write("Portal 2 VR Enriched initialization complete.");
    return true;
}

void *Game::GetInterface(const char *dllname, const char *interfacename)
{
    HMODULE module = GetModuleHandleA(dllname);
    if (!module) return nullptr;
    tCreateInterface CreateInterface = (tCreateInterface)GetProcAddress(module, "CreateInterface");
    if (!CreateInterface) return nullptr;

    int returnCode = 0;
    void *createdInterface = CreateInterface(interfacename, &returnCode);

    Logger::Write(std::string("Interface ") + dllname + "/" + interfacename +
                  (createdInterface ? " OK" : " MISSING"));

    return createdInterface;
}

void Game::errorMsg(const char *msg)
{
    Logger::Write(std::string("Initialization error: ") + msg);
    MessageBox(0, msg, "Portal 2 VR Enriched", MB_ICONERROR | MB_OK);
}

CBaseEntity *Game::GetClientEntity(int entityIndex)
{
    return (CBaseEntity *)(m_ClientEntityList->GetClientEntity(entityIndex));
}

char *Game::getNetworkName(uintptr_t *entity)
{
    uintptr_t *IClientNetworkableVtable = (uintptr_t *)*(entity + 0x8);
    uintptr_t *GetClientClassPtr = (uintptr_t *)*(IClientNetworkableVtable + 0x8);
    uintptr_t *ClientClassPtr = (uintptr_t *)*(GetClientClassPtr + 0x1);
    char *m_pNetworkName = (char *)*(ClientClassPtr + 0x8);
    int classID = (int)*(ClientClassPtr + 0x10);
    std::cout << "ClassID: " << classID << std::endl;
    return m_pNetworkName;
}

void Game::ClientCmd(const char *szCmdString)
{
    m_EngineClient->ClientCmd(szCmdString);
}

void Game::ClientCmd_Unrestricted(const char *szCmdString)
{
    m_EngineClient->ClientCmd_Unrestricted(szCmdString);
}


