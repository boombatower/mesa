/**************************************************************************
 * 
 * Copyright 2008 VMware, Inc.
 * Copyright (c) 2008 VMware, Inc.
 * All Rights Reserved.
 * 
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sub license, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 * 
 * The above copyright notice and this permission notice (including the
 * next paragraph) shall be included in all copies or substantial portions
 * of the Software.
 * 
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
 * OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT.
 * IN NO EVENT SHALL VMWARE AND/OR ITS SUPPLIERS BE LIABLE FOR
 * ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
 * TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
 * SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 * 
 **************************************************************************/


#include "pipe/p_config.h" 

#include "pipe/p_compiler.h"
#include "util/u_debug.h" 
#include "pipe/p_format.h" 
#include "pipe/p_state.h" 
#include "util/u_inlines.h" 
#include "util/u_format.h"
#include "util/u_memory.h" 
#include "util/u_string.h" 
#include "util/u_math.h" 
#include "util/u_tile.h" 
#include "util/u_prim.h"
#include "util/u_surface.h"
#include <inttypes.h>

#include <stdio.h>
#include <limits.h> /* CHAR_BIT */
#include <ctype.h> /* isalnum */

#ifdef _WIN32
#include <windows.h>
#include <stdlib.h>
#endif


void _debug_vprintf(const char *format, va_list ap)
{
   static char buf[4096] = {'\0'};
#if defined(PIPE_OS_WINDOWS) || defined(PIPE_SUBSYSTEM_EMBEDDED)
   /* We buffer until we find a newline. */
   size_t len = strlen(buf);
   int ret = util_vsnprintf(buf + len, sizeof(buf) - len, format, ap);
   if(ret > (int)(sizeof(buf) - len - 1) || util_strchr(buf + len, '\n')) {
      os_log_message(buf);
      buf[0] = '\0';
   }
#else
   util_vsnprintf(buf, sizeof(buf), format, ap);
   os_log_message(buf);
#endif
}


void
debug_disable_error_message_boxes(void)
{
#ifdef _WIN32
   /* When Windows' error message boxes are disabled for this process (as is
    * typically the case when running tests in an automated fashion) we disable
    * CRT message boxes too.
    */
   UINT uMode = SetErrorMode(0);
   SetErrorMode(uMode);
   if (uMode & SEM_FAILCRITICALERRORS) {
      /* Disable assertion failure message box.
       * http://msdn.microsoft.com/en-us/library/sas1dkb2.aspx
       */
      _set_error_mode(_OUT_TO_STDERR);
#ifdef _MSC_VER
      /* Disable abort message box.
       * http://msdn.microsoft.com/en-us/library/e631wekh.aspx
       */
      _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
#endif
   }
#endif /* _WIN32 */
}


#ifdef DEBUG
void debug_print_blob( const char *name,
                       const void *blob,
                       unsigned size )
{
   const unsigned *ublob = (const unsigned *)blob;
   unsigned i;

   debug_printf("%s (%d dwords%s)\n", name, size/4,
                size%4 ? "... plus a few bytes" : "");

   for (i = 0; i < size/4; i++) {
      debug_printf("%d:\t%08x\n", i, ublob[i]);
   }
}
#endif


static boolean
debug_get_option_should_print(void)
{
   static boolean first = TRUE;
   static boolean value = FALSE;

   if (!first)
      return value;

   /* Oh hey this will call into this function,
    * but its cool since we set first to false
    */
   first = FALSE;
   value = debug_get_bool_option("GALLIUM_PRINT_OPTIONS", FALSE);
   /* XXX should we print this option? Currently it wont */
   return value;
}

const char *
debug_get_option(const char *name, const char *dfault)
{
   const char *result;

   result = os_get_option(name);
   if(!result)
      result = dfault;

   if (debug_get_option_should_print())
      debug_printf("%s: %s = %s\n", __FUNCTION__, name, result ? result : "(null)");
   
   return result;
}

