--[[

Currently used build:
premake v5.0.0-beta2

There are 3 supported build toolchains:
- Visual C++ on Windows x64
- GCC or Clang on Linux x64
- Clang or GCC on FreeBSD x64

@TODO: prevent UNICODE and _UNICODE from being #define'd with premake

--]]

-- relative to the LUA script
path_root = ".."
path_src = path_root.."/code"
path_make = path_root.."/makefiles"
path_build = path_root.."/.build"
path_bin = path_root.."/.bin"

-- relative to the makefile
make_path_src = "../../code"
make_path_git_scripts = ".."
make_path_git_header = "../../code/qcommon/git.h"
make_path_build = "../../.build"
make_path_bin = "../../.bin"

-- environment variables
envvar_q3dir  = "QUAKE3DIR" -- Windows: required - Linux: optional
envvar_moddir = "CPMADIR"   -- Windows: required - Linux: unused
abs_path_q3 = string.format("$(%s)", envvar_q3dir)

extra_warnings = 1

-- premake tokens for this script:
-- cfg.buildcfg -> "debug", "release"
-- cfg.architecture -> "x86_64" ("x86" deprecated)
-- cfg.system -> "windows", "linux", "bsd"
-- cfg.platform -> "x64" ("x32" deprecated)

local function GetBinDirName()

	return "%{cfg.buildcfg}"

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

local function GetLibJpegTurboNasmFlags()

	libjpeg_turbo_nasm_flags_map =
	{
		windows = "-fwin64 -DWIN64 -D__x86_64__",
		linux = "-felf64 -DELF -D__x86_64__",
		bsd = "-felf64 -DELF -D__x86_64__"
	}

	return "%{libjpeg_turbo_nasm_flags_map[cfg.system]}"

end

local function GetCompilerObjectExtension()

	if _ACTION == "gmake" then
		return ".o"
	end

	return ".obj"

end

local function ApplyProjectSettings(outputExe)

	--
	-- General
	--

	filter { }

	rtti "Off"
	exceptionhandling "Off"
	staticruntime "On"
	nativewchar "Off"
	flags { "NoPCH", "NoManifest" }

	filter "configurations:debug"
		defines { "DEBUG", "_DEBUG" }
		flags { }

	filter "configurations:release"
		defines { "NDEBUG" }
		optimize "Size"
		omitframepointer "On"
		vectorextensions "SSE2"
		floatingpoint "Fast"
		flags -- others: NoIncrementalLink NoCopyLocal NoImplicitLink NoBufferSecurityChecks
		{
			"NoMinimalRebuild",
			"MultiProcessorCompile",
			"NoRuntimeChecks"
		}

	-- Build directories
	filter {  }
	local objDir = string.format("%s/%s/%s", path_build, GetBinDirName(), "%{prj.name}")
	local libDir = string.format("%s/%s",    path_build, GetBinDirName())
	if outputExe == true then
		local exeDir = string.format("%s/%s", path_bin, GetBinDirName())
		objdir(objDir)
		targetdir(exeDir)
		libdirs(libDir)
	else
		objdir(objDir)
		targetdir(libDir)
		libdirs(libDir)
	end

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
			warnings "Extra"
		end

	filter { "action:vs*", "kind:WindowedApp" }
		entrypoint "WinMainCRTStartup"

	filter { "action:vs*", "configurations:debug" }
		buildoptions { "" }
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
	-- GCC / Clang
	--

	-- "-g1" is the minimum amount of debug information
	-- it should be just enough to get a symbolic stack trace

	filter "action:gmake"
		symbols "On"
		buildoptions
		{
			"-Wno-unused-parameter",
			"-Wno-write-strings",
			"-Wno-parentheses",
			"-Wno-parentheses-equality"
		}
		linkoptions { "" }

	filter { "action:gmake", "configurations:debug" }
		buildoptions { "" }
		linkoptions { "" }

	filter { "action:gmake", "configurations:release" }
		buildoptions { "-g1" }
		linkoptions { "" }

	filter { }

end

local function ApplyLibProjectSettings()

	ApplyProjectSettings(false)

end

