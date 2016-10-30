#include <png.h>
#include <pngstruct.h>
#include <pnginfo.h>
#include <stdlib.h>
#include <zlib.h>
#include <zconf.h>

int read_png_file(char* file_name, png_structp png_ptr)
{
	int y;
	png_byte png_header[8];
	png_infop info_ptr;
	png_uint_32 width, height;
	png_byte color_type, bit_depth;
	int pas_num;
	png_bytep *img_ptr;
	FILE *fp = fopen(file_name, "rb");
	if (!fp) return 1;
	fread(png_header, 1, 8, fp);
	if (png_sig_cmp(png_header, 0, 8)) return 1;

	png_ptr = png_create_read_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
	if (!png_ptr) return 1;

	info_ptr = png_create_info_struct(png_ptr);
	if (!info_ptr) return 1;

	if (setjmp(png_jmpbuf(png_ptr))) return 1;

	png_init_io(png_ptr, fp);
	png_set_sig_bytes(png_ptr, 8);
	png_read_info(png_ptr, info_ptr);

	width = png_get_image_width(png_ptr, info_ptr);
	height = png_get_image_height(png_ptr, info_ptr);
	color_type = png_get_color_type(png_ptr, info_ptr);
	bit_depth = png_get_bit_depth(png_ptr, info_ptr);

	pas_num = png_set_interlace_handling(png_ptr);
	png_read_update_info(png_ptr, info_ptr);

	if(setjmp(png_jmpbuf(png_ptr))) return 1;

	img_ptr = (png_bytep*)malloc(sizeof(png_bytep) * height);
	for (y = 0; y < height; y++) img_ptr[y] = (png_byte*) malloc(png_get_rowbytes(png_ptr,info_ptr));
	png_read_image(png_ptr, img_ptr);

	fclose(fp);
}