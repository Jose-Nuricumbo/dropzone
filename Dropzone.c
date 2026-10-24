/*
 * Dropzone.c — Conversor de documentos moderno y zona de arrastre para GTK 4
 * 
 * Repositorio: https://github.com/your-username/dropzone
 * Licencia: MIT
 */

#include <gtk/gtk.h>
#include <glib/gstdio.h>
#include <string.h>
#include <math.h>
#include <malloc.h>

#if !GTK_CHECK_VERSION(4, 14, 0)
#error "Se requiere GTK 4.14 o superior"
#endif

#define FX_SIZE    280
#define DISC_R     82.0f
#define MAX_BURSTS 3
#define BURST_TIME 1.1

typedef struct {
    gboolean active;
    double   age;
    GdkRGBA  color;
} Burst;

G_DECLARE_FINAL_TYPE(DropFx, drop_fx, DROP, FX, GtkWidget)

struct _DropFx {
    GtkWidget parent_instance;
    double  hover, hover_target;
    double  busy,  busy_target;
    double  t;
    double  spin;
    double  intro;
    double  look_x, look_y, look_tx, look_ty;
    double  zoom, zoom_v, zoom_target;
    gint64  last_us;
    guint   tick_id;
    GtkWidget *fade_widget;
    Burst   bursts[MAX_BURSTS];
};

G_DEFINE_FINAL_TYPE(DropFx, drop_fx, GTK_TYPE_WIDGET)

static const GdkRGBA C_ICE   = { 0.00f, 0.85f, 1.00f, 1.0f };
static const GdkRGBA C_GREEN = { 0.10f, 0.95f, 0.40f, 1.0f };
static const GdkRGBA C_RED   = { 1.00f, 0.20f, 0.30f, 1.0f };

static GdkRGBA with_alpha(const GdkRGBA *c, float a) {
    GdkRGBA r = *c;
    r.alpha = a;
    return r;
}

