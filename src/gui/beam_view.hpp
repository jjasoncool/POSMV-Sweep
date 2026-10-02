#pragma once
#include <gtk/gtk.h>
#include <pango/pangocairo.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
namespace posmv {
// UI-thread only: a plain ship over the water with the beam set by the Offset. Only the beam moves;
// a positive Offset points it to the right. Shows intent, not the commissioned port/starboard side.
class BeamView {
    GtkWidget* area_{};
    double offset_deg_{};
    bool active_{}, over_limit_{};
    static void draw(GtkDrawingArea* area, cairo_t* cr, int width, int height, gpointer data) {
        const auto& self = *static_cast<BeamView*>(data);
        const double pi = 3.14159265358979323846;
        const double u = std::min(width / 6.0, height / 9.0);  // drawing unit
        const double cx = width / 2.0, water = height * 0.42;
        cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
        cairo_set_line_join(cr, CAIRO_LINE_JOIN_ROUND);
        cairo_set_source_rgb(cr, 0.12, 0.35, 0.75);  // water
        cairo_set_line_width(cr, 3);
        for (double x = 4; x <= width - 4; x += 2) {
            const double y = water + std::sin(x / (0.9 * u) * pi) * 0.12 * u;
            if (x == 4) cairo_move_to(cr, x, y); else cairo_line_to(cr, x, y);
        }
        cairo_stroke(cr);
        cairo_set_source_rgb(cr, 0.1, 0.1, 0.1);  // hull, cabin, mast
        cairo_set_line_width(cr, 2.5);
        cairo_move_to(cr, cx - 1.5 * u, water - 1.2 * u);
        cairo_line_to(cr, cx + 1.5 * u, water - 1.2 * u);
        cairo_line_to(cr, cx + 0.9 * u, water);
        cairo_line_to(cr, cx - 0.9 * u, water);
        cairo_close_path(cr);
        cairo_rectangle(cr, cx - 0.8 * u, water - 2.3 * u, 1.6 * u, 1.1 * u);
        cairo_move_to(cr, cx, water - 2.3 * u);
        cairo_line_to(cr, cx, water - 3.2 * u);
        cairo_move_to(cr, cx - 0.45 * u, water - 2.85 * u);
        cairo_line_to(cr, cx + 0.45 * u, water - 2.85 * u);
        cairo_stroke(cr);
        const double length = height - water - 0.3 * u, theta = self.offset_deg_ * pi / 180;
        const double top = water + 0.25 * u;
        cairo_set_source_rgb(cr, 0.6, 0.6, 0.6);  // vertical reference
        cairo_set_line_width(cr, 1);
        const double dashes[] = {4, 4};
        cairo_set_dash(cr, dashes, 2, 0);
        cairo_move_to(cr, cx, top);
        cairo_line_to(cr, cx, top + length);
        cairo_stroke(cr);
        cairo_set_dash(cr, nullptr, 0, 0);
        if (!self.active_) cairo_set_source_rgb(cr, 0.65, 0.65, 0.65);
        else if (self.over_limit_) cairo_set_source_rgb(cr, 0.9, 0.45, 0.0);
        else cairo_set_source_rgb(cr, 0.82, 0.1, 0.1);
        const double radius = 0.35 * length, down = pi / 2, beam = down - theta;  // screen angles, y down
        cairo_set_line_width(cr, 1.5);
        cairo_arc(cr, cx, top, radius, std::min(beam, down), std::max(beam, down));
        cairo_stroke(cr);
        cairo_set_line_width(cr, 4);
        cairo_move_to(cr, cx, top);
        cairo_line_to(cr, cx + length * std::sin(theta), top + length * std::cos(theta));
        cairo_stroke(cr);
        char text[32];
        if (self.active_) std::snprintf(text, sizeof(text), "%+.1f°", self.offset_deg_);
        else std::snprintf(text, sizeof(text), "—");
        auto* layout = gtk_widget_create_pango_layout(GTK_WIDGET(area), text);
        int text_width{}, text_height{};
        pango_layout_get_pixel_size(layout, &text_width, &text_height);
        // Just outside the beam, away from the vertical line, so the text never sits on a line.
        const bool left = theta < 0;
        const double at = beam + (left ? 0.3 : -0.3), label = 0.45 * length;
        double x = cx + label * std::cos(at), y = top + label * std::sin(at) - text_height / 2.0;
        if (left) x -= text_width;
        cairo_move_to(cr, std::clamp(x, 2.0, width - text_width - 2.0), y);
        pango_cairo_show_layout(cr, layout);
        g_object_unref(layout);
    }
public:
    GtkWidget* create() {
        area_ = gtk_drawing_area_new();
        gtk_widget_set_size_request(area_, 240, 160);
        gtk_drawing_area_set_draw_func(GTK_DRAWING_AREA(area_), draw, this, nullptr);
        return area_;
    }
    // `active` is false while no output is sent; `over_limit` while the output Roll exceeds the MBES limit.
    void update(double offset_deg, bool active, bool over_limit) {
        if (offset_deg == offset_deg_ && active == active_ && over_limit == over_limit_) return;
        offset_deg_ = offset_deg; active_ = active; over_limit_ = over_limit;
        if (area_) gtk_widget_queue_draw(area_);
    }
};
}
