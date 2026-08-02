#include "Core.hpp"
#include "../Includes.hpp"

CoreComponent::CoreComponent() : Component("Core", "Initializes globals, components, and modules.") { OnCreate(); }

CoreComponent::~CoreComponent() { OnDestroy(); }

void CoreComponent::OnCreate()
{
	MainThread = nullptr;
}

void CoreComponent::OnDestroy() {
	DestroyThread();
}

void CoreComponent::InitializeThread()
{
	MainThread = CreateThread(nullptr, 0, reinterpret_cast<LPTHREAD_START_ROUTINE>(InitializeGlobals), nullptr, 0, nullptr);
}

void CoreComponent::DestroyThread()
{
	CloseHandle(MainThread);
}

uintptr_t CoreComponent::GetGObjects() {
    HANDLE CurrentBaseHandle = GetCurrentProcess();

    BYTE Bytes[4];
    uintptr_t Pattern;

    // Find the pattern "\x48\x8B\x05\x33\xF2\x03\x02"
    Pattern = Memory::FindPattern(GetModuleHandleW(NULL), (const unsigned char*)"\x48\x8B\x05\x33\xF2\x03\x02", "xxxxxxx");
    if (!Pattern) {
        return 0; // Pattern not found
    }

    // Move to the offset part (Pattern + 3)
    Pattern += 3;
    if (!ReadProcessMemory(CurrentBaseHandle, (LPCVOID)Pattern, Bytes, sizeof(Bytes), NULL)) {
        return 0; // Failed to read memory
    }

    // Calculate the RIP-relative address
    uintptr_t RelativeOffset = Memory::BytesToInt32(Bytes, 0);
    uintptr_t FinalAddress = (Pattern + 4) + RelativeOffset; // Next instruction is 4 bytes ahead

    // Read the value at the final address
    if (!ReadProcessMemory(CurrentBaseHandle, (LPCVOID)FinalAddress, Bytes, sizeof(Bytes), NULL)) {
        return 0; // Failed to read memory
    }

    // Convert the bytes to the final GObjects address
    return Memory::BytesToInt32(Bytes, 0);
}

uintptr_t CoreComponent::GetGNames() {
    HANDLE CurrentBaseHandle = GetCurrentProcess();

    BYTE Bytes[4];
    uintptr_t Pattern;

    // Find the pattern "\x48\x8B\x05\x6E\xDC\xFC\x01"
    Pattern = Memory::FindPattern(GetModuleHandleW(NULL), (const unsigned char*)"\x48\x8B\x05\x6E\xDC\xFC\x01", "xxxxxxx");
    if (!Pattern) {
        return 0; // Pattern not found
    }

    // Move to the offset part (Pattern + 3)
    Pattern += 3;
    if (!ReadProcessMemory(CurrentBaseHandle, (LPCVOID)Pattern, Bytes, sizeof(Bytes), NULL)) {
        return 0; // Failed to read memory
    }

    // Convert the relative offset to an integer
    int32_t RelativeOffset = Memory::BytesToInt32(Bytes, 0);

    // Calculate the final address using RIP-relative addressing
    uintptr_t FinalAddress = (Pattern + 4) + RelativeOffset;

    if (!FinalAddress) {
        return 0; // Failed to calculate the final address
    }

    // Return the offset relative to the base address
    return FinalAddress - reinterpret_cast<uintptr_t>(GetModuleHandleW(NULL));
}

void CoreComponent::InitializeGlobals(HMODULE hModule)
{


	Console.Initialize(std::filesystem::current_path(), "NemHook.log");

	//GObjects = reinterpret_cast<TArray<UObject*>*>(GetGObjects());  //for pattern scanning
	//GNames = reinterpret_cast<TArray<FNameEntry*>*>(GetGNames() - 0x48);  //for pattern scanning
	uintptr_t BaseAddress = reinterpret_cast<uintptr_t>(GetModuleHandle(NULL));
    //Known Offsets
    //GObjects = reinterpret_cast<TArray<UObject*>*>(BaseAddress + 0x247ED38);
    //GNames = reinterpret_cast<TArray<FNameEntry*>*>(BaseAddress + 0x247ECF0);

    /*
        Epic:
        GObjectsOffset: 0x2328540
        GNamesOffset: 0x23284f8

        Steam:
        GObjectsOffset: 0x23a3b00
        GNamesOffset: 0x23a3ab8
    */

	//if (AreGlobalsValid())
    if (true)
	{

		Console.Write("[Core Module] Initialized!");
        
        if (Instances.IsInSteamDirectory()) {
            GObjects = reinterpret_cast<TArray<UObject*>*>(BaseAddress + 0x23a7ca0);
            GNames = reinterpret_cast<TArray<FNameEntry*>*>(BaseAddress + 0x23a7c58);// - 0x48

            Events.platform = 1;
        }
        else {
            GObjects = reinterpret_cast<TArray<UObject*>*>(BaseAddress + 0x232c720);
            GNames = reinterpret_cast<TArray<FNameEntry*>*>(BaseAddress + 0x232c6d8); // - 0x48

            Events.platform = 2;
        }

        //Console.Notify("[Core Module] Entry Point " + Format::ToHex(reinterpret_cast<void*>(GetModuleHandle(NULL))));
        //Console.Notify("[Core Module] Global Objects: " + Format::ToHex(GObjects));
        //Console.Notify("[Core Module] Global Names: " + Format::ToHex(GNames));
        
		void** UnrealVTable = reinterpret_cast<void**>(UObject::StaticClass()->VfTableObject.Dummy);
		EventsComponent::AttachDetour(reinterpret_cast<ProcessEventType>(UnrealVTable[67])); //always 67

		Instances.Initialize();
		Events.Initialize();
		GUI.Initialize();
		Main.Initialize();

        if (Instances.IsInSteamDirectory()) {
            Console.Success("[Core Module] Platform: Steam");
        }
        else {
            Console.Success("[Core Module] Platform: Epic Games");
        }

	}
	else
	{
		Console.Error("[Core Module] GObject and GNames are not valid, wrong address detected!");
	}

}

bool CoreComponent::AreGlobalsValid()
{
	return (AreGObjectsValid() && AreGNamesValid());
}

bool CoreComponent::AreGObjectsValid()
{
	if (GObjects
		&& UObject::GObjObjects()->Num() > 0
		&& UObject::GObjObjects()->Max() > UObject::GObjObjects()->Num())
	{
		return true;
	}

	return false;
}

bool CoreComponent::AreGNamesValid()
{
	if (GNames
		&& FName::Names()->Num() > 0
		&& FName::Names()->Max() > FName::Names()->Num())
	{
		return true;
	}

	return false;
}

class CoreComponent Core {};