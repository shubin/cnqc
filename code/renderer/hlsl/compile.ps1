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
        [string] $PresetMacro,
        [string] $VariableName,
        [parameter(ValueFromRemainingArguments = $true)] [string[]] $Passthrough
    )

    Compile-Shader $HeaderFileName $ShaderFileName "vs" "vs_6_0" $VariableName $PresetMacro "-D SMAA_INCLUDE_VS=1" "-D SMAA_HLSL_5_1=1" "-D SMAA_RT_METRICS=rtMetrics"
}

function Compile-SMAA-PS
{
    param
    (
        [string] $HeaderFileName,
        [string] $ShaderFileName,
        [string] $PresetMacro,
        [string] $VariableName,
        [parameter(ValueFromRemainingArguments = $true)] [string[]] $Passthrough
    )

    Compile-Shader $HeaderFileName $ShaderFileName "ps" "ps_6_0" $VariableName $PresetMacro "-D SMAA_INCLUDE_PS=1" "-D SMAA_HLSL_5_1=1" "-D SMAA_RT_METRICS=rtMetrics"
}

function Compile-SMAA
{
    param
    (
        [string] $PresetName,
        [string] $PresetMacro
    )

    $FileNamePrefix = "smaa_" + $PresetName + "_"
    $VarNamePrefix = "-Vn " + $PresetName + "_"

    Compile-SMAA-VS ($FileNamePrefix + "1_vs.h") "smaa_1.hlsl" $PresetMacro ($VarNamePrefix + "1_vs")
    Compile-SMAA-PS ($FileNamePrefix + "1_ps.h") "smaa_1.hlsl" $PresetMacro ($VarNamePrefix + "1_ps")
    Compile-SMAA-VS ($FileNamePrefix + "2_vs.h") "smaa_2.hlsl" $PresetMacro ($VarNamePrefix + "2_vs")
    Compile-SMAA-PS ($FileNamePrefix + "2_ps.h") "smaa_2.hlsl" $PresetMacro ($VarNamePrefix + "2_ps")
    Compile-SMAA-VS ($FileNamePrefix + "3_vs.h") "smaa_3.hlsl" $PresetMacro ($VarNamePrefix + "3_vs")
    Compile-SMAA-PS ($FileNamePrefix + "3_ps.h") "smaa_3.hlsl" $PresetMacro ($VarNamePrefix + "3_ps")
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

Compile-VS "post_inverse_gamma_vs.h" "post_inverse_gamma.hlsl"
Compile-PS "post_inverse_gamma_ps.h" "post_inverse_gamma.hlsl"

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

Compile-SMAA "low" "-D SMAA_PRESET_LOW=1"
Compile-SMAA "medium" "-D SMAA_PRESET_MEDIUM=1"
Compile-SMAA "high" "-D SMAA_PRESET_HIGH=1"
Compile-SMAA "ultra" "-D SMAA_PRESET_ULTRA=1"

Get-Content shared.hlsli, uber_shader.hlsl | Set-Content uber_shader.temp
./bin2header.exe --output uber_shader.h --hname uber_shader_string uber_shader.temp
rm uber_shader.temp

Get-Content smaa*.h | Set-Content complete_smaa.h

pause
exit
