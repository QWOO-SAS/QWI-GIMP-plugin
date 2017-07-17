/* bmpread.c    reads any bitmap I could get for testing */
/* Alexander.Schulz@stud.uni-karlsruhe.de                */

/*
 * GIMP - The GNU Image Manipulation Program
 * Copyright (C) 1995 Spencer Kimball and Peter Mattis
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 * ----------------------------------------------------------------------------
 */
#include <time.h>

//#include "config.h"

#include <errno.h>
#include <string.h>

#include <glib/gstdio.h>

#include <libgimp/gimp.h>

#include "file-qwi.h"
#include "qwi.h"
#include "stdlib.h"

//#include "libgimp/stdplugins-intl.h"


#if !defined(WIN32) && !defined(__MINGW32__)
#define BI_RGB            0
#define BI_RLE8           1
#define BI_RLE4           2
#define BI_BITFIELDS      3
#define BI_ALPHABITFIELDS 4
#endif

static GimpParasite *code_parasite = NULL;

static guint32 get_duration (guint16 duration) 
{
  if (duration & 0x4000)
    return (duration&0x3fff)*10;
  return (duration&0x3fff)/100;
}


gint32
ReadQWI (const gchar  *name,
		guint32        thumb,
		guint16        *image_width,
		guint16        *image_height,
		GError      **error)
{
	FILE              *fd;
	gint32             image_ID = -1;
	QWI_ELEMENT        element;
	guchar             lowres;
	gshort              elements;
	gint32             layer;
	guint16			   width;
	guint16			   height;
	guchar            *buffer = NULL;
	guchar             plane;
	gshort            *data[4] = {NULL, NULL, NULL, NULL};
	guchar            *dest;
	gint               cur_progress, max_progress;
	GimpImageBaseType  base_type;
	GimpImageType      image_type;
	GimpImageType      layers_type;
	GimpPixelRgn       pixel_rgn;
	GimpDrawable      *drawable;

  guint32 qwi_error = 0;
  guint32 code_length = 0;
  gchar *code = NULL;
#if !defined(WIN32) && !defined(__MINGW32__)
	struct timespec now, tmstart;
	double seconds;
	clock_gettime(CLOCK_REALTIME, &tmstart);
#endif
	memset(&element, 0, sizeof(QWI_ELEMENT));
	gimp_progress_init_printf ("Opening '%s'",
			gimp_filename_to_utf8 (name));

	filename = name;
	fd = g_fopen (filename, "rb");

	if (!fd)
	{
		g_set_error (error, G_FILE_ERROR, g_file_error_from_errno (errno),
				"Could not open '%s' for reading: %s",
				gimp_filename_to_utf8 (filename), g_strerror (errno));
		goto out;
	}

	/* Read the QWI file header */
	buffer = g_malloc (QWI_FILE_HEADER_SIZE);
	if (!ReadOK (fd, buffer, QWI_FILE_HEADER_SIZE))
	{
		g_set_error (error, G_FILE_ERROR, G_FILE_ERROR_FAILED,
				"Error reading QWI file '%s'",
				gimp_filename_to_utf8 (filename));
		goto out;
	}
	if (!qwi_getFileHeader(&element, buffer, &qwi_error)) {
		g_set_error (error, G_FILE_ERROR, G_FILE_ERROR_FAILED,
				"file '%s' seems not to be a QWI image",
				gimp_filename_to_utf8 (filename));
		goto out;
	}
	g_free(buffer);
	buffer = NULL;

	if (qwi_error) {
		g_set_error (error, G_FILE_ERROR, G_FILE_ERROR_FAILED,
				"file '%s' format (%d.%d) is not supported by actual QWI library (%d.%d). Trying to decode anyway...",
				gimp_filename_to_utf8 (filename), (element.file.version>>16)&0xff, (element.file.version>>8)&0xff,
        (QWI_FORMAT>>16)&0xff, (QWI_FORMAT>>8)&0xff);
	}

	if (element.file.optionals) {
    guint32 code_offset = 0;
    guint32 offset;
    guchar *tmpcode;
		buffer = g_malloc(element.file.optionals);
		if (!ReadOK (fd, buffer, element.file.optionals))
		{
			g_set_error (error, G_FILE_ERROR, G_FILE_ERROR_FAILED,
					"Error reading QWI file '%s'",
					gimp_filename_to_utf8 (filename));
			goto out;
		}
	/* manage File Optional sections here */
    // first calculate the length of the sections we are interested in ("PAG", "FNT" & "COD")
    offset = 0;
    while (offset < (element.file.optionals-3)) {
      guint32 opt_length;
      guint32 opt_size;
      offset = qwi_findOptionalSection(&element, "PAG", 0, offset, buffer, &opt_size, &opt_length) + opt_size;
      if (!opt_length)
        offset = element.file.optionals;
      else
        code_length += opt_length + 13;
    }
    offset = 0;
    while (offset < (element.file.optionals-3)) {
      guint32 opt_length;
      guint32 opt_size;
      offset = qwi_findOptionalSection(&element, "FNT", 0, offset, buffer, &opt_size, &opt_length) + opt_size;
      if (!opt_length)
        offset = element.file.optionals;
      else
        code_length += opt_length + 13;
    }
    offset = 0;
    while (offset < (element.file.optionals-3)) {
      guint32 opt_length;
      guint32 opt_size;
      offset = qwi_findOptionalSection(&element, "COD", 0, offset, buffer, &opt_size, &opt_length) + opt_size;
      if (!opt_length)
        offset = element.file.optionals;
      else
        code_length += opt_length + 13;
    }
      // now, get the code and copy it in the global code string
    if (code_length) {
      code = g_malloc(code_length+1); // let's set a nul terminated string
      *(code+code_length) = 0;
      offset = 0;
      while (offset < (element.file.optionals-3)) {
        guint32 tmplength;
        if (!strncmp((gchar *)(buffer+offset+1), "PAG", 3) || !strncmp((gchar *)(buffer+offset+1), "FNT", 3) || !strncmp((gchar *)(buffer+offset+1), "COD", 3))
          switch(buffer[offset+1]) {
            case 'P':
              strncpy(code+code_offset,"<page>", 6);
              code_offset += 6;
              offset += qwi_getOptionalSection(&element, 0, buffer+offset, &tmpcode, &tmplength, &qwi_error);
              memcpy(code+code_offset, tmpcode, tmplength);
              free(tmpcode);
              code_offset += tmplength;
              strncpy(code+code_offset,"</page>", 7);
              code_offset += 7;
              break;
            case 'F':
              strncpy(code+code_offset,"<font>", 6);
              code_offset += 6;
              offset += qwi_getOptionalSection(&element, 0, buffer+offset, &tmpcode, &tmplength, &qwi_error);
              memcpy(code+code_offset, tmpcode, tmplength);
              free(tmpcode);
              code_offset += tmplength;
              strncpy(code+code_offset,"</font>", 7);
              code_offset += 7;
              break;
            case 'C':
              strncpy(code+code_offset,"<code>", 6);
              code_offset += 6;
              offset += qwi_getOptionalSection(&element, 0, buffer+offset, &tmpcode, &tmplength, &qwi_error);
              memcpy(code+code_offset, tmpcode, tmplength);
              free(tmpcode);
              code_offset += tmplength;
              strncpy(code+code_offset,"</code>", 7);
              code_offset += 7;
              break;
          }
        }
    }
		g_free(buffer);
		buffer = NULL;
	}

	cur_progress = 0;
	max_progress = element.file.elements;

  // Let's process each element in the file (in case of a thumbnail request, just do it for the first element)
	for (elements = 0; elements < element.file.elements && (!elements || !thumb); elements++)
	{
		gchar *layername;
    // get element header
		buffer = g_malloc (element.file.split && element.file.base ? QWI_ELEMENT_SHORT_HEADER_SIZE : QWI_ELEMENT_HEADER_SIZE);
		if (!ReadOK (fd, buffer, element.file.split && element.file.base ? QWI_ELEMENT_SHORT_HEADER_SIZE : QWI_ELEMENT_HEADER_SIZE))
		{
			g_set_error (error, G_FILE_ERROR, G_FILE_ERROR_FAILED,
					"Error reading QWI file '%s'",
					gimp_filename_to_utf8 (filename));
			goto out;
		}
		if (!qwi_getElementHeader(&element, buffer)) {
			g_set_error (error, G_FILE_ERROR, G_FILE_ERROR_FAILED,
					"file '%s' seems corrupted or is incompatible with current software",
					gimp_filename_to_utf8 (filename));
			goto out;
		}
		g_free(buffer);
		buffer = NULL;

    // get the element bitstream
		buffer = g_malloc (element.size);
		if (!ReadOK (fd, buffer, element.size))
		{
			g_set_error (error, G_FILE_ERROR, G_FILE_ERROR_FAILED,
					"Error reading QWI file '%s'",
					gimp_filename_to_utf8 (filename));
			goto out;
		}
		if (!element.width) {
			g_free(buffer);
			buffer = NULL;
			continue;
		}
		switch (element.planes)
		{
		case 4 :
			base_type = GIMP_RGB;
			image_type = GIMP_RGBA_IMAGE;
			layers_type = GIMP_RGBA_IMAGE;
			break;
		case 3:
			base_type = GIMP_RGB;
			image_type = GIMP_RGB_IMAGE;
			layers_type = GIMP_RGB_IMAGE;
			break;
		case 2:
			base_type = GIMP_GRAY;
			image_type = GIMP_GRAYA_IMAGE;
			layers_type = GIMP_GRAYA_IMAGE;
			break;
		case 1:
			base_type = GIMP_GRAY;
			image_type = GIMP_GRAY_IMAGE;
			layers_type = GIMP_GRAY_IMAGE;
			break;

		default:
			g_message ("Error while computing QWI file");
			image_ID = -1;
			goto out;
		}

		lowres = 0;
		width = element.width;
		height = element.height;

		if (!elements){
			if ((element.width < 0) || (element.width > GIMP_MAX_IMAGE_SIZE))
			{
				g_message ("Unsupported or invalid image width: %d", element.width);
				image_ID = -1;
				goto out;
			}

			if ((element.height < 0) || (element.height > GIMP_MAX_IMAGE_SIZE))
			{
				g_message ("Unsupported or invalid image height: %d", element.height);
				image_ID = -1;
				goto out;
			}
			if (image_width)
				*image_width = element.width;
			if (image_height)
				*image_height = element.height;
			while (thumb && lowres < element.toplayer && (CEIL_RSHIFT(element.width, lowres) > thumb || CEIL_RSHIFT(element.height, lowres) > thumb))
				lowres++;
			width = CEIL_RSHIFT(element.width, lowres);
			height = CEIL_RSHIFT(element.height, lowres);
			image_ID = gimp_image_new (width, height, base_type);
			gimp_image_set_filename (image_ID, filename);

      if (code_length) {
        if (code_parasite)
          gimp_parasite_free (code_parasite);

        code_parasite = gimp_parasite_new ("code", GIMP_PARASITE_PERSISTENT, code_length + 1, code);
        g_free(code);
        gimp_image_attach_parasite (image_ID, code_parasite);

        gimp_parasite_free (code_parasite);
        code_parasite = NULL;
      }
		}

		// get layer name
		if (elements) {
			guint namesize, namelength;
			guint nameoffset = qwi_findOptionalSection(&element, "NAM", 1, 0, buffer, &namesize, &namelength);
			if (namelength)
        qwi_getOptionalSection(&element, 1, buffer+nameoffset, (guchar**)(&layername), &namelength, &qwi_error);
      else {
        layername = malloc(128);
			  switch (element.file.type)
			  {
			  case QWI_TYPE_MULTILAYER:
				  sprintf(layername, "Layer %d", elements);
				  break;
			  case QWI_TYPE_ANIMATE:
				  sprintf(layername, "Video frame %d (%dms)%s", elements, get_duration(element.duration), element.duration&0x8000?" (combine)":"");
				  break;
			  case QWI_TYPE_SLIDESHOW:
				  sprintf(layername, "Image %d", elements);
				  break;
			  default:
				  sprintf(layername, "%s", name);
			  }
			}

			layer = gimp_layer_new (image_ID, layername, width, height,
					layers_type, 100, GIMP_NORMAL_MODE);
      free (layername);
		}
		else {
			guint namesize, namelength;
			guint nameoffset = qwi_findOptionalSection(&element, "NAM", 1, 0, buffer, &namesize, &namelength);
			if (namelength)
        qwi_getOptionalSection(&element, 1, buffer+nameoffset, (guchar**)(&layername), &namelength, &qwi_error);
      else {
        layername = malloc(128);
			  if (element.file.type == QWI_TYPE_ANIMATE)
				  sprintf(layername, "Video frame %d (%dms)%s", elements, get_duration(element.duration), element.duration&0x8000?" (combine)":"");
			  else if (element.file.type == QWI_TYPE_MULTILAYER)
				  sprintf(layername, "Background");
			  else
				  sprintf(layername, "Image %d", elements);
      }
			layer = gimp_layer_new (image_ID, layername, width, height,
					image_type, 100, GIMP_NORMAL_MODE);
      free(layername);
		}

		gimp_image_insert_layer (image_ID, layer, -1, 0);
		gimp_layer_translate (layer, (gint) element.x, (gint) element.y);
		drawable = gimp_drawable_get (layer);
		drawable->width = width;
		drawable->height = height;

    // allocate 128bit aligned memory for the decoding process
		for (plane = 0; plane < element.planes; plane++)
#if defined(WIN32) || defined(__MINGW32__)
			data[plane] = _aligned_malloc (width * height * sizeof (gshort), 16);
#else
			data[plane] = aligned_alloc (16, width * height * sizeof (gshort));
#endif      

    // allocate memory for the output
		dest      = g_malloc (width * height * element.planes);

		// decode 
		qwi_decode_mt(&element, 1, 0, element.toplayer-lowres, buffer, data, (void*) dest, &qwi_error);
		if (qwi_error) {
			g_set_error (error, G_FILE_ERROR, g_file_error_from_errno (qwi_error),
					"Error while trying to allocate memory when processing %s",
					gimp_filename_to_utf8 (filename));
			return GIMP_PDB_EXECUTION_ERROR;
		}

    // free up the processing memory and the input buffer
		for (plane = 0; plane < element.planes; plane++){
#if defined(WIN32) || defined(__MINGW32__)
			_aligned_free(data[plane]);
#else
			free(data[plane]);
#endif
			data[plane] = NULL;
		}
		g_free(buffer);
		buffer = NULL;
		cur_progress++;
		gimp_progress_update (((gdouble)cur_progress)/max_progress);

    // copy the output into a a new layer
		gimp_pixel_rgn_init (&pixel_rgn, drawable, 0, 0,
				width, height, TRUE, FALSE);
		gimp_pixel_rgn_set_rect (&pixel_rgn, dest,
				0, 0, width, height);

		gimp_drawable_flush (drawable);
		gimp_drawable_detach (drawable);

    // free up the decoded output memory
		g_free (dest);
		if (thumb)
			break;

	};

	gimp_progress_update (1.0);

	out:
	if (fd)
		fclose (fd);
	if (buffer)
		g_free(buffer);
	if (data[0])
		g_free(data[0]);
	if (data[1])
		g_free(data[1]);
	if (data[2])
		g_free(data[2]);
	if (data[3])
		g_free(data[3]);
#if !defined(WIN32) && !defined(__MINGW32__)

	clock_gettime(CLOCK_REALTIME, &now);
	seconds = (double)((now.tv_sec+now.tv_nsec*1e-9) - (double)(tmstart.tv_sec+tmstart.tv_nsec*1e-9));
	printf("QWI file decoded in %fs\n", seconds);
#endif
	return image_ID;
}
