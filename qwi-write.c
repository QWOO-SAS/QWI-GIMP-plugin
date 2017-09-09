/* qwiwrite.c   Writes Qwoo! QWI! files.                            */

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
#include <libgimp/gimpui.h>

#include "file-qwi.h"
#include "qwi.h"

//#include "libgimp/stdplugins-intl.h"

typedef struct
{
	GtkWidget     *preset;              /*preset selector*/
	GtkObject     *quality;               /*quality slidebar*/
	GtkObject     *qualityAlpha;          /*alpha quality slidebar*/
	GtkWidget     *subsampling;         /*subsampling side select*/
	GtkWidget     *layers;              /*layers side select*/
	GtkWidget     *resiliency;          /*resiliency side select*/
	GtkWidget     *animate;             /*animate check box*/
	GtkWidget     *duration;            /*duration text box*/
	GtkTextBuffer *text_buffer;
} QWISaveGui;

static struct
{
	gint preset;
	gint elements;
	gint subsampling;
	gint resiliency;
	gint toplayer;
	gint maxlayers;
	gint quality;
	gint qualityAlpha;
	gint maxquality;
	gint animate;
	gint duration;
} QWISaveData;

static gint    cur_progress = 0;
static gint    max_progress = 0;
static GimpParasite *code_parasite = NULL;
static gchar *globalcode = NULL;

static guint16 set_duration (guint32 duration) 
{
  if (duration < 164)
    return 100*duration;
  if (duration > 163830)
    return 0x7fff;
  return 0x4000 + (duration/10);
}

static guint32 get_duration (guint16 duration) 
{
  if (duration & 0x4000)
    return (duration&0x3fff)*10;
  return (duration&0x3fff)/100;
}

static  gboolean  save_dialog     (gint    channels);

