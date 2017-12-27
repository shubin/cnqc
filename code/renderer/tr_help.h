#define help_r_ext_max_anisotropy \
"max. allowed anisotropy ratio\n" \
"For anisotropic filtering to be enabled, this needs to be 2 or higher.\n" \
S_COLOR_VAL "    2 " S_COLOR_HELP "=  8- 16 tap filtering\n" \
S_COLOR_VAL "    4 " S_COLOR_HELP "= 16- 32 tap filtering\n" \
S_COLOR_VAL "    8 " S_COLOR_HELP "= 32- 64 tap filtering\n" \
S_COLOR_VAL "   16 " S_COLOR_HELP "= 64-128 tap filtering"

#define help_r_picmip \
"lowest allowed mip level\n" \
"Lower number means sharper textures."

#define help_r_roundImagesDown \
"allows image downscaling before texture upload\n" \
"The maximum scale ratio is 2 on both the horizontal and vertical axis."

#define help_r_mode \
"video mode to use with " S_COLOR_CVAR "r_fullscreen " S_COLOR_VAL "1\n" \
S_COLOR_VAL "    0 " S_COLOR_HELP "= No video mode change, desktop resolution\n" \
S_COLOR_VAL "    1 " S_COLOR_HELP "= No video mode change, custom resolution, custom upscaling\n" \
S_COLOR_VAL "    2 " S_COLOR_HELP "= Video mode change, custom resolution\n" \
"Custom resolution uses " S_COLOR_CVAR "r_width " S_COLOR_HELP "and " S_COLOR_CVAR "r_height" S_COLOR_HELP ".\n" \
"Custom upscaling uses " S_COLOR_CVAR "r_blitMode" S_COLOR_HELP "."

#define help_r_blitMode \
"image upscaling mode for " S_COLOR_CVAR "r_mode " S_COLOR_VAL "1\n" \
"This will only be active with " S_COLOR_CVAR "r_fullscreen " S_COLOR_VAL "1 " S_COLOR_HELP "and " S_COLOR_CVAR "r_mode " S_COLOR_VAL "1" S_COLOR_HELP ".\n" \
S_COLOR_VAL "    0 " S_COLOR_HELP "= Aspect-ratio preserving upscale (black bars if necessary)\n" \
S_COLOR_VAL "    1 " S_COLOR_HELP "= No scaling, centered\n" \
S_COLOR_VAL "    2 " S_COLOR_HELP "= Full-screen stretching (no black bars)"

#define help_r_subdivisions \
"tessellation step size for patch surfaces\n" \
"This sets the step size of a subdivision, *not* the number of subdivisions.\n" \
"In other words, lower values produce smoother curves."

#define help_r_gamma \
"gamma correction factor\n" \
S_COLOR_HELP "   <" S_COLOR_VAL "1 " S_COLOR_HELP "= Darker\n" \
S_COLOR_VAL "    1 " S_COLOR_HELP "= No change\n" \
S_COLOR_HELP "   >" S_COLOR_VAL "1 " S_COLOR_HELP "= Brighter"

#define help_r_lodbias \
"MD3 models LOD bias\n" \
"For all MD3 models, including player characters.\n" \
"A higher number means a higher quality loss. " S_COLOR_VAL "0 " S_COLOR_HELP "means no loss."

#define help_r_fastsky \
"makes the sky and portals pure black\n" \
"Portal example: the bottom teleporter on q3dm7."

#define help_r_noportals \
"disables rendering of portals\n" \
"Portal example: the bottom teleporter on q3dm7."

#define help_r_textureMode \
"texture filtering mode\n" \
S_COLOR_VAL "    GL_NEAREST               " S_COLOR_HELP "= No filtering\n" \
S_COLOR_VAL "    GL_LINEAR_MIPMAP_NEAREST " S_COLOR_HELP "= Bilinear filtering\n" \
S_COLOR_VAL "    GL_LINEAR_MIPMAP_LINEAR  " S_COLOR_HELP "= Trilinear filtering\n" \
"For anisotropic filtering, refer to " S_COLOR_CVAR "r_ext_max_anisotropy" S_COLOR_HELP "."

#define help_r_swapInterval \
"v-blanks to wait for before swapping buffers\n" \
S_COLOR_VAL "    0 " S_COLOR_HELP "= No V-Sync\n" \
S_COLOR_VAL "    1 " S_COLOR_HELP "= Synced to the monitor's refresh rate\n" \
S_COLOR_VAL "    2 " S_COLOR_HELP "= Synced to half the monitor's refresh rate\n" \
S_COLOR_VAL "    3 " S_COLOR_HELP "= Synced to one third of the monitor's refresh rate\n" \
S_COLOR_VAL "    N " S_COLOR_HELP "= Synced to monitor_refresh_rate / " S_COLOR_VAL "N\n" \
S_COLOR_HELP "It is not recommended to use V-Sync."

#define help_r_lightmap \
"renders the lightmaps only\n" \
"Shaders with a lightmap stage will only draw the lightmap stage.\n" \
"This is mutually exclusive with " S_COLOR_CVAR "r_fullbright" S_COLOR_HELP "."

#define help_r_fullbright \
"renders the diffuse textures only\n" \
"Shaders with a lightmap stage will not draw the lightmap stage.\n" \
"This is mutually exclusive with " S_COLOR_CVAR "r_lightmap" S_COLOR_HELP "."
