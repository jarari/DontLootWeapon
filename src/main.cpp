#include "include/detours.h"
#include <SimpleIni.h>
#include <fstream>
#include <shared_mutex>
#include <wtypes.h>
using namespace RE;

REL::Relocation<uintptr_t> ptr_EvaluateWeapon{ REL::ID(993923) };
uintptr_t EvaluateWeaponOrig;

CSimpleIniA ini(true, false, false);
BGSKeyword* preventKeyword = nullptr;
TESFaction* followerFaction = nullptr;
bool preventFollower = false;

TESForm* GetFormFromMod(std::string modname, uint32_t formid) {
	if (!modname.length() || !formid)
		return nullptr;
	TESDataHandler* dh = TESDataHandler::GetSingleton();
	TESFile* modFile = nullptr;
	for (auto it = dh->files.begin(); it != dh->files.end(); ++it) {
		TESFile* f = *it;
		if (strcmp(f->filename, modname.c_str()) == 0) {
			modFile = f;
			break;
		}
	}
	if (!modFile)
		return nullptr;
	uint8_t modIndex = modFile->compileIndex;
	uint32_t id = formid;
	if (modIndex < 0xFE) {
		id |= ((uint32_t)modIndex) << 24;
	}
	else {
		uint16_t lightModIndex = modFile->smallFileCompileIndex;
		if (lightModIndex != 0xFFFF) {
			id |= 0xFE000000 | (uint32_t(lightModIndex) << 12);
		}
	}
	return TESForm::GetFormByID(id);
}

void HookedEvaluateWeapon(void* a_combatBehavior, TESObjectREFR* a_wepRefr, BGSObjectInstance* a_wepInstance, float a_scoreRatio, void* a_searchData) {
	Actor* actor = (*(TESForm**)((uintptr_t)a_searchData + 0x8))->As<Actor>();
	if (actor) {
		if (actor->HasKeyword(preventKeyword))
			return;
		else if (preventFollower && actor->IsInFaction(followerFaction))
			return;
	}

	typedef void (*FnEvaluateWeapon)(void*, TESObjectREFR*, BGSObjectInstance*, float, void*);
	FnEvaluateWeapon fn = (FnEvaluateWeapon)EvaluateWeaponOrig;
	if (fn)
		(*fn)(a_combatBehavior, a_wepRefr, a_wepInstance, a_scoreRatio, a_searchData);
}

void LoadConfigs() {
	std::string path = "Data\\MCM\\Config\\DontLootWeapon\\settings.ini";
	if (std::filesystem::exists("Data\\MCM\\Settings\\DontLootWeapon.ini")) {
		path = "Data\\MCM\\Settings\\DontLootWeapon.ini";
	}
	SI_Error result = ini.LoadFile(path.c_str());
	if (result >= 0) {
		preventFollower = std::stoi(ini.GetValue("Main", "bPreventFollower", "0")) > 0;
	}
}

class MenuWatcher : public BSTEventSink<MenuOpenCloseEvent> {
	virtual BSEventNotifyControl ProcessEvent(const MenuOpenCloseEvent& evn, BSTEventSource<MenuOpenCloseEvent>* src) override {
		if (!evn.opening) {
			if (evn.menuName == BSFixedString("PauseMenu") || evn.menuName == BSFixedString("LoadingMenu")) {
				LoadConfigs();
			}
		}
		return BSEventNotifyControl::kContinue;
	}
};


void InitializePlugin() {
	preventKeyword = (BGSKeyword*)GetFormFromMod("DontLootWeapon.esm", 0x800);
	if (!preventKeyword)
		logger::critical(FMT_STRING("Keyword form not found!"));
	followerFaction = (TESFaction*)TESForm::GetFormByID(0x23C01);

	MenuWatcher* mw = new MenuWatcher();
	UI::GetSingleton()->GetEventSource<MenuOpenCloseEvent>()->RegisterSink(mw);
}

extern "C" DLLEXPORT bool F4SEAPI F4SEPlugin_Query(const F4SE::QueryInterface * a_f4se, F4SE::PluginInfo * a_info) {
#ifndef NDEBUG
	auto sink = std::make_shared<spdlog::sinks::msvc_sink_mt>();
#else
	auto path = logger::log_directory();
	if (!path) {
		return false;
	}

	*path /= fmt::format(FMT_STRING("{}.log"), Version::PROJECT);
	auto sink = std::make_shared<spdlog::sinks::basic_file_sink_mt>(path->string(), true);
#endif

	auto log = std::make_shared<spdlog::logger>("global log"s, std::move(sink));

#ifndef NDEBUG
	log->set_level(spdlog::level::trace);
#else
	log->set_level(spdlog::level::info);
	log->flush_on(spdlog::level::warn);
#endif

	spdlog::set_default_logger(std::move(log));
	spdlog::set_pattern("%g(%#): [%^%l%$] %v"s);

	logger::info(FMT_STRING("{} v{}"), Version::PROJECT, Version::NAME);

	a_info->infoVersion = F4SE::PluginInfo::kVersion;
	a_info->name = Version::PROJECT.data();
	a_info->version = Version::MAJOR;

	if (a_f4se->IsEditor()) {
		logger::critical(FMT_STRING("loaded in editor"));
		return false;
	}

	const auto ver = a_f4se->RuntimeVersion();
	if (ver < F4SE::RUNTIME_1_10_162) {
		logger::critical(FMT_STRING("unsupported runtime v{}"), ver.string());
		return false;
	}

	F4SE::AllocTrampoline(8 * 8);

	return true;
}

extern "C" DLLEXPORT bool F4SEAPI F4SEPlugin_Load(const F4SE::LoadInterface * a_f4se) {
	F4SE::Init(a_f4se);

	EvaluateWeaponOrig = ptr_EvaluateWeapon.address();
	DetourTransactionBegin();
	DetourUpdateThread(GetCurrentThread());
	DetourAttach(&(PVOID&)EvaluateWeaponOrig, HookedEvaluateWeapon);
	DetourTransactionCommit();

	const F4SE::MessagingInterface* message = F4SE::GetMessagingInterface();
	message->RegisterListener([](F4SE::MessagingInterface::Message* msg) -> void {
		if (msg->type == F4SE::MessagingInterface::kGameDataReady) {
			InitializePlugin();
		}
	});

	return true;
}