static void drop_fx_snapshot(GtkWidget *w, GtkSnapshot *s) {
    DropFx *fx = DROP_FX(w);
    float cx = gtk_widget_get_width(w)  / 2.0f;
    float cy = gtk_widget_get_height(w) / 2.0f;
    float R  = DISC_R;
    float h  = (float)fx->hover;
    float b  = (float)fx->busy;
    float intro = (float)fx->intro;
    float time  = (float)fx->t;

    float activity = fmaxf(h, b); 
    float throb = activity > 0.01f ? sinf(time * 8.0f) * 0.025f * activity : 0.0f;
    float sc = (0.92f + 0.08f * intro) * (float)fx->zoom + throb;
    
    float px = (float)fx->look_x * 7.0f;
    float py = (float)fx->look_y * 7.0f;

    graphene_point_t to_c = GRAPHENE_POINT_INIT(cx, cy);
    graphene_point_t back = GRAPHENE_POINT_INIT(-cx, -cy);
    graphene_point_t offset = GRAPHENE_POINT_INIT(px, py);

    if (intro < 1.0f) gtk_snapshot_push_opacity(s, intro);
    gtk_snapshot_save(s);
    
    gtk_snapshot_translate(s, &to_c);
    gtk_snapshot_scale(s, sc, sc);
    gtk_snapshot_translate(s, &offset);
    gtk_snapshot_translate(s, &back);

    // Efecto de brillo de neón (renderizado por GPU)
    if (activity > 0.01f) {
        float glow_R = R * 1.8f; 
        graphene_rect_t glow_rect = GRAPHENE_RECT_INIT(cx - glow_R, cy - glow_R, 2 * glow_R, 2 * glow_R);
        GskColorStop glow_stops[2] = {
            { 0.2f, with_alpha(&C_ICE, 0.12f * activity) },
            { 1.0f, { 0.0f, 0.0f, 0.0f, 0.0f } }
        };
        graphene_point_t g_center = GRAPHENE_POINT_INIT(cx, cy);
        gtk_snapshot_append_radial_gradient(s, &glow_rect, &g_center, glow_R, glow_R, 0.0f, 1.0f, glow_stops, 2);
    }

    // Ondas dinámicas expansivas
    if (activity > 0.01f) {
        for (int i = 0; i < 3; i++) {
            float phase = fmodf(time * 1.5f + (i * 0.333f), 1.0f);
            float ease = 1.0f - powf(1.0f - phase, 2.0f); 
            float wave_R = R + ease * 50.0f;
            float thickness = 2.5f * (1.0f - phase); 
            float alpha = (1.0f - phase) * activity * 0.6f; 
            
            GdkRGBA wave_c = { C_ICE.red, C_ICE.green, C_ICE.blue, alpha };
            if (b > 0.01f) { 
                wave_c.red = 1.0f; wave_c.green = 1.0f; wave_c.blue = 1.0f; 
            }
            
            graphene_rect_t rect_w = GRAPHENE_RECT_INIT(cx - wave_R, cy - wave_R, 2 * wave_R, 2 * wave_R);
            GskRoundedRect rr_w;
            gsk_rounded_rect_init_from_rect(&rr_w, &rect_w, wave_R);
            
            float w_w[4] = { thickness, thickness, thickness, thickness };
            GdkRGBA bwc[4] = { wave_c, wave_c, wave_c, wave_c };
            gtk_snapshot_append_border(s, &rr_w, w_w, bwc);
        }
    }

    // Ondas de impacto por eventos
    for (int i = 0; i < MAX_BURSTS; i++) {
        if (!fx->bursts[i].active) continue;
        float p = (float)(fx->bursts[i].age / BURST_TIME);
        float ease_out = 1.0f - powf(1.0f - p, 4.0f);
        float rb = R + ease_out * 55.0f;
        
        GdkRGBA bc = with_alpha(&fx->bursts[i].color, (1.0f - p) * 0.8f);
        graphene_rect_t rect_b = GRAPHENE_RECT_INIT(cx - rb, cy - rb, 2 * rb, 2 * rb);
        GskRoundedRect rr_b;
        gsk_rounded_rect_init_from_rect(&rr_b, &rect_b, rb);
        
        float thickness = 3.0f * (1.0f - p);
        float w_b[4] = { thickness, thickness, thickness, thickness };
        GdkRGBA bwc[4] = { bc, bc, bc, bc };
        gtk_snapshot_append_border(s, &rr_b, w_b, bwc);
    }

    // Núcleo del cuerpo de vidrio
    graphene_rect_t body = GRAPHENE_RECT_INIT(cx - R, cy - R, 2 * R, 2 * R);
    GskRoundedRect rr;
    gsk_rounded_rect_init_from_rect(&rr, &body, R);
    gtk_snapshot_push_rounded_clip(s, &rr);

    GskColorStop bg_stops[2] = {
        { 0.0f, { 0.16f, 0.18f, 0.22f, 0.88f + 0.10f * h } },
        { 1.0f, { 0.04f, 0.04f, 0.05f, 0.95f + 0.05f * h } }
    };
    graphene_point_t p1 = GRAPHENE_POINT_INIT(cx - R, cy - R * 1.5f);
    graphene_point_t p2 = GRAPHENE_POINT_INIT(cx + R, cy + R);
    gtk_snapshot_append_linear_gradient(s, &body, &p1, &p2, bg_stops, 2);
    gtk_snapshot_pop(s); 

    // Bisel del borde interno
    GdkRGBA c_top = { 1.0f, 1.0f, 1.0f, 0.25f + 0.15f * h };
    GdkRGBA c_bot = { 1.0f, 1.0f, 1.0f, 0.02f + 0.02f * h };
    float w4[4] = { 1.0f, 1.0f, 1.0f, 1.0f };
    GdkRGBA border_colors[4] = { c_top, c_bot, c_bot, c_bot }; 
    gtk_snapshot_append_border(s, &rr, w4, border_colors);

    // Spinner indicador de actividad
    if (b > 0.01f) {
        float ro = R + 4.5f;
        graphene_rect_t rc = GRAPHENE_RECT_INIT(cx - ro, cy - ro, 2 * ro, 2 * ro);
        GskRoundedRect rrc;
        gsk_rounded_rect_init_from_rect(&rrc, &rc, ro);

        float bw[4] = { 3.0f, 3.0f, 3.0f, 3.0f };
        GskColorStop prog[4] = {
            { 0.00f, { 1.0f, 1.0f, 1.0f, 0.00f } },
            { 0.65f, { 1.0f, 1.0f, 1.0f, 0.05f } },
            { 0.95f, { 1.0f, 1.0f, 1.0f, 0.95f } },
            { 1.00f, { 1.0f, 1.0f, 1.0f, 0.00f } }
        };

        gtk_snapshot_push_opacity(s, b);
        gtk_snapshot_push_mask(s, GSK_MASK_MODE_ALPHA);
        
        GdkRGBA white = {1,1,1,1};
        GdkRGBA wc[4] = {white, white, white, white};
        gtk_snapshot_append_border(s, &rrc, bw, wc);
        gtk_snapshot_pop(s);
        
        graphene_point_t ctr = GRAPHENE_POINT_INIT(cx, cy);
        gtk_snapshot_append_conic_gradient(s, &rc, &ctr, (float)fx->spin, prog, 4);
        
        gtk_snapshot_pop(s);
        gtk_snapshot_pop(s);
    }

    gtk_snapshot_restore(s);
    if (intro < 1.0f) gtk_snapshot_pop(s);
}

static void drop_fx_measure(GtkWidget *w, GtkOrientation o, int for_size,
                            int *min, int *nat, int *min_b, int *nat_b) {
    (void)w; (void)o; (void)for_size;
    *min = *nat = FX_SIZE;
    *min_b = *nat_b = -1;
}

