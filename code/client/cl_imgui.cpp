/*
===========================================================================
Copyright (C) 2022-2023 Gian 'myT' Schellenbaum

This file is part of Challenge Quake 3 (CNQ3).

Challenge Quake 3 is free software; you can redistribute it
and/or modify it under the terms of the GNU General Public License as
published by the Free Software Foundation; either version 2 of the License,
or (at your option) any later version.

Challenge Quake 3 is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with Challenge Quake 3. If not, see <https://www.gnu.org/licenses/>.
===========================================================================
*/
// Dear ImGui client integration and utility functions


#include "client.h"
#include "cl_imgui.h"
#include "../imgui/ProggyClean.h"


bool BeginTable(const char* name, int count)
{
	ImGui::Text(name);

	const int flags =
		ImGuiTableFlags_RowBg |
		ImGuiTableFlags_Borders |
		ImGuiTableFlags_Resizable |
		ImGuiTableFlags_SizingStretchProp;
	return ImGui::BeginTable(name, count, flags);
}

void TableHeader(int count, ...)
{
	va_list args;
	va_start(args, count);
	for(int i = 0; i < count; ++i)
	{
		const char* header = va_arg(args, const char*);
		ImGui::TableSetupColumn(header);
	}
	va_end(args);

	ImGui::TableHeadersRow();
}

void TableRow(int count, ...)
{
	ImGui::TableNextRow();

	va_list args;
	va_start(args, count);
	for(int i = 0; i < count; ++i)
	{
		const char* item = va_arg(args, const char*);
		ImGui::TableSetColumnIndex(i);
		ImGui::Text(item);
	}
	va_end(args);
}

void TableRow2(const char* item0, bool item1)
{
	TableRow(2, item0, item1 ? "YES" : "NO");
}

void TableRow2(const char* item0, int item1)
{
	TableRow(2, item0, va("%d", item1));
}

void TableRow2(const char* item0, float item1, const char* format)
{
	TableRow(2, item0, va(format, item1));
}

bool IsShortcutPressed(ImGuiKey key)
{
	return ImGui::IsKeyDown(ImGuiMod_Ctrl) && ImGui::IsKeyPressed(key, false);
}

void ToggleBooleanWithShortcut(bool& value, ImGuiKey key)
{
	if(IsShortcutPressed(key))
	{
		value = !value;
	}
}

struct MainMenuItem
{
	GUI_MainMenu::Id menu;
	const char* name;
	const char* shortcut;
	bool* selected;
	bool enabled;
};

struct MainMenu
{
	MainMenuItem items[64];
	int itemCount;
	int itemCountPerMenu[GUI_MainMenu::Count]; // effectively a histogram
};

static MainMenu mm;

#define M(Enum, Desc) Desc,
static const char* mainMenuNames[GUI_MainMenu::Count + 1] =
{
	MAIN_MENU_LIST(M)
	""
};
#undef M

void GUI_AddMainMenuItem(GUI_MainMenu::Id menu, const char* name, const char* shortcut, bool* selected, bool enabled)
{
	if(mm.itemCount >= ARRAY_LEN(mm.items) ||
		(unsigned int)menu >= GUI_MainMenu::Count)
	{
		Q_assert(!"GUI_AddMainMenuItem: can't add menu entry");
		return;
	}

	MainMenuItem& item = mm.items[mm.itemCount++];
	item.menu = menu;
	item.name = name;
	item.shortcut = shortcut;
	item.selected = selected;
	item.enabled = enabled;

	mm.itemCountPerMenu[menu]++;
}

void GUI_DrawMainMenu()
{
	if(ImGui::BeginMainMenuBar())
	{
		for(int m = 0; m < GUI_MainMenu::Count; ++m)
		{
			if(mm.itemCountPerMenu[m] <= 0)
			{
				continue;
			}

			if(ImGui::BeginMenu(mainMenuNames[m]))
			{
				for(int i = 0; i < mm.itemCount; ++i)
				{
					const MainMenuItem& item = mm.items[i];
					if(item.menu == m)
					{
						ImGui::MenuItem(item.name, item.shortcut, item.selected, item.enabled);
					}
				}

				ImGui::EndMenu();
			}
		}

		ImGui::EndMainMenuBar();
	}

	mm.itemCount = 0;
	memset(mm.itemCountPerMenu, 0, sizeof(mm.itemCountPerMenu));
}