GimpPDBStatusType
WriteQWI (const gchar  *filename,
		gint32        image,
		gint32        drawable_ID,
		GError      **error)
{
	FILE          *outfile;
	guchar        *buffer;
	guchar        *pixels;
	gint32 		  *layers;
	gint 		   elements;
	GimpPixelRgn   pixel_rgn;
	GimpDrawable  *drawable;
	GimpImageType  drawable_type;
	//	GimpImageType  layer_type = 0;
	gint32         width;
	gint32         height;
	gint           x;
	gint           y;
	gshort        *data[4];
	QWI_ELEMENT    element;
	guchar 		   planes = 0;
	guchar 		   colorspace;
#if !defined(WIN32) && !defined(__MINGW32__)
	struct timespec now, tmstart;
	double seconds;
#endif
	guint32 qwi_error = 0;

	memset(&element, 0, sizeof(QWI_ELEMENT));

	layers = gimp_image_get_layers (image, &elements);

	drawable = gimp_drawable_get (layers[elements-1]);
	drawable_type   = gimp_drawable_type (layers[elements-1]);

	QWISaveData.preset      = -1;
	QWISaveData.quality     = 100;
	QWISaveData.qualityAlpha= 100;
	QWISaveData.toplayer    = 0;
	QWISaveData.resiliency  = 0;
	QWISaveData.subsampling = 0;
	QWISaveData.animate     = 0;
	QWISaveData.duration    = 0;

	if (drawable_type == GIMP_RGBA_IMAGE || drawable_type == GIMP_RGB_IMAGE)
		planes = 3;
	else
		planes = 1;
	width  = drawable->width;
	height = drawable->height;

  code_parasite = gimp_image_get_parasite (image, "code");
  if (code_parasite)
    globalcode = g_strndup (gimp_parasite_data (code_parasite), gimp_parasite_data_size (code_parasite));

	if (qwi_interactive || qwi_lastvals)
		gimp_get_data (SAVE_PROC, &QWISaveData);

	QWISaveData.elements    = elements;
	QWISaveData.maxquality    = 100;
	QWISaveData.maxlayers   = QWI_MAX_LAYERS-1;
	while (CEIL_RSHIFT(width, QWISaveData.maxlayers) < 32 && QWISaveData.maxlayers)
		QWISaveData.maxlayers--;
	while (CEIL_RSHIFT(height, QWISaveData.maxlayers) < 32 && QWISaveData.maxlayers)
		QWISaveData.maxlayers--;
	QWISaveData.maxlayers++;

	if (qwi_interactive && !save_dialog (planes))
		return GIMP_PDB_CANCEL;

#if !defined(WIN32) && !defined(__MINGW32__)
	clock_gettime(CLOCK_REALTIME, &tmstart);
#endif

	QWISaveData.quality = QWISaveData.quality < 0 ? 0 : QWISaveData.quality > 100 ? 100 : QWISaveData.quality;
	QWISaveData.qualityAlpha = QWISaveData.qualityAlpha < 0 ? 0 : QWISaveData.qualityAlpha > 100 ? 100 : QWISaveData.qualityAlpha;
	QWISaveData.toplayer = QWISaveData.toplayer <= 0 ? 0 : QWISaveData.toplayer == 1 ? 1 : QWISaveData.maxlayers;
	QWISaveData.resiliency = QWISaveData.resiliency < 0 ? 0 : QWISaveData.resiliency > 2 ? 2 : QWISaveData.resiliency;
	QWISaveData.subsampling = QWISaveData.subsampling < 0 ? 0 : QWISaveData.subsampling > 4 ? 0 : QWISaveData.subsampling;
	QWISaveData.subsampling = planes < 3 ? 1 : QWISaveData.subsampling;
	QWISaveData.animate = QWISaveData.elements > 1 ? (QWISaveData.animate ? 1 : 0) : 0;
	QWISaveData.duration = QWISaveData.animate ? get_duration(set_duration(QWISaveData.duration)) : 0;

	gimp_set_data (SAVE_PROC, &QWISaveData, sizeof (QWISaveData));

  if (code_parasite) {
    gimp_image_detach_parasite (image, "code");
    gimp_parasite_free(code_parasite);
    code_parasite = NULL;
  }
  if (globalcode && strlen(globalcode)) {
        code_parasite = gimp_parasite_new ("code", GIMP_PARASITE_PERSISTENT, strlen (globalcode) + 1, globalcode);
        gimp_image_attach_parasite (image, code_parasite);
        gimp_parasite_free (code_parasite);
        code_parasite = NULL;
  }

	gimp_progress_init_printf ("Saving '%s'",
			gimp_filename_to_utf8 (filename));

	outfile = g_fopen (filename, "wb");
	if (!outfile)
	{
		g_set_error (error, G_FILE_ERROR, g_file_error_from_errno (errno),
				"Could not open '%s' for writing: %s",
				gimp_filename_to_utf8 (filename), g_strerror (errno));
		return GIMP_PDB_EXECUTION_ERROR;
	}


	cur_progress = 0;
	max_progress = elements;

	element.file.type     = (elements > 1) + QWISaveData.animate;
	element.file.width    = gimp_image_width(image);
	element.file.height   = gimp_image_height(image);

	// reserve some bytes for file header
	fseek(outfile, QWI_FILE_HEADER_SIZE, SEEK_SET);

	// Any File optional section shall be set here
	if (globalcode && strlen(globalcode)) {
		char *option;
		char *optionend = globalcode;
		buffer = g_malloc(strlen(globalcode) + 256);
		// set PAGE section(s)
		while ((option = strstr(optionend, "<page>")) != NULL) {
			guint32 length;
			optionend = strstr(option, "</page>");
			if (optionend == (char*)NULL) {
				g_set_error (error, G_FILE_ERROR, g_file_error_from_errno (errno),
						"Missing </page> tag in code");
				return GIMP_PDB_EXECUTION_ERROR;
			}
			length = (guint32)(optionend - option) - 6;
			length = qwi_setOptionalSection(&element, "PAG", 0, length, (uint8_t*)option+6, buffer, &qwi_error);
		}
		// set FONT section(s)
		optionend = globalcode;
		while ((option = strstr(optionend, "<font>")) != NULL) {
			guint32 length;
			optionend = strstr(option, "</font>");
			if (optionend == (char*)NULL) {
				g_set_error (error, G_FILE_ERROR, g_file_error_from_errno (errno),
						"Missing </font> tag in code");
				return GIMP_PDB_EXECUTION_ERROR;
			}
			length = (guint32)(optionend - option) - 6;
			length = qwi_setOptionalSection(&element, "FNT", 0, length, (uint8_t*)option+6, buffer, &qwi_error);
		}
		// set CODE section(s)
		optionend = globalcode;
		while ((option = strstr(optionend, "<code>")) != NULL) {
			guint32 length;
			optionend = strstr(option, "</code>");
			if (optionend == (char*)NULL) {
				g_set_error (error, G_FILE_ERROR, g_file_error_from_errno (errno),
						"Missing </code> tag in code");
				return GIMP_PDB_EXECUTION_ERROR;
			}
			length = (guint32)(optionend - option) - 6;
			length = qwi_setOptionalSection(&element, "COD", 0, length, (uint8_t*)option+6, buffer, &qwi_error);
		}
		fwrite(buffer, element.file.optionals, 1, outfile);
		g_free(buffer);
    g_free(globalcode);
    globalcode = NULL;
	}

  // Now, the elements (the gimp layers)
	for (; elements; elements--) {
		guint32 length;
		guint plane;
		gchar *layername;
		drawable = gimp_drawable_get (layers[elements-1]);
		drawable_type   = gimp_drawable_type (layers[elements-1]);

		width  = drawable->width;
		height = drawable->height;
		gimp_drawable_offsets(layers[elements-1], &x, &y);

		if (drawable_type == GIMP_RGBA_IMAGE || drawable_type == GIMP_RGB_IMAGE) {
			planes = 3;
			colorspace = QWI_COLORSPACE_RGBx;
		}
		else {
			planes = 1;
			colorspace = QWI_COLORSPACE_YUVx;
		}
		if (drawable_type == GIMP_RGBA_IMAGE || drawable_type == GIMP_GRAYA_IMAGE)
			planes++;

    // allocate some memory for the bitstream output
    // (we don't know how much, and expect that the compression process will not diverge too much...)
		buffer = g_malloc(MAX(8192, width * height * (planes + 1)));

		//set the description element structure
		qwi_setElement(&element, width, height, x, y, planes, QWISaveData.subsampling-1, colorspace, 8,
				QWISaveData.quality, -1, QWISaveData.toplayer-1, 0, QWISaveData.resiliency, set_duration(QWISaveData.duration));

		//set the layer optionals (here, the layer name)
		layername = gimp_item_get_name (layers[elements-1]);
		if (element.file.type == QWI_TYPE_ANIMATE) {
			guint32 duration;
			gchar *pName;
      while((pName = strchr(layername, 0x28)) != 0) {
			  if (sscanf(pName, "(%ims)", &duration) > 0) {
    			element.duration = set_duration(duration);
          break;
        }
        pName++;
      }
			if (strstr(layername, "(combine)") != NULL)
        element.duration |= 0x8000;
		}
		else if (element.file.type&1)
			qwi_setOptionalSection(&element, "NAM", 1, strlen(layername), (uint8_t*)layername, buffer, &qwi_error);

    // allocate some memory for the coding process
		data[0] = g_malloc (planes * width * height * sizeof (gshort));
		for (plane = 1; plane < planes; plane++)
			data[plane] = data[plane-1] + width * height;


		gimp_pixel_rgn_init (&pixel_rgn, drawable, 0, 0, drawable->width, drawable->height, FALSE, FALSE);
		pixels = g_new (guchar, width * height * planes);
		gimp_pixel_rgn_get_rect (&pixel_rgn, pixels, 0, 0, drawable->width, drawable->height);

		// initialize the coding process memory with the current layer pixels
		for (plane = 0; plane < planes; plane++)
		{
			guint32 i;
			guchar *p = pixels + plane;
			gint16 *q = data[plane];
			for (i = 0; i < width * height; i++, p+=planes, q++)
				*q = *p;
		}
		g_free (pixels);

		// encode the element
		length = qwi_encode (&element, 1, 0, QWI_MAX_LAYERS, data, buffer, &qwi_error);
		if (qwi_error) {
			g_set_error (error, G_FILE_ERROR, g_file_error_from_errno (qwi_error),
					"Could not allocate memory when processing: %s",
					gimp_filename_to_utf8 (filename));
			return GIMP_PDB_EXECUTION_ERROR;
		}
		// Write data to disk
		Write (outfile, buffer, length);
    element.file.top = MAX(element.file.top, element.toplayer);

		g_free (data[0]);
		g_free (buffer);
		cur_progress++;
		gimp_progress_update (((gdouble)cur_progress)/max_progress);
	}

	gimp_progress_update (1.0);
	// write the file header, now that it is valid
	fseek(outfile, 0, SEEK_SET);
	buffer = g_malloc(QWI_FILE_HEADER_SIZE);
	qwi_setFileHeader(&element, buffer);
	(void) Write (outfile, buffer, QWI_FILE_HEADER_SIZE);
	g_free(buffer);


	fseek(outfile, 0, SEEK_END);
	fflush(outfile);
	fclose (outfile);

#if !defined(WIN32) && !defined(__MINGW32__)
	clock_gettime(CLOCK_REALTIME, &now);
	seconds = (double)((now.tv_sec+now.tv_nsec*1e-9) - (double)(tmstart.tv_sec+tmstart.tv_nsec*1e-9));
	printf("QWI file encoded in %fs\n", seconds);
#endif
	return GIMP_PDB_SUCCESS;
}

