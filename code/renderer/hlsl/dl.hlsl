/*
===========================================================================
Copyright (C) 2019 Gian 'myT' Schellenbaum

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
// dynamic light vertex and pixel shaders

cbuffer VertexShaderBuffer
{
    matrix modelViewMatrix;
    matrix projectionMatrix;
	float4 clipPlane;
	float4 osLightPos;
	float4 osEyePos;
};

struct VIn
{
	float4 position : POSITION;
	float4 normal : NORMAL;
	float4 color : COLOR0;
	float2 texCoords : TEXCOORD0;
};

struct VOut
{
	float4 position : SV_Position;
	float3 normal : NORMAL;
	float4 color : COLOR0;
	float2 texCoords : TEXCOORD0;
	float3 osLightVec : TEXCOORD1;
	float3 osEyeVec : TEXCOORD2;
	float clipDist : SV_ClipDistance0;
};

VOut vs_main(VIn input)
{
	float4 positionVS = mul(modelViewMatrix, float4(input.position.xyz, 1));

	VOut output;
	output.position = mul(projectionMatrix, positionVS);
	output.normal = input.normal.xyz;
	output.color = input.color;
	output.texCoords = input.texCoords;
	output.osLightVec = osLightPos.xyz - input.position.xyz;
	output.osEyeVec = osEyePos.xyz - input.position.xyz;
	output.clipDist = dot(positionVS, clipPlane);

	return output;
}

cbuffer PixelShaderBuffer
{
	float3 lightColor;
	float lightRadius;
	uint alphaTest;
	uint dummy[3];
};

Texture2D texture0 : register(t0);
SamplerState sampler0 : register(s0);

float4 ps_main(VOut input) : SV_TARGET
{
	float4 base = texture0.Sample(sampler0, input.texCoords) * input.color;
	if((alphaTest == 1 && base.a == 0.0) ||
	   (alphaTest == 2 && base.a >= 0.5) ||
	   (alphaTest == 3 && base.a <  0.5))
	   discard;

	float3 nL = normalize(input.osLightVec); // normalized object-space light vector
	float3 nV = normalize(input.osEyeVec); // normalized object-space view vector
	float3 nN = input.normal; // normalized object-space normal vector

	// light intensity
	float intensFactor = dot(input.osLightVec, input.osLightVec) * lightRadius;
	float3 intens = lightColor * (1.0 - intensFactor);

	// specular reflection term (N.H)
	float specFactor = clamp(dot(nN, normalize(nL + nV)), 0.0, 1.0);
	float spec = pow(specFactor, 16.0) * 0.25;

	// Lambertian diffuse reflection term (N.L)
	float diffuse = clamp(dot(nN, nL), 0.0, 1.0);
	float4 final = (base * float4(diffuse.rrr, 1.0) + float4(spec.rrr, 1.0)) * float4(intens.rgb, 1.0);

	return final;
}