// applies a modified version of Jan Bielak's Deep Dark theme
static void ImGUI_ApplyTheme()
{
	ImVec4* colors = ImGui::GetStyle().Colors;
	colors[ImGuiCol_Text] = ImVec4(1.00f, 1.00f, 1.00f, 1.00f);
	colors[ImGuiCol_TextDisabled] = ImVec4(0.50f, 0.50f, 0.50f, 1.00f);
	colors[ImGuiCol_WindowBg] = ImVec4(0.10f, 0.10f, 0.10f, 1.00f);
	colors[ImGuiCol_ChildBg] = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
	colors[ImGuiCol_PopupBg] = ImVec4(0.19f, 0.19f, 0.19f, 0.92f);
	colors[ImGuiCol_Border] = ImVec4(0.19f, 0.19f, 0.19f, 0.29f);
	colors[ImGuiCol_BorderShadow] = ImVec4(0.00f, 0.00f, 0.00f, 0.24f);
	colors[ImGuiCol_FrameBg] = ImVec4(0.05f, 0.05f, 0.05f, 0.54f);
	colors[ImGuiCol_FrameBgHovered] = ImVec4(0.19f, 0.19f, 0.19f, 0.54f);
	colors[ImGuiCol_FrameBgActive] = ImVec4(0.20f, 0.22f, 0.23f, 1.00f);
	colors[ImGuiCol_TitleBg] = ImVec4(0.00f, 0.00f, 0.00f, 1.00f);
	colors[ImGuiCol_TitleBgActive] = ImVec4(0.06f, 0.06f, 0.06f, 1.00f);
	colors[ImGuiCol_TitleBgCollapsed] = ImVec4(0.00f, 0.00f, 0.00f, 1.00f);
	colors[ImGuiCol_MenuBarBg] = ImVec4(0.14f, 0.14f, 0.14f, 1.00f);
	colors[ImGuiCol_ScrollbarBg] = ImVec4(0.05f, 0.05f, 0.05f, 0.54f);
	colors[ImGuiCol_ScrollbarGrab] = ImVec4(0.34f, 0.34f, 0.34f, 0.54f);
	colors[ImGuiCol_ScrollbarGrabHovered] = ImVec4(0.40f, 0.40f, 0.40f, 0.54f);
	colors[ImGuiCol_ScrollbarGrabActive] = ImVec4(0.56f, 0.56f, 0.56f, 0.54f);
	colors[ImGuiCol_CheckMark] = ImVec4(0.33f, 0.67f, 0.86f, 1.00f);
	colors[ImGuiCol_SliderGrab] = ImVec4(0.34f, 0.34f, 0.34f, 0.54f);
	colors[ImGuiCol_SliderGrabActive] = ImVec4(0.56f, 0.56f, 0.56f, 0.54f);
	colors[ImGuiCol_Button] = ImVec4(0.05f, 0.05f, 0.05f, 0.54f);
	colors[ImGuiCol_ButtonHovered] = ImVec4(0.19f, 0.19f, 0.19f, 0.54f);
	colors[ImGuiCol_ButtonActive] = ImVec4(0.20f, 0.22f, 0.23f, 1.00f);
	colors[ImGuiCol_Header] = ImVec4(0.00f, 0.00f, 0.00f, 0.52f);
	colors[ImGuiCol_HeaderHovered] = ImVec4(0.00f, 0.00f, 0.00f, 0.36f);
	colors[ImGuiCol_HeaderActive] = ImVec4(0.20f, 0.22f, 0.23f, 0.33f);
	colors[ImGuiCol_Separator] = ImVec4(0.28f, 0.28f, 0.28f, 0.29f);
	colors[ImGuiCol_SeparatorHovered] = ImVec4(0.44f, 0.44f, 0.44f, 0.29f);
	colors[ImGuiCol_SeparatorActive] = ImVec4(0.40f, 0.44f, 0.47f, 1.00f);
	colors[ImGuiCol_ResizeGrip] = ImVec4(0.28f, 0.28f, 0.28f, 0.29f);
	colors[ImGuiCol_ResizeGripHovered] = ImVec4(0.44f, 0.44f, 0.44f, 0.29f);
	colors[ImGuiCol_ResizeGripActive] = ImVec4(0.40f, 0.44f, 0.47f, 1.00f);
	colors[ImGuiCol_Tab] = ImVec4(0.00f, 0.00f, 0.00f, 0.52f);
	colors[ImGuiCol_TabHovered] = ImVec4(0.14f, 0.14f, 0.14f, 1.00f);
	colors[ImGuiCol_TabActive] = ImVec4(0.20f, 0.20f, 0.20f, 0.36f);
	colors[ImGuiCol_TabUnfocused] = ImVec4(0.00f, 0.00f, 0.00f, 0.52f);
	colors[ImGuiCol_TabUnfocusedActive] = ImVec4(0.14f, 0.14f, 0.14f, 1.00f);
	colors[ImGuiCol_PlotLines] = ImVec4(1.00f, 0.00f, 0.00f, 1.00f);
	colors[ImGuiCol_PlotLinesHovered] = ImVec4(1.00f, 0.00f, 0.00f, 1.00f);
	colors[ImGuiCol_PlotHistogram] = ImVec4(1.00f, 0.00f, 0.00f, 1.00f);
	colors[ImGuiCol_PlotHistogramHovered] = ImVec4(1.00f, 0.00f, 0.00f, 1.00f);
	colors[ImGuiCol_TableHeaderBg] = ImVec4(0.00f, 0.00f, 0.00f, 0.52f);
	colors[ImGuiCol_TableBorderStrong] = ImVec4(0.00f, 0.00f, 0.00f, 0.52f);
	colors[ImGuiCol_TableBorderLight] = ImVec4(0.28f, 0.28f, 0.28f, 0.29f);
	colors[ImGuiCol_TableRowBg] = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
	colors[ImGuiCol_TableRowBgAlt] = ImVec4(1.00f, 1.00f, 1.00f, 0.06f);
	colors[ImGuiCol_TextSelectedBg] = ImVec4(0.20f, 0.22f, 0.23f, 1.00f);
	colors[ImGuiCol_DragDropTarget] = ImVec4(0.33f, 0.67f, 0.86f, 1.00f);
	colors[ImGuiCol_NavHighlight] = ImVec4(1.00f, 0.00f, 0.00f, 1.00f);
	colors[ImGuiCol_NavWindowingHighlight] = ImVec4(1.00f, 0.00f, 0.00f, 0.70f);
	colors[ImGuiCol_NavWindowingDimBg] = ImVec4(1.00f, 0.00f, 0.00f, 0.20f);
	colors[ImGuiCol_ModalWindowDimBg] = ImVec4(1.00f, 0.00f, 0.00f, 0.35f);

	const ImVec4 hover(0.26f, 0.59f, 0.98f, 0.4f);
	const ImVec4 active(0.2f, 0.41f, 0.68f, 0.5f);
	colors[ImGuiCol_HeaderHovered] = hover;
	colors[ImGuiCol_HeaderActive] = active;
	colors[ImGuiCol_TabHovered] = hover;
	colors[ImGuiCol_TabActive] = active;

	ImGuiStyle& style = ImGui::GetStyle();
	style.WindowPadding = ImVec2(8.00f, 8.00f);
	style.FramePadding = ImVec2(5.00f, 2.00f);
	style.CellPadding = ImVec2(6.00f, 6.00f);
	style.ItemSpacing = ImVec2(6.00f, 6.00f);
	style.ItemInnerSpacing = ImVec2(6.00f, 6.00f);
	style.TouchExtraPadding = ImVec2(0.00f, 0.00f);
	style.IndentSpacing = 25;
	style.ScrollbarSize = 15;
	style.GrabMinSize = 10;
	style.WindowBorderSize = 1;
	style.ChildBorderSize = 1;
	style.PopupBorderSize = 1;
	style.FrameBorderSize = 1;
	style.TabBorderSize = 1;
	style.WindowRounding = 7;
	style.ChildRounding = 4;
	style.FrameRounding = 3;
	style.PopupRounding = 4;
	style.ScrollbarRounding = 9;
	style.GrabRounding = 3;
	style.LogSliderDeadzone = 4;
	style.TabRounding = 4;
}

