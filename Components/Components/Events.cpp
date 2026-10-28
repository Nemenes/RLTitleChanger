#include "Events.hpp"
#include "../Includes.hpp"

PreEvent::PreEvent() : PE_Caller(nullptr), PE_Function(nullptr), PE_Params(nullptr), PE_Detour(true) {}

PreEvent::PreEvent(class UObject* caller, class UFunction* function, void* params) : PE_Caller(caller), PE_Function(function), PE_Params(params), PE_Detour(true) {}

PreEvent::~PreEvent() {}

class UObject* PreEvent::Caller() const
{
	return PE_Caller;
}

template <typename T> T* PreEvent::GetCaller() const
{
	if (PE_Caller)
	{
		return static_cast<T*>(PE_Caller);
	}

	return nullptr;
}

class UFunction* PreEvent::Function() const
{
	return PE_Function;
}

void* PreEvent::Params() const
{
	return PE_Params;
}

template <typename T> T* PreEvent::GetParams() const
{
	if (PE_Params)
	{
		return reinterpret_cast<T*>(PE_Params);
	}

	return nullptr;
}

bool PreEvent::ShouldDetour() const
{
	if (PE_Function && EventsComponent::IsEventBlacklisted(PE_Function->ObjectInternalInteger))
	{
		return false;
	}

	return PE_Detour;
}

void PreEvent::SetDetour(bool bDetour)
{
	PE_Detour = bDetour;
}

PreEvent PreEvent::operator=(const PreEvent& other)
{
	PE_Caller = other.PE_Caller;
	PE_Function = other.PE_Function;
	PE_Params = other.PE_Params;
	PE_Detour = other.PE_Detour;
	return *this;
}

PostEvent::PostEvent() : PE_Result(nullptr) {}

PostEvent::PostEvent(class UObject* caller, class UFunction* function, void* params, void* result) : PreEvent(caller, function, params), PE_Result(result) {}

PostEvent::~PostEvent() {}

void* PostEvent::Result() const
{
	return PE_Result;
}

template <typename T> T* PostEvent::GetResult() const
{
	if (PE_Result)
	{
		return reinterpret_cast<T*>(PE_Result);
	}

	return nullptr;
}

PostEvent PostEvent::operator=(const PostEvent& other)
{
	PE_Caller = other.PE_Caller;
	PE_Function = other.PE_Function;
	PE_Params = other.PE_Params;
	PE_Detour = other.PE_Detour;
	PE_Result = other.PE_Result;
	return *this;
}

namespace Hooks
{
	void ExecuteFunctions(PreEvent& event)
	{
		for (std::function<void()> Func : MainComponent::GameFunctions) {
			if (Func) {
				Func();
			}

		}
		MainComponent::GameFunctions.clear();
	}

	void HUDPostRender(PreEvent& event)
	{
		if (event.Caller())
		{
			AHUD* hud = event.GetCaller<AHUD>();
			Instances.SetHUD(hud);
		}
	}

	void HUDPostRenderPost(const PostEvent& event)
	{
		Manager.OnTick(); 
	}

	void GameViewPortPostRender(PreEvent& event)
	{
		if (event.Caller())
		{
			Instances.SetGameViewportClient(event.GetCaller<UGameViewportClient>());


		}
	}

	void PlayerControllerTick(PreEvent& event)
	{
		if (event.Caller())
		{
			Instances.SetPlayerController(event.GetCaller<APlayerController>());
		}
	}

	void GameViewPortKeyPress(const PostEvent& event)
	{
		if (event.Params())
		{
			UGameViewportClient_TA_execHandleKeyPress_Params* handleKeyPress = event.GetParams<UGameViewportClient_TA_execHandleKeyPress_Params>();

			if (handleKeyPress->EventType == static_cast<uint8_t>(EInputEvent::IE_Released))
			{
				std::string KeyName = handleKeyPress->Key.ToString();
				if (KeyName == "F4") {
					GUI.IsOpen = !GUI.IsOpen;
				}
			}
		}
	}

