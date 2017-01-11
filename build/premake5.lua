--[[

There are only 2 build toolchains supported:
- Visual C++ on Windows
- GCC on Linux

@TODO: prevent UNICODE and _UNICODE from being #define'd with premake
@TODO: enable Minimal Rebuild from premake (instead of adding /Gm)

--]]

newoption
{
   trigger     = "quake3dir",
   description = "Quake 3 directory path, used for copying the binaries and running the debugger"
}

if not _OPTIONS["quake3dir"] then
	error "quake3dir must be specified on the command-line"
end

-- relative to the LUA script
path_root = "../.."
path_src = path_root.."/cnq3/code"
path_build = path_root.."/cnq3/build"
path_bin = path_root.."/.bin"

-- relative to the makefile
make_path_git_scripts = "../../../cnq3tools/git"
make_path_git_header = "../../code/qcommon/git.h"
make_path_bin = "../../../.bin"

-- the only absolute path we allow
abs_path_q3 = path.getabsolute(_OPTIONS["quake3dir"]) -- os.realpath won't work if we pass in an env. var. here

extra_warnings = 1

local function GetBinDirName()

	return "%{cfg.buildcfg}_%{cfg.platform}"

end

local function CreateGitPreBuildCommand(scriptExtension)

	local make_path_script = string.format("%s/create_git_header%s", make_path_git_scripts, scriptExtension)

	return string.format("\"%s\" \"%s\"", make_path_script, make_path_git_header)

end

local function CreateExeCopyPostBuildCommand(copyCommand, exeName, exeExtension)

	local make_path_exe = string.format("%s/%s/%s%s", make_path_bin, GetBinDirName(), exeName, exeExtension)

	return string.format("%s \"%s\" \"%s\"", copyCommand, make_path_exe, abs_path_q3)

end

local function WIN_CreatePdbCopyPostBuildCommand(exeName)

	local make_path_pdb = string.format("%s/%s/%s.pdb", make_path_bin, GetBinDirName(), exeName)

	return string.format("copy \"%s\" \"%s\"", make_path_pdb, abs_path_q3)

end

local function SetTargetAndLink(dirPath)

	targetdir(dirPath)
	libdirs(dirPath)

end

local function AddSourcesAndHeaders(dirPath)

	files
	{
		path_src.."/"..dirPath.."/*.cpp",
		path_src.."/"..dirPath.."/*.c",
		path_src.."/"..dirPath.."/*.h",
	}

end

local function AddSources(dirPath)

	files
	{
		path_src.."/"..dirPath.."/*.cpp",
		path_src.."/"..dirPath.."/*.c"
	}

end

local function AddHeaders(dirPath)

	files { path_src.."/"..dirPath.."/*.h" }

end

local function AddSourcesFromArray(dir, sourceFiles)

	for idx,path in pairs(sourceFiles) do
		files { string.format("%s/%s/%s", path_src, dir, path) }
	end

end

local function AddAssemblerSourcesFromArray(dir, fileNames)

	for idx,path in pairs(fileNames) do
		files { string.format("%s/%s/%s.asm", path_src, dir, path) }
	end

end

local function GetMakePath(premakePath)

	return "../"..premakePath

end

-- premake tokens for this script:
-- cfg.buildcfg -> "debug", "release"
-- cfg.architecture -> "x86", "x86_64"
-- cfg.system -> "windows", "linux"
-- cfg.platform -> "x32", "x64"

local function GetLibJpegTurboNasmFlags()

	libjpeg_turbo_nasm_flags_map =
	{
	   windows_x32 = "-fwin32 -DWIN32",
	   windows_x64 = "-fwin64 -DWIN64 -D__x86_64__",
	   linux_x32 = "-felf32 -DELF",
	   linux_x64 = "-felf64 -DELF -D__x86_64__"
	}

	return "%{libjpeg_turbo_nasm_flags_map[cfg.system..\"_\"..cfg.platform]}"

end

local function GetCompilerObjectExtension()

	if _ACTION == "gmake" then
		return ".o"
	end

	return ".obj"

end

local function GetExeNameSuffix()

	platform_exe_name_suffix_map =
	{
	   x32 = "-x86",
	   x64 = "-x64"
	}

	return "%{platform_exe_name_suffix_map[cfg.platform]}"