void CL_IMGUI_Init()
{
	ImGui::CreateContext();
	ImPlot::CreateContext();

	ImGuiIO& io = ImGui::GetIO();
	io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
	io.ConfigFlags |= ImGuiConfigFlags_NoMouseCursorChange;
	io.IniFilename = "cnq3/imgui.ini";
	//io.MouseDrawCursor = true; // just use the operating system's

	ImFontConfig fontConfig;
	fontConfig.FontDataOwnedByAtlas = false;
	io.Fonts->AddFontFromMemoryCompressedTTF(
		ProggyClean_compressed_data, ProggyClean_compressed_size, 13.0f, &fontConfig);

	io.KeyMap[ImGuiKey_Tab] = K_TAB;
	io.KeyMap[ImGuiKey_LeftArrow] = K_LEFTARROW;
	io.KeyMap[ImGuiKey_RightArrow] = K_RIGHTARROW;
	io.KeyMap[ImGuiKey_UpArrow] = K_UPARROW;
	io.KeyMap[ImGuiKey_DownArrow] = K_DOWNARROW;
	io.KeyMap[ImGuiKey_PageUp] = K_PGUP;
	io.KeyMap[ImGuiKey_PageDown] = K_PGDN;
	io.KeyMap[ImGuiKey_Home] = K_HOME;
	io.KeyMap[ImGuiKey_End] = K_END;
	io.KeyMap[ImGuiKey_Insert] = K_INS;
	io.KeyMap[ImGuiKey_Delete] = K_DEL;
	io.KeyMap[ImGuiKey_Backspace] = K_BACKSPACE;
	io.KeyMap[ImGuiKey_Space] = K_SPACE;
	io.KeyMap[ImGuiKey_Enter] = K_ENTER;
	io.KeyMap[ImGuiKey_Escape] = K_ESCAPE;
	io.KeyMap[ImGuiKey_KeyPadEnter] = K_KP_ENTER;
	for(int i = 0; i < 26; ++i)
	{
		io.KeyMap[ImGuiKey_A + i] = 'a' + i;
	}
	for(int i = 0; i < 10; ++i)
	{
		io.KeyMap[ImGuiKey_0 + i] = '0' + i;
	}

	ImGUI_ApplyTheme();
}

