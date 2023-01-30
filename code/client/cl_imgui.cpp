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


bool BeginTable(const char* name, int count)
{
	ImGui::Text(name);

	return ImGui::BeginTable(name, count, ImGuiTableFlags_RowBg | ImGuiTableFlags_Borders | ImGuiTableFlags_Resizable);
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

void TableRow2Bool(const char* item0, bool item1)
{
	TableRow(2, item0, item1 ? "YES" : "NO");
}
