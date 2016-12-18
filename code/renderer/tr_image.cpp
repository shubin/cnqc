/*
===========================================================================
Copyright (C) 1999-2005 Id Software, Inc.

This file is part of Quake III Arena source code.

Quake III Arena source code is free software; you can redistribute it
and/or modify it under the terms of the GNU General Public License as
published by the Free Software Foundation; either version 2 of the License,
or (at your option) any later version.

Quake III Arena source code is distributed in the hope that it will be
useful, but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with Quake III Arena source code; if not, write to the Free Software
Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
===========================================================================
*/
// tr_image.c
#include "tr_local.h"


#define IMAGE_HASH_SIZE 1024
static image_t* hashTable[IMAGE_HASH_SIZE];


static byte s_intensitytable[256];
static byte s_gammatable[256];

void R_GammaCorrect( byte* buffer, int bufSize )
{
	for (int i = 0; i < bufSize; ++i) {
		buffer[i] = s_gammatable[buffer[i]];
	}
}


static int gl_filter_min = GL_LINEAR_MIPMAP_NEAREST;
static int gl_filter_max = GL_LINEAR;

typedef struct {
	const char* name;
	int minimize, maximize;
} textureMode_t;

static const textureMode_t modes[] = {
	{ "GL_NEAREST", GL_NEAREST, GL_NEAREST },
	{ "GL_LINEAR", GL_LINEAR, GL_LINEAR },
	{ "GL_NEAREST_MIPMAP_NEAREST", GL_NEAREST_MIPMAP_NEAREST, GL_NEAREST },
	{ "GL_LINEAR_MIPMAP_NEAREST", GL_LINEAR_MIPMAP_NEAREST, GL_LINEAR },
	{ "GL_NEAREST_MIPMAP_LINEAR", GL_NEAREST_MIPMAP_LINEAR, GL_NEAREST },
	{ "GL_LINEAR_MIPMAP_LINEAR", GL_LINEAR_MIPMAP_LINEAR, GL_LINEAR },
	{ 0 }
};

void GL_TextureMode( const char* string )
{
	int i;

	for (i = 0; modes[i].name; ++i) {
		if ( !Q_stricmp( modes[i].name, string ) ) {
			break;
		}
	}

	if (!modes[i].name) {
		ri.Printf( PRINT_ALL, "bad filter name\n" );
		return;
	}

	gl_filter_min = modes[i].minimize;
	gl_filter_max = modes[i].maximize;

	// change all the existing mipmap texture objects
	for ( i = 0 ; i < tr.numImages ; i++ ) {
		const image_t* glt = tr.images[ i ];
		if ( !(glt->flags & IMG_NOMIPMAP) ) {
			GL_Bind( glt );
			qglTexParameterf( GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, gl_filter_min );
			qglTexParameterf( GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, gl_filter_max );
		}
	}
}


void R_ImageList_f( void )
{
	int i, vram = 0;

	ri.Printf( PRINT_ALL, "\nwide high MPI W format name\n" );

	for ( i = 0; i < tr.numImages; ++i ) {
		const image_t* image = tr.images[i];
		GL_Bind( image );

		GLint compressed;
		qglGetTexLevelParameteriv( GL_TEXTURE_2D, 0, GL_TEXTURE_COMPRESSED_ARB, &compressed );
		if (compressed)
			qglGetTexLevelParameteriv( GL_TEXTURE_2D, 0, GL_TEXTURE_COMPRESSED_IMAGE_SIZE_ARB, &compressed );
		else
			compressed = image->width * image->height;

		if ( !(image->flags & IMG_NOMIPMAP) && (image->width > 1) && (image->height > 1) )
			vram += compressed * 1.33f; // will overestimate, but that's what we want anyway
		vram += compressed;

		ri.Printf( PRINT_ALL, "%4i %4i %c%c%c ",
			image->width, image->height,
			(image->flags & IMG_NOMIPMAP) ? ' ' : 'M',
			(image->flags & IMG_NOPICMIP) ? ' ' : 'P',
			(image->flags & IMG_NOIMANIP) ? ' ' : 'I'
			);

		switch ( image->wrapClampMode ) {
		case GL_REPEAT: ri.Printf( PRINT_ALL, "R " ); break;
		case GL_CLAMP:  ri.Printf( PRINT_ALL, "C " ); break;
		case GL_CLAMP_TO_EDGE: ri.Printf( PRINT_ALL, "E " ); break;
		default: ri.Printf( PRINT_ALL, "? " ); break;
		}

		switch ( image->format ) {
		case GL_RGB:   ri.Printf( PRINT_ALL, "RGB   " ); break;
		case GL_RGB5:  ri.Printf( PRINT_ALL, "RGB5  " ); break;
		case GL_RGB8:  ri.Printf( PRINT_ALL, "RGB8  " ); break;
		case GL_RGBA:  ri.Printf( PRINT_ALL, "RGBA  " ); break;
		case GL_RGBA4: ri.Printf( PRINT_ALL, "RGBA4 " ); break;
		case GL_RGBA8: ri.Printf( PRINT_ALL, "RGBA8 " ); break;
		case GL_LUMINANCE_ALPHA: ri.Printf( PRINT_ALL, "L8A8  " ); break;
		case GL_COMPRESSED_RGB_S3TC_DXT1_EXT: ri.Printf( PRINT_ALL, "DXT1  " ); break;
		default:
			ri.Printf( PRINT_ALL, "%5i ", image->format );
			break;
		}

		ri.Printf( PRINT_ALL, " %s\n", image->name );
	}

	ri.Printf( PRINT_ALL, "---------\n" );
	ri.Printf( PRINT_ALL, "%i images\n", tr.numImages );
	// just assume/pretend that everything is 4-component
	ri.Printf( PRINT_ALL, "Estimated VRAM use: %iMB\n\n", vram / (1024 * 1024 / 4) );
}


