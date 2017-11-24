/*
 *  Generic Monitor plugin for the Xfce4 panel
 *  Main file for the Battmon plugin
 *  Copyright (c) 2004 Roger Seguin <roger_seguin@msn.com>
 *                                  <http://rmlx.dyndns.org>
 *  Copyright (c) 2006 Julien Devemy <jujucece@gmail.com>
 *  Copyright (c) 2012 John Lindgren <john.lindgren@aol.com>
 *
 *  This library is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU Lesser General Public
 *  License as published by the Free Software Foundation; either
 *  version 2.1 of the License, or (at your option) any later version.

 *  This library is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *  Lesser General Public License for more details.

 *  You should have received a copy of the GNU Lesser General Public
 *  License along with this library; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include "config_gui.h"

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <gtk/gtk.h>

#include <libxfce4panel/xfce-panel-convenience.h>
#include <libxfce4panel/xfce-panel-plugin.h>
#include <libxfce4ui/libxfce4ui.h>
#include <libxfce4util/libxfce4util.h>

#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define PLUGIN_NAME "Battmon"
#define BORDER 2

typedef struct param_t {
  /* Configurable parameters */
  uint32_t iPeriod_ms;
  char *acFont;
} param_t;

typedef struct conf_t {
  GtkWidget *wTopLevel;
  struct gui_t oGUI; /* Configuration/option dialog */
  struct param_t oParam;
} conf_t;

typedef struct monitor_t {
  /* Plugin monitor */
  GtkWidget *wEventBox;
  GtkWidget *wBox;
  GtkWidget *wImgBox;
  GtkWidget *wValue;
  GtkWidget *wImage;
} monitor_t;

typedef struct battmon_t {
  XfcePanelPlugin *plugin;
  unsigned int iTimerId; /* Cyclic update */
  struct conf_t oConf;
  struct monitor_t oMonitor;
} battmon_t;

typedef enum battstatus_t {
  BattStatus_NoBatt,
  BattStatus_Full,
  BattStatus_Charging,
  BattStatus_Discharging,
  BattStatus_Unknown
} battstatus_t;

typedef enum battlevel_t {
  BattLevel_Full,
  BattLevel_OK,
  BattLevel_Low,
  BattLevel_Critical,
  BattLevel_Unknown,
} battlevel_t;

static battlevel_t GetBatteryLevel(int percent) {
  if(percent > 100)
    percent = 100;
  else if(percent < 0)
    percent = 0;

  if(percent > 90)
    return BattLevel_Full;
  else if(percent > 45)
    return BattLevel_OK;
  else if(percent > 15)
    return BattLevel_Low;
  else
    return BattLevel_Critical;
}

static int GetBatteryPercent() {
  const char* file = "/sys/class/power_supply/BAT0/capacity";
  FILE* fp = NULL;
  int percent = 0;
  
  if(fp = fopen(file, "r")) {
    fscanf(fp, "%d", &percent);
    return percent;
  }

  return percent;
}

static battstatus_t GetBatteryStatus() {
  const char* file = "/sys/class/power_supply/BAT0/status";
  char status[256];
  FILE* fp = NULL;
  
  if(fp = fopen(file, "r")) {
    fgets(status, 256, fp);
    if(strcmp(status, "Full\n") == 0)
      return BattStatus_Full;
    else if(strcmp(status, "Charging\n") == 0)
      return BattStatus_Charging;
    else if(strcmp(status, "Discharging\n") == 0)
      return BattStatus_Discharging;
    return BattStatus_Unknown;
  }
  
  return BattStatus_NoBatt;
}