static gboolean fx_tick(GtkWidget *w, GdkFrameClock *clock, gpointer data) {
    (void)data;
    DropFx *fx = DROP_FX(w);
    gint64 now = gdk_frame_clock_get_frame_time(clock);
    double dt  = fx->last_us ? (now - fx->last_us) / 1e6 : 0.016;
    if (dt > 0.1) dt = 0.1;
    fx->last_us = now;

    double k  = 1.0 - exp(-dt * 9.0);
    double kl = 1.0 - exp(-dt * 12.0);
    fx->hover  += (fx->hover_target - fx->hover) * k;
    fx->busy   += (fx->busy_target  - fx->busy)  * k;
    fx->look_x += (fx->look_tx - fx->look_x) * kl;
    fx->look_y += (fx->look_ty - fx->look_y) * kl;
    fx->t      += dt;
    fx->spin    = fmod(fx->spin + dt * 320.0 * fx->busy, 360.0); 

    double left = dt;
    while (left > 0) {
        double step = fmin(left, 0.008);
        double acc  = -180.0 * (fx->zoom - fx->zoom_target) - 15.0 * fx->zoom_v;
        fx->zoom_v += acc * step;
        fx->zoom   += fx->zoom_v * step;
        left -= step;
    }

    if (fx->intro < 1.0) {
        fx->intro = fmin(1.0, fx->intro + dt / 0.9);
        if (fx->fade_widget)
            gtk_widget_set_opacity(fx->fade_widget, fmin(1.0, fx->intro * 1.8));
    }

    gboolean bursts_left = FALSE;
    for (int i = 0; i < MAX_BURSTS; i++) {
        if (!fx->bursts[i].active) continue;
        fx->bursts[i].age += dt;
        if (fx->bursts[i].age >= BURST_TIME) fx->bursts[i].active = FALSE;
        else bursts_left = TRUE;
    }

    gtk_widget_queue_draw(w);

    if (fx->busy_target > 0 || fx->hover_target > 0 || bursts_left || fx->intro < 1.0 ||
        fabs(fx->hover - fx->hover_target) > 0.004 || fabs(fx->busy) > 0.004 ||
        fabs(fx->zoom - fx->zoom_target) > 0.0004 || fabs(fx->zoom_v) > 0.01 ||
        fabs(fx->look_x - fx->look_tx) > 0.003 || fabs(fx->look_y - fx->look_ty) > 0.003)
        return G_SOURCE_CONTINUE;

    fx->hover  = fx->hover_target;
    fx->busy   = 0;
    fx->zoom   = fx->zoom_target;
    fx->zoom_v = 0;
    fx->look_x = fx->look_tx;
    fx->look_y = fx->look_ty;
    fx->tick_id = 0;
    fx->last_us = 0;
    gtk_widget_queue_draw(w);
    
    // Libera la memoria sobrante del heap hacia el SO de inmediato
    malloc_trim(0);
    return G_SOURCE_REMOVE;
}

static void fx_wake(DropFx *fx) {
    GtkWidget *w = GTK_WIDGET(fx);
    gboolean anim = TRUE;
    g_object_get(gtk_widget_get_settings(w), "gtk-enable-animations", &anim, NULL);
    if (!anim) {
        fx->hover = fx->hover_target; fx->busy = fx->busy_target;
        fx->zoom = fx->zoom_target; fx->zoom_v = 0;
        fx->intro = 1.0;
        if (fx->fade_widget) gtk_widget_set_opacity(fx->fade_widget, 1.0);
        gtk_widget_queue_draw(w);
        return;
    }
    if (fx->tick_id == 0)
        fx->tick_id = gtk_widget_add_tick_callback(w, fx_tick, NULL, NULL);
}

static void drop_fx_class_init(DropFxClass *klass) {
    GtkWidgetClass *wc = GTK_WIDGET_CLASS(klass);
    wc->snapshot = drop_fx_snapshot;
    wc->measure  = drop_fx_measure;
}

static void drop_fx_init(DropFx *fx) {
    fx->intro = 1.0;
    fx->zoom = fx->zoom_target = 1.0;
    gtk_widget_set_halign(GTK_WIDGET(fx), GTK_ALIGN_CENTER);
    gtk_widget_set_valign(GTK_WIDGET(fx), GTK_ALIGN_CENTER);
    gtk_widget_set_can_target(GTK_WIDGET(fx), FALSE);
}

static DropFx *drop_fx_new(void) { return g_object_new(drop_fx_get_type(), NULL); }
static void drop_fx_set_fade_widget(DropFx *fx, GtkWidget *w) { fx->fade_widget = w; }

static void drop_fx_play_intro(DropFx *fx) {
    fx->intro = 0.0;
    if (fx->fade_widget) gtk_widget_set_opacity(fx->fade_widget, 0.0);
    fx_wake(fx);
}

static void drop_fx_set_hover(DropFx *fx, gboolean on) {
    fx->hover_target = on ? 1.0 : 0.0;
    fx->zoom_target  = on ? 1.05 : 1.0; 
    fx_wake(fx);
}

static void drop_fx_set_busy(DropFx *fx, gboolean on) {
    fx->busy_target = on ? 1.0 : 0.0;
    fx_wake(fx);
}

static void drop_fx_squash(DropFx *fx) {
    fx->zoom_v -= 1.35;
    fx_wake(fx);
}

static void drop_fx_look(DropFx *fx, double nx, double ny) {
    fx->look_tx = CLAMP(nx, -1.0, 1.0);
    fx->look_ty = CLAMP(ny, -1.0, 1.0);
    fx_wake(fx);
}