boolean
debug_get_bool_option(const char *name, boolean dfault)
{
   const char *str = os_get_option(name);
   boolean result;
   
   if(str == NULL)
      result = dfault;
   else if(!util_strcmp(str, "n"))
      result = FALSE;
   else if(!util_strcmp(str, "no"))
      result = FALSE;
   else if(!util_strcmp(str, "0"))
      result = FALSE;
   else if(!util_strcmp(str, "f"))
      result = FALSE;
   else if(!util_strcmp(str, "F"))
      result = FALSE;
   else if(!util_strcmp(str, "false"))
      result = FALSE;
   else if(!util_strcmp(str, "FALSE"))
      result = FALSE;
   else
      result = TRUE;

   if (debug_get_option_should_print())
      debug_printf("%s: %s = %s\n", __FUNCTION__, name, result ? "TRUE" : "FALSE");
   
   return result;
}


long
debug_get_num_option(const char *name, long dfault)
{
   long result;
   const char *str;
   
   str = os_get_option(name);
   if(!str)
      result = dfault;
   else {
      long sign;
      char c;
      c = *str++;
      if(c == '-') {
	 sign = -1;
	 c = *str++;
      } 
      else {
	 sign = 1;
      }
      result = 0;
      while('0' <= c && c <= '9') {
	 result = result*10 + (c - '0');
	 c = *str++;
      }
      result *= sign;
   }

   if (debug_get_option_should_print())
      debug_printf("%s: %s = %li\n", __FUNCTION__, name, result);

   return result;
}

static boolean str_has_option(const char *str, const char *name)
{
   /* Empty string. */
   if (!*str) {
      return FALSE;
   }

   /* OPTION=all */
   if (!util_strcmp(str, "all")) {
      return TRUE;
   }

   /* Find 'name' in 'str' surrounded by non-alphanumeric characters. */
   {
      const char *start = str;
      unsigned name_len = strlen(name);

      /* 'start' is the beginning of the currently-parsed word,
       * we increment 'str' each iteration.
       * if we find either the end of string or a non-alphanumeric character,
       * we compare 'start' up to 'str-1' with 'name'. */

      while (1) {
         if (!*str || !(isalnum(*str) || *str == '_')) {
            if (str-start == name_len &&
                !memcmp(start, name, name_len)) {
               return TRUE;
            }

            if (!*str) {
               return FALSE;
            }

            start = str+1;
         }

         str++;
      }
   }

   return FALSE;
}

uint64_t
debug_get_flags_option(const char *name, 
                       const struct debug_named_value *flags,
                       uint64_t dfault)
{
   uint64_t result;
   const char *str;
   const struct debug_named_value *orig = flags;
   unsigned namealign = 0;
   
   str = os_get_option(name);
   if(!str)
      result = dfault;
   else if (!util_strcmp(str, "help")) {
      result = dfault;
      _debug_printf("%s: help for %s:\n", __FUNCTION__, name);
      for (; flags->name; ++flags)
         namealign = MAX2(namealign, strlen(flags->name));
      for (flags = orig; flags->name; ++flags)
         _debug_printf("| %*s [0x%0*"PRIx64"]%s%s\n", namealign, flags->name,
                      (int)sizeof(uint64_t)*CHAR_BIT/4, flags->value,
                      flags->desc ? " " : "", flags->desc ? flags->desc : "");
   }
   else {
      result = 0;
      while( flags->name ) {
	 if (str_has_option(str, flags->name))
	    result |= flags->value;
	 ++flags;
      }
   }

   if (debug_get_option_should_print()) {
      if (str) {
         debug_printf("%s: %s = 0x%"PRIx64" (%s)\n", __FUNCTION__, name, result, str);
      } else {
         debug_printf("%s: %s = 0x%"PRIx64"\n", __FUNCTION__, name, result);
      }
   }

   return result;
}


void _debug_assert_fail(const char *expr, 
                        const char *file, 
                        unsigned line, 
                        const char *function) 
{
   _debug_printf("%s:%u:%s: Assertion `%s' failed.\n", file, line, function, expr);
   os_abort();
}