///////////////////////////////////////////////////////////////

/*
================
Used to resample images in a more general than quartering fashion.

This will only be filtered properly if the resampled size
is greater than half the original size.

If a larger shrinking is needed, use the mipmap function 
before or after.
================
*/
static void ResampleTexture( unsigned *in, int inwidth, int inheight, unsigned *out,
							int outwidth, int outheight ) {
	int		i, j;
	unsigned	*inrow, *inrow2;
	unsigned	frac, fracstep;
	unsigned	p1[2048], p2[2048];
	byte		*pix1, *pix2, *pix3, *pix4;

	if (outwidth>2048)
		ri.Error(ERR_DROP, "ResampleTexture: max width");

	fracstep = inwidth*0x10000/outwidth;

	frac = fracstep>>2;
	for ( i=0 ; i<outwidth ; i++ ) {
		p1[i] = 4*(frac>>16);
		frac += fracstep;
	}
	frac = 3*(fracstep>>2);
	for ( i=0 ; i<outwidth ; i++ ) {
		p2[i] = 4*(frac>>16);
		frac += fracstep;
	}

	for (i=0 ; i<outheight ; i++, out += outwidth) {
		inrow = in + inwidth*(int)((i+0.25)*inheight/outheight);
		inrow2 = in + inwidth*(int)((i+0.75)*inheight/outheight);
		frac = fracstep >> 1;
		for (j=0 ; j<outwidth ; j++) {
			pix1 = (byte *)inrow + p1[j];
			pix2 = (byte *)inrow + p2[j];
			pix3 = (byte *)inrow2 + p1[j];
			pix4 = (byte *)inrow2 + p2[j];
			((byte *)(out+j))[0] = (pix1[0] + pix2[0] + pix3[0] + pix4[0])>>2;
			((byte *)(out+j))[1] = (pix1[1] + pix2[1] + pix3[1] + pix4[1])>>2;
			((byte *)(out+j))[2] = (pix1[2] + pix2[2] + pix3[2] + pix4[2])>>2;
			((byte *)(out+j))[3] = (pix1[3] + pix2[3] + pix3[3] + pix4[3])>>2;
		}
	}
}


// scale up the pixel values in a texture to increase the lighting range

static void R_LightScaleTexture( unsigned* in, int width, int height, qbool only_gamma )
{
	byte* p = (byte*)in;
	int i, c = width * height;

	if ( only_gamma )
	{
		if ( !glConfig.deviceSupportsGamma )
		{
			for (i=0 ; i<c ; i++, p+=4)
			{
				p[0] = s_gammatable[p[0]];
				p[1] = s_gammatable[p[1]];
				p[2] = s_gammatable[p[2]];
			}
		}
	}
	else
	{
		if ( glConfig.deviceSupportsGamma )
		{
			for (i=0 ; i<c ; i++, p+=4)
			{
				p[0] = s_intensitytable[p[0]];
				p[1] = s_intensitytable[p[1]];
				p[2] = s_intensitytable[p[2]];
			}
		}
		else
		{
			for (i=0 ; i<c ; i++, p+=4)
			{
				p[0] = s_gammatable[s_intensitytable[p[0]]];
				p[1] = s_gammatable[s_intensitytable[p[1]]];
				p[2] = s_gammatable[s_intensitytable[p[2]]];
			}
		}
	}
}


// operates in place, quartering the size of the texture - proper linear filter

static void R_MipMap( unsigned* in, int inWidth, int inHeight )
{
	int			i, j, k;
	byte		*outpix;
	int			total;

	int outWidth = inWidth >> 1;
	int outHeight = inHeight >> 1;
	unsigned* temp = (unsigned*)ri.Hunk_AllocateTempMemory( outWidth * outHeight * 4 );

	int inWidthMask = inWidth - 1;
	int inHeightMask = inHeight - 1;

	for ( i = 0 ; i < outHeight ; i++ ) {
		for ( j = 0 ; j < outWidth ; j++ ) {
			outpix = (byte *) ( temp + i * outWidth + j );
			for ( k = 0 ; k < 4 ; k++ ) {
				total = 
					1 * ((byte *)&in[ ((i*2-1)&inHeightMask)*inWidth + ((j*2-1)&inWidthMask) ])[k] +
					2 * ((byte *)&in[ ((i*2-1)&inHeightMask)*inWidth + ((j*2)&inWidthMask) ])[k] +
					2 * ((byte *)&in[ ((i*2-1)&inHeightMask)*inWidth + ((j*2+1)&inWidthMask) ])[k] +
					1 * ((byte *)&in[ ((i*2-1)&inHeightMask)*inWidth + ((j*2+2)&inWidthMask) ])[k] +

					2 * ((byte *)&in[ ((i*2)&inHeightMask)*inWidth + ((j*2-1)&inWidthMask) ])[k] +
					4 * ((byte *)&in[ ((i*2)&inHeightMask)*inWidth + ((j*2)&inWidthMask) ])[k] +
					4 * ((byte *)&in[ ((i*2)&inHeightMask)*inWidth + ((j*2+1)&inWidthMask) ])[k] +
					2 * ((byte *)&in[ ((i*2)&inHeightMask)*inWidth + ((j*2+2)&inWidthMask) ])[k] +

					2 * ((byte *)&in[ ((i*2+1)&inHeightMask)*inWidth + ((j*2-1)&inWidthMask) ])[k] +
					4 * ((byte *)&in[ ((i*2+1)&inHeightMask)*inWidth + ((j*2)&inWidthMask) ])[k] +
					4 * ((byte *)&in[ ((i*2+1)&inHeightMask)*inWidth + ((j*2+1)&inWidthMask) ])[k] +
					2 * ((byte *)&in[ ((i*2+1)&inHeightMask)*inWidth + ((j*2+2)&inWidthMask) ])[k] +

					1 * ((byte *)&in[ ((i*2+2)&inHeightMask)*inWidth + ((j*2-1)&inWidthMask) ])[k] +
					2 * ((byte *)&in[ ((i*2+2)&inHeightMask)*inWidth + ((j*2)&inWidthMask) ])[k] +
					2 * ((byte *)&in[ ((i*2+2)&inHeightMask)*inWidth + ((j*2+1)&inWidthMask) ])[k] +
					1 * ((byte *)&in[ ((i*2+2)&inHeightMask)*inWidth + ((j*2+2)&inWidthMask) ])[k];
				outpix[k] = total / 36;
			}
		}
	}

	Com_Memcpy( in, temp, outWidth * outHeight * 4 );
	ri.Hunk_FreeTempMemory( temp );
}


