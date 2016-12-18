!!ARBfp1.0
OPTION ARB_precision_hint_fastest;

PARAM lightRGB = program.local[0];
PARAM lightRange2recip = program.local[1];

# nv/ati drivers will doubtless reorder fetches to as early as possible
# since they'll block dependent ALU till they complete
# but we'll be nice and put them up here anyway  :P
TEMP base; TEX base, fragment.texcoord[0], texture[0], 2D;
TEMP bump; TEX bump, fragment.texcoord[0], texture[1], 2D;
TEMP spec; TEX spec, fragment.texcoord[0], texture[2], 2D;

# i hate ARB shaders SO much
# having to use TCs to pass everything is such a pain in the ass
ATTRIB dnLV = fragment.texcoord[4];
ATTRIB dnEV = fragment.texcoord[5];

TEMP tmp;
TEMP lv;
TEMP light;

# normalize the light vector, keeping len^2 for attenuation
DP3 tmp, dnLV, dnLV;
RSQ lv.w, tmp.w;
MUL lv.xyz, dnLV, lv.w;

MUL tmp.x, tmp.w, lightRange2recip;
SUB tmp.x, 1.0, tmp.x;
MUL light.rgb, lightRGB, tmp.x;

# passing the UNsquared reciprocal radius makes for easier-to-read code
# (and is easier to mess around with) and is only one more instruction, but...
#PARAM invR = { 0.008, 0.008, 0.008, 0.008 };
#MUL tmp, lv, invR;
#DP3 tmp, tmp, tmp;
#SUB tmp, 1.0, tmp.w;
#MUL light.rgb, lightRGB, tmp;


# respace the bump map
MAD bump.xyz, bump, 2.0, -1.0;

# even post-minification and even with compression,
# the bump maps really don't seem to need this
# what's potentially interesting is that NOT pre-normalising a bumpmap gives you FAR more accuracy
# so we may want to do that at some point
#DP3 bump.w, bump, bump;
#RSQ bump.w, bump.w;
#MUL bump.xyz, bump, bump.w;

DP3_SAT bump.w, bump, lv;


# note: Q4 specmaps are garbage: half are black, and the other half look like plastic
# because there's no specular POWER component in them
# so i'm going to just hardcode shininess till we get some decent textures
# the ones with 0 RGB will still suck anyway, but...  :/

PARAM specEXP = 16.0;

TEMP ev;
DP3 ev, dnEV, dnEV;
RSQ ev.w, ev.w;
MUL ev.xyz, dnEV, ev.w;

ADD tmp, lv, ev;
DP3 tmp.w, tmp, tmp;
RSQ tmp.w, tmp.w;
MUL tmp.xyz, tmp, tmp.w;

DP3_SAT tmp.w, bump, tmp;

POW tmp.w, tmp.w, specEXP.w;
MUL spec.rgb, spec, tmp.w;


MAD base, base, bump.w, spec;
MUL result.color.rgb, base, light;


END