const char *
debug_dump_enum(const struct debug_named_value *names, 
                unsigned long value)
{
   static char rest[64];
   
   while(names->name) {
      if(names->value == value)
	 return names->name;
      ++names;
   }

   util_snprintf(rest, sizeof(rest), "0x%08lx", value);
   return rest;
}


const char *
debug_dump_enum_noprefix(const struct debug_named_value *names, 
                         const char *prefix,
                         unsigned long value)
{
   static char rest[64];
   
   while(names->name) {
      if(names->value == value) {
         const char *name = names->name;
         while (*name == *prefix) {
            name++;
            prefix++;
         }
         return name;
      }
      ++names;
   }

   

   util_snprintf(rest, sizeof(rest), "0x%08lx", value);
   return rest;
}


const char *
debug_dump_flags(const struct debug_named_value *names, 
                 unsigned long value)
{
   static char output[4096];
   static char rest[256];
   int first = 1;

   output[0] = '\0';

   while(names->name) {
      if((names->value & value) == names->value) {
	 if (!first)
	    util_strncat(output, "|", sizeof(output) - strlen(output) - 1);
	 else
	    first = 0;
	 util_strncat(output, names->name, sizeof(output) - strlen(output) - 1);
	 output[sizeof(output) - 1] = '\0';
	 value &= ~names->value;
      }
      ++names;
   }
   
   if (value) {
      if (!first)
	 util_strncat(output, "|", sizeof(output) - strlen(output) - 1);
      else
	 first = 0;
      
      util_snprintf(rest, sizeof(rest), "0x%08lx", value);
      util_strncat(output, rest, sizeof(output) - strlen(output) - 1);
      output[sizeof(output) - 1] = '\0';
   }
   
   if(first)
      return "0";
   
   return output;
}


#ifdef DEBUG
void debug_print_format(const char *msg, unsigned fmt )
{
   debug_printf("%s: %s\n", msg, util_format_name(fmt));
}
#endif


/** Return string name of given primitive type */
const char *
u_prim_name(unsigned prim)
{
   static const struct debug_named_value names[] = {
      DEBUG_NAMED_VALUE(PIPE_PRIM_POINTS),
      DEBUG_NAMED_VALUE(PIPE_PRIM_LINES),
      DEBUG_NAMED_VALUE(PIPE_PRIM_LINE_LOOP),
      DEBUG_NAMED_VALUE(PIPE_PRIM_LINE_STRIP),
      DEBUG_NAMED_VALUE(PIPE_PRIM_TRIANGLES),
      DEBUG_NAMED_VALUE(PIPE_PRIM_TRIANGLE_STRIP),
      DEBUG_NAMED_VALUE(PIPE_PRIM_TRIANGLE_FAN),
      DEBUG_NAMED_VALUE(PIPE_PRIM_QUADS),
      DEBUG_NAMED_VALUE(PIPE_PRIM_QUAD_STRIP),
      DEBUG_NAMED_VALUE(PIPE_PRIM_POLYGON),
      DEBUG_NAMED_VALUE(PIPE_PRIM_LINES_ADJACENCY),
      DEBUG_NAMED_VALUE(PIPE_PRIM_LINE_STRIP_ADJACENCY),
      DEBUG_NAMED_VALUE(PIPE_PRIM_TRIANGLES_ADJACENCY),
      DEBUG_NAMED_VALUE(PIPE_PRIM_TRIANGLE_STRIP_ADJACENCY),
      DEBUG_NAMED_VALUE_END
   };
   return debug_dump_enum(names, prim);
}



#ifdef DEBUG
int fl_indent = 0;
const char* fl_function[1024];

int debug_funclog_enter(const char* f, const int line, const char* file)
{
   int i;

   for (i = 0; i < fl_indent; i++)
      debug_printf("  ");
   debug_printf("%s\n", f);

   assert(fl_indent < 1023);
   fl_function[fl_indent++] = f;

   return 0;
}

