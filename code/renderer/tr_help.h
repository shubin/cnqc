#define help_r_ext_max_anisotropy \
"max. allowed anisotropy ratio\n" \
"For anisotropic filtering to be enabled, this needs to be 2 or higher.\n" \
"    2 =  8- 16 tap filtering\n" \
"    4 = 16- 32 tap filtering\n" \
"    8 = 32- 64 tap filtering\n" \
"   16 = 64-128 tap filtering"

#define help_r_picmip \
"lowest allowed mip level\n" \
"Lower number means sharper textures."

#define help_r_roundImagesDown \
"allows image downscaling before texture upload\n" \
"The maximum scale ratio is 2 on both the horizontal and vertical axis."

#define help_r_mapBrightness \
"brightness of lightmap textures\n" \
"The lightmaps' brightness will be: r_mapBrightness - r_brightness."

#define help_r_mode \
"video mode to use when r_fullscreen is 1\n" \
"    0 = no video mode change, desktop resolution\n" \
"    1 = no video mode change, custom resolution, custom upscaling\n" \
"    2 = video mode change, custom resolution\n" \
"Custom resolutions use r_width and r_height.\n" \
"Custom upscaling uses r_blitMode."

#define help_r_blitMode \
"image upscaling mode for r_mode 1\n" \
"This will only be active when r_fullscreen is 1 and r_mode is 1.\n" \
"    0 = aspect-ratio preserving upscale (black bars if necessary)\n" \
"    1 = no scaling, centered\n" \
"    2 = full-screen stretching (no black bars)"

#define help_r_mode01 \
"\nOnly used when r_mode is 0 or 1 and r_fullscreen is 1."

#define help_r_subdivisions \
"tessellation step size for patch surfaces\n" \
"This sets the step size of a subdivision, *not* the number of subdivisions.\n" \
"In other words, lower values produce smoother curves."

#define help_r_gamma \
"gamma correction factor\n" \
"   <1 = darker\n" \
"    1 = no change\n" \
"   >1 = brighter"

#define help_r_lodbias \
"MD3 models LOD bias\n" \
"For all MD3 models, including player characters.\n" \
"A higher number means a higher quality loss. 0 means no loss."

#define help_r_fastsky \
"makes the sky and portals pure black\n" \
"Portal example: the bottom teleporter on q3dm7."

#define help_r_noportals \
"disables rendering of portals\n" \
"Portal example: the bottom teleporter on q3dm7."

#define help_r_textureMode \
"texture filtering mode\n" \
"    GL_NEAREST               = no filtering\n" \
"    GL_LINEAR_MIPMAP_NEAREST = bilinear filtering\n" \
"    GL_LINEAR_MIPMAP_LINEAR  = trilinear filtering\n" \
"For anisotropic filtering, refer to /help r_ext_max_anisotropy."

#define help_r_swapInterval \
"v-blanks to wait for before swapping buffers\n" \
"    0 = No V-Sync\n" \
"    1 = Synced to the monitor's refresh rate\n" \
"    2 = Synced to half the monitor's refresh rate\n" \
"    3 = Synced to one third of the monitor's refresh rate\n" \
"    N = Synced to monitor_refresh_rate / N\n" \
"It is not recommended to use V-Sync."

#define help_r_lightmap \
"renders the lightmaps only\n" \
"Shaders with a lightmap stage will only draw the lightmap stage.\n" \
"This is mutually exclusive with r_fullbright."

#define help_r_fullbright \
"renders the diffuse textures only\n" \
"Shaders with a lightmap stage will not draw the lightmap stage.\n" \
"This is mutually exclusive with r_lightmap."
