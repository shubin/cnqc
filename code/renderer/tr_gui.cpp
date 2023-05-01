/*
===========================================================================
Copyright (C) 2023 Gian 'myT' Schellenbaum

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
// Main renderer GUI tools


#include "tr_local.h"
#include "../client/cl_imgui.h"


#define IMAGE_WINDOW_NAME  "Image Details"
#define SHADER_WINDOW_NAME "Shader Details"


struct ImageWindow
{
	const image_t* image;
	bool active;
	int mip;
};

struct ShaderWindow
{
	char formattedCode[4096];
	shader_t* shader;
	bool active;
};

struct ShaderReplacement
{
	shader_t original;
	int shaderIndex;
};

struct ShaderReplacements
{
	ShaderReplacement shaders[16];
	int count;
};

static ImageWindow imageWindow;
static ShaderWindow shaderWindow;
static ShaderReplacements shaderReplacements;

static const char* mipNames[16] =
{
	"Mip 0",
	"Mip 1",
	"Mip 2",
	"Mip 3",
	"Mip 4",
	"Mip 5",
	"Mip 6",
	"Mip 7",
	"Mip 8",
	"Mip 9",
	"Mip 10",
	"Mip 11",
	"Mip 12",
	"Mip 13",
	"Mip 14",
	"Mip 15"
};

struct ImageFlag
{
	int flag;
	const char* description;
};

static const ImageFlag imageFlags[] =
{
	{ IMG_NOPICMIP, "'nopicmip'" },
	{ IMG_NOMIPMAP, "'nomipmap'" },
	{ IMG_NOIMANIP, "'noimanip'" },
	{ IMG_LMATLAS, "int. LM" },
	{ IMG_EXTLMATLAS, "ext. LM" },
	{ IMG_NOAF, "no AF" }
};


static void TitleText(const char* text)
{
	ImGui::TextColored(ImVec4(1.0f, 0.25f, 0.0f, 1.0f), text);
}

static void OpenImageDetails(const image_t* image)
{
	ImGui::SetWindowFocus(IMAGE_WINDOW_NAME);

	imageWindow.active = true;
	imageWindow.image = image;
	imageWindow.mip = 0;
}

static void OpenShaderDetails(shader_t* shader)
{
#define Append(Text) Q_strcat(shaderWindow.formattedCode, sizeof(shaderWindow.formattedCode), Text)

	ImGui::SetWindowFocus(SHADER_WINDOW_NAME);

	shaderWindow.active = true;
	shaderWindow.shader = shader;

	shaderWindow.formattedCode[0] = '\0';
	if(shader->text == NULL)
	{
		return;
	}

	const char* s = shader->text;
	int tabs = 0;
	for(;;)
	{
		const char c0 = s[0];
		const char c1 = s[1];
		if(c0 == '{')
		{
			tabs++;
			Append("{");
		}
		else if(c0 == '\n')
		{
			Append("\n");
			if(c1 == '}')
			{
				tabs--;
				if(tabs == 0)
				{
					Append("}\n");
					return;
				}
			}
			for(int i = 0; i < tabs; i++)
			{
				Append("    ");
			}
		}
		else
		{
			Append(va("%c", c0));
		}
		s++;
	}

#undef Append
}

static void AddShaderReplacement(int shaderIndex)
{
	if(shaderReplacements.count >= ARRAY_LEN(shaderReplacements.shaders))
	{
		return;
	}

	if(shaderIndex < 0 || shaderIndex >= tr.numShaders)
	{
		return;
	}

	for(int i = 0; i < shaderReplacements.count; ++i)
	{
		if(shaderReplacements.shaders[i].shaderIndex == shaderIndex)
		{
			return;
		}
	}

	ShaderReplacement& sr = shaderReplacements.shaders[shaderReplacements.count++];
	sr.shaderIndex = shaderIndex;
	sr.original = *tr.shaders[shaderIndex];
	*tr.shaders[shaderIndex] = *tr.defaultShader;
	Q_strncpyz(tr.shaders[shaderIndex]->name, sr.original.name, sizeof(tr.shaders[shaderIndex]->name));
	tr.shaders[shaderIndex]->index = sr.original.index;
	tr.shaders[shaderIndex]->sortedIndex = sr.original.sortedIndex;
}

static void RemoveShaderReplacement(int shaderIndex)
{
	for(int i = 0; i < shaderReplacements.count; ++i)
	{
		const ShaderReplacement& sr = shaderReplacements.shaders[i];
		if(shaderIndex == sr.shaderIndex && shaderIndex >= 0 && shaderIndex < tr.numShaders)
		{
			*tr.shaders[sr.shaderIndex] = sr.original;
			if(i < shaderReplacements.count - 1)
			{
				shaderReplacements.shaders[i] = shaderReplacements.shaders[shaderReplacements.count - 1];
			}
			shaderReplacements.count--;
			break;
		}
	}
}

static bool IsReplacedShader(int shaderIndex)
{
	for(int i = 0; i < shaderReplacements.count; ++i)
	{
		const ShaderReplacement& sr = shaderReplacements.shaders[i];
		if(shaderIndex == sr.shaderIndex)
		{
			return true;
		}
	}

	return false;
}

static void ClearShaderReplacements()
{
	for(int i = 0; i < shaderReplacements.count; ++i)
	{
		const ShaderReplacement& sr = shaderReplacements.shaders[i];
		if(sr.shaderIndex >= 0 && sr.shaderIndex < tr.numShaders)
		{
			*tr.shaders[sr.shaderIndex] = sr.original;
		}
	}
	shaderReplacements.count = 0;
}

static void DrawFilter(char* filter, size_t filterSize)
{
	if(ImGui::Button("Clear filter"))
	{
		filter[0] = '\0';
	}
	ImGui::SameLine();
	if(ImGui::IsWindowAppearing())
	{
		ImGui::SetKeyboardFocusHere();
	}
	ImGui::InputText(" ", filter, filterSize);
	if(ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal))
	{
		ImGui::SetTooltip("Use * to match any character any amount of times.");
	}
}

static void DrawImageToolTip(const image_t* image)
{
	const float scaleX = 128.0f / image->width;
	const float scaleY = 128.0f / image->height;
	const float scale = min(scaleX, scaleY);
	const float w = max(1.0f, scale * (float)image->width);
	const float h = max(1.0f, scale * (float)image->height);
	ImGui::BeginTooltip();
	ImGui::Image((ImTextureID)image->textureIndex, ImVec2(w, h));
	ImGui::EndTooltip();
}

static void DrawImageList()
{
	static bool listActive = false;
	ToggleBooleanWithShortcut(listActive, ImGuiKey_I);
	GUI_AddMainMenuItem(GUI_MainMenu::Tools, "Image Explorer", "Ctrl+I", &listActive);
	if(listActive)
	{
		if(ImGui::Begin("Image Explorer", &listActive))
		{
			static char filter[256];
			DrawFilter(filter, sizeof(filter));

			if(BeginTable("Images", 1))
			{
				for(int i = 0; i < tr.numImages; ++i)
				{
					const image_t* image = tr.images[i];
					if(filter[0] != '\0' && !Com_Filter(filter, image->name))
					{
						continue;
					}

					ImGui::TableNextRow();
					ImGui::TableSetColumnIndex(0);
					if(ImGui::Selectable(va("%s##%d", image->name, i), false))
					{
						OpenImageDetails(image);
					}
					else if(ImGui::IsItemHovered())
					{
						DrawImageToolTip(image);
					}
				}

				ImGui::EndTable();
			}
		}

		ImGui::End();
	}
}

static void DrawImageWindow()
{
	ImageWindow& window = imageWindow;
	if(window.active)
	{
		if(ImGui::Begin(IMAGE_WINDOW_NAME, &window.active, ImGuiWindowFlags_AlwaysAutoResize))
		{
			TitleText(window.image->name);

			char pakName[256];
			if(FS_GetPakPath(pakName, sizeof(pakName), window.image->pakChecksum))
			{
				ImGui::Text(pakName);
			}

			ImGui::Text("%dx%d", window.image->width, window.image->height);
			if(window.image->wrapClampMode == TW_CLAMP_TO_EDGE)
			{
				ImGui::SameLine();
				ImGui::Text("'clampMap'");
			}
			for(int f = 0; f < ARRAY_LEN(imageFlags); ++f)
			{
				if(window.image->flags & imageFlags[f].flag)
				{
					ImGui::SameLine();
					ImGui::Text(imageFlags[f].description);
				}
			}

			ImGui::NewLine();
			ImGui::Text("Shaders:");
			for(int is = 0; is < ARRAY_LEN(tr.imageShaders); ++is)
			{
				const int i = tr.imageShaders[is] & 0xFFFF;
				if(i != window.image->index)
				{
					continue;
				}

				const int s = (tr.imageShaders[is] >> 16) & 0xFFFF;
				const shader_t* const shader = tr.shaders[s];
				if(ImGui::Selectable(va("%s##%d", shader->name, is), false))
				{
					OpenShaderDetails((shader_t*)shader);
				}
				else if(ImGui::IsItemHovered())
				{
					const char* const shaderPath = R_GetShaderPath(shader);
					if(shaderPath != NULL)
					{
						ImGui::BeginTooltip();
						ImGui::Text(shaderPath);
						ImGui::EndTooltip();
					}
				}
			}
			ImGui::NewLine();

			int width = window.image->width;
			int height = window.image->height;
			if((window.image->flags & IMG_NOMIPMAP) == 0)
			{
				ImGui::Combo("Mip", &window.mip, mipNames, R_ComputeMipCount(width, height));
			}
			for(int m = 0; m < window.mip; ++m)
			{
				width = max(width / 2, 1);
				height = max(height / 2, 1);
			}

			const ImTextureID textureId =
				(ImTextureID)window.image->textureIndex |
				(ImTextureID)(window.mip << 16);
			ImGui::Image(textureId, ImVec2(width, height));
		}

		ImGui::End();
	}
}

static void DrawShaderList()
{
	static bool listActive = false;
	ToggleBooleanWithShortcut(listActive, ImGuiKey_S);
	GUI_AddMainMenuItem(GUI_MainMenu::Tools, "Shader Explorer", "Ctrl+S", &listActive);
	if(listActive)
	{
		if(ImGui::Begin("Shader Explorer", &listActive))
		{
			if(shaderReplacements.count > 0 && ImGui::Button("Restore Shaders"))
			{
				ClearShaderReplacements();
			}

			static char filter[256];
			DrawFilter(filter, sizeof(filter));

			if(BeginTable("Shaders", 1))
			{
				for(int s = 0; s < tr.numShaders; ++s)
				{
					shader_t* shader = tr.shaders[s];
					if(filter[0] != '\0' && !Com_Filter(filter, shader->name))
					{
						continue;
					}

					ImGui::TableNextRow();
					ImGui::TableSetColumnIndex(0);
					if(ImGui::Selectable(va("%s##%d", shader->name, s), false))
					{
						OpenShaderDetails(shader);
					}
				}

				ImGui::EndTable();
			}
		}

		ImGui::End();
	}
}

static void DrawShaderWindow()
{
	ShaderWindow& window = shaderWindow;
	if(window.active)
	{
		if(ImGui::Begin(SHADER_WINDOW_NAME, &window.active, ImGuiWindowFlags_AlwaysAutoResize))
		{
			shader_t* shader = window.shader;
			TitleText(shader->name);

			const char* const shaderPath = R_GetShaderPath(shader);
			if(shaderPath != NULL)
			{
				ImGui::Text(shaderPath);
			}

			if(IsReplacedShader(shader->index))
			{
				if(ImGui::Button("Restore Shader"))
				{
					RemoveShaderReplacement(shader->index);
				}
			}
			else
			{
				if(ImGui::Button("Replace Shader"))
				{
					AddShaderReplacement(shader->index);
				}
			}

			ImGui::NewLine();
			ImGui::Text("Images:");
			if(shader->isSky)
			{
				for(int i = 0; i < 6; ++i)
				{
					const image_t* image = shader->sky.outerbox[i];
					if(image == NULL)
					{
						continue;
					}

					if(ImGui::Selectable(va("%s##skybox_%d", image->name, i), false))
					{
						OpenImageDetails(image);
					}
					else if(ImGui::IsItemHovered())
					{
						DrawImageToolTip(image);
					}
				}
			}
			for(int s = 0; s < shader->numStages; ++s)
			{
				const textureBundle_t& bundle = shader->stages[s]->bundle;
				const int imageCount = max(bundle.numImageAnimations, 1);
				for(int i = 0; i < imageCount; ++i)
				{
					const image_t* image = bundle.image[i];
					if(ImGui::Selectable(va("%s##%d_%d", image->name, s, i), false))
					{
						OpenImageDetails(image);
					}
					else if(ImGui::IsItemHovered())
					{
						DrawImageToolTip(image);
					}
				}
			}
			ImGui::NewLine();

			if(window.formattedCode[0] != '\0')
			{
				ImGui::TextUnformatted(window.formattedCode);
			}
			else
			{
				ImGui::Text("No code available");
			}
		}

		ImGui::End();
	}
}

void R_DrawGUI()
{
	DrawImageList();
	DrawImageWindow();
	DrawShaderList();
	DrawShaderWindow();
}

void R_ShutDownGUI()
{
	// this is necessary to avoid crashes in the detail windows
	// following a map change or video restart:
	// the renderer is shut down and the pointers become stale
	imageWindow.active = false;
	imageWindow.image = NULL;
	shaderWindow.active = false;
	shaderWindow.shader = NULL;
	ClearShaderReplacements();
}