void debug_funclog_exit(const char* f, const int line, const char* file)
{
   --fl_indent;
   assert(fl_indent >= 0);
   assert(fl_function[fl_indent] == f);
}

void debug_funclog_enter_exit(const char* f, const int line, const char* file)
{
   int i;
   for (i = 0; i < fl_indent; i++)
      debug_printf("  ");
   debug_printf("%s\n", f);
}
#endif



#ifdef DEBUG
/**
 * Dump an image to .ppm file.
 * \param format  PIPE_FORMAT_x
 * \param cpp  bytes per pixel
 * \param width  width in pixels
 * \param height height in pixels
 * \param stride  row stride in bytes
 */
void debug_dump_image(const char *prefix,
                      enum pipe_format format, unsigned cpp,
                      unsigned width, unsigned height,
                      unsigned stride,
                      const void *data)     
{
   /* write a ppm file */
   char filename[256];
   unsigned char *rgb8;
   FILE *f;

   util_snprintf(filename, sizeof(filename), "%s.ppm", prefix);

   rgb8 = MALLOC(height * width * 3);
   if (!rgb8) {
      return;
   }

   util_format_translate(
         PIPE_FORMAT_R8G8B8_UNORM,
         rgb8, width * 3,
         0, 0,
         format,
         data, stride,
         0, 0, width, height);

   /* Must be opened in binary mode or DOS line ending causes data
    * to be read with one byte offset.
    */
   f = fopen(filename, "wb");
   if (f) {
      fprintf(f, "P6\n");
      fprintf(f, "# ppm-file created by gallium\n");
      fprintf(f, "%i %i\n", width, height);
      fprintf(f, "255\n");
      fwrite(rgb8, 1, height * width * 3, f);
      fclose(f);
   }
   else {
      fprintf(stderr, "Can't open %s for writing\n", filename);
   }

   FREE(rgb8);
}

/* FIXME: dump resources, not surfaces... */
void debug_dump_surface(struct pipe_context *pipe,
                        const char *prefix,
                        struct pipe_surface *surface)
{
   struct pipe_resource *texture;
   struct pipe_transfer *transfer;
   void *data;

   if (!surface)
      return;

   /* XXX: this doesn't necessarily work, as the driver may be using
    * temporary storage for the surface which hasn't been propagated
    * back into the texture.  Need to nail down the semantics of views
    * and transfers a bit better before we can say if extra work needs
    * to be done here:
    */
   texture = surface->texture;

   data = pipe_transfer_map(pipe, texture, surface->u.tex.level,
                            surface->u.tex.first_layer,
                            PIPE_TRANSFER_READ,
                            0, 0, surface->width, surface->height, &transfer);
   if(!data)
      return;

   debug_dump_image(prefix,
                    texture->format,
                    util_format_get_blocksize(texture->format),
                    util_format_get_nblocksx(texture->format, surface->width),
                    util_format_get_nblocksy(texture->format, surface->height),
                    transfer->stride,
                    data);

   pipe->transfer_unmap(pipe, transfer);
}


void debug_dump_texture(struct pipe_context *pipe,
                        const char *prefix,
                        struct pipe_resource *texture)
{
   struct pipe_surface *surface, surf_tmpl;

   if (!texture)
      return;

   /* XXX for now, just dump image for layer=0, level=0 */
   u_surface_default_template(&surf_tmpl, texture);
   surface = pipe->create_surface(pipe, texture, &surf_tmpl);
   if (surface) {
      debug_dump_surface(pipe, prefix, surface);
      pipe->surface_destroy(pipe, surface);
   }
}


#pragma pack(push,2)
struct bmp_file_header {
   uint16_t bfType;
   uint32_t bfSize;
   uint16_t bfReserved1;
   uint16_t bfReserved2;
   uint32_t bfOffBits;
};
#pragma pack(pop)