// apply a color blend over a set of pixels - used for r_colorMipLevels

static void R_BlendOverTexture( byte *data, int pixelCount, const byte blend[4] )
{
	int premult[3];
	int inverseAlpha = 255 - blend[3];

	premult[0] = blend[0] * blend[3];
	premult[1] = blend[1] * blend[3];
	premult[2] = blend[2] * blend[3];

	for (int i = 0; i < pixelCount; ++i, data+=4) {
		data[0] = ( data[0] * inverseAlpha + premult[0] ) >> 9;
		data[1] = ( data[1] * inverseAlpha + premult[1] ) >> 9;
		data[2] = ( data[2] * inverseAlpha + premult[2] ) >> 9;
	}
}

static const byte mipBlendColors[16][4] = {
	{0,0,0,0},
	{255,0,0,128},
	{0,255,0,128},
	{0,0,255,128},
	{255,0,0,128},
	{0,255,0,128},
	{0,0,255,128},
	{255,0,0,128},
	{0,255,0,128},
	{0,0,255,128},
	{255,0,0,128},
	{0,255,0,128},
	{0,0,255,128},
	{255,0,0,128},
	{0,255,0,128},
	{0,0,255,128},
};


// note that the "32" here is for the image's STRIDE - it has nothing to do with the actual COMPONENTS

static void Upload32( image_t* image, unsigned int* data )
{
	int scaled_width, scaled_height;

	// convert to exact power of 2 sizes
	//
	for ( scaled_width = 1; scaled_width < image->width; scaled_width <<= 1 )
		;
	for ( scaled_height = 1; scaled_height < image->height; scaled_height <<=1 )
		;
	if ( r_roundImagesDown->integer && scaled_width > image->width )
		scaled_width >>= 1;
	if ( r_roundImagesDown->integer && scaled_height > image->height )
		scaled_height >>= 1;

	RI_AutoPtr pResampled;
	if ( scaled_width != image->width || scaled_height != image->height ) {
		pResampled.Alloc( scaled_width * scaled_height * 4 );
		ResampleTexture( data, image->width, image->height, pResampled.Get<unsigned int>(), scaled_width, scaled_height );
		data = pResampled.Get<unsigned int>();
		image->width = scaled_width;
		image->height = scaled_height;
	}

	// perform optional picmip operation
	if ( !(image->flags & IMG_NOPICMIP) ) {
		scaled_width >>= r_picmip->integer;
		scaled_height >>= r_picmip->integer;
	}

	// clamp to minimum size
	scaled_width = max( scaled_width, 1 );
	scaled_height = max( scaled_height, 1 );

	// clamp to the current upper OpenGL limit
	// scale both axis down equally so we don't have to
	// deal with a half mip resampling
	//
	while ( scaled_width > glInfo.maxTextureSize || scaled_height > glInfo.maxTextureSize ) {
		scaled_width >>= 1;
		scaled_height >>= 1;
	}

	// validate and/or override the internal format
	switch (image->format) {
	case GL_RGB:
		/* if ( flags & IMG_NOIMANIP ) {
			;
		} else */
		if ( r_ext_compressed_textures->integer ) {
			image->format = GL_COMPRESSED_RGB_ARB;
		}
		else if ( r_texturebits->integer == 16 ) {
			image->format = GL_RGB5;
		}
		else if ( r_texturebits->integer == 32 ) {
			image->format = GL_RGB8;
		}
		break;

	case GL_RGBA:
		/* compressed RGBA *still* somehow manages to suck ass, even with DXT5
		if ( r_ext_compressed_textures->integer ) {
			format = GL_COMPRESSED_RGBA_ARB;
		} else */
		if ( r_texturebits->integer == 16 ) {
			image->format = GL_RGBA4;
		}
		else if ( r_texturebits->integer == 32 ) {
			image->format = GL_RGBA8;
		}
		break;

	case GL_LUMINANCE_ALPHA:
		break;

	default:
		ri.Error( ERR_DROP, "Upload32: Invalid format %d\n", image->format );
	}

	RI_AutoPtr pScaled( sizeof(unsigned) * scaled_width * scaled_height );
	// copy or resample data as appropriate for first MIP level
	if ( ( scaled_width == image->width ) && ( scaled_height == image->height ) ) {
		if ( image->flags & IMG_NOMIPMAP ) {
			if ( !(image->flags & IMG_NOIMANIP) )
				R_LightScaleTexture( data, scaled_width, scaled_height, qtrue );
			qglTexImage2D( GL_TEXTURE_2D, 0, image->format, image->width, image->height, 0, GL_RGBA, GL_UNSIGNED_BYTE, data );
			goto done;
		}
		Com_Memcpy( pScaled, data, image->width * image->height * 4 );
	}
	else
	{
		// use the normal mip-mapping function to go down from here
		while ( image->width > scaled_width || image->height > scaled_height ) {
			R_MipMap( (unsigned*)data, image->width, image->height );
			image->width = max( image->width >> 1, 1 );
			image->height = max( image->height >> 1, 1 );
		}
		Com_Memcpy( pScaled, data, image->width * image->height * 4 );
	}

	if ( !(image->flags & IMG_NOIMANIP) )
		R_LightScaleTexture( pScaled.Get<unsigned int>(), scaled_width, scaled_height, (image->flags & IMG_NOMIPMAP) );

	qglTexImage2D( GL_TEXTURE_2D, 0, image->format, scaled_width, scaled_height, 0, GL_RGBA, GL_UNSIGNED_BYTE, pScaled );

	if ( !(image->flags & IMG_NOMIPMAP) )
	{
		int miplevel = 0;
		while (scaled_width > 1 || scaled_height > 1)
		{
			R_MipMap( pScaled.Get<unsigned>(), scaled_width, scaled_height );
			scaled_width = max( scaled_width >> 1, 1 );
			scaled_height = max( scaled_height >> 1, 1 );
			++miplevel;

			if ( r_colorMipLevels->integer )
				R_BlendOverTexture( pScaled, scaled_width * scaled_height, mipBlendColors[miplevel] );

			qglTexImage2D( GL_TEXTURE_2D, miplevel, image->format, scaled_width, scaled_height, 0, GL_RGBA, GL_UNSIGNED_BYTE, pScaled );
		}
	}

done:

	qglGetTexLevelParameteriv( GL_TEXTURE_2D, 0, GL_TEXTURE_INTERNAL_FORMAT, (GLint*)&image->format );

	if (r_ext_max_anisotropy->integer > 1)
		qglTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MAX_ANISOTROPY_EXT, r_ext_max_anisotropy->integer );

	if ( image->flags & IMG_NOMIPMAP ) {
		qglTexParameterf( GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR );
		qglTexParameterf( GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR );
	} else {
		qglTexParameterf( GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, gl_filter_min );
		qglTexParameterf( GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, gl_filter_max );
	}

	GL_CheckErrors();
}