static void drop_fx_burst(DropFx *fx, const GdkRGBA *color) {
    int slot = 0;
    for (int i = 0; i < MAX_BURSTS; i++)
        if (!fx->bursts[i].active) { slot = i; break; }
    fx->bursts[slot].active = TRUE;
    fx->bursts[slot].age    = 0;
    fx->bursts[slot].color  = *color;
    fx_wake(fx);
}

static void drop_fx_finish(DropFx *fx, gboolean ok) {
    drop_fx_burst(fx, ok ? &C_GREEN : &C_RED);
}

// Tabla de mapeo para las conversiones soportadas
typedef enum { TOOL_OFFICE, TOOL_PDFTOTEXT, TOOL_PDFTOPPM } Tool;

typedef struct {
    const char *src_ext;
    const char *label;
    Tool        tool;
    const char *target;
} Conversion;

static const Conversion CONVERSIONS[] = {
    { "pdf",  "Word (.docx)",        TOOL_OFFICE, "docx"     },
    { "pdf",  "Texto (.txt)",        TOOL_PDFTOTEXT,   NULL  },
    { "pdf",  "Imagen (.png)",       TOOL_PDFTOPPM,    NULL  },
    { "doc",  "PDF",                 TOOL_OFFICE, "pdf"      },
    { "doc",  "Texto (.txt)",        TOOL_OFFICE, "txt:Text" },
    { "doc",  "Word (.docx)",        TOOL_OFFICE, "docx"     },
    { "docx", "PDF",                 TOOL_OFFICE, "pdf"      },
    { "docx", "Texto (.txt)",        TOOL_OFFICE, "txt:Text" },
    { "docx", "OpenDocument (.odt)", TOOL_OFFICE, "odt"      },
    { "txt",  "PDF",                 TOOL_OFFICE, "pdf"      },
    { "txt",  "Word (.docx)",        TOOL_OFFICE, "docx"     },
    { "xls",  "PDF",                 TOOL_OFFICE, "pdf"      },
    { "xls",  "CSV",                 TOOL_OFFICE, "csv"      },
    { "xls",  "Excel (.xlsx)",       TOOL_OFFICE, "xlsx"     },
    { "xlsx", "PDF",                 TOOL_OFFICE, "pdf"      },
    { "xlsx", "CSV",                 TOOL_OFFICE, "csv"      },
    { "xlsx", "OpenDocument (.ods)", TOOL_OFFICE, "ods"      },
    { "ppt",  "PDF",                 TOOL_OFFICE, "pdf"      },
    { "ppt",  "PowerPoint (.pptx)",  TOOL_OFFICE, "pptx"     },
    { "pptx", "PDF",                 TOOL_OFFICE, "pdf"      },
    { "pptx", "OpenDocument (.odp)", TOOL_OFFICE, "odp"      },
};

typedef struct {
    GtkWidget *window;
    GtkWidget *circle;
    DropFx    *fx;
    GtkWidget *icon;
    GtkWidget *label;
    GtkWidget *popover;
    char      *path;
    int        backend;
    char      *backend_bin;
    guint      reset_id;
} AppState;

static const char *IDLE_TEXT = "Suelta aquí";

static void set_status(AppState *st, const char *icon, const char *text) {
    gtk_image_set_from_icon_name(GTK_IMAGE(st->icon), icon);
    gtk_label_set_text(GTK_LABEL(st->label), text);
}

static gboolean reset_status_cb(gpointer data) {
    AppState *st = data;
    st->reset_id = 0;
    set_status(st, "folder-download-symbolic", IDLE_TEXT);
    malloc_trim(0);
    return G_SOURCE_REMOVE;
}

static void cancel_reset(AppState *st) {
    if (st->reset_id) {
        g_source_remove(st->reset_id);
        st->reset_id = 0;
    }
}

static void schedule_reset(AppState *st) {
    cancel_reset(st);
    st->reset_id = g_timeout_add_seconds(3, reset_status_cb, st);
}

typedef enum { BACKEND_NONE, BACKEND_ONLYOFFICE, BACKEND_LIBREOFFICE } Backend;
#define X2T_DEFAULT "/opt/onlyoffice/desktopeditors/converter/x2t"

static char *find_x2t(void) {
    const char *env = g_getenv("DROPZONE_X2T");
    if (env && g_file_test(env, G_FILE_TEST_IS_EXECUTABLE)) return g_strdup(env);
    if (g_file_test(X2T_DEFAULT, G_FILE_TEST_IS_EXECUTABLE)) return g_strdup(X2T_DEFAULT);
    return NULL;
}

static char *find_libreoffice(void) {
    char *p = g_find_program_in_path("libreoffice");
    if (!p) p = g_find_program_in_path("soffice");
    return p;
}

static Backend choose_backend(char **bin) {
    const char *want = g_getenv("DROPZONE_BACKEND");
    char *p = NULL;
    if (!want || g_strcmp0(want, "onlyoffice") == 0) {
        if ((p = find_x2t())) { *bin = p; return BACKEND_ONLYOFFICE; }
    }
    if (!want || g_strcmp0(want, "libreoffice") == 0) {
        if ((p = find_libreoffice())) { *bin = p; return BACKEND_LIBREOFFICE; }
    }
    return BACKEND_NONE;
}