	void GFxDataMainMenuAdded(PreEvent& event)
	{
		GameState.MainMenuAdded();
		//event.SetDetour(false);  Purely an example only, if you were to "SetDetour(false)" your hooked function will NOT go through Process Event, so the game will never recognize that it was called.
	}
}

EventsComponent::EventsComponent() : Component("Events", "Manages function hooks and process event.") { OnCreate(); }

EventsComponent::~EventsComponent() { OnDestroy(); }

void EventsComponent::OnCreate()
{
	Detoured = false;
	ProcessEvent = nullptr;
}

void EventsComponent::OnDestroy()
{
	DetachDetour();
}

bool EventsComponent::IsDetoured()
{
	return (Detoured && ProcessEvent);
}

void EventsComponent::AttachDetour(const ProcessEventType& detourFunction)
{
	if (!IsDetoured())
	{
		ProcessEvent = detourFunction;
		DetourTransactionBegin();
		DetourUpdateThread(GetCurrentThread());
		DetourAttach(&reinterpret_cast<PVOID&>(ProcessEvent), reinterpret_cast<PVOID>(ProcessEventDetour));
		DetourTransactionCommit();
		Detoured = true;
	}
}

void EventsComponent::DetachDetour()
{
	if (IsDetoured())
	{
		DetourTransactionBegin();
		DetourUpdateThread(GetCurrentThread());
		DetourDetach(&reinterpret_cast<PVOID&>(ProcessEvent), reinterpret_cast<PVOID>(ProcessEventDetour));
		DetourTransactionCommit();
		Detoured = false;
		ProcessEvent = nullptr;
	}
}

void EventsComponent::ProcessEventDetour(class UObject* caller, class UFunction* function, void* params, void* result)
{
	if (ProcessEvent && function)
	{
		auto preIt = PreHookedEvents.find(function->ObjectInternalInteger);
		PreEvent event(caller, function, params);

		if (preIt != PreHookedEvents.end())
		{
			for (const auto& preEvent : preIt->second)
			{
				preEvent(event);

				if (event.ShouldDetour())
				{
					ProcessEvent(caller, function, params, result);
				}
			}
		}
		else if (event.ShouldDetour())
		{
			ProcessEvent(caller, function, params, result);
		}

		auto postIt = PostHookedEvents.find(function->ObjectInternalInteger);

		if (postIt != PostHookedEvents.end())
		{
			for (const auto& postEvent : postIt->second)
			{
				postEvent(PostEvent(caller, function, params, result));
			}
		}
	}
}

bool EventsComponent::IsEventBlacklisted(uint32_t functionInteger)
{
	return (std::find(BlacklistedEvents.begin(), BlacklistedEvents.end(), functionInteger) != BlacklistedEvents.end());
}

void EventsComponent::BlacklistEvent(const std::string& function)
{
	UFunction* foundFunction = Instances.FindStaticFunction(function);

	if (foundFunction)
	{
		if (!IsEventBlacklisted(foundFunction->ObjectInternalInteger))
		{
			BlacklistedEvents.emplace_back(foundFunction->ObjectInternalInteger);
			std::sort(BlacklistedEvents.begin(), BlacklistedEvents.end());
		}
	}
	else
	{
		Console.Warning(GetNameFormatted() + "Warning: Failed to blacklist function \"" + function + "\"!");
	}
}

void EventsComponent::WhitelistEvent(const std::string& functionName)
{
	UFunction* foundFunction = Instances.FindStaticFunction(functionName);

	if (foundFunction)
	{
		std::vector<uint32_t>::iterator blackIt = std::find(BlacklistedEvents.begin(), BlacklistedEvents.end(), foundFunction->ObjectInternalInteger);

		if (blackIt != BlacklistedEvents.end())
		{
			BlacklistedEvents.erase(blackIt);
			std::sort(BlacklistedEvents.begin(), BlacklistedEvents.end());
		}
	}
	else
	{
		Console.Warning(GetNameFormatted() + "Warning: Failed to whitelist function \"" + functionName + "\"!");
	}
}

