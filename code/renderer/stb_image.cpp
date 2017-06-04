#include "tr_local.h"

#if defined(_MSC_VER)
#	pragma warning(disable: 4505) // unreferenced local function
#endif

static void*	q_malloc( size_t );
static void		q_free( void* );
static void*	q_realloc_sized( void*, size_t, size_t );

// we only support png and tga
#define STBI_MALLOC			q_malloc
#define STBI_REALLOC_SIZED	q_realloc_sized
#define STBI_FREE			q_free
#define STBI_NO_STDIO
#define STBI_FAILURE_USERMSG
#define STBI_NO_JPEG
#define STBI_NO_BMP
#define STBI_NO_PSD
#define STBI_NO_GIF
#define STBI_NO_HDR
#define STBI_NO_PIC
#define STBI_NO_PNM
#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"


static void* q_malloc( size_t bytes )
{
	return ri.Malloc((int)bytes);
}


static void q_free( void* buffer )
{
	if (buffer != NULL)
		ri.Free(buffer);
}


static void* q_realloc_sized( void* ptr, size_t oldSize, size_t newSize )
{
	if (ptr == NULL)
		return q_malloc(newSize);

	if (newSize <= oldSize)
		return ptr;

	void* ptr2 = q_malloc(newSize);
	memcpy(ptr2, ptr, oldSize);
	q_free(ptr);

	return ptr2;
}


qbool LoadSTB( const char* fileName, byte* buffer, int len, byte** pic, int* w, int* h, GLenum* format )
{
	int comp;
	*pic = (byte*)stbi_load_from_memory(buffer, len, w, h, &comp, 4);
	if (*pic == NULL) {
		ri.Printf(PRINT_WARNING, "stb_image: couldn't load %s: %s\n", fileName, stbi_failure_reason());
		return qfalse;
	}
		
	*format = comp == 4 ? GL_RGBA : GL_RGB;

	return qtrue;
}

