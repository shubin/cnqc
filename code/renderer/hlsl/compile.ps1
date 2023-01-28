Write-Output "Compiling..."

function Compile-Shader
{
    param
    (
        [string] $HeaderFileName,
        [string] $ShaderFileName,
        [string] $EntryPoint,
        [string] $TargetProfile,
        [parameter(ValueFromRemainingArguments = $true)] [string[]] $Passthrough
    )

    # -Zi embeds debug info
    # -Qembed_debug embeds debug info in shader container
    # -Vn header variable name
    # -WX warnings as errors
    # -O3 or -O0 optimization level
    # -Wno-warning disables the warning
    C:\Programs\dxc\bin\x64\dxc.exe -Fh $HeaderFileName -E $EntryPoint -T $TargetProfile -WX $Passthrough $ShaderFileName
}

function Compile-SMAA-VS
{
    param
    (
        [string] $HeaderFileName,
        [string] $ShaderFileName,
        [parameter(ValueFromRemainingArguments = $true)] [string[]] $Passthrough
    )

    Compile-Shader $HeaderFileName $ShaderFileName "vs" "vs_6_0" "-D SMAA_INCLUDE_VS=1" "-D SMAA_HLSL_5_1=1" "-D SMAA_RT_METRICS=rtMetrics" "-D SMAA_PRESET_HIGH=1"
}

function Compile-SMAA-PS
{
    param
    (
        [string] $HeaderFileName,
        [string] $ShaderFileName,
        [parameter(ValueFromRemainingArguments = $true)] [string[]] $Passthrough
    )

    Compile-Shader $HeaderFileName $ShaderFileName "ps" "ps_6_0" "-D SMAA_INCLUDE_PS=1" "-D SMAA_HLSL_5_1=1" "-D SMAA_RT_METRICS=rtMetrics" "-D SMAA_PRESET_HIGH=1"
}

function Compile-VS
{
    param
    (
        [string] $HeaderFileName,
        [string] $ShaderFileName,
        [parameter(ValueFromRemainingArguments = $true)] [string[]] $Passthrough
    )

    Compile-Shader $HeaderFileName $ShaderFileName "vs" "vs_6_0" "-D VERTEX_SHADER=1"
}

function Compile-PS
{
    param
    (
        [string] $HeaderFileName,
        [string] $ShaderFileName,
        [parameter(ValueFromRemainingArguments = $true)] [string[]] $Passthrough
    )

    Compile-Shader $HeaderFileName $ShaderFileName "ps" "ps_6_0" "-D PIXEL_SHADER=1"
}

function Compile-CS
{
    param
    (
        [string] $HeaderFileName,
        [string] $ShaderFileName,
        [parameter(ValueFromRemainingArguments = $true)] [string[]] $Passthrough
    )

    Compile-Shader $HeaderFileName $ShaderFileName "cs" "cs_6_0" "-D COMPUTE_SHADER=1"
}

Compile-VS "post_gamma_vs.h" "post_gamma.hlsl"
Compile-PS "post_gamma_ps.h" "post_gamma.hlsl"

Compile-VS "imgui_vs.h" "imgui.hlsl"
Compile-PS "imgui_ps.h" "imgui.hlsl"

Compile-VS "ui_vs.h" "ui.hlsl"
Compile-PS "ui_ps.h" "ui.hlsl"

Compile-VS "depth_pre_pass_vs.h" "depth_pre_pass.hlsl"
Compile-PS "depth_pre_pass_ps.h" "depth_pre_pass.hlsl"

Compile-VS "fog_vs.h" "fog_inside.hlsl"
Compile-PS "fog_inside_ps.h" "fog_inside.hlsl"
Compile-PS "fog_outside_ps.h" "fog_outside.hlsl"

Compile-CS "mip_1_cs.h" "mip_1.hlsl"
Compile-CS "mip_2_cs.h" "mip_2.hlsl"
Compile-CS "mip_3_cs.h" "mip_3.hlsl"

Compile-SMAA-VS "smaa_1_vs.h" "smaa_1.hlsl"
Compile-SMAA-PS "smaa_1_ps.h" "smaa_1.hlsl"
Compile-SMAA-VS "smaa_2_vs.h" "smaa_2.hlsl"
Compile-SMAA-PS "smaa_2_ps.h" "smaa_2.hlsl"
Compile-SMAA-VS "smaa_3_vs.h" "smaa_3.hlsl"
Compile-SMAA-PS "smaa_3_ps.h" "smaa_3.hlsl"

pause
exit
