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


#include "cl_imgui.h"
#include "../qcommon/q_shared.h"
#include "../qcommon/qcommon.h"


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