end

local function ApplyProjectSettings()

	--
	-- General
	--

	filter { }

	location ( path_build.."/".._ACTION )

	rtti "Off"
	exceptionhandling "Off"
	flags { "NoPCH", "StaticRuntime", "NoManifest", "NoNativeWChar" }

	filter "configurations:debug"
		defines { "DEBUG", "_DEBUG" }
		flags { }

	filter "configurations:release"
		defines { "NDEBUG" }
		flags -- others: NoIncrementalLink NoCopyLocal NoImplicitLink NoBufferSecurityChecks
		{
			"NoMinimalRebuild",
			"OptimizeSize",
			"NoFramePointer",
			"EnableSSE2",
			"FloatFast",
			"MultiProcessorCompile",
			"NoRuntimeChecks"
		}

	-- Build directories
	filter {  }
	SetTargetAndLink ( string.format("%s/%s", path_bin, GetBinDirName()) )

	--
	-- Visual C++
	--

	-- Some build options:
	-- /GT  => Support Fiber-Safe Thread-Local Storage
	-- /GS- => Buffer Security Check disabled
	-- /GL  => Whole Program Optimization
	-- /Zi  => Debug info, but not for edit and continue
	-- /Os  => Favor size over speed
	-- /Gm  => Enable Minimal Rebuild

	filter "action:vs*"
		symbols "On"
		editandcontinue "Off"
		defines { "_CRT_SECURE_NO_WARNINGS", "WIN32", "_WIN32" }
		if extra_warnings == 1 then
			flags { "ExtraWarnings" }
		end

	filter { "action:vs*", "kind:WindowedApp" }
		flags { "WinMain" }

	filter { "action:vs*", "configurations:debug" }
		buildoptions { "/Gm" }
		linkoptions { "" }

	filter { "action:vs*", "configurations:release" }
		flags { "LinkTimeOptimization" } -- I had no success with GCC's -flto
		buildoptions { "/GL"  }
		linkoptions { "" }

	-- disable the "conversion from 'X' to 'Y', possible loss of data" warning
	-- this should be removed once the x64 port is complete
	filter { "action:vs*", "platforms:x64" }
		buildoptions { "/wd4267" }

	--
	-- GCC
	--

	-- "-g1" is the minimum amount of debug information
	-- it should be just enough to get a symbolic stack trace

	filter "action:gmake"
		symbols "On"
		buildoptions { "-Wno-unused-parameter -Wno-write-strings" }
		linkoptions { "" }

	filter { "action:gmake", "configurations:rebug" }
		buildoptions { "" }
		linkoptions { "" }

	filter { "action:gmake", "configurations:release" }
		buildoptions { "-g1" }
		linkoptions { "" }

end

local function ApplyLibProjectSettings()

	ApplyProjectSettings()

end

