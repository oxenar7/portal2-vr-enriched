#pragma once
#include <cstdint>
#include <iostream>
#include "MinHook.h"
#include "bitbuf.h"
#include "logger.h"
#include "hud_capture.h"
#include "required_hooks.h"
#include "optional_hooks.h"
#include "compatibility_hook_policy.h"
#include "instructor_hints.h"

class Game;
class VR;
class ITexture;
class CViewSetup;
class CUserCmd;
class QAngle;
class Vector;
struct matrix3x4_t;
struct edict_t;
struct ModelRenderInfo_t;
struct trace_tx;
class IMatRenderContext;
struct vrect_t;
struct Ray_t;
class VMatrix;
struct Rect_t;
class bf_write;
class bf_read;


template <typename T>
struct Hook {
	T fOriginal{};
	LPVOID pTarget = nullptr;
	bool isEnabled = false;

	int createHook(LPVOID targetFunc, LPVOID detourFunc)
	{
		if (!targetFunc)
			return 1;
		const MH_STATUS status = MH_CreateHook(targetFunc, detourFunc, reinterpret_cast<LPVOID *>(&fOriginal));
		if (status != MH_OK)
		{
			Logger::Write(std::string("MinHook creation failed: ") + typeid(T).name() +
			              " status=" + std::to_string(status));
			return 1;
		}
		pTarget = targetFunc;
		return 0;
	}

	int enableHook()
	{
		if (!pTarget)
			return 1;

		MH_STATUS status = MH_EnableHook(pTarget);
		if (status != MH_OK)
		{
			Logger::Write("MinHook enable failed: " + std::to_string(status));
			return 1;
		}
		isEnabled = true;
		return 0;
	}

	int disableHook()
	{
		if (!pTarget || MH_DisableHook(pTarget) != MH_OK)
		{
			Logger::Write("Failed to disable hook");
			return 1;
		}
		isEnabled = false;
		return 0;
	}
};


// Source Engine functions
typedef void(__thiscall *tRenderView)(void *thisptr, CViewSetup &setup, CViewSetup &hudViewSetup, int nClearFlags, int whatToDraw);
typedef bool(__thiscall *tCreateMove)(void *thisptr, float flInputSampleTime, CUserCmd *cmd);
typedef void(__thiscall *tCalcViewModelView)(void *thisptr, const Vector &eyePosition, const QAngle &eyeAngles);
typedef void(__thiscall *tDrawViewModels)(void *, const CViewSetup &, bool);
typedef void(__thiscall *tViewmodelCalcView)(void *, void *owner, const Vector &, const QAngle &);
typedef void(__cdecl *tFormatViewModelAttachment)(void *owner, Vector &, bool);
typedef void(__thiscall *tSetViewmodelLocalOrigin)(void *, const Vector &);
typedef void(__thiscall *tSetViewmodelLocalAngles)(void *, const QAngle &);
typedef float(__thiscall *tViewmodelScreenAspect)(void *, int, int);
typedef void(__thiscall *tViewmodelFormatAttachment)(void *, int, matrix3x4_t &);
typedef int(__thiscall *tLookupViewmodelAttachment)(void *, const char *);
typedef void *(__thiscall *tGetViewmodelOwner)(void *);
typedef const Vector &(__thiscall *tGetViewmodelAbsOrigin)(void *);
typedef const QAngle &(__thiscall *tGetViewmodelAbsAngles)(void *);
typedef float(__thiscall *tProcessUsercmds)(void *thisptr, edict_t *player, void *buf, int numcmds, int totalcmds, int dropped_packets, bool ignore, bool paused);
typedef int(__cdecl *tReadUsercmd)(void *buf, CUserCmd *move, CUserCmd *from);
typedef int(__cdecl *tWriteUsercmd)(void *buf, CUserCmd *to, CUserCmd *from);
typedef Vector *(__thiscall *tEyePosition)(void *thisptr, Vector *eyePos);
typedef void(__thiscall *tPushRenderTargetAndViewport)(void *thisptr, ITexture *pTexture, ITexture *pDepthTexture, int nViewX, int nViewY, int nViewW, int nViewH);
typedef void(__thiscall *tPopRenderTargetAndViewport)(void *thisptr);
typedef void(__thiscall *tVgui_Paint)(void *thisptr, int mode);
typedef void(__thiscall *tPaintTraverseEx)(void *thisptr, std::uintptr_t panel, bool paintPopups);

typedef bool(__thiscall* tTraceFirePortal)(void* thisptr, const Vector& vTraceStart, const Vector& vDirection, bool bPortal2, int iPlacedBy, void* tr);

typedef void(__thiscall* tPlayerPortalled)(void* thisptr, void* a2, __int64 a3);