void EventsComponent::HookEventPre(const std::string& functionName, std::function<void(PreEvent&)> preHook)
{
	UFunction* foundFunction = Instances.FindStaticFunction(functionName);

	if (foundFunction)
	{
		HookEventPre(foundFunction->ObjectInternalInteger, preHook);
	}
	else
	{
		Console.Warning(GetNameFormatted() + "Warning: Failed to hook function \"" + functionName + "\"!");
	}
}

void EventsComponent::HookEventPre(uint32_t functionIndex, std::function<void(PreEvent&)> preHook)
{
	UObject* foundFunction = UObject::GObjObjects()->At(functionIndex);

	if (foundFunction && foundFunction->IsA(UFunction::StaticClass()))
	{
		if (PreHookedEvents.find(functionIndex) != PreHookedEvents.end())
		{
			PreHookedEvents[functionIndex].push_back(preHook);
		}
		else
		{
			PreHookedEvents[functionIndex] = std::vector<std::function<void(PreEvent&)>>{ preHook };
		}
	}
	else
	{
		Console.Warning(GetNameFormatted() + "Warning: Failed to hook function at index \"" + std::to_string(functionIndex) + "\"!");
	}
}

void EventsComponent::HookEventPost(const std::string& functionName, std::function<void(const PostEvent&)> postHook)
{
	UFunction* foundFunction = Instances.FindStaticFunction(functionName);

	if (foundFunction)
	{
		HookEventPost(foundFunction->ObjectInternalInteger, postHook);
	}
	else
	{
		Console.Warning(GetNameFormatted() + "Warning: Failed to hook function \"" + functionName + "\"!");
	}
}

void EventsComponent::HookEventPost(uint32_t functionIndex, std::function<void(const PostEvent&)> postHook)
{
	UObject* foundFunction = UObject::GObjObjects()->At(functionIndex);

	if (foundFunction && foundFunction->IsA(UFunction::StaticClass()))
	{
		if (PostHookedEvents.find(functionIndex) != PostHookedEvents.end())
		{
			PostHookedEvents[functionIndex].push_back(postHook);
		}
		else
		{
			PostHookedEvents[functionIndex] = std::vector<std::function<void(const PostEvent&)>>{ postHook };
		}
	}
	else
	{
		Console.Warning(GetNameFormatted() + "Warning: Failed to hook function at index \"" + std::to_string(functionIndex) + "\"!");
	}
}