local function ApplyExeProjectSettings(exeName, server)

	ApplyProjectSettings()

	filter { }

	exeName = exeName..GetExeNameSuffix()
	targetname(exeName)

	local server_sources =
	{
		"qcommon/cmd.cpp",
		"qcommon/cm_load.cpp",
		"qcommon/cm_patch.cpp",
		"qcommon/cm_polylib.cpp",
		"qcommon/cm_test.cpp",
		"qcommon/cm_trace.cpp",
		"qcommon/common.cpp",
		"qcommon/cvar.cpp",
		"qcommon/files.cpp",
		"qcommon/huffman.cpp",
		"qcommon/md4.cpp",
		"qcommon/md5.cpp",
		"qcommon/msg.cpp",
		"qcommon/net_chan.cpp",
		"qcommon/net_ip.cpp",
		"qcommon/q_math.c",
		"qcommon/q_shared.c",
		"qcommon/unzip.cpp",
		"qcommon/vm.cpp",
		"qcommon/vm_interpreted.cpp",
		"qcommon/vm_x86.cpp",
		"server/sv_bot.cpp",
		"server/sv_ccmds.cpp",
		"server/sv_client.cpp",
		"server/sv_game.cpp",
		"server/sv_init.cpp",
		"server/sv_main.cpp",
		"server/sv_net_chan.cpp",
		"server/sv_snapshot.cpp",
		"server/sv_world.cpp"
	}

	local server_sources_windows =
	{
		"win32/win_main.cpp",
		"win32/win_shared.cpp",
		"win32/win_syscon.cpp"
	}

	local server_sources_unix =
	{
		"unix/unix_main.cpp",
		"unix/unix_shared.cpp",
		"unix/linux_signals.cpp"
	}

	local client_sources =
	{
		"client/cl_avi.cpp",
		"client/cl_browser.cpp",
		"client/cl_cgame.cpp",
		"client/cl_cin.cpp",
		"client/cl_console.cpp",
		"client/cl_curl.cpp",
		"client/cl_input.cpp",
		"client/cl_keys.cpp",
		"client/cl_main.cpp",
		"client/cl_net_chan.cpp",
		"client/cl_parse.cpp",
		"client/cl_scrn.cpp",
		"client/cl_ui.cpp",
		"client/snd_codec.cpp",
		"client/snd_codec_wav.cpp",
		"client/snd_dma.cpp",
		"client/snd_main.cpp",
		"client/snd_mem.cpp",
		"client/snd_mix.cpp",
		"qcommon/cmd.cpp",
		"qcommon/cm_load.cpp",
		"qcommon/cm_patch.cpp",
		"qcommon/cm_polylib.cpp",
		"qcommon/cm_test.cpp",
		"qcommon/cm_trace.cpp",
		"qcommon/common.cpp",
		"qcommon/cvar.cpp",
		"qcommon/files.cpp",
		"qcommon/huffman.cpp",
		"qcommon/md4.cpp",
		"qcommon/md5.cpp",
		"qcommon/msg.cpp",
		"qcommon/net_chan.cpp",
		"qcommon/net_ip.cpp",
		"qcommon/q_math.c",
		"qcommon/q_shared.c",
		"qcommon/unzip.cpp",
		"qcommon/vm.cpp",
		"qcommon/vm_interpreted.cpp",
		"qcommon/vm_x86.cpp",
		"server/sv_bot.cpp",
		"server/sv_ccmds.cpp",
		"server/sv_client.cpp",
		"server/sv_game.cpp",
		"server/sv_init.cpp",
		"server/sv_main.cpp",
		"server/sv_net_chan.cpp",
		"server/sv_snapshot.cpp",
		"server/sv_world.cpp"
	}

	local client_sources_windows =
	{
		"win32/win_input.cpp",
		"win32/win_main.cpp",
		"win32/win_shared.cpp",
		"win32/win_snd.cpp",
		"win32/win_syscon.cpp",
		"win32/win_wndproc.cpp",
		"win32/win_glimp.cpp",
		"win32/win_qgl.c"
	}

	local client_sources_unix =
	{
		"unix/unix_main.cpp",
		"unix/unix_shared.cpp",
		"unix/linux_joystick.c",
		"unix/linux_signals.cpp",
		"unix/linux_qgl.c",
		"unix/linux_snd.c",
		"unix/linux_glimp.c"
	}

	AddHeaders("botlib")
	AddHeaders("qcommon")
	AddHeaders("server")
	AddHeaders("client")
	AddHeaders("cgame")
	AddHeaders("game")
	AddHeaders("ui")

	links { "botlib" }

	if (server == 1) then
		AddSourcesFromArray(".", server_sources)
	else
		AddSourcesFromArray(".", client_sources)
		includedirs { path_src.."/freetype/include" }
		AddHeaders("renderer")
		links { "renderer", "freetype", "libjpeg-turbo" }
	end

	filter { "system:windows" }
		if (server == 1) then
			AddSourcesFromArray(".", server_sources_windows)
		else
			AddSourcesFromArray(".", client_sources_windows)
		end
		AddHeaders("win32")

	filter { "system:not windows" }
		if (server == 1) then
			AddSourcesFromArray(".", server_sources_unix)
		else
			AddSourcesFromArray(".", client_sources_unix)
		end

	-- create git info header
	-- copy the binaries over to the test q3 install
	-- it seems that "filter" doesn't work with "prebuildcommands", "postbuildcommands"
	filter { }
	if os.is("windows") then
		prebuildcommands { path.translate(CreateGitPreBuildCommand(".cmd"), "\\") }
		postbuildcommands { path.translate(CreateExeCopyPostBuildCommand("copy", exeName, ".exe"), "\\") }
		postbuildcommands { path.translate(WIN_CreatePdbCopyPostBuildCommand(exeName), "\\") }
	else
		prebuildcommands { CreateGitPreBuildCommand(".sh") }
		postbuildcommands { CreateExeCopyPostBuildCommand("cp -u", exeName, "") }
	end

	-- create VC++ debug settings
	filter "action:vs*"
		local abs_path_exe = string.format("%s\\%s.exe", abs_path_q3, exeName)
		debugcommand(abs_path_exe)
		if (server == 1) then
			debugargs { "+set sv_pure 0" }
		else
			debugargs { "+set sv_pure 0 +set r_fullscreen 0" }
		end
		debugdir(abs_path_q3)

	filter "system:windows"
		links { "Winmm", "ws2_32" }
		if (server == 0) then
			links { "opengl32" }
		end

	filter "system:not windows"
		links { "dl", "m" }
		if (server == 0) then
			buildoptions { "-pthread" }
			links { "X11", "pthread" }
		end

	-- RC will compile the .rc into a .res
	-- LINK accepts .res files directly
	filter "action:vs*"
		linkoptions { path.translate(path_src.."/win32/winquake.res", "\\"), "/STACK:8388608" }

	filter { "action:vs*", "configurations:release" }
		linkoptions { "/OPT:REF", "/OPT:ICF" }

	-- force everything to be compiled as C++ for now
	-- otherwise, we run into problems (that should really be fixed)
	filter "action:gmake"
		buildoptions { "-x c++" }

