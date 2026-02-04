/*
 * XFCE Volume Mixer Panel Plugin
 * Per-application volume control using PulseAudio
 *
 * UI: Popup window via xfce_panel_plugin_popup_window() so the panel positions
 *     it correctly and we control size (GtkPopover was unusably small).
 * - Undecorated window, fixed size, closes on focus-out.
 * - Vertical GtkScale sliders (one per application), Windows 7 mixer style.
 */

#include <gtk/gtk.h>
#include <libxfce4panel/libxfce4panel.h>
#include <pulse/pulseaudio.h>
#include <pulse/glib-mainloop.h>

/* Plugin structure - inherits from XfcePanelPlugin for proper GType registration */
typedef struct _VolumeMixerPlugin {
    XfcePanelPlugin parent;
    
    GtkWidget *button;
    GtkWidget *icon;
    GtkWindow *popup_window;  /* Panel popup window (positioned by xfce_panel_plugin_popup_window) */
    GtkWidget *mixer_box;
    
    pa_glib_mainloop *pa_mainloop;
    pa_context *pa_context;
    
    gboolean connected;
    GHashTable *sink_inputs; /* Maps sink_input index to slider widget */
} VolumeMixerPlugin;

typedef struct _VolumeMixerPluginClass {
    XfcePanelPluginClass parent_class;
} VolumeMixerPluginClass;

G_DEFINE_TYPE(VolumeMixerPlugin, volume_mixer_plugin, XFCE_TYPE_PANEL_PLUGIN)

typedef struct {
    guint32 index;
    gchar *name;
    gchar *app_name;
    gchar *icon_name;  /* Application icon name from PulseAudio properties */
    pa_cvolume volume;
    guint32 sink;
    VolumeMixerPlugin *plugin;
} SinkInputInfo;

/* Forward declarations */
static void update_icon(VolumeMixerPlugin *mixer);
static void connect_to_pulseaudio(VolumeMixerPlugin *mixer);

/* Cleanup sink input info */
static void sink_input_info_free(SinkInputInfo *info) {
    if (info) {
        g_free(info->name);
        g_free(info->app_name);
        g_free(info->icon_name);
        g_free(info);
    }
}

/* Volume slider changed callback */
static void on_volume_changed(GtkRange *range, gpointer user_data) {
    SinkInputInfo *info = (SinkInputInfo *)user_data;
    VolumeMixerPlugin *mixer = info->plugin;
    
    if (!mixer->pa_context || !mixer->connected)
        return;
    
    gdouble value = gtk_range_get_value(range);
    pa_cvolume new_volume = info->volume;
    
    /* Set all channels to the same volume */
    pa_cvolume_set(&new_volume, new_volume.channels, (pa_volume_t)(value * PA_VOLUME_NORM / 100.0));
    
    /* Update PulseAudio */
    pa_context_set_sink_input_volume(mixer->pa_context, info->index, &new_volume, NULL, NULL);
}

/* Channel column layout constants */
#define CHANNEL_ICON_PIXEL_SIZE  32
#define CHANNEL_LABEL_WIDTH_PX  96
#define CHANNEL_COLUMN_SPACING  6
#define CHANNEL_SLIDER_MIN_HEIGHT 140