// this is the only way any image_t are created
// !!! i'm pretty sure this DOESN'T work correctly for non-POT images

image_t* R_CreateImage( const char* name, byte* pic, int width, int height, GLenum format, int flags, int glWrapClampMode )
{
	if (strlen(name) >= MAX_QPATH)
		ri.Error( ERR_DROP, "R_CreateImage: \"%s\" is too long\n", name );

	if ( tr.numImages == MAX_DRAWIMAGES )
		ri.Error( ERR_DROP, "R_CreateImage: MAX_DRAWIMAGES hit\n" );

	image_t* image = tr.images[tr.numImages] = RI_New<image_t>();
	qglGenTextures( 1, &image->texnum );
	tr.numImages++;

	strcpy( image->name, name );

	image->format = format;
	image->flags = flags;

	image->width = width;
	image->height = height;
	image->wrapClampMode = glWrapClampMode;

	GL_Bind( image );
	Upload32( image, (unsigned int*)pic );

	qglTexParameterf( GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, glWrapClampMode );
	qglTexParameterf( GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, glWrapClampMode );

	// KHB  there are times we have no interest in naming an image at all (notably, font glyphs)
	// but atm the rest of the system is too dependent on everything being named
	//if (name) {
		int hash = Q_FileHash(name, IMAGE_HASH_SIZE);
		image->next = hashTable[hash];
		hashTable[hash] = image;
	//}

	return image;
}


static qbool LoadTGA( const char* name, byte** pic, int* w, int* h, GLenum* format )
{
	*pic = NULL;

	byte* buffer;
	ri.FS_ReadFile( name, (void**)&buffer );
	if (!buffer)
		return qfalse;

	byte* p = buffer;

	TargaHeader targa_header;
	targa_header.id_length = p[0];
	targa_header.colormap_type = p[1];
	targa_header.image_type = p[2];
	targa_header.width = LittleShort( *(short*)&p[12] );
	targa_header.height = LittleShort( *(short*)&p[14] );
	targa_header.pixel_size = p[16];
	targa_header.attributes = p[17];

	p += sizeof(TargaHeader);

	if ((targa_header.image_type != 2) && (targa_header.image_type != 10) && (targa_header.image_type != 3))
		ri.Error( ERR_DROP, "LoadTGA %s: Only type 2 and 10 (RGB/A) and 3 (gray) images supported\n", name );

	if ( targa_header.colormap_type )
		ri.Error( ERR_DROP, "LoadTGA %s: Colormaps not supported\n", name );

	if ( ( targa_header.pixel_size != 32 && targa_header.pixel_size != 24 ) && targa_header.image_type != 3 )
		ri.Error( ERR_DROP, "LoadTGA %s: Only 32 or 24 bit color images supported\n", name );

	*format = (targa_header.pixel_size == 32) ? GL_RGBA : GL_RGB;

	int columns = targa_header.width;
	int rows = targa_header.height;
	unsigned numBytes = columns * rows * 4;

	*w = columns;
	*h = rows;

	if ( !columns || !rows || (numBytes > 0x7FFFFFFF) || (numBytes / columns / 4 != rows) )
		ri.Error( ERR_DROP, "LoadTGA: %s has an invalid image size\n", name );

	*pic = (byte*)ri.Malloc(numBytes);
	byte* dst;

	if (targa_header.id_length != 0)
		p += targa_header.id_length;  // skip TARGA image comment

	byte pixel[4] = { 0, 0, 0, 255 };
	int bpp = (targa_header.pixel_size >> 3);

	// uncompressed greyscale - expand it into fake-RGB
	if (targa_header.image_type == 3) {
		for (int y = rows-1; y >= 0; --y) {
			dst = *pic + y*columns*4;
			for (int x = 0; x < columns; ++x) {
				pixel[0] = pixel[1] = pixel[2] = *p++;
			}
		}
	}

	// one of these days we'll actually just use GL_BGRA_EXT, but until then...
	#define UNMUNGE_TGA_PIXEL { *dst++ = pixel[2]; *dst++ = pixel[1]; *dst++ = pixel[0]; *dst++ = pixel[3]; }

	// uncompressed BGR(A)
	if (targa_header.image_type == 2) {
		for (int y = rows-1; y >= 0; --y) {
			dst = *pic + y*columns*4;
			for (int x = 0; x < columns; ++x) {
				for (int i = 0; i < bpp; ++i)
					pixel[i] = *p++;
				UNMUNGE_TGA_PIXEL;
			}
		}
	}

	#define WRAP_TGA if ((++x == columns) && y--) { x = 0; dst = *pic + y*columns*4; }

	// RLE BGR(A)
	if (targa_header.image_type == 10) {
		int y = rows-1;
		while (y >= 0) {
			dst = *pic + y*columns*4;
			int x = 0;
			while (x < columns) {
				int rle = *p++;
				int n = 1 + (rle & 0x7F);
				if (rle & 0x80) { // RLE packet, 1 pixel n times
					for (int i = 0; i < bpp; ++i)
						pixel[i] = *p++;
					while (n--) {
						UNMUNGE_TGA_PIXEL;
						WRAP_TGA;
					}
				}
				else while (n--) { // n distinct pixels
					for (int i = 0; i < bpp; ++i)
						pixel[i] = *p++;
					UNMUNGE_TGA_PIXEL;
					WRAP_TGA;
				}
			}
		}
	}

	#undef WRAP_TGA

	#undef UNMUNGE_TGA_PIXEL

	if (targa_header.attributes & 0x20)
		ri.Printf( PRINT_WARNING, "WARNING: '%s' TGA file header declares top-down image, ignoring\n", name );

	ri.FS_FreeFile( buffer );
	return qtrue;
}