void EventsComponent::Initialize()
{

    // you will need to function scan in your game for your own to hook!
    //BlacklistEvent("Function Engine.Tracker.ReportMetrics");

    // You can hook functions like this.
    //HookEventPre("Function Engine.HUD.PostRender", &Hooks::HUDPostRender);

    // Or like this, with std::bind.
    //HookEventPost("Function Engine.HUD.PostRender", std::bind(&Hooks::HUDPostRenderPost, std::placeholders::_1));

    // Or even like this, with lambda expressions.
    //HookEventPre("Function Engine.GameViewportClient.PostRender", [&](PreEvent& event) {
    //	Hooks::GameViewPortPostRender(event);
    //	Console.Write("I'm a lambda function hook!");
    //});

    HookEventPre("Function TAGame.MenuSequencer_TA.Tick", [&](PreEvent& event) {

        if (Events.resettitle == true) {
            UGFxData_PlayerTitles_TA* playertitles = Instances.GetInstanceOf<UGFxData_PlayerTitles_TA>();
            if (playertitles) {
                playertitles->UpdatePlayerTitles();
                Events.resettitle = false;
            }
        }

        if (setcustomtitle && RGBTitle) {
            Manager.OnTick();

            std::string RTC = Colors::ColorToHex(GRainbowColor::GetByte());
            std::string RTGC = Colors::ColorToHex(GRainbowColor::GetByte());

            if (!RTC.empty() && RTC.front() == '#') {
                RTC.erase(0, 1);
            }
            if (!RTGC.empty() && RTGC.front() == '#') {
                RTGC.erase(0, 1);
            }

            if (!RTC.empty() && !RTGC.empty()) {
                rainbowtitlecolor = RTC;
                rainbowtitleglowcolor = RTGC;
            }
            else {
                Console.Error("no bueno");
            }
        }


        if (setcustomtitle)
        {
            if (!dataStore)
                dataStore = Instances.GetInstanceOf<UGFxDataStore_X>();

            if (dataStore)
            {
                
                wchar_t* p = new wchar_t[titletext.size() + 1];
                for (std::string::size_type i = 0; i < titletext.size(); ++i)
                    p[i] = titletext[i];

                p[titletext.size()] = '\0';
                FString fstitletext = FString(p);

                FName t = FName(L"PlayerTitlesPlayerTitles");
                FASValue count = dataStore->GetValue(L"PlayerTitles", NULL, L"SelectedTitle");
                dataStore->SetStringValue(t, count.I, L"Text", fstitletext);
                FASValue fasColor = dataStore->GetValue(t, count.I, L"Color");
                fasColor.Type = 7;

                if (RGBTitle) {
                    fasColor.I = dataStore->FromHex(Format::ToFString(rainbowtitlecolor));
                }else {
                    fasColor.I = dataStore->FromHex(Format::ToFString(titlecolor));
                }


                FASValue fasGlowColor = dataStore->GetValue(t, count.I, L"GlowColor");
                fasGlowColor.Type = 7;

                if (RGBTitle) {
                    fasGlowColor.I = dataStore->FromHex(Format::ToFString(rainbowtitleglowcolor));
                }
                else {
                    fasGlowColor.I = dataStore->FromHex(Format::ToFString(titleglowcolor));
                }


                dataStore->SetASValue(t, count.I, L"Color", fasColor);
                dataStore->SetASValue(t, count.I, L"GlowColor", fasGlowColor);

                delete[] p;
            }
            wchar_t* p = new wchar_t[titletext.size() + 1];
            for (std::string::size_type i = 0; i < titletext.size(); ++i)
                p[i] = titletext[i];

            p[titletext.size()] = '\0';
            FString fstitletext = FString(p);

            FName t = FName(L"PlayerTitlesPlayerTitles");
            FASValue count = dataStore->GetValue(L"PlayerTitles", NULL, L"SelectedTitle");
            dataStore->SetStringValue(t, count.I, L"Text", fstitletext);
            FASValue fasColor = dataStore->GetValue(t, count.I, L"Color");
            fasColor.Type = 7;
            if (RGBTitle) {
                fasColor.I = dataStore->FromHex(Format::ToFString(rainbowtitlecolor));
            }
            else {
                fasColor.I = dataStore->FromHex(Format::ToFString(titlecolor));
            }


            FASValue fasGlowColor = dataStore->GetValue(t, count.I, L"GlowColor");
            fasGlowColor.Type = 7;

            if (RGBTitle) {
                fasGlowColor.I = dataStore->FromHex(Format::ToFString(rainbowtitleglowcolor));
            }
            else {
                fasGlowColor.I = dataStore->FromHex(Format::ToFString(titleglowcolor));
            }
            dataStore->SetASValue(t, count.I, L"Color", fasColor);
            dataStore->SetASValue(t, count.I, L"GlowColor", fasGlowColor);

            delete[] p;
        }

        if (Events.dumptitles) {
            Events.dumptitles = false;
            UPlayerTitleConfig_X* titleConfig = Instances.GetInstanceOf<UPlayerTitleConfig_X>();
            if (titleConfig) {

                json titledata = titleConfig->ToJson().ToString();
                std::string path = "titles.json";

                std::ofstream jsonFile(path);
                if (jsonFile.is_open()) {
                    jsonFile << titledata.dump(6);
                    jsonFile.close();
                    Console.Notify("Saved title data to " + path);
                }
                else {
                    Console.Error("Could not open jsonFile");
                }


                //Console.Write(titleConfig->ToJson().ToString());
                Console.Notify("Found " + std::to_string(titleConfig->Titles.Num()) + " titles!");

            }
        }

        if (Events.titlesforimgui.size() == 0) {

            UPlayerTitleConfig_X* titleConfig = Instances.GetInstanceOf<UPlayerTitleConfig_X>();

            int autoTour = 0;
            for (int i = 0; i < titleConfig->Titles.Num(); i++) {
                std::string titleId = titleConfig->Titles[i].Id.ToString();
                if (!titleId.find("AutoTour"))
                    autoTour++;
                if (autoTour == 1)
                    titleId = titleId + "_1";
                else if (autoTour == 2) {
                    titleId = titleId + "_2";
                    autoTour = 0;
                }
                Events.titlesforimgui.push_back({ titleId, titleConfig->Titles[i].Text.ToString(), Colors::ColorToHex(titleConfig->Titles[i].Color), Colors::ColorToHex(titleConfig->Titles[i].GlowColor), titleConfig->Titles[i].Color, titleConfig->Titles[i].GlowColor });
            }
        }

        /*
        if (!dataStore)
            dataStore = Instances.GetInstanceOf<UGFxDataStore_X>();
        if (dataStore) {
            FCustomTitleData playertitle;
            playertitle.titletext = "Nem!";
            playertitle.titlecolor = "FFD700"; // Gold color
            playertitle.titleglow = "FFFFFF"; // White glow

            if (playertitle.titletext != "") {
                    FName t = L"PlayerTitlesPlayerTitles";
                    FASValue count = dataStore->GetValue(L"PlayerTitles", NULL, L"SelectedTitle");
                    dataStore->SetStringValue(t, count.I, L"Text", Format::ToFString(playertitle.titletext));
                    FASValue fasColor = dataStore->GetValue(t, count.I, L"Color");
                    fasColor.Type = 7;
                    fasColor.I = dataStore->FromHex(Format::ToFString(playertitle.titlecolor));
                    FASValue fasGlowColor = dataStore->GetValue(t, count.I, L"GlowColor");
                    fasGlowColor.Type = 7;
                    fasGlowColor.I = dataStore->FromHex(Format::ToFString(playertitle.titleglow));
                    dataStore->SetASValue(t, count.I, L"Color", fasColor);
                    dataStore->SetASValue(t, count.I, L"GlowColor", fasGlowColor);
                }
            }*/
        

    });

    HookEventPre("Function TAGame.GFxHUD_TA.Tick", [&](PreEvent& event) {

        AGFxHUD_TA* hud = event.GetCaller<AGFxHUD_TA>();

        if (setcustomtitle && RGBTitle) {
            Manager.OnTick();

            std::string RTC = Colors::ColorToHex(GRainbowColor::GetByte());
            std::string RTGC = Colors::ColorToHex(GRainbowColor::GetByte());

            if (!RTC.empty() && RTC.front() == '#') {
                RTC.erase(0, 1);
            }
            if (!RTGC.empty() && RTGC.front() == '#') {
                RTGC.erase(0, 1);
            }

            if (!RTC.empty() && !RTGC.empty()) {
                rainbowtitlecolor = RTC;
                rainbowtitleglowcolor = RTGC;
            }
            else {
                Console.Error("no bueno");
            }
        }

        for (UGFxData_PRI_TA* pri : hud->PRIData) {
                Events.hud = hud;

                    if (setcustomtitle == true) {

                        wchar_t* p = new wchar_t[titletext.size() + 1];
                        for (std::string::size_type i = 0; i < titletext.size(); ++i)
                            p[i] = titletext[i];

                        p[titletext.size()] = '\0';
                        FString fstitletext = FString(p);

                        FName t = L"PlayerInfo";
                        dataStore->SetStringValue(t, pri->RowIndex, L"XPTitle", fstitletext);

                        FASValue fasColor = dataStore->GetValue(t, pri->RowIndex, L"TitleColor");
                        fasColor.Type = 7;
                        if (RGBTitle) {
                            fasColor.I = pri->FromHex(Format::ToFString(rainbowtitlecolor));
                        }
                        else {
                            fasColor.I = pri->FromHex(Format::ToFString(titlecolor));
                        }

                        FASValue fasGlowColor = dataStore->GetValue(t, pri->RowIndex, L"TitleGlowColor");
                        fasGlowColor.Type = 7;

                        if (RGBTitle) {
                            fasGlowColor.I = pri->FromHex(Format::ToFString(rainbowtitleglowcolor));
                        }
                        else {
                            fasGlowColor.I = pri->FromHex(Format::ToFString(titleglowcolor));
                        }

                        dataStore->SetASValue(t, pri->RowIndex, L"TitleColor", fasColor);
                        dataStore->SetASValue(t, pri->RowIndex, L"TitleGlowColor", fasGlowColor);

                        delete[] p;

                        if (setcustomtitle == false) {

                            wchar_t* p = new wchar_t[titletext.size() + 1];
                            for (std::string::size_type i = 0; i < titletext.size(); ++i)
                                p[i] = titletext[i];

                            p[titletext.size()] = '\0';
                            FString fstitletext = FString(p);

                            FName t = FName(L"PlayerTitlesPlayerTitles");
                            FASValue count = dataStore->GetValue(L"PlayerTitles", NULL, L"SelectedTitle");
                            dataStore->SetStringValue(t, count.I, L"Text", fstitletext);
                            FASValue fasColor = dataStore->GetValue(t, count.I, L"Color");
                            fasColor.Type = 7;
                            if (RGBTitle) {
                                fasColor.I = pri->FromHex(Format::ToFString(rainbowtitlecolor));
                            }
                            else {
                                fasColor.I = pri->FromHex(Format::ToFString(titlecolor));
                            }


                            FASValue fasGlowColor = dataStore->GetValue(t, pri->RowIndex, L"TitleGlowColor");
                            fasGlowColor.Type = 7;

                            if (RGBTitle) {
                                fasGlowColor.I = pri->FromHex(Format::ToFString(rainbowtitleglowcolor));
                            }
                            else {
                                fasGlowColor.I = pri->FromHex(Format::ToFString(titleglowcolor));
                            }
                            dataStore->SetASValue(t, count.I, L"Color", fasColor);
                            dataStore->SetASValue(t, count.I, L"GlowColor", fasGlowColor);

                            delete[] p;
                        }
                    }
                else {

                    wchar_t* p = new wchar_t[titletext.size() + 1];
                    for (std::string::size_type i = 0; i < titletext.size(); ++i)
                        p[i] = titletext[i];

                    p[titletext.size()] = '\0';
                    FString fstitletext = FString(p);

                    FName t = L"PlayerInfo";
                    dataStore->SetStringValue(t, pri->RowIndex, L"XPTitle", fstitletext);
                    FASValue fasColor = dataStore->GetValue(t, pri->RowIndex, L"TitleColor");
                    fasColor.Type = 7;
                    if (RGBTitle) {
                        fasColor.I = pri->FromHex(Format::ToFString(rainbowtitlecolor));
                    }
                    else {
                        fasColor.I = pri->FromHex(Format::ToFString(titlecolor));
                    }


                    FASValue fasGlowColor = dataStore->GetValue(t, pri->RowIndex, L"TitleGlowColor");
                    fasGlowColor.Type = 7;

                    if (RGBTitle) {
                        fasGlowColor.I = pri->FromHex(Format::ToFString(rainbowtitleglowcolor));
                    }
                    else {
                        fasGlowColor.I = pri->FromHex(Format::ToFString(titleglowcolor));
                    }
                    dataStore->SetASValue(t, pri->RowIndex, L"TitleColor", fasColor);
                    dataStore->SetASValue(t, pri->RowIndex, L"TitleGlowColor", fasGlowColor);

                    delete[] p;
                }
            }
        

        if (setcustomtitle)
        {
            if (hud && dataStore)
            {
                FUniqueNetId NetID = Instances.GetUniqueID();

                UGFxData_PRI_TA* localplayerpri = hud->GetPRIDataFromID(NetID);

                if (localplayerpri)
                {
                    wchar_t* p = new wchar_t[titletext.size() + 1];
                    for (std::string::size_type i = 0; i < titletext.size(); ++i)
                        p[i] = titletext[i];

                    p[titletext.size()] = '\0';
                    FString fstitletext = FString(p);

                    FName t = L"PlayerInfo";
                    dataStore->SetStringValue(t, localplayerpri->RowIndex, L"XPTitle", fstitletext);
                    FASValue fasColor = dataStore->GetValue(t, localplayerpri->RowIndex, L"TitleColor");
                    fasColor.Type = 7;
                    if (RGBTitle) {
                        fasColor.I = dataStore->FromHex(Format::ToFString(rainbowtitlecolor));
                    }
                    else {
                        fasColor.I = dataStore->FromHex(Format::ToFString(titlecolor));
                    }


                    FASValue fasGlowColor = dataStore->GetValue(t, localplayerpri->RowIndex, L"GlowColor");
                    fasGlowColor.Type = 7;

                    if (RGBTitle) {
                        fasGlowColor.I = dataStore->FromHex(Format::ToFString(rainbowtitleglowcolor));
                    }
                    else {
                        fasGlowColor.I = dataStore->FromHex(Format::ToFString(titleglowcolor));
                    }
                    dataStore->SetASValue(t, localplayerpri->RowIndex, L"TitleColor", fasColor);
                    dataStore->SetASValue(t, localplayerpri->RowIndex, L"TitleGlowColor", fasGlowColor);

                    delete[] p;
                }
            }
            wchar_t* p = new wchar_t[titletext.size() + 1];
            for (std::string::size_type i = 0; i < titletext.size(); ++i)
                p[i] = titletext[i];

            p[titletext.size()] = '\0';
            FString fstitletext = FString(p);

            FName t = FName(L"PlayerTitlesPlayerTitles");
            FASValue count = dataStore->GetValue(L"PlayerTitles", NULL, L"SelectedTitle");
            dataStore->SetStringValue(t, count.I, L"Text", fstitletext);
            FASValue fasColor = dataStore->GetValue(t, count.I, L"Color");
            fasColor.Type = 7;
            if (RGBTitle) {
                fasColor.I = dataStore->FromHex(Format::ToFString(rainbowtitlecolor));
            }
            else {
                fasColor.I = dataStore->FromHex(Format::ToFString(titlecolor));
            }


            FASValue fasGlowColor = dataStore->GetValue(t, count.I, L"GlowColor");
            fasGlowColor.Type = 7;

            if (RGBTitle) {
                fasGlowColor.I = dataStore->FromHex(Format::ToFString(rainbowtitleglowcolor));
            }
            else {
                fasGlowColor.I = dataStore->FromHex(Format::ToFString(titleglowcolor));
            }
            dataStore->SetASValue(t, count.I, L"Color", fasColor);
            dataStore->SetASValue(t, count.I, L"GlowColor", fasGlowColor);

            delete[] p;
        }



    });


	HookEventPre("Function Engine.PlayerController.PlayerTick", &Hooks::PlayerControllerTick);
	HookEventPost("Function TAGame.GameViewportClient_TA.HandleKeyPress", &Hooks::GameViewPortKeyPress);
	HookEventPre("Function TAGame.GFxData_MainMenu_TA.MainMenuAdded", &Hooks::GFxDataMainMenuAdded);

	Console.Write(GetNameFormatted() + std::to_string(PreHookedEvents.size()) + " Pre-Hook(s) Initialized!");
	Console.Write(GetNameFormatted() + std::to_string(PostHookedEvents.size()) + " Post-Hook(s) Initialized!");
	Console.Write(GetNameFormatted() + std::to_string(BlacklistedEvents.size()) + " Backlisted Event(s) Initialized!");
}

class EventsComponent Events{};