/* Create one channel column: icon, label, vertical slider in a vertical GtkBox */
static GtkWidget *create_app_volume_slider(SinkInputInfo *info) {
    GtkWidget *box, *icon, *label, *slider;
    gdouble current_volume;
    const gchar *icon_name;
    
    box = gtk_box_new(GTK_ORIENTATION_VERTICAL, CHANNEL_COLUMN_SPACING);
    
    /* Icon: fixed pixel size, centered */
    icon_name = (info->icon_name && info->icon_name[0] != '\0')
                ? info->icon_name
                : "audio-volume-high-symbolic";
    icon = gtk_image_new_from_icon_name(icon_name, GTK_ICON_SIZE_LARGE_TOOLBAR);
    gtk_image_set_pixel_size(GTK_IMAGE(icon), CHANNEL_ICON_PIXEL_SIZE);
    gtk_widget_set_halign(icon, GTK_ALIGN_CENTER);
    gtk_box_pack_start(GTK_BOX(box), icon, FALSE, FALSE, 0);
    
    /* Label: fixed width, single line, ellipsize, center-aligned */
    label = gtk_label_new(info->app_name ? info->app_name : "Unknown Application");
    gtk_widget_set_halign(label, GTK_ALIGN_CENTER);
    gtk_widget_set_size_request(label, CHANNEL_LABEL_WIDTH_PX, -1);
    gtk_label_set_ellipsize(GTK_LABEL(label), PANGO_ELLIPSIZE_END);
    gtk_label_set_single_line_mode(GTK_LABEL(label), TRUE);
    gtk_label_set_max_width_chars(GTK_LABEL(label), 12);
    gtk_box_pack_start(GTK_BOX(box), label, FALSE, FALSE, 0);
    
    /* Slider: vertical, min height, expands vertically within column, no horizontal expansion */
    current_volume = (gdouble)pa_cvolume_avg(&info->volume) * 100.0 / PA_VOLUME_NORM;
    slider = gtk_scale_new_with_range(GTK_ORIENTATION_VERTICAL, 0.0, 100.0, 5.0);
    gtk_range_set_value(GTK_RANGE(slider), current_volume);
    gtk_range_set_inverted(GTK_RANGE(slider), TRUE); /* 0 at bottom, 100 at top */
    gtk_widget_set_halign(slider, GTK_ALIGN_CENTER);
    gtk_widget_set_vexpand(slider, TRUE);
    gtk_widget_set_hexpand(slider, FALSE);
    gtk_widget_set_size_request(slider, -1, CHANNEL_SLIDER_MIN_HEIGHT);
    gtk_scale_set_draw_value(GTK_SCALE(slider), TRUE);
    gtk_scale_set_value_pos(GTK_SCALE(slider), GTK_POS_TOP);
    g_signal_connect(slider, "value-changed", G_CALLBACK(on_volume_changed), info);
    gtk_box_pack_start(GTK_BOX(box), slider, TRUE, TRUE, 0);
    
    return box;
}

/* Update the mixer UI with current applications */
static void update_mixer_ui(VolumeMixerPlugin *mixer) {
    /* Clear existing widgets */
    gtk_container_foreach(GTK_CONTAINER(mixer->mixer_box), 
                         (GtkCallback)gtk_widget_destroy, NULL);
    
    if (g_hash_table_size(mixer->sink_inputs) == 0) {
        GtkWidget *label = gtk_label_new("No applications playing audio");
        gtk_box_pack_start(GTK_BOX(mixer->mixer_box), label, TRUE, TRUE, 5);
    } else {
        GHashTableIter iter;
        gpointer key, value;
        /* Each channel = vertical column (label + slider), packed horizontally */
        g_hash_table_iter_init(&iter, mixer->sink_inputs);
        while (g_hash_table_iter_next(&iter, &key, &value)) {
            SinkInputInfo *info = (SinkInputInfo *)value;
            GtkWidget *column = create_app_volume_slider(info);
            gtk_box_pack_start(GTK_BOX(mixer->mixer_box), column, FALSE, FALSE, 8);
        }
    }
    
    gtk_widget_show_all(mixer->mixer_box);
    /* Resize popup to fit all channels (no horizontal scroll); mixer_box is direct child */
    {
        GtkRequisition min_req, nat_req;
        gtk_widget_get_preferred_size(mixer->mixer_box, &min_req, &nat_req);
        gtk_window_resize(mixer->popup_window, nat_req.width, nat_req.height);
    }
}

