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
 */

#ifndef __FILE_QWI_H__
#define __FILE_QWI_H__

#define LOAD_PROC       "file-qwi-load"
#define LOAD_THUMB_PROC "file-qwi-load-thumb"
#define SAVE_PROC       "file-qwi-save"
#define PLUG_IN_BINARY  "file-qwi"
#define PLUG_IN_ROLE    "gimp-file-qwi"

#define BitSet(byte, bit)        (((byte) & (bit)) == (bit))

#define ReadOK(file,buffer,len)  (fread(buffer, len, 1, file) != 0)
#define Write(file,buffer,len)   fwrite(buffer, len, 1, file)
#define WriteOK(file,buffer,len) (Write(buffer, len, file) != 0)

#define CEIL_RSHIFT(a,b) (((a) + (1<<b)-1) >> b)

gint32             ReadQWI   (const gchar  *filename,
		  	  	  	  	  	  guint32       thumb,
		  	  	  	  	  	  guint16       *image_width,
		  	  	  	  	  	  guint16       *image_height,
                              GError      **error);
GimpPDBStatusType  WriteQWI  (const gchar  *filename,
                              gint32        image,
                              gint32        drawable_ID,
                              GError      **error);


extern       gboolean  qwi_interactive;
extern       gboolean  qwi_lastvals;
extern const gchar    *filename;
extern 		 gchar    *javascript_code;

#endif /* __FILE_QWI_H__ */