static char *target_ext(const char *target) {
    const char *colon = strchr(target, ':');
    return colon ? g_strndup(target, colon - target) : g_strdup(target);
}

static void rm_rf(const char *path) {
    if (g_file_test(path, G_FILE_TEST_IS_DIR) && !g_file_test(path, G_FILE_TEST_IS_SYMLINK)) {
        GDir *d = g_dir_open(path, 0, NULL);
        if (d) {
            const char *name;
            while ((name = g_dir_read_name(d))) {
                char *child = g_build_filename(path, name, NULL);
                rm_rf(child);
                g_free(child);
            }
            g_dir_close(d);
        }
        g_rmdir(path);
    } else {
        g_remove(path);
    }
}

static char *write_x2t_params(const char *in, const char *out, const char *tmpdir) {
    char *e_in  = g_markup_escape_text(in, -1);
    char *e_out = g_markup_escape_text(out, -1);
    char *e_tmp = g_markup_escape_text(tmpdir, -1);

    GString *x = g_string_new(
        "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n"
        "<TaskQueueDataConvert xmlns:xsi=\"http://www.w3.org/2001/XMLSchema-instance\" "
        "xmlns:xsd=\"http://www.w3.org/2001/XMLSchema\">\n");
    g_string_append_printf(x, "  <m_sFileFrom>%s</m_sFileFrom>\n", e_in);
    g_string_append_printf(x, "  <m_sFileTo>%s</m_sFileTo>\n", e_out);
    g_string_append(x, "  <m_bIsNoBase64>false</m_bIsNoBase64>\n");
    g_string_append(x, "  <m_sFontDir>/usr/share/fonts</m_sFontDir>\n");
    g_string_append_printf(x, "  <m_sTempDir>%s</m_sTempDir>\n", e_tmp);
    g_string_append(x, "  <m_nCsvTxtEncoding>46</m_nCsvTxtEncoding>\n");
    g_string_append(x, "  <m_nCsvDelimiter>4</m_nCsvDelimiter>\n");

    char *allfonts = g_build_filename(g_get_user_data_dir(), "onlyoffice", "desktopeditors", "data", "fonts", "AllFonts.js", NULL);
    if (g_file_test(allfonts, G_FILE_TEST_EXISTS)) {
        char *e = g_markup_escape_text(allfonts, -1);
        g_string_append_printf(x, "  <m_sAllFontsPath>%s</m_sAllFontsPath>\n", e);
        g_free(e);
    }
    const char *themes = "/opt/onlyoffice/desktopeditors/editors/sdkjs/slide/themes";
    if (g_file_test(themes, G_FILE_TEST_IS_DIR))
        g_string_append_printf(x, "  <m_sThemeDir>%s</m_sThemeDir>\n", themes);

    g_string_append(x, "</TaskQueueDataConvert>\n");

    char *params = g_build_filename(tmpdir, "params.xml", NULL);
    gboolean ok = g_file_set_contents(params, x->str, -1, NULL);

    g_string_free(x, TRUE);
    g_free(allfonts); g_free(e_in); g_free(e_out); g_free(e_tmp);
    if (!ok) { g_free(params); return NULL; }
    return params;
}

typedef struct {
    AppState *st;
    char     *tmpdir;
} Job;

static GPtrArray *build_argv(AppState *st, const Conversion *c, const char *path, char **tmpdir_out) {
    GPtrArray *argv = g_ptr_array_new_with_free_func(g_free);
    char *dir  = g_path_get_dirname(path);
    char *base = g_path_get_basename(path);
    char *dot  = strrchr(base, '.');
    if (dot) *dot = '\0';
    char *stem = g_build_filename(dir, base, NULL);

    switch (c->tool) {
    case TOOL_OFFICE:
        if (st->backend == BACKEND_ONLYOFFICE) {
            char *tmpdir = g_dir_make_tmp("dropzone-XXXXXX", NULL);
            char *ext    = target_ext(c->target);
            char *out    = g_strconcat(stem, ".", ext, NULL);
            char *params = tmpdir ? write_x2t_params(path, out, tmpdir) : NULL;
            g_free(ext); g_free(out);
            if (!params) {
                if (tmpdir) { rm_rf(tmpdir); g_free(tmpdir); }
                g_ptr_array_free(argv, TRUE); argv = NULL; break;
            }
            *tmpdir_out = tmpdir;
            g_ptr_array_add(argv, g_strdup(st->backend_bin));
            g_ptr_array_add(argv, params);
        } else if (st->backend == BACKEND_LIBREOFFICE) {
            g_ptr_array_add(argv, g_strdup(st->backend_bin));
            g_ptr_array_add(argv, g_strdup("--headless"));
            if (g_strcmp0(c->src_ext, "pdf") == 0)
                g_ptr_array_add(argv, g_strdup("--infilter=writer_pdf_import"));
            g_ptr_array_add(argv, g_strdup("--convert-to"));
            g_ptr_array_add(argv, g_strdup(c->target));
            g_ptr_array_add(argv, g_strdup("--outdir"));
            g_ptr_array_add(argv, g_strdup(dir));
            g_ptr_array_add(argv, g_strdup(path));
        } else {
            g_ptr_array_free(argv, TRUE); argv = NULL;
        }
        break;
    case TOOL_PDFTOTEXT:
        g_ptr_array_add(argv, g_strdup("pdftotext"));
        g_ptr_array_add(argv, g_strdup(path));
        g_ptr_array_add(argv, g_strconcat(stem, ".txt", NULL));
        break;
    case TOOL_PDFTOPPM:
        g_ptr_array_add(argv, g_strdup("pdftoppm"));
        g_ptr_array_add(argv, g_strdup("-png"));
        g_ptr_array_add(argv, g_strdup("-singlefile"));
        g_ptr_array_add(argv, g_strdup("-r"));
        g_ptr_array_add(argv, g_strdup("150"));
        g_ptr_array_add(argv, g_strdup(path));
        g_ptr_array_add(argv, g_strdup(stem));
        break;
    }

    if (argv) g_ptr_array_add(argv, NULL);
    g_free(dir); g_free(base); g_free(stem);
    return argv;
}