typedef int(__thiscall* tDrawSelf)(void* thisptr, int x, int y, int w, int h, const void* clr, float flApparentZ);
typedef bool(__cdecl* tClipTransform)(const Vector& point, Vector* pClip);

// HUD

typedef void(__cdecl* tGetHudSize)(int& w, int& h);


typedef Vector* (__thiscall* tWeapon_ShootPosition)(void* thisptr, Vector* shootPos);
typedef bool(__thiscall* tUpdateObject)(void* thisptr, void* pPlayer, float flError, bool bIsTeleport);
typedef bool(__thiscall* tUpdateObjectVM)(void* thisptr, void* pPlayer, float flError);
typedef bool(__cdecl* tGetPreferredCarryAngles)(void* pObject, void* pPlayer, const void* localToWorld, void* outAngles);
typedef void(__thiscall* tPollForUseEntity)(void* thisptr, bool basicUse, void** useEntity, void** usePortal);
typedef void*(__thiscall* tFindUseEntity)(void* thisptr, void** usePortal);
typedef QAngle&(__thiscall* tEyeAngles)(void* thisptr);


typedef int(__cdecl* tGetDefaultFOV)(void*& thisptr);
typedef double(__cdecl* tGetFOV)(void*& thisptr);
using tGetViewModelFOV = CompatibilityHooks::ViewModelFov;

typedef void(__thiscall* tSetDrawOnlyForSplitScreenUser)(void* thisptr, int nSlot);
typedef bool(__thiscall* tCHudCrosshair_ShouldDraw)(void* thisptr);

typedef void*(__cdecl* tUTIL_Portal_FirstAlongRay)(const Ray_t& ray, float& fMustBeCloserThan);
typedef float(__cdecl* tUTIL_IntersectRayWithPortal)(const Ray_t& ray, const void* pPortal);
typedef void(__cdecl* tUTIL_Portal_AngleTransform)(const VMatrix& matThisToLinked, const QAngle& qSource, QAngle& qTransformed);
typedef int(__thiscall* tEntindex)(void* thisptr);
typedef void*(__thiscall* tGetOwner)(void* thisptr);
typedef void* (__thiscall* tCWeaponPortalgun_FirePortal)(void* thisptr, bool bPortal2, Vector* pVector);
typedef void(__cdecl* tRumbleEffect)(int splitSlot, int index, int data, int flags);
typedef void(__thiscall* tButtonPress)(void* thisptr, void* pActivator);
typedef void(__thiscall* tLocatorPaint)(void* thisptr);
typedef void(__thiscall* tLocatorDrawTarget)(void* thisptr, void* target);
typedef bool(__cdecl* tGetVectorInScreenSpace)(InstructorHints::Vec3 pos, int& x, int& y, const InstructorHints::Vec3* offset);
typedef void(__thiscall* tLocatorSetBinding)(void* thisptr, const char* binding);
typedef const char*(__thiscall* tLocatorUseBindingImage)(void* thisptr, char* iconName, size_t size);

class Hooks
{
public:
	bool m_Ready = false;
	bool m_MinHookInitialized = false;
	bool m_OptionalRollbackFailed = false;
	RequiredHooks m_RequiredHooks;
	OptionalHooks m_CompatibilityHooks;
	bool m_HudCaptureHooksReady = false;
    bool m_EyeHudHookReady = false;
	static inline Game *m_Game;
	static inline VR *m_VR;

