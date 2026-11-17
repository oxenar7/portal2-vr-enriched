#pragma once
#include <cstdint>
#include <array>
#include <atomic>
#include "vector.h"

class IClientEntityList;
class IEngineTrace;
class IEngineClient;
class IVDebugOverlay;
class IMaterialSystem;
class IBaseClientDLL;
class IViewRender;
class IViewRender;
class CBaseEntity;
class IModelInfo;
class IModelRender;
class IMaterial;
class IInput;
class ISurface;
class IClientMode;
class C_BasePlayer;
struct model_t;

class Game;
class Offsets;
class VR;
class Hooks;

// Published only after initialization succeeds; DXVK reads it on the render thread.
inline std::atomic<Game *> g_Game{nullptr};

struct Player
{
    C_BasePlayer *pPlayer;
    bool isUsingVR;
    QAngle controllerAngle;
    Vector controllerPos;
    QAngle prevControllerAngle;

    Player()
        : isUsingVR(false),
        controllerAngle({ 0,0,0 }),
        controllerPos({ 0,0,0 }),
        prevControllerAngle({ 0,0,0 })
    {}
};

class Game
{
public:
    IClientEntityList* m_ClientEntityList = nullptr;
    IEngineTrace* m_EngineTrace = nullptr;
    IEngineClient* m_EngineClient = nullptr;
    IVDebugOverlay* m_DebugOverlay = nullptr; // optional VDebugOverlay004
    IMaterialSystem* m_MaterialSystem = nullptr;
    IBaseClientDLL* m_BaseClientDll = nullptr;
    IViewRender* m_EngineViewRender = nullptr;
    IModelInfo* m_ModelInfo = nullptr;
    IModelRender* m_ModelRender = nullptr;
    IInput* m_VguiInput = nullptr;
    ISurface* m_VguiSurface = nullptr;
    IClientMode* m_ClientMode = nullptr;

    uintptr_t m_BaseEngine = 0;
    uintptr_t m_BaseClient = 0;
    uintptr_t m_BaseServer = 0;
    uintptr_t m_BaseMaterialSystem = 0;
    uintptr_t m_BaseVgui2 = 0;


    Offsets *m_Offsets = nullptr;
    VR *m_VR = nullptr;
    Hooks *m_Hooks = nullptr;

    bool m_Initialized = false;

    std::array<Player, 24> m_PlayersVRInfo;
    int m_CurrentUsercmdID = -1;

    model_t *m_ArmsModel = nullptr;
    IMaterial *m_ArmsMaterial = nullptr;
    bool m_CachedArmsModel = false;

    Game();
    ~Game();
    bool Initialize();

    void *GetInterface(const char *dllname, const char *interfacename);

    static void errorMsg(const char *msg);

    CBaseEntity *GetClientEntity(int entityIndex);
    char *getNetworkName(uintptr_t *entity);
    void ClientCmd(const char *szCmdString);
    void ClientCmd_Unrestricted(const char *szCmdString);

    typedef void *(__cdecl *tCreateInterface)(const char *name, int *returnCode);
};