///////////////////////////////////////////////////////////////


extern "C"
{
	#define JPEG_INTERNALS
	#include "../jpeg-6/jpeglib.h"

	void* jpeg_get_small( j_common_ptr cinfo, size_t sizeofobject ) { return (void*)ri.Malloc(sizeofobject); }
	void jpeg_free_small( j_common_ptr cinfo, void* object, size_t sizeofobject ) { ri.Free(object); }
	void* jpeg_get_large( j_common_ptr cinfo, size_t sizeofobject ) { return jpeg_get_small( cinfo, sizeofobject ); }
	void jpeg_free_large( j_common_ptr cinfo, void* object, size_t sizeofobject ) { jpeg_free_small( cinfo, object, sizeofobject ); }

	void error_exit( j_common_ptr cinfo )
	{
		char buffer[JMSG_LENGTH_MAX];
		(*cinfo->err->format_message)(cinfo, buffer);
		jpeg_destroy(cinfo);
		ri.Error( ERR_FATAL, "%s\n", buffer );
	}

	void output_message( j_common_ptr cinfo )
	{
		char buffer[JMSG_LENGTH_MAX];
		(*cinfo->err->format_message)(cinfo, buffer);
		ri.Printf(PRINT_ALL, "%s\n", buffer);
	}
	typedef void (*jcallback)(j_common_ptr cinfo);

	jcallback error_exit_ptr = &error_exit;
	jcallback output_message_ptr = &output_message;

	typedef void* (*jmem_get)( j_common_ptr cinfo, size_t sizeofobject );
	typedef void (*jmem_free)( j_common_ptr cinfo, void* object, size_t sizeofobject );

	jmem_get jpeg_get_small_ptr = &jpeg_get_small;
	jmem_free jpeg_free_small_ptr = &jpeg_free_small;
	jmem_get jpeg_get_large_ptr = &jpeg_get_large;
	jmem_free jpeg_free_large_ptr = &jpeg_free_large;
};


static qbool LoadJPG( const char* name, byte** pic, int* w, int* h )
{
	struct jpeg_decompress_struct cinfo;
	struct jpeg_error_mgr jerr;

	cinfo.err = jpeg_std_error( &jerr );
	jpeg_create_decompress( &cinfo );

	byte* buffer;
	int len = ri.FS_ReadFile( name, (void**)&buffer );
	if (!buffer)
		return qfalse;

	jpeg_ram_src( &cinfo, buffer, len );

	jpeg_read_header( &cinfo, TRUE );
	jpeg_start_decompress( &cinfo );

	unsigned numBytes = cinfo.output_width * cinfo.output_height * 4;
	*pic = (byte*)ri.Malloc(numBytes);
	*w = cinfo.output_width;
	*h = cinfo.output_height;

	JSAMPROW row_pointer[1];
	unsigned row_stride = cinfo.output_width * cinfo.output_components;
	while (cinfo.output_scanline < cinfo.output_height) {
		row_pointer[0] = *pic + (row_stride * cinfo.output_scanline);
		jpeg_read_scanlines( &cinfo, row_pointer, 1 );
	}

	jpeg_finish_decompress( &cinfo );
	jpeg_destroy_decompress( &cinfo );

	ri.FS_FreeFile( buffer );
	return qtrue;
}


// custom destination object for memory output

typedef struct {
	struct jpeg_destination_mgr pub;
	byte*	buffer;
	int		size;
	int		datacount;
} my_destination_mgr;


static void jpg_init_destination( j_compress_ptr cinfo )
{
	my_destination_mgr* dest = (my_destination_mgr*)cinfo->dest;
	dest->pub.next_output_byte = dest->buffer;
	dest->pub.free_in_buffer = dest->size;
}


static boolean jpg_empty_output_buffer( j_compress_ptr cinfo )
{
	return TRUE;
}