static int GetBatteryTime(int* hrs, int* mins) {
  const char* fileCharge = "/sys/class/power_supply/BAT0/charge_now";
  const char* fileEnergy = "/sys/class/power_supply/BAT0/energy_now";
  const char* fileCurrent = "/sys/class/power_supply/BAT0/current_now";
  const char* filePower = "/sys/class/power_supply/BAT0/power_now";
  FILE* fpTotal, *fpNow;
  long total = 0, now = 1;
  double ratio = 0.0;

  if(fpTotal = fopen(fileCharge, "r"))
    ;
  else if(fpTotal = fopen(fileEnergy, "r"))
    ;
  
  if(fpNow = fopen(fileCurrent, "r"))
    ;
  else if(fpNow = fopen(filePower, "r"))
    ;
  
  if(fpTotal && fpNow) {
    fscanf(fpTotal, "%ld", &total);
    fscanf(fpNow, "%ld", &now);
    ratio = (float) total / (float) now;
    *hrs = floor(ratio);
    *mins = floor((ratio - *hrs) * 60);

    fclose(fpTotal);
    fclose(fpNow);
    
    return 1;
  }

  return 0;
}

/**************************************************************/
static int DisplayBatteryLevel(struct battmon_t *p_poPlugin)
/* Launch the command, get its output and display it in the panel-docked
   text field */
{
  const char *colors[] = {
    "#FF0000", "#FF1A00", "#FF3500", "#FF5000", "#FF6B00", "#FF8600", "#FFA100",
    "#FFBB00", "#FFD600", "#FFF100", "#F1FF00", "#D6FF00", "#BBFF00", "#A1FF00",
    "#86FF00", "#6BFF00", "#50FF00", "#35FF00", "#1AFF00", "#00FF00"};
  int steps = 100 / (sizeof(colors) / sizeof(const char*));
  struct param_t *poConf = &(p_poPlugin->oConf.oParam);
  struct monitor_t *poMonitor = &(p_poPlugin->oMonitor);
  const char* img_dir = "/usr/share/icons/hicolor/24x24/apps/xfce4-battery-";
  const char* space = "                  ";
  int percent = 0, hrs = -1, mins = -1;
  battstatus_t status = BattStatus_NoBatt;
  char img[PATH_MAX];
  char time[512];
  
  percent = GetBatteryPercent();
  status = GetBatteryStatus();
  switch(status) {
  case BattStatus_Full:
    snprintf(img, PATH_MAX, "%s%s.png", img_dir, "full-charging");
    break;
  case BattStatus_Charging:
    switch(GetBatteryLevel(percent)) {
    case BattLevel_Full:
      snprintf(img, PATH_MAX, "%s%s.png", img_dir, "full-charging");
      break;
    case BattLevel_OK:
      snprintf(img, PATH_MAX, "%s%s.png", img_dir, "ok-charging");
      break;
    case BattLevel_Low:
      snprintf(img, PATH_MAX, "%s%s.png", img_dir, "low-charging");
      break;
    default:
      /* Should never get here */
      break;
    }
    break;
  case BattStatus_Discharging:
    switch(GetBatteryLevel(percent)) {
    case BattLevel_Full:
      snprintf(img, PATH_MAX, "%s%s.png", img_dir, "full");
      break;
    case BattLevel_OK:
      snprintf(img, PATH_MAX, "%s%s.png", img_dir, "ok");
      break;
    case BattLevel_Low:
      snprintf(img, PATH_MAX, "%s%s.png", img_dir, "low");
      break;
    case BattLevel_Critical:
      snprintf(img, PATH_MAX, "%s%s.png", img_dir, "critical");
      break;
    default:
      /* Should never get here */
      break;
    }
    break;
  case BattStatus_Unknown:
    snprintf(img, PATH_MAX, "%s%s.png", img_dir, "missing");
    break;
  case BattStatus_NoBatt:
    snprintf(img, PATH_MAX, "%s%s.png", img_dir, "missing");
    break;
  default:
    /* Should never get here */
    break;
  }

  if(GetBatteryTime(&hrs, &mins)) {
    const char* color = colors[percent / steps];
    snprintf(time, sizeof(time),
             "<span font='2' bgcolor='%s' weight='Bold'>%s</span>"
             "\n"
             "<span color='black' bgcolor='%s' weight='Bold'> %1d:%02d </span>"
             "\n"
             "<span font='2' bgcolor='%s' weight='Bold'>%s</span>",
             color, space, color, hrs, mins, color, space);
  } else {
    if(status == BattStatus_Charging)
      snprintf(time, sizeof(time),
               "<span color='black' bgcolor='skyblue'>----</span>");
    else
      snprintf(time, sizeof(time),
               "<span color='black' bgcolor='gray'>----</span>");
  }
  gtk_label_set_markup(GTK_LABEL(poMonitor->wValue), time);
  gtk_image_set_from_file(GTK_IMAGE(poMonitor->wImage), img);
  
  gtk_widget_show(poMonitor->wImage);
  gtk_widget_show(poMonitor->wValue);

  return (0);

} /* DisplayBatteryLevel() */