local function ApplyExeProjectSettings(exeName, server)

	ApplyProjectSettings(true)

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
		"qcommon/crash.cpp",
		"qcommon/cvar.cpp",
		"qcommon/files.cpp",
		"qcommon/huffman.cpp",
		"qcommon/huffman_static.cpp",
		"qcommon/json.cpp",
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
		"win32/win_exception.cpp",
		"win32/win_syscon.cpp"
	}

	local server_sources_linux =
	{
		"linux/linux_main.cpp",
		"linux/linux_shared.cpp",
		"linux/linux_signals.cpp",
		"linux/linux_tty.cpp"
	}

	local client_sources =
	{
		"client/cl_avi.cpp",
		"client/cl_browser.cpp",
		"client/cl_cgame.cpp",
		"client/cl_cin.cpp",
		"client/cl_console.cpp",
		"client/cl_demo.cpp",
		"client/cl_download.cpp",
		"client/cl_imgui.cpp",
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
		"qcommon/crash.cpp",
		"qcommon/cvar.cpp",
		"qcommon/files.cpp",
		"qcommon/huffman.cpp",
		"qcommon/huffman_static.cpp",
		"qcommon/json.cpp",
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
		"win32/win_exception.cpp",
		"win32/win_snd.cpp",
		"win32/win_syscon.cpp",
		"win32/win_wndproc.cpp",
		"win32/win_glimp.cpp"
	}

	local client_sources_linux =
	{
		"linux/linux_main.cpp",
		"linux/linux_shared.cpp",
		"linux/linux_signals.cpp",
		"linux/linux_tty.cpp",
		"linux/sdl_core.cpp",
		"linux/sdl_glimp.cpp",
		"linux/sdl_snd.cpp"
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
		AddSourcesAndHeaders("imgui")
		AddSourcesAndHeaders("implot")
		AddHeaders("renderer")
		links { "renderer", "libjpeg-turbo" }
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
			AddSourcesFromArray(".", server_sources_linux)
		else
			AddSourcesFromArray(".", client_sources_linux)
		end

	-- create git info header
	-- copy the binaries over to the test q3 install
	-- it seems that "filter" doesn't work with "prebuildcommands", "postbuildcommands"
	filter { }
	if os.istarget("windows") then
		prebuildcommands { path.translate(CreateGitPreBuildCommand(".cmd"), "\\") }
		postbuildcommands
		{
			path.translate(CreateExeCopyPostBuildCommand("copy", exeName, ".exe"), "\\"),
			path.translate(WIN_CreatePdbCopyPostBuildCommand(exeName), "\\")
		}
	else
		prebuildcommands { CreateGitPreBuildCommand(".sh") }
		postbuildcommands { string.format("if [ -n \"$$%s\" ]; then %s; fi",
			envvar_q3dir, CreateExeCopyPostBuildCommand("cp", exeName, "")) }
	end

	-- create VC++ debug settings
	filter "action:vs*"
		local abs_path_exe = string.format("%s\\%s.exe", abs_path_q3, exeName)
		debugcommand(abs_path_exe)
		if (server == 1) then
			debugargs { string.format("+set fs_game $(%s) +set sv_pure 0", envvar_moddir) }
		else
			debugargs { string.format("+set fs_game $(%s) +set sv_pure 0 +set r_fullscreen 0", envvar_moddir) }
		end
		debugdir(abs_path_q3)

	filter "system:windows"
		links { "Winmm", "ws2_32", "Version" }
		if (server == 0) then
			links { "D3D12", "DXGI", "Dwmapi", "nvapi64" }
		end

	filter "system:not windows"
		links { "dl", "m" }

	filter "system:bsd"
		links { "execinfo" }

	-- RC will compile the .rc into a .res
	-- LINK accepts .res files directly
	filter "action:vs*"
		linkoptions { path.translate(make_path_src.."/win32/winquake.res", "\\"), "/STACK:8388608" }
		if (server == 0) then
			linkoptions { "/MANIFEST:EMBED", "/MANIFESTINPUT:"..path.translate(make_path_src.."/win32/client.manifest", "\\") }
		end

	filter { "action:vs*", "configurations:release" }
		linkoptions { "/OPT:REF", "/OPT:ICF" }

	-- force everything to be compiled as C++ for now
	-- otherwise, we run into problems (that should really be fixed)
	filter "action:gmake"
		buildoptions { "-x c++" }

	filter { }

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

	filter "platforms:x64"
		AddAssemblerSourcesFromArray("libjpeg-turbo/simd", jpeg_asm_sources_x64)
		files { path_src.."/libjpeg-turbo/simd/jsimd_x86_64.c" }
		defines { "SIZEOF_SIZE_T=8" }

	local asm_inc_path = GetMakePath(path_src.."/libjpeg-turbo")
	local nasm_flags = GetLibJpegTurboNasmFlags()
	local nasm_includes
	if os.istarget("windows") then
		asm_inc_path = path.translate(asm_inc_path, "\\")
		nasm_includes = string.format("-I%s\\ -I%s\\win\\ -I%s\\simd\\", asm_inc_path, asm_inc_path, asm_inc_path)
	else
		nasm_includes = string.format("-I%s/ -I%s/win/ -I%s/simd/", asm_inc_path, asm_inc_path, asm_inc_path)
	end

	local obj_file_path = string.format("%s%s", "%{cfg.objdir}/%{file.basename}", GetCompilerObjectExtension())
	local command = string.format("nasm -o%s %s %s %s", obj_file_path, nasm_flags, nasm_includes, "%{file.relpath}")
	if os.istarget("windows") then
		command = path.translate(command, "\\")
		obj_file_path = path.translate(obj_file_path, "\\")
	end

	filter "files:**.asm"
		buildmessage "%{file.basename}.asm"
		buildcommands { command }
		buildoutputs { obj_file_path }

	filter { }

	extra_warnings = 0
	ApplyLibProjectSettings()
	extra_warnings = 1

end

solution "cnq3"

	platforms { "x64" }

	location ( string.format("%s/%s_%s", path_make, os.target(), _ACTION) )
	configurations { "debug", "release" }

	project "botlib"

		kind "StaticLib"
		language "C++"
		defines { "BOTLIB" }
		AddSourcesAndHeaders("botlib")
		ApplyLibProjectSettings()
		filter "action:gmake"
			buildoptions { "-std=c++98" }

	project "cnq3-server"

		kind "WindowedApp"
		language "C++"
		defines { "DEDICATED" }
		ApplyExeProjectSettings("cnq3-server", 1)
		filter "action:gmake"
			buildoptions { "-std=c++98" }

	if os.istarget("windows") then

		project "shadercomp"

			kind "ConsoleApp"
			language "C++"
			AddSourcesAndHeaders("shadercomp")
			postbuildcommands { string.format("{copyfile} \"%%{cfg.buildtarget.directory}/%%{cfg.buildtarget.basename}.exe\" \"%s/renderer/hlsl\"", make_path_src) }
			postbuildcommands { string.format("{copyfile} \"%%{cfg.buildtarget.directory}/%%{cfg.buildtarget.basename}.pdb\" \"%s/renderer/hlsl\"", make_path_src) }
			postbuildcommands { string.format("\"%s/renderer/hlsl/%%{cfg.buildtarget.name}\"", make_path_src) }
			ApplyProjectSettings(true)
			--[[
			VC++ STILL requires absolute paths for these... maybe it will be fixed a few decades after I'm in the grave
			local debug_path_dir = string.format("%s/renderer/hlsl", make_path_src)
			local debug_path_exe = string.format("%s/%%{cfg.buildtarget.name}", debug_path_dir)
			debugdir(debug_path_dir)
			debugcommand(debug_path_exe)
			--]]

		project "renderer"

			dependson "shadercomp"
			kind "StaticLib"
			language "C++"
			AddSourcesAndHeaders("renderer")
			if os.istarget("bsd") then
				includedirs { "/usr/local/include" }
			end
			if os.istarget("windows") then
				files { string.format("%s/renderer/hlsl/*.hlsl", path_src) }
				files { string.format("%s/renderer/hlsl/*.hlsli", path_src) }
				filter "files:**.hlsl"
					flags { "ExcludeFromBuild" }
				filter { }
			end
			ApplyLibProjectSettings()
			includedirs { path_src.."/imgui" }
			filter "action:gmake"
				buildoptions { "-std=c++98" }

		project "libjpeg-turbo"

			kind "StaticLib"
			language "C"
			ApplyLibJpegTurboProjectSettings()

		project "cnq3"

			kind "WindowedApp"
			language "C++"
			if os.istarget("windows") then
				includedirs { path_src.."/imgui" }
				libdirs { path_src.."/nvapi" }
			end
			if os.istarget("bsd") then
				includedirs { "/usr/local/include" }
				libdirs { "/usr/local/lib" }
			end
			ApplyExeProjectSettings("cnq3", 0)
			filter "action:gmake"
				buildoptions { "-std=c++98" }

	end

