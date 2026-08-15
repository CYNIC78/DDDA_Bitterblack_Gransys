#include "stdafx.h"
#include "d3d9.h"
#include "Hotkeys.h"
#include "ImGui/imgui_impl_dx9.h"
#include "ImGui/imgui_internal.h"

std::vector<void(*)()> content;
std::vector<void(*)(bool)> windows;
struct FontEntry { LPCSTR filename; float size; ImFont** font; };
std::vector<FontEntry> fonts;
void onLostDevice() { ImGui_ImplDX9_InvalidateDeviceObjects(); }
void onResetDevice() { ImGui_ImplDX9_CreateDeviceObjects(); }
void onCreateDevice(LPDIRECT3DDEVICE9 pD3DDevice)
{
	ImGui_ImplDX9_Init(pD3DDevice);
	ImGui::GetIO().IniFilename = nullptr;
	ImGui::GetStyle().WindowTitleAlign = ImGuiAlign_Center;
	ImGui::GetStyle().WindowFillAlphaDefault = 0.95f;
	ImGui::GetIO().Fonts->AddFontDefault();
	for (size_t i = 0; i < fonts.size(); i++)
	{
		CHAR syspath[MAX_PATH];
		GetWindowsDirectory(syspath, MAX_PATH);
		strcat_s(syspath, "\\Fonts\\");
		strcat_s(syspath, fonts[i].filename);

		ImFont **font = fonts[i].font;
		*font = ImGui::GetIO().Fonts->AddFontFromFileTTF(syspath, fonts[i].size);
		if (!*font)
			logFile << "InGameClock: failed to load font - " << syspath << std::endl;
	}
}

bool inGameUIEnabled = false;
void onEndScene()
{
	ImGui_ImplDX9_NewFrame();
	for (size_t i = 0; i < windows.size(); i++)
		windows[i](inGameUIEnabled);
	ImGui::Render();
}

void renderDDDAFixUI(bool getsInput)
{
	if (!getsInput)
		return;

	static char titleBuffer[64];
	sprintf_s(titleBuffer, "DDDAFix - %.1f FPS###DDDAFix", ImGui::GetIO().Framerate);
	ImGui::SetNextWindowPos(ImVec2(5, 5), ImGuiSetCond_Once);
	ImGui::Begin(titleBuffer, nullptr, ImVec2(450, 600));
	for (size_t i = 0; i < content.size(); i++)
	{
		ImGui::PushID(i);
		content[i]();
		ImGui::PopID();
	}
	ImGui::End();
}

LPBYTE pInGameUI, oInGameUI;
// Перехват GetAsyncKeyState: не глушим весь ввод, а только клавиши,
// которые ImGui реально захватил (текстовый ввод, навигация по меню).
// WASD/стрелки/мышь продолжают работать, даже если ползунок активен.
SHORT WINAPI HGetAsyncKeyState(int vKey) {
    // Если ни один ImGui-виджет не активен — пропускаем всё
    if (!ImGui::GetIO().WantCaptureKeyboard) return GetAsyncKeyState(vKey);
    // Если активен текстовый ввод — глушим только клавиши, которые
    // конфликтуют с набором текста: Enter, Esc, Tab, Backspace
    if (ImGui::GetIO().WantTextInput) {
        if (vKey == VK_RETURN || vKey == VK_ESCAPE || vKey == VK_TAB || vKey == VK_BACK) return 0;
        return GetAsyncKeyState(vKey);
    }
    // Виджет активен (слайдер, комбобокс) — глушим только клавиши
    // навигации (стрелки, Enter, Esc), остальное пропускаем
    if (vKey == VK_UP || vKey == VK_DOWN || vKey == VK_LEFT || vKey == VK_RIGHT ||
        vKey == VK_RETURN || vKey == VK_ESCAPE || vKey == VK_TAB) return 0;
    return GetAsyncKeyState(vKey);
}
void __declspec(naked) HInGameUI()
{
	__asm	mov		ebp, HGetAsyncKeyState;
	__asm	jmp		oInGameUI;
}

WPARAM inGameUIHotkey;
LRESULT CALLBACK inGameUIEvent(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
	if (msg == WM_KEYDOWN && (HIWORD(lParam) & KF_REPEAT) == 0 && wParam == inGameUIHotkey)
		Hooks::SwitchHook("InGameUI", pInGameUI, inGameUIEnabled = !inGameUIEnabled);
	return inGameUIEnabled ? ImGui_ImplDX9_WndProcHandler(hwnd, msg, wParam, lParam) : 0;
}

LRESULT CALLBACK inGameUIInit(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
	if (Hooks::D3D9(onCreateDevice, onLostDevice, onResetDevice, onEndScene))
		Hooks::HotkeysHandler(inGameUIEvent);
	return 0;
}

bool Hooks::InGameUI()
{
	if (!config.getBool("inGameUI", "enabled", false))
	{
		logFile << "InGameUI: disabled" << std::endl;
		return false;
	}

	BYTE sigRun[] = { 0x8B, 0x2D, 0xCC, 0xCC, 0xCC, 0xCC,	//mov	ebp, ds:GetAsyncKeyState
					0x8D, 0x7E, 0x01 };						//lea	edi, [esi+1]
	if (FindSignature("InGameUI", sigRun, &pInGameUI))
	{
		CreateHook("InGameUI", pInGameUI, &HInGameUI, &oInGameUI, inGameUIEnabled);
		oInGameUI += 6;
	}

	inGameUIHotkey = config.getUInt("hotkeys", "keyUI", VK_F12) & 0xFF;
	InGameUIAddWindow(renderDDDAFixUI);
	HotkeysHandler(inGameUIInit);
	return true;
}

void Hooks::InGameUIAdd(void(*callback)()) { content.push_back(callback); }
void Hooks::InGameUIAddWindow(void(*callback)(bool getsInput)) { windows.push_back(callback); }
void Hooks::InGameUIAddFont(const char *filename, float size_pixels, ImFont **font) { fonts.push_back({filename, size_pixels, font}); }

namespace ImGui
{
	bool InputFloatN(const char* label, float* v, int count, float item_width, float min, float max, int precision)
	{
		if (item_width > 0.0f)
			PushItemWidth(item_width * count);
		bool changed = InputFloatN(label, v, count, precision, 0);
		if (item_width > 0.0f)
			PopItemWidth();
		if (changed)
			for (int i = 0; i < count; i++)
			{
				if (v[i] < min)
					v[i] = min;
				if (v[i] > max)
					v[i] = max;
			}
		return changed;
	}

	bool InputFloatEx(const char* label, float* v, float step, float min, float max, int precision)
	{
		if (!InputFloat(label, v, step, step * 100, precision, 0))
			return false;
		if (*v < min)
			*v = min;
		if (*v > max)
			*v = max;
		return true;
	}

	void TextUnformatted(const char* label, float pos)
	{
		SameLine(pos);
		TextUnformatted(label);
	}
}