	static inline Hook<tRenderView> hkRenderView;
	static inline Hook<tCreateMove> hkCreateMove;
	static inline Hook<tCalcViewModelView> hkCalcViewModelView;
    static inline Hook<tDrawViewModels> hkDrawViewModels;
    static inline Hook<tViewmodelCalcView> hkViewmodelCalcView;
    static inline Hook<tFormatViewModelAttachment> hkFormatViewModelAttachment;
    static inline Hook<tViewmodelScreenAspect> hkViewmodelScreenAspect;
    static inline tSetViewmodelLocalOrigin SetViewmodelLocalOrigin = nullptr;
    static inline tSetViewmodelLocalAngles SetViewmodelLocalAngles = nullptr;
    static inline thread_local float m_ViewmodelDrawAspect = 0.0f;
    static inline thread_local bool m_ControllerViewmodelUpdate = false;
    bool m_ViewmodelAlignmentReady = false;
    bool m_MuzzleSamplingReady = false;
    static inline Hook<tViewmodelFormatAttachment> hkViewmodelFormatAttachment;
    static inline tLookupViewmodelAttachment LookupViewmodelAttachment = nullptr;
    static inline tGetViewmodelOwner GetViewmodelOwner = nullptr;
    static inline tGetViewmodelAbsOrigin GetViewmodelAbsOrigin = nullptr;
    static inline tGetViewmodelAbsAngles GetViewmodelAbsAngles = nullptr;
	static inline Hook<tProcessUsercmds> hkProcessUsercmds;
	static inline Hook<tReadUsercmd> hkReadUsercmd;
	static inline Hook<tWriteUsercmd> hkWriteUsercmd;
	static inline Hook<tEyePosition> hkEyePosition;
	static inline Hook<tPushRenderTargetAndViewport> hkPushRenderTargetAndViewport;
	static inline Hook<tPopRenderTargetAndViewport> hkPopRenderTargetAndViewport;
	static inline Hook<tVgui_Paint> hkVgui_Paint;
	static inline Hook<tPaintTraverseEx> hkPaintTraverseEx;
	static inline bool m_VguiPaintActive = false;
	static inline bool m_HudTargetActive = false;
	static inline bool m_HudPushSeenDuringPaint = false;
	static inline bool m_HudRedirectSeenDuringPaint = false;
	static inline bool m_ExplicitHudCaptureActive = false;
	static inline thread_local const CViewSetup *m_ActiveAimEyeView = nullptr;
	static inline thread_local int m_ActiveAimEye = 0; // 1=left, 2=right
	static inline thread_local float m_ActiveReticleScale = 1.0f;
	static inline bool m_HudUnexpectedPopDuringPaint = false;
	static inline HudCapture::RouteState m_HudCaptureRoute;
	static inline unsigned m_HudPushDepth = 0;
	static inline Hook<tWeapon_ShootPosition> hkWeapon_ShootPosition;
	static inline Hook<tTraceFirePortal> hkTraceFirePortal;

	static inline Hook<tDrawSelf> hkDrawSelf;
	static inline tClipTransform ClipTransform = nullptr;
	static inline Hook<tPlayerPortalled> hkPlayerPortalled;


	static inline Hook<tGetHudSize> hkGetHudSize;
	// Game instructor hints in the caption overlay (instructor_hints.h).
	static inline Hook<tLocatorPaint> hkLocatorPaint;
	static inline Hook<tLocatorDrawTarget> hkLocatorDrawTarget;
	static inline Hook<tGetVectorInScreenSpace> hkGetVectorInScreenSpace;
	static inline Hook<tLocatorSetBinding> hkLocatorSetBinding;
	static inline Hook<tLocatorUseBindingImage> hkLocatorUseBindingImage;
	static void __fastcall dLocatorPaint(void* ecx, void* edx);
	static void __fastcall dLocatorDrawTarget(void* ecx, void* edx, void* target);
	static bool __cdecl dGetVectorInScreenSpace(InstructorHints::Vec3 pos, int& x, int& y, const InstructorHints::Vec3* offset);
	static void __fastcall dLocatorSetBinding(void* ecx, void* edx, const char* binding);
	static const char* __fastcall dLocatorUseBindingImage(void* ecx, void* edx, char* iconName, size_t size);
	
	static inline Hook<tUpdateObject> hkUpdateObject;
	static inline Hook<tUpdateObjectVM> hkUpdateObjectVM;
	static inline Hook<tGetPreferredCarryAngles> hkGetPreferredCarryAngles;
	static inline Hook<tPollForUseEntity> hkServerPollForUse;
	static inline Hook<tPollForUseEntity> hkClientPollForUse;
	// The server player whose +use search is running with the gun as its eye.
	static inline void *m_UseAimPlayer = nullptr;
	static inline tFindUseEntity FindUseEntity = nullptr;
	static inline Hook<tEyeAngles> hkEyeAngles;

	
	static inline Hook<tGetDefaultFOV> hkGetDefaultFOV;
	static inline Hook<tGetFOV> hkGetFOV;
	static inline Hook<tGetViewModelFOV> hkGetViewModelFOV;

	static inline Hook<tSetDrawOnlyForSplitScreenUser> hkSetDrawOnlyForSplitScreenUser;
	static inline Hook<tCHudCrosshair_ShouldDraw> hkCHudCrosshair_ShouldDraw;
	static inline Hook<tCWeaponPortalgun_FirePortal> hkCWeaponPortalgun_FirePortal;
	static inline Hook<tRumbleEffect> hkRumbleEffect;
	static inline Hook<tButtonPress> hkPropButtonPress;
	static inline Hook<tButtonPress> hkFloorButtonPress;
	static inline Hook<tButtonPress> hkFloorButtonUnpress;


	Hooks() {};
	Hooks(Game *game);
	void Initialize();

	~Hooks();
	bool RollbackFailedInitialization();

	int initSourceHooks();
    void InitViewmodelAlignment();
    void InitMuzzleSampling();
    static bool CanAlignViewmodel();

