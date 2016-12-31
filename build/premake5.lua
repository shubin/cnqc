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
os.mkdir(path_bin)

-- relative to the makefile
make_path_git_scripts = "../../../cnq3tools/git"
make_path_git_header = "../../code/qcommon/git.h"
make_path_bin = "../../../.bin"

abs_path_q3 = path.getabsolute(_OPTIONS["quake3dir"]) -- os.realpath won't work if we pass in an env. var. here

local function CreateGitPreBuildCommand(scriptExtension)

	local make_path_script = string.format("%s/create_git_header%s", make_path_git_scripts, scriptExtension)

	return string.format("\"%s\" \"%s\"", make_path_script, make_path_git_header)

end

local function CreateExeCopyPostBuildCommand(copyCommand, exeName, exeExtension)

	local make_path_exe = string.format("%s/%s/%s%s", make_path_bin, "%{cfg.buildcfg}", exeName, exeExtension)

	return string.format("%s \"%s\" \"%s\"", copyCommand, make_path_exe, abs_path_q3)

end

local function WIN_CreatePdbCopyPostBuildCommand(exeName)

	local make_path_pdb = string.format("%s/%s/%s.pdb", make_path_bin, "%{cfg.buildcfg}", exeName)

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

local function AddSourcesFromArray(sourceFiles)

	for idx,path in pairs(sourceFiles) do
		files { string.format("%s/%s", path_src, path) }
	end

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
	filter "configurations:debug"
		SetTargetAndLink ( path_bin.."/debug" )
	filter "configurations:release"
		SetTargetAndLink ( path_bin.."/release" )

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
		flags { "ExtraWarnings" }

	filter { "action:vs*", "kind:WindowedApp" }
		flags { "WinMain" }

	filter { "action:vs*", "configurations:debug" }
		buildoptions { "/Gm" }
		linkoptions { "" }

	filter { "action:vs*", "configurations:release" }
		flags { "LinkTimeOptimization" } -- I had no success with GCC's -flto
		buildoptions { "/GL"  }
		linkoptions { "" }

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
		AddSourcesFromArray(server_sources)
	else
		AddSourcesFromArray(client_sources)
		includedirs { path_src.."/freetype/include" }
		AddHeaders("renderer")
		AddHeaders("jpeg-6")
		links { "renderer", "freetype" }
	end

	filter { "system:windows" }
		if (server == 1) then
			AddSourcesFromArray(server_sources_windows)
		else
			AddSourcesFromArray(client_sources_windows)
		end

	filter { "system:not windows" }
		if (server == 1) then
			AddSourcesFromArray(server_sources_unix)
		else
			AddSourcesFromArray(client_sources_unix)
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

exe_suffix = "";
if os.is("linux") then
	exe_suffix = "-x86"
end

solution "cnq3"

	location ( path_build.."/".._ACTION )
	platforms { "x32" }
	configurations { "debug", "release" }

	project "cnq3"

		kind "WindowedApp"
		language "C++"
		ApplyExeProjectSettings("cnq3"..exe_suffix, 0)

	project "cnq3-server"

		kind "WindowedApp"
		language "C++"
		defines { "DEDICATED" }
		ApplyExeProjectSettings("cnq3-server"..exe_suffix, 1)

	project "botlib"

		kind "StaticLib"
		language "C++"
		defines { "BOTLIB" }
		AddSourcesAndHeaders("botlib")
		ApplyLibProjectSettings()

	project "renderer"

		local jpeg_sources =
		{
		    "jpeg-6/jcapimin.c",
			"jpeg-6/jccoefct.c",
			"jpeg-6/jccolor.c",
			"jpeg-6/jcdctmgr.c",
			"jpeg-6/jchuff.c",
			"jpeg-6/jcinit.c",
			"jpeg-6/jcmainct.c",
			"jpeg-6/jcmarker.c",
			"jpeg-6/jcmaster.c",
			"jpeg-6/jcomapi.c",
			"jpeg-6/jcparam.c",
			"jpeg-6/jcphuff.c",
			"jpeg-6/jcprepct.c",
			"jpeg-6/jcsample.c",
			"jpeg-6/jctrans.c",
			"jpeg-6/jdapimin.c",
			"jpeg-6/jdapistd.c",
			"jpeg-6/jdatadst.c",
			"jpeg-6/jdatasrc.c",
			"jpeg-6/jdcoefct.c",
			"jpeg-6/jdcolor.c",
			"jpeg-6/jddctmgr.c",
			"jpeg-6/jdhuff.c",
			"jpeg-6/jdinput.c",
			"jpeg-6/jdmainct.c",
			"jpeg-6/jdmarker.c",
			"jpeg-6/jdmaster.c",
			"jpeg-6/jdpostct.c",
			"jpeg-6/jdsample.c",
			"jpeg-6/jdtrans.c",
			"jpeg-6/jerror.c",
			"jpeg-6/jfdctflt.c",
			"jpeg-6/jidctflt.c",
			"jpeg-6/jmemmgr.c",
			"jpeg-6/jmemnobs.c",
			"jpeg-6/jutils.c"
		}

		kind "StaticLib"
		language "C++"
		AddSourcesAndHeaders("renderer")
		AddSourcesFromArray(jpeg_sources)
		includedirs { path_src.."/freetype/include" }
		ApplyLibProjectSettings()

		filter "action:gmake"
			-- We force libjpeg files to be compiled as C++.
			buildoptions { "-x c++" }

	project "freetype"

		local ft_sources =
		{
			"freetype/src/base/ftbbox.c",
			"freetype/src/base/ftsynth.c",
			"freetype/src/base/ftbase.c",
			"freetype/src/base/ftglyph.c",
			"freetype/src/base/ftinit.c",
			"freetype/src/base/ftstroke.c",
			"freetype/src/base/ftsystem.c",
			"freetype/src/sfnt/sfnt.c",
			"freetype/src/smooth/smooth.c",
			"freetype/src/truetype/truetype.c"
		}

		kind "StaticLib"
		language "C"
		AddSourcesFromArray(ft_sources)
		includedirs { path_src.."/freetype/include" }
		defines { "_LIB", "FT2_BUILD_LIBRARY", "_BIND_TO_CURRENT_VCLIBS_VERSION=1" }
		ApplyLibProjectSettings()
