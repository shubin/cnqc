/*
Copyright (C) 2009, Kevin H Blenkinsopp
Conditions of distribution and use are as per the IJG code - see the accompanying README file

this file contains decompression data source routines for files that have
already been loaded into memory
*/

#include "jinclude.h"
#include "jpeglib.h"
#include "jerror.h"


METHODDEF void ram_noop( j_decompress_ptr cinfo )
{
}


METHODDEF boolean ram_fill_input_buffer( j_decompress_ptr cinfo )
{
	// if this gets called, either libjpeg has a logic failure or the data was corrupt
	ERREXIT( cinfo, JERR_INPUT_EOF );
	return FALSE;
}


METHODDEF void ram_skip_input_data( j_decompress_ptr cinfo, long num_bytes )
{
	cinfo->src->next_input_byte += (size_t)num_bytes;
	cinfo->src->bytes_in_buffer -= (size_t)num_bytes;
}


GLOBAL void jpeg_ram_src( j_decompress_ptr cinfo, const unsigned char* buffer, int len )
{
	if (!cinfo->src) {
		cinfo->src = (struct jpeg_source_mgr *)
			(*cinfo->mem->alloc_small)( (j_common_ptr)cinfo, JPOOL_PERMANENT, sizeof(struct jpeg_source_mgr) );
	}

	cinfo->src->next_input_byte = buffer;
	cinfo->src->bytes_in_buffer = len;

	cinfo->src->init_source = ram_noop;
	cinfo->src->fill_input_buffer = ram_fill_input_buffer;
	cinfo->src->skip_input_data = ram_skip_input_data;
	cinfo->src->resync_to_restart = jpeg_resync_to_restart;
	cinfo->src->term_source = ram_noop;
}