static void jpg_term_destination( j_compress_ptr cinfo )
{
	my_destination_mgr* dest = (my_destination_mgr*)cinfo->dest;
	dest->datacount = dest->size - dest->pub.free_in_buffer;
}


static void jpg_set_destination( j_compress_ptr cinfo, byte* buffer, int size )
{
	if (!cinfo->dest) {
		cinfo->dest = (struct jpeg_destination_mgr *)
			(*cinfo->mem->alloc_small)( (j_common_ptr)cinfo, JPOOL_PERMANENT, sizeof(my_destination_mgr) );
	}

	my_destination_mgr* dest = (my_destination_mgr*)cinfo->dest;
	dest->pub.init_destination = jpg_init_destination;
	dest->pub.empty_output_buffer = jpg_empty_output_buffer;
	dest->pub.term_destination = jpg_term_destination;
	dest->buffer = buffer;
	dest->size = size;
}


static void jpg_start_compress( j_compress_ptr cinfo )
{
	if (cinfo->global_state != CSTATE_START)
		ERREXIT1( cinfo, JERR_BAD_STATE, cinfo->global_state );

	jpeg_suppress_tables( cinfo, FALSE );

	(*cinfo->err->reset_error_mgr)( (j_common_ptr)cinfo );
	(*cinfo->dest->init_destination)( cinfo );

	jinit_compress_master( cinfo );

	(*cinfo->master->prepare_for_pass)( cinfo );
	cinfo->next_scanline = 0;
	cinfo->global_state = (cinfo->raw_data_in ? CSTATE_RAW_OK : CSTATE_SCANNING);

	// libjpeg fails to do this automatically by deliberate poor design
	if (cinfo->master->call_pass_startup)
		(*cinfo->master->pass_startup)( cinfo );
}


static JDIMENSION jpg_write_scanlines( j_compress_ptr cinfo, JSAMPARRAY scanlines, JDIMENSION num_lines )
{
	if (cinfo->global_state != CSTATE_SCANNING)
		ERREXIT1( cinfo, JERR_BAD_STATE, cinfo->global_state );
	if (cinfo->next_scanline >= cinfo->image_height)
		ERREXIT( cinfo, JWRN_TOO_MUCH_DATA );

	JDIMENSION row_ctr = 0;
	(*cinfo->main->process_data)( cinfo, scanlines, &row_ctr, num_lines );
	cinfo->next_scanline += row_ctr;
	return row_ctr;
}


int SaveJPGToBuffer( byte* out, int quality, int image_width, int image_height, byte* image_buffer )
{
	struct jpeg_compress_struct cinfo;
	struct jpeg_error_mgr jerr;

	cinfo.err = jpeg_std_error( &jerr );
	jpeg_create_compress( &cinfo );

	jpg_set_destination( &cinfo, out, image_width * image_height * 4 );

	cinfo.image_width = image_width;
	cinfo.image_height = image_height;
	cinfo.input_components = 4;
	cinfo.in_color_space = JCS_RGB;

	jpeg_set_defaults( &cinfo );
	jpeg_set_quality( &cinfo, quality, TRUE );

	jpg_start_compress( &cinfo );

	JSAMPROW row_pointer[1];
	unsigned row_stride = image_width * cinfo.input_components;
	while (cinfo.next_scanline < cinfo.image_height) {
		row_pointer[0] = & image_buffer[((cinfo.image_height-1)*row_stride)-cinfo.next_scanline * row_stride];
		jpg_write_scanlines( &cinfo, row_pointer, 1 );
	}

	jpeg_finish_compress( &cinfo );

	int csize = ((my_destination_mgr*)cinfo.dest)->datacount;

	jpeg_destroy_compress( &cinfo );

	return csize;
}


///////////////////////////////////////////////////////////////


static void R_LoadImage( const char* name, byte** pic, int* w, int* h, GLenum* format )
{
	*pic = NULL;

	*w = 0;
	*h = 0;

	int len = strlen(name);
	if (len < 5) {
		ri.Printf( PRINT_ALL, "ERROR: invalid image name %s\n", name );
		return;
	}

	if (!Q_stricmp( name+len-4, ".tga" ) && LoadTGA( name, pic, w, h, format ))
		return;

	*format = GL_RGB;
#if defined(_DEBUG)
	// either this is REALLY a jpg, or just as likely some moron got the extension wrong
	if (!Q_stricmp( name+len-4, ".jpg" ) && LoadJPG( name, pic, w, h ))
		return;

	ri.Printf( PRINT_DEVELOPER, "WARNING: idiot has misnamed %s\n", name );
#endif

	char altname[MAX_QPATH];
	Q_strncpyz( altname, name, sizeof(altname) );
	len = strlen( altname );
	altname[len-3] = 'j';
	altname[len-2] = 'p';
	altname[len-1] = 'g';
	LoadJPG( altname, pic, w, h );

	return;
}


// finds or loads the given image - returns NULL if it fails, not a default image

const image_t* R_FindImageFile( const char* name, int flags, int glWrapClampMode )
{
	if (!name)
		return NULL;

	int hash = Q_FileHash(name, IMAGE_HASH_SIZE);

	// see if the image is already loaded
	//
	image_t* image;
	for (image=hashTable[hash]; image; image=image->next) {
		if ( !strcmp( name, image->name ) ) {
			/* since this WASN'T enforced as an error, half the shaders out there (including most of id's)
				have been geting it wrong for years and this is just useless noise
			// the white image can be used with any set of parms, but other mismatches are errors
			if ( strcmp( name, "*white" ) ) {
				if ( image->mipmap != mipmap ) {
					ri.Printf( PRINT_DEVELOPER, "WARNING: reused image %s with mixed mipmap parm\n", name );
				}
				if ( image->allowPicmip != allowPicmip ) {
					ri.Printf( PRINT_DEVELOPER, "WARNING: reused image %s with mixed allowPicmip parm\n", name );
				}
				if ( image->wrapClampMode != glWrapClampMode ) {
					ri.Printf( PRINT_ALL, "WARNING: reused image %s with mixed glWrapClampMode parm\n", name );
				}
			}
			*/
			return image;
		}
	}

	// load the pic from disk
	//
	byte* pic;
	int width, height;
	GLenum format;
	R_LoadImage( name, &pic, &width, &height, &format );

	if (!pic)
		return NULL;

	image = R_CreateImage( name, pic, width, height, format, flags, glWrapClampMode );
	ri.Free( pic );
	return image;
}