end

local function ApplyLibJpegTurboProjectSettings()

	local jpeg_sources =
	{
		"jcapimin.c",
		"jcapistd.c",
		"jccoefct.c",
		"jccolor.c",
		"jcdctmgr.c",
		"jchuff.c",
		"jcinit.c",
		"jcmainct.c",
		"jcmarker.c",
		"jcmaster.c",
		"jcomapi.c",
		"jcparam.c",
		"jcphuff.c",
		"jcprepct.c",
		"jcsample.c",
		"jctrans.c",
		"jdapimin.c",
		"jdapistd.c",
		"jdatadst.c",
		"jdatasrc.c",
		"jdcoefct.c",
		"jdcolor.c",
		"jddctmgr.c",
		"jdhuff.c",
		"jdinput.c",
		"jdmainct.c",
		"jdmarker.c",
		"jdmaster.c",
		"jdmerge.c",
		"jdphuff.c",
		"jdpostct.c",
		"jdsample.c",
		"jdtrans.c",
		"jerror.c",
		"jfdctflt.c",
		"jfdctfst.c",
		"jfdctint.c",
		"jidctflt.c",
		"jidctfst.c",
		"jidctint.c",
		"jidctred.c",
		"jquant1.c",
		"jquant2.c",
		"jutils.c",
		"jmemmgr.c"
	}

	local jpeg_asm_sources_x86 =
	{
		"jsimdcpu",
		"jfdctflt-3dn",
		"jidctflt-3dn",
		"jquant-3dn",
		"jccolor-mmx",
		"jcgray-mmx",
		"jcsample-mmx",
		"jdcolor-mmx",
		"jdmerge-mmx",
		"jdsample-mmx",
		"jfdctfst-mmx",
		"jfdctint-mmx",
		"jidctfst-mmx",
		"jidctint-mmx",
		"jidctred-mmx",
		"jquant-mmx",
		"jfdctflt-sse",
		"jidctflt-sse",
		"jquant-sse",
		"jccolor-sse2",
		"jcgray-sse2",
		"jchuff-sse2",
		"jcsample-sse2",
		"jdcolor-sse2",
		"jdmerge-sse2",
		"jdsample-sse2",
		"jfdctfst-sse2",
		"jfdctint-sse2",
		"jidctflt-sse2",
		"jidctfst-sse2",
		"jidctint-sse2",
		"jidctred-sse2",
		"jquantf-sse2",
		"jquanti-sse2"
	}

	local jpeg_asm_sources_x64 =
	{
		"jfdctflt-sse-64",
		"jccolor-sse2-64",
		"jcgray-sse2-64",
		"jchuff-sse2-64",
		"jcsample-sse2-64",
		"jdcolor-sse2-64",
		"jdmerge-sse2-64",
		"jdsample-sse2-64",
		"jfdctfst-sse2-64",
		"jfdctint-sse2-64",
		"jidctflt-sse2-64",
		"jidctfst-sse2-64",
		"jidctint-sse2-64",
		"jidctred-sse2-64",
		"jquantf-sse2-64",
		"jquanti-sse2-64"
	}

	AddSourcesFromArray("libjpeg-turbo", jpeg_sources)
	includedirs { path_src.."/libjpeg-turbo", path_src.."/libjpeg-turbo/simd" }
	defines { "WITH_SIMD" }

	filter "platforms:x32"
		AddAssemblerSourcesFromArray("libjpeg-turbo/simd", jpeg_asm_sources_x86)
		files { path_src.."/libjpeg-turbo/simd/jsimd_i386.c" }
		defines { "SIZEOF_SIZE_T=4" }

	filter "platforms:x64"
		AddAssemblerSourcesFromArray("libjpeg-turbo/simd", jpeg_asm_sources_x64)
		files { path_src.."/libjpeg-turbo/simd/jsimd_x86_64.c" }
		defines { "SIZEOF_SIZE_T=8" }

	local asm_inc_path = GetMakePath(path_src.."/libjpeg-turbo")
	local nasm_flags = GetLibJpegTurboNasmFlags()
	local nasm_includes
	if os.is("windows") then
		asm_inc_path = path.translate(asm_inc_path, "\\")
		nasm_includes = string.format("-I%s\\ -I%s\\win\\ -I%s\\simd\\", asm_inc_path, asm_inc_path, asm_inc_path)
	else
		nasm_includes = string.format("-I%s/ -I%s/win/ -I%s/simd/", asm_inc_path, asm_inc_path, asm_inc_path)
	end

	local obj_file_path = string.format("%s%s", "%{cfg.targetdir}/%{file.basename}", GetCompilerObjectExtension())
	local command = string.format("nasm -o%s %s %s %s", obj_file_path, nasm_flags, nasm_includes, "%{file.relpath}")
	if os.is("windows") then
		command = path.translate(command, "\\")
		obj_file_path = path.translate(obj_file_path, "\\")
	end

	filter "files:**.asm"
		buildmessage "%{file.basename}.asm"
		buildcommands { command }
		buildoutputs { obj_file_path }

	extra_warnings = 0
	ApplyLibProjectSettings()
	extra_warnings = 1