static void
load_preset (GtkWidget *w, QWISaveGui *pg)
{
	gint idx;
	const int presets[][5] = {
			{0, 100, 100, 1, 0}, // drawing
			{0, 100, 100, 0, 0}, // Archive lossless
			{0, 90, 100, -1, 1}, // Archive photo good
			{1, 90, 100, -1, 4},  // Web photo good
			{1, 80, 100, -1, 4},  // Web photo smaller
	};

	if (!gimp_int_combo_box_get_active(GIMP_INT_COMBO_BOX(pg->preset), &idx) || idx < 0)
		return;

	QWISaveData.preset = idx;
	QWISaveData.resiliency = presets[idx][0];
	QWISaveData.quality = presets[idx][1];
	QWISaveData.qualityAlpha = presets[idx][2];
	QWISaveData.toplayer = presets[idx][3] < 0 ? QWISaveData.maxlayers : presets[idx][3];
	QWISaveData.subsampling = presets[idx][4];

	gtk_adjustment_set_value (GTK_ADJUSTMENT (pg->quality),
			QWISaveData.quality);
	gtk_adjustment_set_value (GTK_ADJUSTMENT (pg->qualityAlpha),
			QWISaveData.qualityAlpha);
	gimp_int_combo_box_set_active (GIMP_INT_COMBO_BOX (pg->layers),
			QWISaveData.toplayer > 2 ? 2 : QWISaveData.toplayer);
	gimp_int_combo_box_set_active (GIMP_INT_COMBO_BOX (pg->resiliency),
			QWISaveData.resiliency);
	gimp_int_combo_box_set_active (GIMP_INT_COMBO_BOX (pg->subsampling),
			QWISaveData.subsampling);
}