struct bmp_info_header {
   uint32_t biSize;
   int32_t biWidth;
   int32_t biHeight;
   uint16_t biPlanes;
   uint16_t biBitCount;
   uint32_t biCompression;
   uint32_t biSizeImage;
   int32_t biXPelsPerMeter;
   int32_t biYPelsPerMeter;
   uint32_t biClrUsed;
   uint32_t biClrImportant;
};

struct bmp_rgb_quad {
   uint8_t rgbBlue;
   uint8_t rgbGreen;
   uint8_t rgbRed;
   uint8_t rgbAlpha;
};

void
debug_dump_surface_bmp(struct pipe_context *pipe,
                       const char *filename,
                       struct pipe_surface *surface)
{
   struct pipe_transfer *transfer;
   struct pipe_resource *texture = surface->texture;
   void *ptr;

   ptr = pipe_transfer_map(pipe, texture, surface->u.tex.level,
                           surface->u.tex.first_layer, PIPE_TRANSFER_READ,
                           0, 0, surface->width, surface->height, &transfer);

   debug_dump_transfer_bmp(pipe, filename, transfer, ptr);

   pipe->transfer_unmap(pipe, transfer);
}

void
debug_dump_transfer_bmp(struct pipe_context *pipe,
                        const char *filename,
                        struct pipe_transfer *transfer, void *ptr)
{
   float *rgba;

   if (!transfer)
      goto error1;

   rgba = MALLOC(transfer->box.width *
		 transfer->box.height *
		 transfer->box.depth *
		 4*sizeof(float));
   if(!rgba)
      goto error1;

   pipe_get_tile_rgba(transfer, ptr, 0, 0,
                      transfer->box.width, transfer->box.height,
                      rgba);

   debug_dump_float_rgba_bmp(filename,
                             transfer->box.width, transfer->box.height,
                             rgba, transfer->box.width);

   FREE(rgba);
error1:
   ;
}

void
debug_dump_float_rgba_bmp(const char *filename,
                          unsigned width, unsigned height,
                          float *rgba, unsigned stride)
{
   FILE *stream;
   struct bmp_file_header bmfh;
   struct bmp_info_header bmih;
   unsigned x, y;

   if(!rgba)
      goto error1;

   bmfh.bfType = 0x4d42;
   bmfh.bfSize = 14 + 40 + height*width*4;
   bmfh.bfReserved1 = 0;
   bmfh.bfReserved2 = 0;
   bmfh.bfOffBits = 14 + 40;

   bmih.biSize = 40;
   bmih.biWidth = width;
   bmih.biHeight = height;
   bmih.biPlanes = 1;
   bmih.biBitCount = 32;
   bmih.biCompression = 0;
   bmih.biSizeImage = height*width*4;
   bmih.biXPelsPerMeter = 0;
   bmih.biYPelsPerMeter = 0;
   bmih.biClrUsed = 0;
   bmih.biClrImportant = 0;

   stream = fopen(filename, "wb");
   if(!stream)
      goto error1;

   fwrite(&bmfh, 14, 1, stream);
   fwrite(&bmih, 40, 1, stream);

   y = height;
   while(y--) {
      float *ptr = rgba + (stride * y * 4);
      for(x = 0; x < width; ++x)
      {
         struct bmp_rgb_quad pixel;
         pixel.rgbRed   = float_to_ubyte(ptr[x*4 + 0]);
         pixel.rgbGreen = float_to_ubyte(ptr[x*4 + 1]);
         pixel.rgbBlue  = float_to_ubyte(ptr[x*4 + 2]);
         pixel.rgbAlpha = float_to_ubyte(ptr[x*4 + 3]);
         fwrite(&pixel, 1, 4, stream);
      }
   }

   fclose(stream);
error1:
   ;
}


/**
 * Print PIPE_TRANSFER_x flags with a message.
 */