	// Detour functions
	static void __fastcall dRenderView(void *ecx, void *edx, CViewSetup &setup, CViewSetup &hudViewSetup, int nClearFlags, int whatToDraw);
	static bool __fastcall dCreateMove(void *ecx, void *edx, float flInputSampleTime, CUserCmd *cmd);
	static void __fastcall dCalcViewModelView(void *ecx, void *edx, const Vector &eyePosition, const QAngle &eyeAngles);
    static void __fastcall dDrawViewModels(void *, void *, const CViewSetup &, bool);
    static void __fastcall dViewmodelCalcView(void *, void *, void *, const Vector &, const QAngle &);
    static void __cdecl dFormatViewModelAttachment(void *, Vector &, bool);
    static float __fastcall dViewmodelScreenAspect(void *, void *, int, int);
    static void __fastcall dViewmodelFormatAttachment(void *, void *, int, matrix3x4_t &);
	static float __fastcall dProcessUsercmds(void *ecx, void *edx, edict_t *player, void *buf, int numcmds, int totalcmds, int dropped_packets, bool ignore, bool paused);
	static int dReadUsercmd(bf_read *buf, CUserCmd *move, CUserCmd *from);
	static int dWriteUsercmd(bf_write *buf, CUserCmd *to, CUserCmd *from);
	static Vector *__fastcall dEyePosition(void *ecx, void *edx, Vector *eyePos);
	static void __fastcall dPushRenderTargetAndViewport(void *ecx, void *edx, ITexture *pTexture, ITexture *pDepthTexture, int nViewX, int nViewY, int nViewW, int nViewH);
	static void __fastcall dPopRenderTargetAndViewport(void *ecx, void *edx);
	static void __fastcall dVGui_Paint(void *ecx, void *edx, int mode);
	static void __fastcall dPaintTraverseEx(void *ecx, void *edx, std::uintptr_t panel, bool paintPopups);

	// Fire portals from right controller
	static bool __fastcall dTraceFirePortal(void* ecx, void* edx, const Vector& vTraceStart, const Vector& vDirection, bool bPortal2, int iPlacedBy, void* tr);

	// Portalling angle fix
	static void __fastcall dPlayerPortalled(void* ecx, void* edx, void* a2, __int64 a3);

	// Crosshair
	static int __fastcall dDrawSelf(void* ecx, void* edx, int x, int y, int w, int h, const void* clr, float flApparentZ);
	static void dGetHudSize(int& w, int& h);


	static bool ScreenTransform(const Vector& point, Vector* pScreen, int width, int height);

	// Grabbable objects
	static Vector* __fastcall dWeapon_ShootPosition(void* ecx, void* edx, Vector* shootPos);
	static bool __fastcall dUpdateObject(void* ecx, void* edx, void* pPlayer, float flError, bool bIsTeleport = false);
	static bool __fastcall dUpdateObjectVM(void* ecx, void* edx, void* pPlayer, float flError);
	static bool __cdecl dGetPreferredCarryAngles(void* pObject, void* pPlayer, const void* localToWorld, void* outAngles);
	static void __fastcall dServerPollForUse(void* ecx, void* edx, bool basicUse, void** useEntity, void** usePortal);
	static void __fastcall dClientPollForUse(void* ecx, void* edx, bool basicUse, void** useEntity, void** usePortal);
	static QAngle& __fastcall dEyeAngles(void* ecx, void* edx);

	// Haptics
	static void __cdecl dRumbleEffect(int splitSlot, int index, int data, int flags);
	static void __fastcall dPropButtonPress(void* ecx, void* edx, void* pActivator);
	static void __fastcall dFloorButtonPress(void* ecx, void* edx, void* pActivator);
	static void __fastcall dFloorButtonUnpress(void* ecx, void* edx, void* pActivator);

	static int __fastcall dGetDefaultFOV(void* ecx, void* edx);
	static double __fastcall dGetFOV(void* ecx, void* edx);
	static float __fastcall dGetViewModelFOV(void* ecx, void* edx);

	static void __fastcall dSetDrawOnlyForSplitScreenUser(void* ecx, void* edx, int nSlot);
	static bool __fastcall dCHudCrosshair_ShouldDraw(void* ecx, void* edx);

	static void* __fastcall dCWeaponPortalgun_FirePortal(void* ecx, void* edx, bool bPortal2, Vector* pVector = 0);

	static inline bool m_PushedHud;


	static inline tUTIL_Portal_FirstAlongRay UTIL_Portal_FirstAlongRay;
	static inline tUTIL_IntersectRayWithPortal UTIL_IntersectRayWithPortal;
	static inline tUTIL_Portal_AngleTransform UTIL_Portal_AngleTransform;
	static inline tEntindex EntityIndex;
	static inline tGetOwner GetOwner;
};