/* Extract application icon name from PulseAudio properties */
static gchar *extract_icon_name(const pa_proplist *proplist) {
    const gchar *icon_name;
    const gchar *app_id;
    gchar *result = NULL;
    
    /* Try PA_PROP_APPLICATION_ICON_NAME first (most direct) */
    icon_name = pa_proplist_gets(proplist, PA_PROP_APPLICATION_ICON_NAME);
    if (icon_name && icon_name[0] != '\0') {
        return g_strdup(icon_name);
    }
    
    /* Try PA_PROP_APPLICATION_ID (desktop entry ID like "firefox.desktop") */
    app_id = pa_proplist_gets(proplist, PA_PROP_APPLICATION_ID);
    if (app_id && app_id[0] != '\0') {
        /* Remove .desktop suffix if present, use as icon name */
        if (g_str_has_suffix(app_id, ".desktop")) {
            result = g_strndup(app_id, strlen(app_id) - 8); /* Remove ".desktop" */
        } else {
            result = g_strdup(app_id);
        }
        return result;
    }
    
    /* Fallback: try application.name (less reliable but sometimes works) */
    icon_name = pa_proplist_gets(proplist, PA_PROP_APPLICATION_NAME);
    if (icon_name && icon_name[0] != '\0') {
        /* Convert to lowercase and replace spaces with hyphens for icon name */
        result = g_ascii_strdown(icon_name, -1);
        g_strdelimit(result, " ", '-');
        return result;
    }
    
    return NULL; /* No icon found, will use fallback in UI */
}

/* PulseAudio callback: sink input info */
static void sink_input_info_callback(pa_context *c, const pa_sink_input_info *i, 
                                     int eol, void *userdata) {
    VolumeMixerPlugin *mixer = (VolumeMixerPlugin *)userdata;
    
    if (eol > 0) {
        update_mixer_ui(mixer);
        return;
    }
    
    if (!i)
        return;
    
    SinkInputInfo *info = g_new0(SinkInputInfo, 1);
    info->index = i->index;
    info->name = g_strdup(i->name);
    info->app_name = g_strdup(pa_proplist_gets(i->proplist, PA_PROP_APPLICATION_NAME));
    info->icon_name = extract_icon_name(i->proplist);
    info->volume = i->volume;
    info->sink = i->sink;
    info->plugin = mixer;
    
    g_hash_table_insert(mixer->sink_inputs, GUINT_TO_POINTER(i->index), info);
}

/* PulseAudio callback: context state changed */
static void context_state_callback(pa_context *c, void *userdata) {
    VolumeMixerPlugin *mixer = (VolumeMixerPlugin *)userdata;
    pa_context_state_t state = pa_context_get_state(c);
    
    switch (state) {
        case PA_CONTEXT_READY:
            mixer->connected = TRUE;
            /* Get initial list of sink inputs */
            pa_context_get_sink_input_info_list(c, sink_input_info_callback, mixer);
            break;
            
        case PA_CONTEXT_FAILED:
        case PA_CONTEXT_TERMINATED:
            mixer->connected = FALSE;
            break;
            
        default:
            break;
    }
}

/* PulseAudio callback: subscription events */
static void subscribe_callback(pa_context *c, pa_subscription_event_type_t type,
                               uint32_t idx, void *userdata) {
    VolumeMixerPlugin *mixer = (VolumeMixerPlugin *)userdata;
    
    if ((type & PA_SUBSCRIPTION_EVENT_FACILITY_MASK) == PA_SUBSCRIPTION_EVENT_SINK_INPUT) {
        if ((type & PA_SUBSCRIPTION_EVENT_TYPE_MASK) == PA_SUBSCRIPTION_EVENT_REMOVE) {
            /* Application stopped playing */
            g_hash_table_remove(mixer->sink_inputs, GUINT_TO_POINTER(idx));
            update_mixer_ui(mixer);
        } else {
            /* Application started or changed */
            pa_context_get_sink_input_info(c, idx, sink_input_info_callback, mixer);
        }
    }
}