void R_InitFogTable()
{
	const float exp = 0.5;

	for (int i = 0; i < FOG_TABLE_SIZE; ++i) {
		tr.fogTable[i] = pow( (float)i/(FOG_TABLE_SIZE-1), exp );
	}
}


/*
Returns a 0.0 to 1.0 fog density value
This is called for each texel of the fog texture on startup
and for each vertex of transparent shaders in fog dynamically
*/
float R_FogFactor( float s, float t )
{
	s -= 1.0/512;
	if ( s < 0 ) {
		return 0;
	}
	if ( t < 1.0/32 ) {
		return 0;
	}
	if ( t < 31.0/32 ) {
		s *= (t - 1.0f/32.0f) / (30.0f/32.0f);
	}

	// we need to leave a lot of clamp range
	s *= 8;

	if ( s > 1.0 ) {
		s = 1.0;
	}

	return tr.fogTable[ (int)(s * (FOG_TABLE_SIZE-1)) ];
}


static void R_CreateFogImage()
{
	const int FOG_S = 256;
	const int FOG_T = 32;

	RI_AutoPtr ap( FOG_S * FOG_T * 4 );
	byte* p = ap;

	// S is distance, T is depth
	for (int x = 0; x < FOG_S; ++x) {
		for (int y = 0; y < FOG_T; ++y) {
			float d = R_FogFactor( ( x + 0.5f ) / FOG_S, ( y + 0.5f ) / FOG_T );
			p[(y*FOG_S+x)*4+0] = p[(y*FOG_S+x)*4+1] = p[(y*FOG_S+x)*4+2] = 255;
			p[(y*FOG_S+x)*4+3] = 255*d;
		}
	}

	tr.fogImage = R_CreateImage( "*fog", p, FOG_S, FOG_T, GL_RGBA, IMG_NOPICMIP, GL_CLAMP_TO_EDGE );
	qglTexParameterfv( GL_TEXTURE_2D, GL_TEXTURE_BORDER_COLOR, colorWhite );
}


static void R_CreateDefaultImage()
{
	const int DEFAULT_SIZE = 16;
	byte data[DEFAULT_SIZE][DEFAULT_SIZE][4];

	// the default image is a box showing increasing s and t
	Com_Memset( data, 32, sizeof( data ) );

	for ( int i = 0; i < DEFAULT_SIZE; ++i ) {
		byte b = (byte)( 64 + (128 * i / DEFAULT_SIZE) );
		data[0][i][0] = b;
		data[0][i][3] = 255;
		data[i][0][1] = b;
		data[i][0][3] = 255;
		data[i][i][0] = data[i][i][1] = b;
		data[i][i][3] = 255;
	}

	tr.defaultImage = R_CreateImage( "*default", (byte*)data, DEFAULT_SIZE, DEFAULT_SIZE, GL_RGBA, IMG_NOPICMIP, GL_REPEAT );
}


static void R_CreateBuiltinImages()
{
	int i;
	byte data[4];

	R_CreateDefaultImage();

	// we use a solid white image instead of disabling texturing
	Com_Memset( data, 255, 4 );
	tr.whiteImage = R_CreateImage( "*white", data, 1, 1, GL_RGBA, IMG_NOMIPMAP, GL_REPEAT );

	data[0] = 128;
	data[1] = 128;
	tr.flatImage = R_CreateImage( "*flat", data, 1, 1, GL_RGBA, IMG_NOMIPMAP | IMG_NOIMANIP, GL_REPEAT );

	Com_Memset( data, 64, 4 );
	tr.shinyImage = R_CreateImage( "*shiny", data, 1, 1, GL_RGBA, IMG_NOMIPMAP | IMG_NOIMANIP, GL_REPEAT );

	// scratchimages usually used for cinematic drawing (signal-quality effects)
	// these are just placeholders: RE_StretchRaw will regenerate them when it wants them
	for (i = 0; i < 16; ++i) // MAX_VIDEO_HANDLES
		tr.scratchImage[i] = R_CreateImage( "*scratch", data, 1, 1, GL_RGBA, IMG_NOMIPMAP | IMG_NOPICMIP, GL_CLAMP );

	R_CreateFogImage();
}


void R_SetColorMappings()
{
	// allow 2 overbright bits in 24 bit, but only 1 in 16 bit
	tr.overbrightBits = Com_Clamp( 0, (glConfig.colorBits > 16) ? 2 : 1, r_overBrightBits->integer );

	// setup the overbright lighting - needs hw support and fullscreen
	if (!glConfig.deviceSupportsGamma || !glInfo.isFullscreen)
		tr.overbrightBits = 0;

	tr.identityLight = 1.0f / (1 << tr.overbrightBits);
	tr.identityLightByte = 255 * tr.identityLight;

	if ( r_intensity->value < 1 )
		ri.Cvar_Set( "r_intensity", "1" );

	if ( r_gamma->value < 0.5f )
		ri.Cvar_Set( "r_gamma", "0.5" );
	if ( r_gamma->value > 3.0f )
		ri.Cvar_Set( "r_gamma", "3.0" );

	for (int i = 0; i < 256; ++i) {
		int n = 255 * pow( i / 255.0f, 1.0f / r_gamma->value ) + 0.5f;
		s_gammatable[i] = Com_Clamp( 0, 255, n << tr.overbrightBits );
		s_intensitytable[i] = (byte)min( r_intensity->value * i, 255.0f );
	}

	if (glConfig.deviceSupportsGamma)
		GLimp_SetGamma( s_gammatable, s_gammatable, s_gammatable );
}