static void on_conversion_done(GObject *source, GAsyncResult *res, gpointer data) {
    Job         *job  = data;
    AppState    *st   = job->st;
    GSubprocess *proc = G_SUBPROCESS(source);
    GError      *err  = NULL;

    gboolean ok = g_subprocess_wait_finish(proc, res, &err) && g_subprocess_get_successful(proc);

    if (ok) {
        set_status(st, "emblem-ok-symbolic", "Listo");
    } else {
        g_printerr("La conversión falló: %s\n", err ? err->message : "código de salida != 0");
        set_status(st, "dialog-error-symbolic", "Error");
    }
    drop_fx_set_busy(st->fx, FALSE);
    drop_fx_finish(st->fx, ok);
    g_clear_error(&err);
    g_object_unref(proc);

    if (job->tmpdir) { rm_rf(job->tmpdir); g_free(job->tmpdir); }
    g_free(job);
    
    malloc_trim(0);
    schedule_reset(st);
}

static void run_conversion(AppState *st, const Conversion *c) {
    char      *tmpdir = NULL;
    GPtrArray *argv   = build_argv(st, c, st->path, &tmpdir);
    GError    *err    = NULL;

    if (!argv) {
        set_status(st, "dialog-error-symbolic", "Sin motor");
        drop_fx_finish(st->fx, FALSE);
        schedule_reset(st);
        return;
    }

    GSubprocessLauncher *launcher = g_subprocess_launcher_new(G_SUBPROCESS_FLAGS_STDOUT_SILENCE | G_SUBPROCESS_FLAGS_STDERR_SILENCE);

    if (c->tool == TOOL_OFFICE && st->backend == BACKEND_ONLYOFFICE) {
        char *libdir = g_path_get_dirname(st->backend_bin);
        const char *old = g_getenv("LD_LIBRARY_PATH");
        char *val = (old && *old) ? g_strconcat(libdir, ":", old, NULL) : g_strdup(libdir);
        g_subprocess_launcher_setenv(launcher, "LD_LIBRARY_PATH", val, TRUE);
        g_free(libdir); g_free(val);
    }

    GSubprocess *proc = g_subprocess_launcher_spawnv(launcher, (const gchar * const *)argv->pdata, &err);
    g_object_unref(launcher);
    g_ptr_array_free(argv, TRUE);

    if (!proc) {
        g_clear_error(&err);
        if (tmpdir) { rm_rf(tmpdir); g_free(tmpdir); }
        set_status(st, "dialog-error-symbolic", "Error de inicio");
        drop_fx_finish(st->fx, FALSE);
        schedule_reset(st);
        return;
    }

    Job *job = g_new0(Job, 1);
    job->st = st; job->tmpdir = tmpdir;

    cancel_reset(st);
    set_status(st, "emblem-synchronizing-symbolic", "Convirtiendo…");
    drop_fx_set_busy(st->fx, TRUE);
    g_subprocess_wait_async(proc, NULL, on_conversion_done, job);
}

static void on_option_clicked(GtkButton *btn, gpointer data) {
    AppState *st = data;
    const Conversion *c = g_object_get_data(G_OBJECT(btn), "conversion");
    gtk_popover_popdown(GTK_POPOVER(st->popover));
    if (c && st->path) run_conversion(st, c);
}