void CL_IMGUI_Frame()
{
	if(Cvar_VariableIntegerValue("r_debugInput"))
	{
		cls.keyCatchers |= KEYCATCH_IMGUI;
	}
	else
	{
		cls.keyCatchers &= ~KEYCATCH_IMGUI;
	}

	static int64_t prevUS = 0;
	const int64_t currUS = Sys_Microseconds();
	const int64_t elapsedUS = currUS - prevUS;
	prevUS = currUS;

	int x, y;
	Sys_GetCursorPosition(&x, &y);

	ImGuiIO& io = ImGui::GetIO();
	io.DeltaTime = (float)((double)elapsedUS / 1000000.0);
	io.MousePos[0] = x;
	io.MousePos[1] = y;
	io.KeyCtrl = io.KeysDown[K_CTRL];
	io.KeyShift = io.KeysDown[K_SHIFT];
	io.KeyAlt = io.KeysDown[K_ALT];
}

void CL_IMGUI_MouseEvent(int dx, int dy)
{
	ImGuiIO& io = ImGui::GetIO();
	io.MouseDelta[0] += dx;
	io.MouseDelta[1] += dy;
}

void CL_IMGUI_KeyEvent(int key, qbool down)
{
	ImGuiIO& io = ImGui::GetIO();
	switch(key)
	{
		case K_MOUSE1: io.MouseDown[0] = !!down; break;
		case K_MOUSE2: io.MouseDown[1] = !!down; break;
		case K_MOUSE3: io.MouseDown[2] = !!down; break;
		case K_MOUSE4: io.MouseDown[3] = !!down; break;
		case K_MOUSE5: io.MouseDown[4] = !!down; break;
		case K_MWHEELDOWN: io.MouseWheel -= 1.0f; break;
		case K_MWHEELUP: io.MouseWheel += 1.0f; break;
		default: io.KeysDown[key] = !!down; break;
	}
}

void CL_IMGUI_CharEvent(char key)
{
	ImGui::GetIO().AddInputCharacter(key);
}

void CL_IMGUI_Shutdown()
{
	ImPlot::DestroyContext();
	ImGui::DestroyContext();
}