void R_InitImages()
{
	Com_Memset( hashTable, 0, sizeof(hashTable) );
	R_SetColorMappings(); // build brightness translation tables
	R_CreateBuiltinImages(); // create default textures (white, fog, etc)
}


void R_DeleteTextures()
{
	for ( int i = 0; i < tr.numImages; ++i )
		qglDeleteTextures( 1, &tr.images[i]->texnum );

	tr.numImages = 0;
	Com_Memset( tr.images, 0, sizeof( tr.images ) );
	Com_Memset( glState.texID, 0, sizeof( glState.texID ) );

	for ( int i = MAX_TMUS - 1; i >= 0; --i ) {
		GL_SelectTexture( i );
		qglBindTexture( GL_TEXTURE_2D, 0 );
	}
}


/*
============================================================================

SKINS

============================================================================
*/


// unfortunatly, skin files aren't compatible with our normal parsing rules. oops  :/

static const char* CommaParse( const char** data )
{
	static char com_token[MAX_TOKEN_CHARS];

	int c = 0;
	const char* p = *data;

	while (*p && (*p < 32))
		++p;

	while ((*p > 32) && (*p != ',') && (c < MAX_TOKEN_CHARS-1))
		com_token[c++] = *p++;

	*data = p;
	com_token[c] = 0;
	return com_token;
}


qhandle_t RE_RegisterSkin( const char* name )
{
	if (!name || !name[0] || (strlen(name) >= MAX_QPATH))
		ri.Error( ERR_DROP, "RE_RegisterSkin: invalid name [%s]\n", name ? name : "NULL" );

	skin_t* skin;
	qhandle_t hSkin;
	// see if the skin is already loaded
	for (hSkin = 1; hSkin < tr.numSkins; ++hSkin) {
		skin = tr.skins[hSkin];
		if ( !Q_stricmp( skin->name, name ) ) {
			return (skin->numSurfaces ? hSkin : 0);
		}
	}

	// allocate a new skin
	if ( tr.numSkins == MAX_SKINS ) {
		ri.Printf( PRINT_WARNING, "WARNING: RE_RegisterSkin( '%s' ) MAX_SKINS hit\n", name );
		return 0;
	}

	tr.numSkins++;
	skin = RI_New<skin_t>();
	tr.skins[hSkin] = skin;
	Q_strncpyz( skin->name, name, sizeof( skin->name ) );
	skin->numSurfaces = 0;

	// make sure the render thread is stopped
	// KHB why? we're not uploading anything...   R_SyncRenderThread();

	// if not a .skin file, load as a single shader
	if ( Q_stricmpn( name + strlen( name ) - 5, ".skin", 6 ) ) {
		skin->numSurfaces = 1;
		skin->surfaces[0] = RI_New<skinSurface_t>();
		skin->surfaces[0]->shader = R_FindShader( name, LIGHTMAP_NONE, qtrue );
		return hSkin;
	}

	char* text;
	// load and parse the skin file
	ri.FS_ReadFile( name, (void **)&text );
	if (!text)
		return 0;

	const char* token;
	const char* p = text;
	char surfName[MAX_QPATH];

	while (p && *p) {
		// get surface name
		token = CommaParse( &p );
		Q_strncpyz( surfName, token, sizeof( surfName ) );

		if ( !token[0] )
			break;

		// lowercase the surface name so skin compares are faster
		Q_strlwr( surfName );

		if (*p == ',') 
			++p;

		if ( strstr( token, "tag_" ) )
			continue;

		// parse the shader name
		token = CommaParse( &p );

		skinSurface_t* surf = skin->surfaces[ skin->numSurfaces ] = RI_New<skinSurface_t>();
		Q_strncpyz( surf->name, surfName, sizeof( surf->name ) );
		surf->shader = R_FindShader( token, LIGHTMAP_NONE, qtrue );
		skin->numSurfaces++;
	}

	ri.FS_FreeFile( text );

	return (skin->numSurfaces ? hSkin : 0); // never let a skin have 0 shaders
}


void R_InitSkins()
{
	tr.numSkins = 1;

	// make the default skin have all default shaders
	tr.skins[0] = RI_New<skin_t>();
	tr.skins[0]->numSurfaces = 1;
	tr.skins[0]->surfaces[0] = RI_New<skinSurface_t>();
	tr.skins[0]->surfaces[0]->shader = tr.defaultShader;
	Q_strncpyz( tr.skins[0]->name, "<default skin>", sizeof( tr.skins[0]->name ) );
}


const skin_t* R_GetSkinByHandle( qhandle_t hSkin )
{
	return ((hSkin > 0) && (hSkin < tr.numSkins) ? tr.skins[hSkin] : tr.skins[0]);
}


void R_SkinList_f( void )
{
	ri.Printf( PRINT_ALL, "------------------\n" );

	for (int i = 0; i < tr.numSkins; ++i) {
		const skin_t* skin = tr.skins[i];

		ri.Printf( PRINT_ALL, "%3i:%s\n", i, skin->name );
		for (int j = 0; j < skin->numSurfaces; ++j) {
			ri.Printf( PRINT_ALL, "       %s = %s\n",
				skin->surfaces[j]->name, skin->surfaces[j]->shader->name );
		}
	}

	ri.Printf( PRINT_ALL, "------------------\n" );
}