/* Connect to PulseAudio */
static void connect_to_pulseaudio(VolumeMixerPlugin *mixer) {
    mixer->pa_mainloop = pa_glib_mainloop_new(NULL);
    pa_mainloop_api *api = pa_glib_mainloop_get_api(mixer->pa_mainloop);
    
    mixer->pa_context = pa_context_new(api, "XFCE Volume Mixer");
    
    pa_context_set_state_callback(mixer->pa_context, context_state_callback, mixer);
    
    if (pa_context_connect(mixer->pa_context, NULL, PA_CONTEXT_NOFLAGS, NULL) < 0) {
        g_warning("Failed to connect to PulseAudio");
        return;
    }
    
    /* Subscribe to sink input events */
    pa_context_set_subscribe_callback(mixer->pa_context, subscribe_callback, mixer);
    pa_context_subscribe(mixer->pa_context, PA_SUBSCRIPTION_MASK_SINK_INPUT, NULL, NULL);
}

/* Toggle popup window; position with xfce_panel_plugin_position_widget so we keep full size control */
static void toggle_popup(VolumeMixerPlugin *mixer) {
    GtkWidget *placeholder;
    gint x, y;
    const gint width = 300;
    const gint height = 420;
    if (gtk_widget_get_visible(GTK_WIDGET(mixer->popup_window))) {
        gtk_widget_hide(GTK_WIDGET(mixer->popup_window));
        return;
    }
    g_hash_table_remove_all(mixer->sink_inputs);
    gtk_container_foreach(GTK_CONTAINER(mixer->mixer_box), (GtkCallback)gtk_widget_destroy, NULL);
    placeholder = gtk_label_new("Loading…");
    gtk_box_pack_start(GTK_BOX(mixer->mixer_box), placeholder, TRUE, TRUE, 5);
    if (mixer->pa_context && mixer->connected) {
        pa_context_get_sink_input_info_list(mixer->pa_context,
                                            sink_input_info_callback, mixer);
    }
    /* Get panel-anchored position, then show our window there (panel doesn't resize it) */
    xfce_panel_plugin_position_widget(XFCE_PANEL_PLUGIN(mixer),
                                      GTK_WIDGET(mixer->popup_window),
                                      mixer->button, &x, &y);
    gtk_window_move(mixer->popup_window, x, y);
    gtk_window_resize(mixer->popup_window, width, height);
    gtk_widget_show_all(GTK_WIDGET(mixer->popup_window));
}

/* Button click: show panel-anchored popup */
static gboolean on_button_clicked(GtkWidget *button, GdkEventButton *event,
                                  VolumeMixerPlugin *mixer) {
    if (event->button != GDK_BUTTON_PRIMARY)
        return FALSE;
    toggle_popup(mixer);
    return TRUE;
}

/* Update the panel icon */
static void update_icon(VolumeMixerPlugin *mixer) {
    gtk_image_set_from_icon_name(GTK_IMAGE(mixer->icon), 
                                 "multimedia-volume-control", 
                                 GTK_ICON_SIZE_BUTTON);
}

/* Create the popup window; mixer_box is direct child so window can resize to fit all channels */
static void create_popup_window(VolumeMixerPlugin *mixer) {
    const gint width = 300;
    const gint height = 420;
    
    mixer->popup_window = GTK_WINDOW(gtk_window_new(GTK_WINDOW_TOPLEVEL));
    gtk_window_set_decorated(mixer->popup_window, FALSE);
    gtk_window_set_default_size(mixer->popup_window, width, height);
    gtk_window_set_resizable(mixer->popup_window, TRUE);
    gtk_window_set_type_hint(mixer->popup_window, GDK_WINDOW_TYPE_HINT_POPUP_MENU);
    gtk_window_set_skip_taskbar_hint(mixer->popup_window, TRUE);
    /* No fixed size_request so window can grow to fit mixer_box natural width */
    
    /* Horizontal box: each channel is a vertical column; direct child of window, no scrolling */
    mixer->mixer_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_container_set_border_width(GTK_CONTAINER(mixer->mixer_box), 12);
    gtk_container_add(GTK_CONTAINER(mixer->popup_window), mixer->mixer_box);
    
    g_signal_connect_swapped(GTK_WIDGET(mixer->popup_window), "focus-out-event",
                             G_CALLBACK(gtk_widget_hide), mixer->popup_window);
}