/**************************************************************/

static gboolean SetTimer(void *p_pvPlugin)
/* Recurrently update the panel-docked monitor through a timer */
/* Warning : should not be called directly (except the 1st time) */
/* To avoid multiple timers */
{
  struct battmon_t *poPlugin = (battmon_t *)p_pvPlugin;
  struct param_t *poConf = &(poPlugin->oConf.oParam);

  DisplayBatteryLevel(poPlugin);

  if (poPlugin->iTimerId == 0) {
    poPlugin->iTimerId =
        g_timeout_add(poConf->iPeriod_ms, (GSourceFunc)SetTimer, poPlugin);
    return FALSE;
  }

  return TRUE;
} /* SetTimer() */

/**************************************************************/

static battmon_t *battmon_create_control(XfcePanelPlugin *plugin)
/* Plugin API */
/* Create the plugin */
{
  struct battmon_t *poPlugin;
  struct param_t *poConf;
  struct monitor_t *poMonitor;
  GtkOrientation orientation = xfce_panel_plugin_get_orientation(plugin);
  GtkSettings *settings;
  gchar *default_font;

#if GTK_CHECK_VERSION(3, 16, 0)
  GtkStyleContext *context;
  GtkCssProvider *css_provider;
  gchar *css;
#endif

  poPlugin = g_new(battmon_t, 1);
  memset(poPlugin, 0, sizeof(battmon_t));
  poConf = &(poPlugin->oConf.oParam);
  poMonitor = &(poPlugin->oMonitor);

  poPlugin->plugin = plugin;

  poConf->iPeriod_ms = 30 * 1000;
  poPlugin->iTimerId = 0;

  // PangoFontDescription needs a font and we can't use "(Default)" anymore.
  // Use GtkSettings to get the current default font and use that, or set
  // default to "Sans 10"
  settings = gtk_settings_get_default();
  if (g_object_class_find_property(G_OBJECT_GET_CLASS(settings),
                                   "gtk-font-name")) {
    g_object_get(settings, "gtk-font-name", &default_font, NULL);
    poConf->acFont = g_strdup(default_font);
  } else
    poConf->acFont = g_strdup("Sans 9.8");

  poMonitor->wEventBox = gtk_event_box_new();
  gtk_event_box_set_visible_window(GTK_EVENT_BOX(poMonitor->wEventBox), FALSE);
  gtk_widget_show(poMonitor->wEventBox);

  xfce_panel_plugin_add_action_widget(plugin, poMonitor->wEventBox);

  poMonitor->wBox = gtk_box_new(orientation, 0);
#if GTK_CHECK_VERSION(3, 16, 0)
  context = gtk_widget_get_style_context(poMonitor->wBox);
  gtk_style_context_add_class(context, "battmon_plugin");
#endif
  gtk_widget_show(poMonitor->wBox);
  gtk_container_set_border_width(GTK_CONTAINER(poMonitor->wBox), 0);
  gtk_container_add(GTK_CONTAINER(poMonitor->wEventBox), poMonitor->wBox);

  /* Create a Box to put image and text */
  poMonitor->wImgBox = gtk_box_new(orientation, 0);
#if GTK_CHECK_VERSION(3, 16, 0)
  context = gtk_widget_get_style_context(poMonitor->wImgBox);
  gtk_style_context_add_class(context, "battmon_imagebox");
#endif
  gtk_widget_show(poMonitor->wImgBox);
  gtk_container_set_border_width(GTK_CONTAINER(poMonitor->wImgBox), 0);
  gtk_container_add(GTK_CONTAINER(poMonitor->wBox), poMonitor->wImgBox);

  /* Add Image */
  poMonitor->wImage = gtk_image_new();
#if GTK_CHECK_VERSION(3, 16, 0)
  context = gtk_widget_get_style_context(poMonitor->wImage);
  gtk_style_context_add_class(context, "battmon_image");
#endif
  gtk_box_pack_start(GTK_BOX(poMonitor->wImgBox), GTK_WIDGET(poMonitor->wImage),
                     TRUE, FALSE, 0);

  /* Add Value */
  poMonitor->wValue = gtk_label_new("");
#if GTK_CHECK_VERSION(3, 16, 0)
  context = gtk_widget_get_style_context(poMonitor->wValue);
  gtk_style_context_add_class(context, "battmon_value");
#endif
  gtk_widget_show(poMonitor->wValue);
  gtk_box_pack_start(GTK_BOX(poMonitor->wImgBox), GTK_WIDGET(poMonitor->wValue),
                     TRUE, FALSE, 0);

/* make widget padding consistent */
#if GTK_CHECK_VERSION(3, 16, 0)
#if GTK_CHECK_VERSION(3, 20, 0)
  css = g_strdup_printf("\
            progressbar.horizontal trough { min-height: 6px; }\
            progressbar.horizontal progress { min-height: 6px; }\
            progressbar.vertical trough { min-width: 6px; }\
            progressbar.vertical progress { min-width: 6px; }");
#else
  css = g_strdup_printf("\
            .progressbar.horizontal trough { min-height: 6px; }\
            .progressbar.horizontal progress { min-height: 6px; }\
            .progressbar.vertical trough { min-width: 6px; }\
            .progressbar.vertical progress { min-width: 6px; }");
#endif

  css_provider = gtk_css_provider_new();
  gtk_css_provider_load_from_data(css_provider, css, strlen(css), NULL);
  gtk_style_context_add_provider(GTK_STYLE_CONTEXT(gtk_widget_get_style_context(
                                     GTK_WIDGET(poMonitor->wImage))),
                                 GTK_STYLE_PROVIDER(css_provider),
                                 GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
  gtk_style_context_add_provider(GTK_STYLE_CONTEXT(gtk_widget_get_style_context(
                                     GTK_WIDGET(poMonitor->wValue))),
                                 GTK_STYLE_PROVIDER(css_provider),
                                 GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);

  g_free(css);
#endif

  g_free(default_font);

  return poPlugin;
} /* battmon_create_control() */

/**************************************************************/

static void battmon_free(XfcePanelPlugin *plugin, battmon_t *poPlugin)
/* Plugin API */
{
  TRACE("battmon_free()\n");

  if (poPlugin->iTimerId)
    g_source_remove(poPlugin->iTimerId);

  g_free(poPlugin->oConf.oParam.acFont);
  g_free(poPlugin);
} /* battmon_free() */

/**************************************************************/

static int SetMonitorFont(void *p_pvPlugin) {
  struct battmon_t *poPlugin = (battmon_t *)p_pvPlugin;
  struct monitor_t *poMonitor = &(poPlugin->oMonitor);
  struct param_t *poConf = &(poPlugin->oConf.oParam);

#if GTK_CHECK_VERSION(3, 16, 0)
  GtkCssProvider *css_provider;
  gchar *css;
#if GTK_CHECK_VERSION(3, 20, 0)
  PangoFontDescription *font;
  font = pango_font_description_from_string(poConf->acFont);
  if (G_LIKELY(font)) {
    css = g_strdup_printf(
        "label { font-family: %s; font-size: %dpx; font-style: %s; "
        "font-weight: %s }",
        pango_font_description_get_family(font),
        pango_font_description_get_size(font) / PANGO_SCALE,
        (pango_font_description_get_style(font) == PANGO_STYLE_ITALIC ||
         pango_font_description_get_style(font) == PANGO_STYLE_OBLIQUE)
            ? "italic"
            : "normal",
        (pango_font_description_get_weight(font) >= PANGO_WEIGHT_BOLD)
            ? "bold"
            : "normal");
    pango_font_description_free(font);
  } else
    css = g_strdup_printf("label { font: %s; }",
#else
  css = g_strdup_printf(".label { font: %s; }",
#endif
                          poConf->acFont);
  /* Setup Gtk style */
  DBG("css: %s", css);

  css_provider = gtk_css_provider_new();
  gtk_css_provider_load_from_data(css_provider, css, strlen(css), NULL);
  gtk_style_context_add_provider(GTK_STYLE_CONTEXT(gtk_widget_get_style_context(
                                     GTK_WIDGET(poMonitor->wValue))),
                                 GTK_STYLE_PROVIDER(css_provider),
                                 GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
  g_free(css);
#else

  PangoFontDescription *poFont;

  if (!strcmp(poConf->acFont, "(default)")) /* Default font */
    return (1);
  poFont = pango_font_description_from_string(poConf->acFont);
  if (!poFont)
    return (-1);

  gtk_widget_override_font(poMonitor->wValue, poFont);
  gtk_widget_override_font(poMonitor->wValButton, poFont);

  pango_font_description_free(poFont);

#endif

  return (0);
} /* SetMonitorFont() */

/**************************************************************/

/* Configuration Keywords */
#define CONF_USE_LABEL "UseLabel"
#define CONF_LABEL_TEXT "Text"
#define CONF_CMD "Command"
#define CONF_UPDATE_PERIOD "UpdatePeriod"
#define CONF_FONT "Font"

/**************************************************************/

static void battmon_read_config(XfcePanelPlugin *plugin, battmon_t *poPlugin)
/* Plugin API */
/* Executed when the panel is started - Read the configuration
   previously stored in xml file */
{
  struct param_t *poConf = &(poPlugin->oConf.oParam);
  struct monitor_t *poMonitor = &(poPlugin->oMonitor);
  const char *pc;
  char *file;
  XfceRc *rc;

  if (!(file = xfce_panel_plugin_lookup_rc_file(plugin)))
    return;

  rc = xfce_rc_simple_open(file, TRUE);
  g_free(file);

  if (!rc)
    return;

  poConf->iPeriod_ms =
      xfce_rc_read_int_entry(rc, (CONF_UPDATE_PERIOD), 30 * 1000);

  if ((pc = xfce_rc_read_entry(rc, (CONF_FONT), NULL))) {
    g_free(poConf->acFont);
    poConf->acFont = g_strdup(pc);
  }

  xfce_rc_close(rc);
} /* battmon_read_config() */

/**************************************************************/

static void battmon_write_config(XfcePanelPlugin *plugin, battmon_t *poPlugin)
/* Plugin API */
/* Write plugin configuration into xml file */
{
  struct param_t *poConf = &(poPlugin->oConf.oParam);
  XfceRc *rc;
  char *file;

  if (!(file = xfce_panel_plugin_save_location(plugin, TRUE)))
    return;

  rc = xfce_rc_simple_open(file, FALSE);
  g_free(file);

  if (!rc)
    return;

  TRACE("battmon_write_config()\n");

  xfce_rc_write_int_entry(rc, CONF_UPDATE_PERIOD, poConf->iPeriod_ms);

  xfce_rc_write_entry(rc, CONF_FONT, poConf->acFont);

  xfce_rc_close(rc);
} /* battmon_write_config() */

/**************************************************************/

static void SetPeriod(GtkWidget *p_wSc, void *p_pvPlugin)
/* Set the update period - To be used by the timer */
{
  struct battmon_t *poPlugin = (battmon_t *)p_pvPlugin;
  struct param_t *poConf = &(poPlugin->oConf.oParam);
  float r;

  TRACE("SetPeriod()\n");
  r = gtk_spin_button_get_value(GTK_SPIN_BUTTON(p_wSc));
  poConf->iPeriod_ms = (r * 1000);
} /* SetPeriod() */

/**************************************************************/

static void UpdateConf(void *p_pvPlugin)
/* Called back when the configuration/options window is closed */
{
  struct battmon_t *poPlugin = (battmon_t *)p_pvPlugin;
  struct conf_t *poConf = &(poPlugin->oConf);
  struct gui_t *poGUI = &(poConf->oGUI);

  TRACE("UpdateConf()\n");
  SetMonitorFont(poPlugin);
  /* Restart timer */
  if (poPlugin->iTimerId) {
    g_source_remove(poPlugin->iTimerId);
    poPlugin->iTimerId = 0;
  }
  SetTimer(p_pvPlugin);
} /* UpdateConf() */

/**************************************************************/

static void About(XfcePanelPlugin *plugin) {
  GdkPixbuf *icon;

  const gchar *auth[] = {"Tarun Prabhu <tarun.prabhu@gmail.com>", NULL };

  icon = xfce_panel_pixbuf_from_source("battery", NULL, 32);
  gtk_show_about_dialog(
      NULL, "logo", icon, "license",
      xfce_get_license_text(XFCE_LICENSE_TEXT_GPL), "version", VERSION,
      "program-name", PACKAGE, "comments",
      _("Monitors the battery"),
      "website",
      "",
      "copyright",
      _("Copyright \xc2\xa9 2017 Tarun Prabhu\n"),
      "author", auth, NULL);

  if (icon)
    g_object_unref(G_OBJECT(icon));
}

/**************************************************************/

static void ChooseFont(GtkWidget *p_wPB, void *p_pvPlugin) {
  struct battmon_t *poPlugin = (battmon_t *)p_pvPlugin;
  struct param_t *poConf = &(poPlugin->oConf.oParam);
  GtkWidget *wDialog;
  const char *pcFont;
  int iResponse;

  wDialog = gtk_font_chooser_dialog_new(
      _("Font Selection"), GTK_WINDOW(gtk_widget_get_toplevel(p_wPB)));
  gtk_window_set_transient_for(GTK_WINDOW(wDialog),
                               GTK_WINDOW(poPlugin->oConf.wTopLevel));
  if (strcmp(poConf->acFont, "(default)")) /* Default font */
    gtk_font_chooser_set_font(GTK_FONT_CHOOSER(wDialog), poConf->acFont);
  iResponse = gtk_dialog_run(GTK_DIALOG(wDialog));
  if (iResponse == GTK_RESPONSE_OK) {
    pcFont = gtk_font_chooser_get_font(GTK_FONT_CHOOSER(wDialog));
    if (pcFont) {
      g_free(poConf->acFont);
      poConf->acFont = g_strdup(pcFont);
      gtk_button_set_label(GTK_BUTTON(p_wPB), poConf->acFont);
    }
  }
  gtk_widget_destroy(wDialog);
} /* ChooseFont() */

/**************************************************************/

static void battmon_dialog_response(GtkWidget *dlg, int response,
                                    battmon_t *battmon) {
  UpdateConf(battmon);
  gtk_widget_destroy(dlg);
  xfce_panel_plugin_unblock_menu(battmon->plugin);
  battmon_write_config(battmon->plugin, battmon);
  /* Do not wait the next timer to update display */
  DisplayBatteryLevel(battmon);
}

static void battmon_create_options(XfcePanelPlugin *plugin, battmon_t *poPlugin)
/* Plugin API */
/* Create/pop up the configuration/options GUI */
{
  GtkWidget *dlg, *vbox;
  struct param_t *poConf = &(poPlugin->oConf.oParam);
  struct gui_t *poGUI = &(poPlugin->oConf.oGUI);

  TRACE("battmon_create_options()\n");

  xfce_panel_plugin_block_menu(plugin);

  dlg = xfce_titled_dialog_new_with_buttons(
      _("Configuration"),
      GTK_WINDOW(gtk_widget_get_toplevel(GTK_WIDGET(plugin))),
      GTK_DIALOG_DESTROY_WITH_PARENT, "gtk-close", GTK_RESPONSE_OK, NULL);

  g_signal_connect(dlg, "response", G_CALLBACK(battmon_dialog_response),
                   poPlugin);

  xfce_titled_dialog_set_subtitle(XFCE_TITLED_DIALOG(dlg),
                                  _("Battery Monitor"));

  vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, BORDER + 6);
  gtk_container_set_border_width(GTK_CONTAINER(vbox), BORDER + 4);
  gtk_widget_show(vbox);
  gtk_box_pack_start(GTK_BOX(gtk_dialog_get_content_area(GTK_DIALOG(dlg))),
                     vbox, TRUE, TRUE, 0);

  poPlugin->oConf.wTopLevel = dlg;

  (void)battmon_CreateConfigGUI(GTK_WIDGET(vbox), poGUI);

  gtk_spin_button_set_value(GTK_SPIN_BUTTON(poGUI->wSc_Period),
                            ((double)poConf->iPeriod_ms / 1000));
  g_signal_connect(GTK_WIDGET(poGUI->wSc_Period), "value_changed",
                   G_CALLBACK(SetPeriod), poPlugin);

  if (strcmp(poConf->acFont, "(default)")) /* Default font */
    gtk_button_set_label(GTK_BUTTON(poGUI->wPB_Font), poConf->acFont);
  g_signal_connect(G_OBJECT(poGUI->wPB_Font), "clicked", G_CALLBACK(ChooseFont),
                   poPlugin);

  gtk_widget_show(dlg);
} /* battmon_create_options() */

/**************************************************************/

static void battmon_set_orientation(XfcePanelPlugin *plugin,
                                    GtkOrientation p_iOrientation,
                                    battmon_t *poPlugin)
/* Plugin API */
/* Invoked when the panel changes orientation */
{
  struct monitor_t *poMonitor = &(poPlugin->oMonitor);

  gtk_orientable_set_orientation(GTK_ORIENTABLE(poMonitor->wBox),
                                 p_iOrientation);
  gtk_orientable_set_orientation(GTK_ORIENTABLE(poMonitor->wImgBox),
                                 p_iOrientation);

  SetMonitorFont(poPlugin);

} /* battmon_set_orientation() */

/**************************************************************/
// call: xfce4-panel --plugin-event=battmon-X:refresh:bool:true
//    where battmon-X is the battmon widget id (e.g. battmon-7)

static gboolean battmon_remote_event(XfcePanelPlugin *plugin, const gchar *name,
                                     const GValue *value, battmon_t *battmon) {
  g_return_val_if_fail(value == NULL || G_IS_VALUE(value), FALSE);
  if (strcmp(name, "refresh") == 0) {
    if (value != NULL && G_VALUE_HOLDS_BOOLEAN(value) &&
        g_value_get_boolean(value)) {
      /* update the display */
      DisplayBatteryLevel(battmon);
    }
    return TRUE;
  }

  return FALSE;
} /* battmon_remote_event() */

/**************************************************************/

static void battmon_construct(XfcePanelPlugin *plugin) {
  battmon_t *battmon;
  
  battmon = battmon_create_control(plugin);

  battmon_read_config(plugin, battmon);

  gtk_container_add(GTK_CONTAINER(plugin), battmon->oMonitor.wEventBox);

  SetMonitorFont(battmon);
  SetTimer(battmon);

  g_signal_connect(plugin, "free-data", G_CALLBACK(battmon_free), battmon);

  g_signal_connect(plugin, "save", G_CALLBACK(battmon_write_config), battmon);

  g_signal_connect(plugin, "orientation-changed",
                   G_CALLBACK(battmon_set_orientation), battmon);

  xfce_panel_plugin_menu_show_about(plugin);

  g_signal_connect(plugin, "about", G_CALLBACK(About), plugin);

  xfce_panel_plugin_menu_show_configure(plugin);
  g_signal_connect(plugin, "configure-plugin",
                   G_CALLBACK(battmon_create_options), battmon);

  g_signal_connect(plugin, "remote-event", G_CALLBACK(battmon_remote_event),
                   battmon);

}

XFCE_PANEL_PLUGIN_REGISTER(battmon_construct)