static gboolean
save_dialog (gint channels)
{
	QWISaveGui pg;
	GtkWidget *dialog;
	GtkWidget *vbox_main;
	GtkWidget *vbox;
	GtkWidget *vbox2;
	GtkWidget *expander;
	gboolean   run;
	GtkObject *entry;
	GtkWidget *table;
	GtkWidget *label;
	GtkWidget *combo;
//	GtkWidget *button;
	GtkWidget     *text_view;
	GtkTextBuffer *text_buffer;
	GtkWidget     *scrolled_window;
	GtkWidget *check = NULL;
	GtkWidget *spin = NULL;
	GtkTextIter  start_iter;
	GtkTextIter  end_iter;
	GtkAdjustment *adjustment;

	/* Dialog init */
	dialog = gimp_export_dialog_new ("QWI", PLUG_IN_BINARY, SAVE_PROC);

	gtk_window_set_resizable (GTK_WINDOW (dialog), FALSE);

	vbox_main = gtk_box_new (GTK_ORIENTATION_VERTICAL, 12);
	gtk_container_set_border_width (GTK_CONTAINER (vbox_main), 12);
	gtk_box_pack_start (GTK_BOX (gimp_export_dialog_get_content_area (dialog)),
			vbox_main, TRUE, TRUE, 0);
	gtk_widget_show (vbox_main);

	table = gtk_table_new (3, 3, FALSE);
	gtk_table_set_col_spacings (GTK_TABLE (table), 6);
	gtk_box_pack_start (GTK_BOX (vbox_main), table, FALSE, FALSE, 0);
	gtk_widget_show (table);

	/* preset */
	label = gtk_label_new_with_mnemonic ("_Preset:");
	gtk_misc_set_alignment (GTK_MISC (label), 0.0, 0.5);
	gtk_table_attach (GTK_TABLE (table), label, 0, 1, 0, 1,
			GTK_FILL, GTK_FILL, 0, 0);
	gtk_widget_show (label);

	pg.preset =
			combo = gimp_int_combo_box_new (
					" ", -1,
					"Drawing/diagram", 0,
					"Archive Lossless", 1,
					"Archive photo good", 2,
					"Web photo Good", 3,
					"Web photo smaller", 4,
					NULL);
	gtk_table_attach (GTK_TABLE (table), combo, 1, 2, 0, 1,
			GTK_FILL | GTK_EXPAND, GTK_FILL, 0, 0);
	gtk_widget_show (combo);

	gtk_label_set_mnemonic_widget (GTK_LABEL (label), combo);
	gimp_int_combo_box_set_active (GIMP_INT_COMBO_BOX (combo), QWISaveData.preset);

	g_signal_connect (combo, "changed",
			G_CALLBACK (load_preset),
			&pg);

	/* quality */
	pg.quality = entry = gimp_scale_entry_new (GTK_TABLE (table), 0, 1,
			"_Quality:",
			100, 0, QWISaveData.quality,
			0.0, (QWISaveData.maxquality + 0.0), 1.0, 8.0, 0,
			TRUE, 0.0, 0.0,
			"100 = best quality, 0 = smallest file",
			"file-qwi-save-quality");
	g_signal_connect (entry, "value-changed",
			G_CALLBACK (gimp_uint_adjustment_update),
			&QWISaveData.quality);


	/* alpha quality */
	pg.qualityAlpha = entry = gimp_scale_entry_new (GTK_TABLE (table), 0, 2,
			"_Alpha Quality:",
			100, 0, QWISaveData.qualityAlpha,
			0.0, (QWISaveData.maxquality + 0.0), 1.0, 8.0, 0,
			TRUE, 0.0, 0.0,
			"100 = best quality, 0 = smallest file",
			"file-qwi-save-quality");
	g_signal_connect (entry, "value-changed",
			G_CALLBACK (gimp_uint_adjustment_update),
			&QWISaveData.qualityAlpha);

	if (QWISaveData.elements > 1)
	{
		/* animate checkbox */
		pg.animate = check = gtk_check_button_new_with_mnemonic ("An_imate");
		gtk_box_pack_start (GTK_BOX (vbox_main), check, FALSE, FALSE, 0);
		gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (check), QWISaveData.animate);
		gtk_widget_show (check);
		g_signal_connect (check, "toggled",
				G_CALLBACK (gimp_toggle_button_update),
				&QWISaveData.animate);

		table = gtk_table_new (1, 2, FALSE);
		gtk_table_set_col_spacings (GTK_TABLE (table), 6);
		gtk_box_pack_start (GTK_BOX (vbox_main), table, FALSE, FALSE, 0);
		gtk_widget_show (table);

		/* duration spin */
		label = gtk_label_new_with_mnemonic ("Default _Frame Duration (ms):");
		gtk_misc_set_alignment (GTK_MISC (label), 0.0, 0.5);
		gtk_table_attach (GTK_TABLE (table), label, 0, 1, 0, 1,
				GTK_FILL, GTK_FILL, 0, 0);
		gtk_widget_show (label);

		adjustment = (GtkAdjustment*)gtk_adjustment_new(QWISaveData.duration, 1.0, 163830.0, 1.0, 10.0, 0.0);
		pg.duration = spin = gtk_spin_button_new (adjustment, 1.0, 0);
		//		  gtk_spin_button_set_value (spin, QWISaveData.duration);
		gtk_table_attach (GTK_TABLE (table), spin, 1, 2, 0, 1,
				GTK_FILL, GTK_FILL, 0, 0);
		gtk_widget_show (spin);
		g_signal_connect (adjustment, "value-changed",
				G_CALLBACK (gimp_int_adjustment_update),
				&QWISaveData.duration);
	}

	/* Advanced Options */
	expander = gtk_expander_new_with_mnemonic ("_Advanced Options");

	gtk_box_pack_start (GTK_BOX (vbox_main), expander, TRUE, TRUE, 0);
	gtk_widget_show (expander);

	vbox2 = gtk_box_new (GTK_ORIENTATION_VERTICAL, 12);
	gtk_container_set_border_width (GTK_CONTAINER (vbox2), 12);
	gtk_container_add (GTK_CONTAINER (expander), vbox2);
	gtk_widget_show (vbox2);

	table = gtk_table_new (1, 2, FALSE);
	gtk_table_set_col_spacings (GTK_TABLE (table), 6);
	gtk_box_pack_start (GTK_BOX (vbox2), table, FALSE, FALSE, 0);
	gtk_widget_show (table);

	/* layers */
	label = gtk_label_new_with_mnemonic ("L_ayers:");
	gtk_misc_set_alignment (GTK_MISC (label), 0.0, 0.5);
	gtk_table_attach (GTK_TABLE (table), label, 0, 1, 0, 1,
			GTK_FILL, GTK_FILL, 0, 0);
	gtk_widget_show (label);

	pg.layers =
			combo = gimp_int_combo_box_new (
					"Auto", 0,
					"1", 1,
					"max", 2,
					NULL);
	gtk_table_attach (GTK_TABLE (table), combo, 1, 2, 0, 1,
			GTK_FILL | GTK_EXPAND, GTK_FILL, 0, 0);
	gtk_widget_show (combo);

	gtk_label_set_mnemonic_widget (GTK_LABEL (label), combo);
	if (QWISaveData.maxlayers == 1) {
		gimp_int_combo_box_set_active (GIMP_INT_COMBO_BOX (combo), 1);
		gtk_widget_set_sensitive (combo, FALSE);
	}
	else {
		gimp_int_combo_box_set_active (GIMP_INT_COMBO_BOX (combo), 0);

		g_signal_connect (combo, "changed",
				G_CALLBACK (gimp_int_combo_box_get_active),
				&QWISaveData.toplayer);
	}
	/* resiliency */
	label = gtk_label_new_with_mnemonic ("_Resiliency mode:");
	gtk_misc_set_alignment (GTK_MISC (label), 0.0, 0.5);
	gtk_table_attach (GTK_TABLE (table), label, 0, 1, 1, 2,
			GTK_FILL, GTK_FILL, 0, 0);
	gtk_widget_show (label);

	pg.resiliency =
			combo = gimp_int_combo_box_new ("None (local use)", 0,
					"Intermediate", 1,
					"Full (best resistance on bad network)", 2,
					NULL);
	gtk_table_attach (GTK_TABLE (table), combo, 1, 2, 1, 2,
			GTK_FILL | GTK_EXPAND, GTK_FILL, 0, 0);
	gtk_widget_show (combo);

	gtk_label_set_mnemonic_widget (GTK_LABEL (label), combo);

	gimp_int_combo_box_set_active (GIMP_INT_COMBO_BOX (combo),QWISaveData.resiliency);

	g_signal_connect (combo, "changed",
			G_CALLBACK (gimp_int_combo_box_get_active),
			&QWISaveData.resiliency);

	/* Subsampling */
	label = gtk_label_new_with_mnemonic ("Su_bsampling:");
	gtk_misc_set_alignment (GTK_MISC (label), 0.0, 0.5);
	gtk_table_attach (GTK_TABLE (table), label, 0, 1, 3, 4,
			GTK_FILL, GTK_FILL, 0, 0);
	gtk_widget_show (label);

	pg.subsampling =
			combo = gimp_int_combo_box_new ("Auto", 0,
					"4:4:4 (best quality)", 1,
					"4:2:2 horizontal (chroma halved)", 2,
					"4:2:2 vertical (chroma halved)", 3,
					"4:2:0 (chroma quartered)", 4,
					NULL);
	gtk_table_attach (GTK_TABLE (table), combo, 1, 2, 3, 4,
			GTK_FILL | GTK_EXPAND, GTK_FILL, 0, 0);
	gtk_widget_show (combo);

	gtk_label_set_mnemonic_widget (GTK_LABEL (label), combo);

	gimp_int_combo_box_set_active (GIMP_INT_COMBO_BOX (combo), QWISaveData.subsampling);
	if (channels < 3)
		gtk_widget_set_sensitive (combo, FALSE);
	else
		g_signal_connect (combo, "changed",
				G_CALLBACK (gimp_int_combo_box_get_active),
				&QWISaveData.subsampling);

	// default qualities buttons
	vbox = gtk_box_new (GTK_ORIENTATION_VERTICAL, 12);
	gtk_container_set_border_width (GTK_CONTAINER (vbox), 12);
	gtk_box_pack_start (GTK_BOX (gimp_export_dialog_get_content_area (dialog)),
			vbox, TRUE, TRUE, 0);
	gtk_widget_show (vbox);

	table = gtk_table_new (1, 1, FALSE);
	gtk_table_set_col_spacings (GTK_TABLE (table), 6);
	gtk_box_pack_start (GTK_BOX (vbox), table, FALSE, FALSE, 0);
	gtk_widget_show (table);

	label = gtk_label_new_with_mnemonic ("Scri_pt");
	gtk_misc_set_alignment (GTK_MISC (label), 0.0, 0.5);
	gtk_table_attach (GTK_TABLE (table), label, 0, 1, 0, 1,
			GTK_FILL, GTK_FILL, 0, 0);
	gtk_widget_show (label);

	scrolled_window = gtk_scrolled_window_new (NULL, NULL);
	gtk_scrolled_window_set_shadow_type (GTK_SCROLLED_WINDOW (scrolled_window),
			GTK_SHADOW_IN);
	gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (scrolled_window),
			GTK_POLICY_AUTOMATIC,
			GTK_POLICY_AUTOMATIC);
	gtk_widget_set_size_request (scrolled_window, 250, 300);
	gtk_container_add (GTK_CONTAINER (vbox), scrolled_window);
	gtk_widget_show (scrolled_window);

	pg.text_buffer = text_buffer = gtk_text_buffer_new (NULL);

	text_view = gtk_text_view_new_with_buffer (text_buffer);
	gtk_text_view_set_wrap_mode (GTK_TEXT_VIEW (text_view), GTK_WRAP_WORD);

	gtk_container_add (GTK_CONTAINER (scrolled_window), text_view);
  if (globalcode)
    gtk_text_buffer_set_text (text_buffer, globalcode, -1);

	gtk_widget_show (text_view);

	/* Dialog show */
	gtk_widget_show (dialog);

	run = (gimp_dialog_run (GIMP_DIALOG (dialog)) == GTK_RESPONSE_OK);

	gtk_text_buffer_get_bounds (pg.text_buffer, &start_iter, &end_iter);
	globalcode = gtk_text_buffer_get_text (pg.text_buffer, &start_iter, &end_iter, FALSE);

	gtk_widget_destroy (dialog);

	return run;
}
