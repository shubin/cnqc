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

jpeg_asm_file_names = 
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

local function GetMakePath(premakePath)

	return "../"..premakePath

end

local function GetJpegObjPath()

	local obj_format = "win32"
	if os.is("linux") then
		obj_format = "elf32"
	end

	return string.format("%s/nasm/libjpeg-turbo/%s", GetMakePath(path_build), obj_format)

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
		links { "renderer", "freetype", "libjpeg-turbo" }
	end

	filter { "system:windows" }
		if (server == 1) then
			AddSourcesFromArray(server_sources_windows)
		else
			AddSourcesFromArray(client_sources_windows)
		end
		AddHeaders("win32")

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
		
	if (server == 0 and os.is("linux")) then
		for idx,name in pairs(jpeg_asm_file_names) do
			local obj_path = string.format("%s/%s.obj", GetJpegObjPath(), name)
			linkoptions { obj_path }
		end
	end

end

local function ApplyLibJpegTurboProjectSettings()

	local jpeg_sources =
	{
		"libjpeg-turbo/jcapimin.c",
		"libjpeg-turbo/jcapistd.c",
		"libjpeg-turbo/jccoefct.c",
		"libjpeg-turbo/jccolor.c",
		"libjpeg-turbo/jcdctmgr.c",
		"libjpeg-turbo/jchuff.c",
		"libjpeg-turbo/jcinit.c",
		"libjpeg-turbo/jcmainct.c",
		"libjpeg-turbo/jcmarker.c",
		"libjpeg-turbo/jcmaster.c",
		"libjpeg-turbo/jcomapi.c",
		"libjpeg-turbo/jcparam.c",
		"libjpeg-turbo/jcphuff.c",
		"libjpeg-turbo/jcprepct.c",
		"libjpeg-turbo/jcsample.c",
		"libjpeg-turbo/jctrans.c",
		"libjpeg-turbo/jdapimin.c",
		"libjpeg-turbo/jdapistd.c",
		"libjpeg-turbo/jdatadst.c",
		"libjpeg-turbo/jdatasrc.c",
		"libjpeg-turbo/jdcoefct.c",
		"libjpeg-turbo/jdcolor.c",
		"libjpeg-turbo/jddctmgr.c",
		"libjpeg-turbo/jdhuff.c",
		"libjpeg-turbo/jdinput.c",
		"libjpeg-turbo/jdmainct.c",
		"libjpeg-turbo/jdmarker.c",
		"libjpeg-turbo/jdmaster.c",
		"libjpeg-turbo/jdmerge.c",
		"libjpeg-turbo/jdphuff.c",
		"libjpeg-turbo/jdpostct.c",
		"libjpeg-turbo/jdsample.c",
		"libjpeg-turbo/jdtrans.c",
		"libjpeg-turbo/jerror.c",
		"libjpeg-turbo/jfdctflt.c",
		"libjpeg-turbo/jfdctfst.c",
		"libjpeg-turbo/jfdctint.c",
		"libjpeg-turbo/jidctflt.c",
		"libjpeg-turbo/jidctfst.c",
		"libjpeg-turbo/jidctint.c",
		"libjpeg-turbo/jidctred.c",
		"libjpeg-turbo/jquant1.c",
		"libjpeg-turbo/jquant2.c",
		"libjpeg-turbo/jutils.c",
		"libjpeg-turbo/jmemmgr.c",
		"libjpeg-turbo/simd/jsimd_i386.c"
	}

	AddSourcesFromArray(jpeg_sources)
	includedirs { path_src.."/libjpeg-turbo", path_src.."/libjpeg-turbo/simd" }
	defines { "WITH_SIMD", "SIZEOF_SIZE_T=4" }

	local asm_inc_path = GetMakePath(path_src.."/libjpeg-turbo")
	local nasm_flags;
	if os.is("windows") then
		asm_inc_path = path.translate(asm_inc_path, "\\")
		nasm_flags = string.format("-fwin32 -DWIN32 -I%s\\ -I%s\\win\\ -I%s\\simd\\", asm_inc_path, asm_inc_path, asm_inc_path)
	else
		nasm_flags = string.format("-felf32 -DELF -I%s/ -I%s/win/ -I%s/simd/", asm_inc_path, asm_inc_path, asm_inc_path)
	end
	
	-- the very first pre-build step is to make sure the output directory exists
	-- no, NASM will not create it for us if it doesn't exist
	if os.is("windows") then
		local obj_path = path.translate(string.format("%s/", GetJpegObjPath()), "\\")
		prebuildcommands { string.format("if not exist \"%s\" mkdir \"%s\"", obj_path, obj_path) }
	else
		prebuildcommands { string.format("mkdir -p %s", GetJpegObjPath()) }
	end
	
	for idx,name in pairs(jpeg_asm_file_names) do
		local src_path = string.format("%s/libjpeg-turbo/simd/%s.asm", GetMakePath(path_src), name)
		local obj_path = string.format("%s/%s.obj", GetJpegObjPath(), name)
		if os.is("windows") then
			obj_path = path.translate(obj_path, "\\")
		end
		prebuildcommands { string.format("echo %s.asm && nasm -o%s %s %s ", name, obj_path, nasm_flags, src_path) }
		-- on Linux, we link those directly against the client
		if os.is("windows") then
			linkoptions { obj_path }
		end
	end
	
	extra_warnings = 0
	ApplyLibProjectSettings()
	extra_warnings = 1

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