static gboolean show_options(AppState *st, const char *ext) {
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
    gtk_widget_set_margin_top(box, 6); gtk_widget_set_margin_bottom(box, 6);
    gtk_widget_set_margin_start(box, 6); gtk_widget_set_margin_end(box, 6);

    char *title_txt = g_strdup_printf("Convertir .%s a:", ext);
    GtkWidget *title = gtk_label_new(title_txt);
    gtk_widget_add_css_class(title, "heading");
    gtk_box_append(GTK_BOX(box), title);
    g_free(title_txt);

    int found = 0;
    for (gsize i = 0; i < G_N_ELEMENTS(CONVERSIONS); i++) {
        if (g_strcmp0(CONVERSIONS[i].src_ext, ext) != 0) continue;
        GtkWidget *btn = gtk_button_new_with_label(CONVERSIONS[i].label);
        g_object_set_data(G_OBJECT(btn), "conversion", (gpointer)&CONVERSIONS[i]);
        g_signal_connect(btn, "clicked", G_CALLBACK(on_option_clicked), st);
        gtk_box_append(GTK_BOX(box), btn);
        found++;
    }

    if (!found) { g_object_ref_sink(box); g_object_unref(box); return FALSE; }

    gtk_popover_set_child(GTK_POPOVER(st->popover), box);
    gtk_popover_popup(GTK_POPOVER(st->popover));
    return TRUE;
}

static gboolean on_drop(GtkDropTarget *target, const GValue *value, double x, double y, gpointer data) {
    (void)target; (void)x; (void)y;
    AppState *st = data;

    drop_fx_set_hover(st->fx, FALSE);
    drop_fx_look(st->fx, 0, 0);

    GdkFileList *list = g_value_get_boxed(value);
    GSList *files = list ? gdk_file_list_get_files(list) : NULL;
    if (!files) return FALSE;

    GFile *file = G_FILE(files->data);
    char  *path = g_file_get_path(file);
    if (!path) {
        set_status(st, "dialog-warning-symbolic", "Error local");
        drop_fx_finish(st->fx, FALSE);
        schedule_reset(st);
        return FALSE;
    }

    g_free(st->path);
    st->path = path;
    drop_fx_squash(st->fx);

    const char *dot = strrchr(path, '.');
    char *ext = dot ? g_ascii_strdown(dot + 1, -1) : g_strdup("");

    if (!show_options(st, ext)) {
        set_status(st, "dialog-warning-symbolic", "No soportado");
        drop_fx_finish(st->fx, FALSE);
        schedule_reset(st);
    }

    g_free(ext);
    return TRUE;
}

static GdkDragAction on_drag_enter(GtkDropTarget *target, double x, double y, gpointer data) {
    (void)target; (void)x; (void)y;
    AppState *st = data;
    drop_fx_set_hover(st->fx, TRUE);
    drop_fx_burst(st->fx, &C_ICE);
    return GDK_ACTION_COPY;
}

static void on_drag_leave(GtkDropTarget *target, gpointer data) {
    (void)target;
    AppState *st = data;
    drop_fx_set_hover(st->fx, FALSE);
    drop_fx_look(st->fx, 0, 0);
}

static void update_look(AppState *st, double x, double y) {
    double w = gtk_widget_get_width(st->circle);
    double h = gtk_widget_get_height(st->circle);
    if (w < 1 || h < 1) return;
    drop_fx_look(st->fx, (x - w / 2) / (w / 2), (y - h / 2) / (h / 2));
}

static GdkDragAction on_drag_motion(GtkDropTarget *target, double x, double y, gpointer data) {
    (void)target; update_look(data, x, y); return GDK_ACTION_COPY;
}

static void on_pointer_motion(GtkEventControllerMotion *c, double x, double y, gpointer data) {
    (void)c; update_look(data, x, y);
}

static void on_pointer_leave(GtkEventControllerMotion *c, gpointer data) {
    (void)c; drop_fx_look(((AppState *)data)->fx, 0, 0);
}

static const char *CSS =
    "window.dropzone, window.dropzone.background, window.dropzone.csd,"
    "window.dropzone.solid-csd, window.dropzone.background.csd {"
    "  background: none; background-color: transparent; box-shadow: none;"
    "  border: none; border-radius: 0; outline: none;"
    "}"
    "window.dropzone *:drop(active), window.dropzone:drop(active) {"
    "  box-shadow: none; outline: none;"
    "}"
    ".circle { color: white; }"
    ".circle .title { font-weight: 700; font-size: 13px; font-family: system-ui, sans-serif; letter-spacing: 0.5px; margin-top: 10px; }"
    ".circle image { opacity: 1.0; -gtk-icon-shadow: 0 2px 8px rgba(0,0,0,0.5); }";