end

solution "cnq3"

	location ( path_build.."/".._ACTION )
	platforms { "x32", "x64" }
	configurations { "debug", "release" }

	project "cnq3"

		kind "WindowedApp"
		language "C++"
		ApplyExeProjectSettings("cnq3", 0)

	project "cnq3-server"

		kind "WindowedApp"
		language "C++"
		defines { "DEDICATED" }
		ApplyExeProjectSettings("cnq3-server", 1)

	project "botlib"

		kind "StaticLib"
		language "C++"
		defines { "BOTLIB" }
		AddSourcesAndHeaders("botlib")
		ApplyLibProjectSettings()

	project "renderer"

		kind "StaticLib"
		language "C++"
		AddSourcesAndHeaders("renderer")
		includedirs { path_src.."/freetype/include" }
		ApplyLibProjectSettings()

	project "libjpeg-turbo"

		kind "StaticLib"
		language "C"
		ApplyLibJpegTurboProjectSettings()

	project "freetype"

		local ft_sources =
		{
			"base/ftbbox.c",
			"base/ftsynth.c",
			"base/ftbase.c",
			"base/ftglyph.c",
			"base/ftinit.c",
			"base/ftstroke.c",
			"base/ftsystem.c",
			"sfnt/sfnt.c",
			"smooth/smooth.c",
			"truetype/truetype.c"
		}

		kind "StaticLib"
		language "C"
		AddSourcesFromArray("freetype/src", ft_sources)
		includedirs { path_src.."/freetype/include" }
		defines { "_LIB", "FT2_BUILD_LIBRARY", "_BIND_TO_CURRENT_VCLIBS_VERSION=1" }
		ApplyLibProjectSettings()
		filter "action:vs*"
			buildoptions { "/wd4324" } -- "structure was padded due to __declspec(align())"
