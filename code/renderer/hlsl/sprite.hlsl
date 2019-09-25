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
// depth sprite vertex and pixel shaders

cbuffer VertexShaderBuffer
{
    matrix modelViewMatrix;
    matrix projectionMatrix;
	float4 clipPlane;
};

struct VIn
{
	float4 position : POSITION;
	float4 color : COLOR0;
	float2 texCoords : TEXCOORD0;
};

struct VOut
{
	float4 position : SV_Position;
	float4 color : COLOR0;
	float2 texCoords : TEXCOORD0;
	float depthVS : DEPTHVS;
	float clipDist : SV_ClipDistance0;
};

VOut vs_main(VIn input)
{
	float4 positionVS = mul(modelViewMatrix, float4(input.position.xyz, 1));

	VOut output;
	output.position = mul(projectionMatrix, positionVS);
	output.color = input.color;
	output.texCoords = input.texCoords;
	output.depthVS = -positionVS.z;
	output.clipDist = dot(positionVS, clipPlane);

	return output;
}

cbuffer PixelShaderBuffer
{
	uint alphaTest;
	float proj22;
	float proj32;
	float additive;
	float distance;
	float offset;
	uint dummy0;
	uint dummy1;
};

Texture2D texture0 : register(t0);
SamplerState sampler0 : register(s0);

Texture2DMS<float2> textureDepth;

float LinearDepth(float zwDepth)
{
	return proj32 / (zwDepth - proj22);
}

float Contrast(float d, float power)
{
	bool aboveHalf = d > 0.5;
	float base = saturate(2.0 * (aboveHalf ? (1.0 - d) : d));
	float r = 0.5 * pow(base, power);

	return aboveHalf ? (1.0 - r) : r;
}

float4 ps_main(VOut input) : SV_TARGET
{
	float4 r = input.color * texture0.Sample(sampler0, input.texCoords);
	if((alphaTest == 1 && r.a == 0.0) ||
	   (alphaTest == 2 && r.a >= 0.5) ||
	   (alphaTest == 3 && r.a <  0.5))
		discard;

	float depthS = LinearDepth(textureDepth.Load(input.position.xy, 0).x);
	float depthP = input.depthVS - offset;
	float scale = Contrast((depthS - depthP) * distance, 2.0);
	float scaleColor = max(scale, 1.0 - additive);
	float scaleAlpha = max(scale, additive);
	float4 r2 = float4(r.rgb * scaleColor, r.a * scaleAlpha);

	return r2;
}