/* Plugin finalize - cleanup when object is destroyed */
static void volume_mixer_plugin_finalize(GObject *object) {
    VolumeMixerPlugin *mixer = (VolumeMixerPlugin *)object;
    
    if (mixer->pa_context) {
        pa_context_disconnect(mixer->pa_context);
        pa_context_unref(mixer->pa_context);
    }
    
    if (mixer->pa_mainloop) {
        pa_glib_mainloop_free(mixer->pa_mainloop);
    }
    
    if (mixer->sink_inputs) {
        g_hash_table_destroy(mixer->sink_inputs);
    }
    
    if (mixer->popup_window) {
        gtk_widget_destroy(GTK_WIDGET(mixer->popup_window));
    }
    
    G_OBJECT_CLASS(volume_mixer_plugin_parent_class)->finalize(object);
}

/* Class initialization */
static void volume_mixer_plugin_class_init(VolumeMixerPluginClass *klass) {
    GObjectClass *object_class = G_OBJECT_CLASS(klass);
    object_class->finalize = volume_mixer_plugin_finalize;
}

/* Instance initialization - create UI and connect to PulseAudio */
static void volume_mixer_plugin_init(VolumeMixerPlugin *mixer) {
    mixer->connected = FALSE;
    mixer->sink_inputs = g_hash_table_new_full(g_direct_hash, g_direct_equal,
                                               NULL,
                                               (GDestroyNotify)sink_input_info_free);
    
    /* Create the panel button */
    mixer->button = gtk_button_new();
    gtk_button_set_relief(GTK_BUTTON(mixer->button), GTK_RELIEF_NONE);
    
    /* Create the icon */
    mixer->icon = gtk_image_new();
    update_icon(mixer);
    gtk_container_add(GTK_CONTAINER(mixer->button), mixer->icon);
    
    /* Add to panel */
    gtk_container_add(GTK_CONTAINER(mixer), mixer->button);
    xfce_panel_plugin_add_action_widget(XFCE_PANEL_PLUGIN(mixer), mixer->button);
    
    /* Create popup window (shown via xfce_panel_plugin_popup_window for position + size) */
    create_popup_window(mixer);
    
    /* Connect signals */
    g_signal_connect(mixer->button, "button-press-event",
                    G_CALLBACK(on_button_clicked), mixer);
    
    /* Connect to PulseAudio */
    connect_to_pulseaudio(mixer);
    
    /* Show the plugin */
    gtk_widget_show_all(GTK_WIDGET(mixer));
}

/* XFCE Panel Plugin API - required exports for X-XFCE-Module plugins.
 * When run in the external wrapper process, the wrapper calls init() and uses
 * its return value as the plugin GType; it does not call get_type(). */

G_MODULE_EXPORT GType
xfce_panel_module_init(GTypeModule *type_module, gboolean *make_resident)
{
    if (make_resident != NULL)
        *make_resident = FALSE;
    /* Return our plugin GType so the wrapper can instantiate it with g_object_new() */
    return volume_mixer_plugin_get_type();
}

G_MODULE_EXPORT GType
xfce_panel_module_get_type(XfcePanelTypeModule *module)
{
    return volume_mixer_plugin_get_type();
}

G_MODULE_EXPORT GObject *
xfce_panel_module_construct(XfcePanelTypeModule *module, gint unique_id, gint position)
{
    /* Create and return the plugin instance */
    return G_OBJECT(g_object_new(volume_mixer_plugin_get_type(), NULL));
}