void
debug_print_transfer_flags(const char *msg, unsigned usage)
{
   static const struct debug_named_value names[] = {
      DEBUG_NAMED_VALUE(PIPE_TRANSFER_READ),
      DEBUG_NAMED_VALUE(PIPE_TRANSFER_WRITE),
      DEBUG_NAMED_VALUE(PIPE_TRANSFER_MAP_DIRECTLY),
      DEBUG_NAMED_VALUE(PIPE_TRANSFER_DISCARD_RANGE),
      DEBUG_NAMED_VALUE(PIPE_TRANSFER_DONTBLOCK),
      DEBUG_NAMED_VALUE(PIPE_TRANSFER_UNSYNCHRONIZED),
      DEBUG_NAMED_VALUE(PIPE_TRANSFER_FLUSH_EXPLICIT),
      DEBUG_NAMED_VALUE(PIPE_TRANSFER_DISCARD_WHOLE_RESOURCE),
      DEBUG_NAMED_VALUE(PIPE_TRANSFER_PERSISTENT),
      DEBUG_NAMED_VALUE(PIPE_TRANSFER_COHERENT),
      DEBUG_NAMED_VALUE_END
   };

   debug_printf("%s: %s\n", msg, debug_dump_flags(names, usage));
}


/**
 * Print PIPE_BIND_x flags with a message.
 */
void
debug_print_bind_flags(const char *msg, unsigned usage)
{
   static const struct debug_named_value names[] = {
      DEBUG_NAMED_VALUE(PIPE_BIND_DEPTH_STENCIL),
      DEBUG_NAMED_VALUE(PIPE_BIND_RENDER_TARGET),
      DEBUG_NAMED_VALUE(PIPE_BIND_BLENDABLE),
      DEBUG_NAMED_VALUE(PIPE_BIND_SAMPLER_VIEW),
      DEBUG_NAMED_VALUE(PIPE_BIND_VERTEX_BUFFER),
      DEBUG_NAMED_VALUE(PIPE_BIND_INDEX_BUFFER),
      DEBUG_NAMED_VALUE(PIPE_BIND_CONSTANT_BUFFER),
      DEBUG_NAMED_VALUE(PIPE_BIND_DISPLAY_TARGET),
      DEBUG_NAMED_VALUE(PIPE_BIND_TRANSFER_WRITE),
      DEBUG_NAMED_VALUE(PIPE_BIND_TRANSFER_READ),
      DEBUG_NAMED_VALUE(PIPE_BIND_STREAM_OUTPUT),
      DEBUG_NAMED_VALUE(PIPE_BIND_CURSOR),
      DEBUG_NAMED_VALUE(PIPE_BIND_CUSTOM),
      DEBUG_NAMED_VALUE(PIPE_BIND_GLOBAL),
      DEBUG_NAMED_VALUE(PIPE_BIND_SHADER_BUFFER),
      DEBUG_NAMED_VALUE(PIPE_BIND_SHADER_IMAGE),
      DEBUG_NAMED_VALUE(PIPE_BIND_COMPUTE_RESOURCE),
      DEBUG_NAMED_VALUE(PIPE_BIND_COMMAND_ARGS_BUFFER),
      DEBUG_NAMED_VALUE(PIPE_BIND_SCANOUT),
      DEBUG_NAMED_VALUE(PIPE_BIND_SHARED),
      DEBUG_NAMED_VALUE(PIPE_BIND_LINEAR),
      DEBUG_NAMED_VALUE_END
   };

   debug_printf("%s: %s\n", msg, debug_dump_flags(names, usage));
}


/**
 * Print PIPE_USAGE_x enum values with a message.
 */
void
debug_print_usage_enum(const char *msg, unsigned usage)
{
   static const struct debug_named_value names[] = {
      DEBUG_NAMED_VALUE(PIPE_USAGE_DEFAULT),
      DEBUG_NAMED_VALUE(PIPE_USAGE_IMMUTABLE),
      DEBUG_NAMED_VALUE(PIPE_USAGE_DYNAMIC),
      DEBUG_NAMED_VALUE(PIPE_USAGE_STREAM),
      DEBUG_NAMED_VALUE(PIPE_USAGE_STAGING),
      DEBUG_NAMED_VALUE_END
   };

   debug_printf("%s: %s\n", msg, debug_dump_enum(names, usage));
}


#endif