static void load_css(void) {
    GtkCssProvider *provider = gtk_css_provider_new();
#if GTK_CHECK_VERSION(4, 12, 0)
    gtk_css_provider_load_from_string(provider, CSS);
#else
    gtk_css_provider_load_from_data(provider, CSS, -1);
#endif
    gtk_style_context_add_provider_for_display(
        gdk_display_get_default(),
        GTK_STYLE_PROVIDER(provider),
        GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
    g_object_unref(provider);
}

static void on_right_click(GtkGestureClick *g, int n, double x, double y, gpointer data) {
    (void)g; (void)n; (void)x; (void)y;
    gtk_window_close(GTK_WINDOW(((AppState *)data)->window));
}

static gboolean on_key(GtkEventControllerKey *c, guint keyval, guint keycode, GdkModifierType mods, gpointer data) {
    (void)c; (void)keycode; (void)mods;
    if (keyval == GDK_KEY_Escape) {
        gtk_window_close(GTK_WINDOW(((AppState *)data)->window));
        return TRUE;
    }
    return FALSE;
}

static void on_circle_destroy(GtkWidget *w, gpointer data) {
    (void)w; AppState *st = data;
    if (st->popover) gtk_widget_unparent(st->popover);
}

static void activate(GtkApplication *app, gpointer user_data) {
    (void)user_data;
    AppState *st = g_new0(AppState, 1);
    st->backend = choose_backend(&st->backend_bin);

    load_css();

    st->window = gtk_application_window_new(app);
    gtk_window_set_title(GTK_WINDOW(st->window), "Dropzone");
    gtk_window_set_decorated(GTK_WINDOW(st->window), FALSE);
    gtk_window_set_resizable(GTK_WINDOW(st->window), FALSE);
    gtk_window_set_default_size(GTK_WINDOW(st->window), FX_SIZE, FX_SIZE);
    gtk_widget_add_css_class(st->window, "dropzone");

    GtkWidget *handle = gtk_window_handle_new();
    gtk_window_set_child(GTK_WINDOW(st->window), handle);

    GtkWidget *overlay = gtk_overlay_new();
    gtk_window_handle_set_child(GTK_WINDOW_HANDLE(handle), overlay);

    st->fx = drop_fx_new();
    gtk_overlay_set_child(GTK_OVERLAY(overlay), GTK_WIDGET(st->fx));

    st->circle = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_set_size_request(st->circle, (int)(DISC_R * 2), (int)(DISC_R * 2));
    gtk_widget_set_halign(st->circle, GTK_ALIGN_CENTER);
    gtk_widget_set_valign(st->circle, GTK_ALIGN_CENTER);
    gtk_widget_add_css_class(st->circle, "circle");
    gtk_overlay_add_overlay(GTK_OVERLAY(overlay), st->circle);

    GtkWidget *content = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    gtk_widget_set_halign(content, GTK_ALIGN_CENTER);
    gtk_widget_set_valign(content, GTK_ALIGN_CENTER);
    gtk_widget_set_hexpand(content, TRUE);
    gtk_widget_set_vexpand(content, TRUE);
    gtk_box_append(GTK_BOX(st->circle), content);

    st->icon = gtk_image_new_from_icon_name("folder-download-symbolic");
    gtk_image_set_pixel_size(GTK_IMAGE(st->icon), 34);
    gtk_box_append(GTK_BOX(content), st->icon);

    st->label = gtk_label_new(IDLE_TEXT);
    gtk_widget_add_css_class(st->label, "title");
    gtk_box_append(GTK_BOX(content), st->label);

    st->popover = gtk_popover_new();
    gtk_widget_set_parent(st->popover, st->circle);
    g_object_add_weak_pointer(G_OBJECT(st->popover), (gpointer *)&st->popover);
    gtk_popover_set_position(GTK_POPOVER(st->popover), GTK_POS_BOTTOM);
    g_signal_connect(st->circle, "destroy", G_CALLBACK(on_circle_destroy), st);

    GtkDropTarget *drop = gtk_drop_target_new(GDK_TYPE_FILE_LIST, GDK_ACTION_COPY);
    g_signal_connect(drop, "enter",  G_CALLBACK(on_drag_enter), st);
    g_signal_connect(drop, "leave",  G_CALLBACK(on_drag_leave), st);
    g_signal_connect(drop, "motion", G_CALLBACK(on_drag_motion), st);
    g_signal_connect(drop, "drop",   G_CALLBACK(on_drop), st);
    gtk_widget_add_controller(st->circle, GTK_EVENT_CONTROLLER(drop));

    GtkEventController *motion = gtk_event_controller_motion_new();
    g_signal_connect(motion, "motion", G_CALLBACK(on_pointer_motion), st);
    g_signal_connect(motion, "leave",  G_CALLBACK(on_pointer_leave), st);
    gtk_widget_add_controller(st->circle, motion);

    GtkGesture *rclick = gtk_gesture_click_new();
    gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(rclick), GDK_BUTTON_SECONDARY);
    g_signal_connect(rclick, "pressed", G_CALLBACK(on_right_click), st);
    gtk_widget_add_controller(st->circle, GTK_EVENT_CONTROLLER(rclick));

    GtkEventController *keys = gtk_event_controller_key_new();
    g_signal_connect(keys, "key-pressed", G_CALLBACK(on_key), st);
    gtk_widget_add_controller(st->window, keys);

    drop_fx_set_fade_widget(st->fx, st->circle);
    drop_fx_play_intro(st->fx);

    gtk_window_present(GTK_WINDOW(st->window));
}

int main(int argc, char **argv) {
    g_setenv("GSK_RENDERER", "gl", FALSE);
    
    // Optimización de la gestión de memoria para reducir el consumo en RAM
    mallopt(M_ARENA_MAX, 2);
    mallopt(M_TRIM_THRESHOLD, 128 * 1024);

    GtkApplication *app = gtk_application_new("org.example.Dropzone", G_APPLICATION_DEFAULT_FLAGS);
    g_signal_connect(app, "activate", G_CALLBACK(activate), NULL);
    int status = g_application_run(G_APPLICATION(app), argc, argv);
    g_object_unref(app);
    return status;
}