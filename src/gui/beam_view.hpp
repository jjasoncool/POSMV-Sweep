#pragma once
#include <gtk/gtk.h>
#include <pango/pangocairo.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <utility>
namespace posmv {
// UI-thread only sketch, seen from the stern: the ship leans with Real Roll (positive to the
// right), and the MBES steers the beam relative to the hull by the Roll it receives, but only
// within its limit. Dashed: the Output we send. Solid: where the MBES can actually steer. In the
// water the dashed beam is at the Offset angle. The port/starboard side still needs commissioning.
class BeamView {
    GtkWidget* area_{};
    double real_deg_{}, output_deg_{}, limit_deg_ = 10;
    bool active_{};
    static void text(cairo_t* cr, GtkWidget* widget, const char* value, double x, double y, bool right_aligned, int width) {
        auto* layout = gtk_widget_create_pango_layout(widget, value);
        int w{}, h{};
        pango_layout_get_pixel_size(layout, &w, &h);
        if (right_aligned) x -= w;
        cairo_move_to(cr, std::clamp(x, 2.0, width - w - 2.0), y - h / 2.0);
        pango_cairo_show_layout(cr, layout);
        g_object_unref(layout);
    }
    static void draw(GtkDrawingArea* area, cairo_t* cr, int width, int height, gpointer data) {
        const auto& self = *static_cast<BeamView*>(data);
        auto* widget = GTK_WIDGET(area);
        const double pi = 3.14159265358979323846, degree = pi / 180;
        const double u = std::min(width / 6.0, height / 10.0);  // drawing unit
        const double cx = width / 2.0, water = height * 0.5, length = height - water - 0.3 * u;
        const double roll = self.active_ ? self.real_deg_ : 0;
        const double limit = self.limit_deg_, output = self.active_ ? self.output_deg_ : 0;
        const double actual = std::clamp(output, -limit, limit);
        // World angles from straight down, positive to the right: the hull's down axis leans to -roll.
        const double axis = -roll;
        auto at = [&](double angle, double r) { return std::pair{cx + r * std::sin(angle * degree), water + r * std::cos(angle * degree)}; };
        cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
        cairo_set_line_join(cr, CAIRO_LINE_JOIN_ROUND);
        cairo_set_source_rgb(cr, 0.12, 0.35, 0.75);  // water
        cairo_set_line_width(cr, 3);
        for (double x = 4; x <= width - 4; x += 2) {
            const double y = water + std::sin(x / (0.9 * u) * pi) * 0.12 * u;
            if (x == 4) cairo_move_to(cr, x, y); else cairo_line_to(cr, x, y);
        }
        cairo_stroke(cr);
        // MBES range: ±limit around the hull's down axis, so it leans with the ship.
        cairo_set_source_rgba(cr, 0.45, 0.45, 0.45, 0.18);
        cairo_move_to(cr, cx, water);
        cairo_arc(cr, cx, water, length, pi / 2 - (axis + limit) * degree, pi / 2 - (axis - limit) * degree);
        cairo_close_path(cr);
        cairo_fill(cr);
        cairo_save(cr);  // ship, leaning about the keel
        cairo_translate(cr, cx, water);
        cairo_rotate(cr, roll * degree);
        cairo_set_source_rgb(cr, 0.1, 0.1, 0.1);
        cairo_set_line_width(cr, 2.5);
        cairo_move_to(cr, -1.5 * u, -1.2 * u);
        cairo_line_to(cr, 1.5 * u, -1.2 * u);
        cairo_line_to(cr, 0.9 * u, 0);
        cairo_line_to(cr, -0.9 * u, 0);
        cairo_close_path(cr);
        cairo_rectangle(cr, -0.8 * u, -2.3 * u, 1.6 * u, 1.1 * u);
        cairo_move_to(cr, 0, -2.3 * u);
        cairo_line_to(cr, 0, -3.2 * u);
        cairo_move_to(cr, -0.45 * u, -2.85 * u);
        cairo_line_to(cr, 0.45 * u, -2.85 * u);
        cairo_stroke(cr);
        cairo_restore(cr);
        const bool clipped = self.active_ && std::abs(output) > limit;
        const double red[] = {0.82, 0.1, 0.1}, orange[] = {0.9, 0.45, 0.0}, grey[] = {0.62, 0.62, 0.62};
        const double* beam = !self.active_ ? grey : clipped ? orange : red;
        if (clipped) {  // dashed: the Output we send, beyond the MBES range
            const auto [x, y] = at(axis + output, length);
            cairo_set_source_rgb(cr, red[0], red[1], red[2]);
            cairo_set_line_width(cr, 2);
            const double dashes[] = {6, 4};
            cairo_set_dash(cr, dashes, 2, 0);
            cairo_move_to(cr, cx, water);
            cairo_line_to(cr, x, y);
            cairo_stroke(cr);
            cairo_set_dash(cr, nullptr, 0, 0);
        }
        const auto [bx, by] = at(axis + actual, length);  // solid: where the MBES can steer
        cairo_set_source_rgb(cr, beam[0], beam[1], beam[2]);
        cairo_set_line_width(cr, 4);
        cairo_move_to(cr, cx, water);
        cairo_line_to(cr, bx, by);
        cairo_stroke(cr);
        char value[48];
        cairo_set_source_rgb(cr, 0.1, 0.1, 0.1);
        if (self.active_) std::snprintf(value, sizeof(value), "Roll %+.1f°", roll);
        else std::snprintf(value, sizeof(value), "Roll —");
        auto* layout = gtk_widget_create_pango_layout(widget, value);  // above the ship
        int w{}, h{};
        pango_layout_get_pixel_size(layout, &w, &h);
        cairo_move_to(cr, cx - w / 2.0, 2);
        pango_cairo_show_layout(cr, layout);
        g_object_unref(layout);
        cairo_set_source_rgb(cr, grey[0] - 0.2, grey[1] - 0.2, grey[2] - 0.2);
        std::snprintf(value, sizeof(value), "MBES ±%.0f°", limit);
        text(cr, widget, value, 2, height - 10, false, width);
        if (!self.active_) return;
        // Labels sit just outside the lines: Output beyond the dashed one, the actual angle inside the solid one.
        const bool right = output >= 0;
        const double label = 0.55 * length, spread = 4;
        if (clipped) {
            const auto [ox, oy] = at(axis + output + (right ? spread : -spread), label);
            cairo_set_source_rgb(cr, red[0], red[1], red[2]);
            std::snprintf(value, sizeof(value), "Output %+.1f°", output);
            text(cr, widget, value, ox, oy, !right, width);
            const auto [ix, iy] = at(axis + actual - (right ? spread : -spread), label);
            cairo_set_source_rgb(cr, orange[0], orange[1], orange[2]);
            std::snprintf(value, sizeof(value), "實際 %+.1f°", actual);
            text(cr, widget, value, ix, iy, right, width);
        } else {
            const auto [ox, oy] = at(axis + output + (right ? spread : -spread), label);
            cairo_set_source_rgb(cr, red[0], red[1], red[2]);
            std::snprintf(value, sizeof(value), "Output %+.1f°", output);
            text(cr, widget, value, ox, oy, !right, width);
        }
    }
public:
    GtkWidget* create() {
        area_ = gtk_drawing_area_new();
        gtk_widget_set_size_request(area_, 260, 170);
        gtk_drawing_area_set_draw_func(GTK_DRAWING_AREA(area_), draw, this, nullptr);
        gtk_widget_set_tooltip_text(area_, "從船尾往船頭看。船依 Real Roll 傾斜；灰色扇形是 MBES 能補償的範圍，跟著船轉。"
            "虛線：送出的 Output；實線：MBES 實際能轉到的角度（限制在上限內）。左右舷待現場確認");
        return area_;
    }
    // `active` is false while no output is sent.
    void update(double real_deg, double output_deg, double limit_deg, bool active) {
        if (real_deg == real_deg_ && output_deg == output_deg_ && limit_deg == limit_deg_ && active == active_) return;
        real_deg_ = real_deg; output_deg_ = output_deg; limit_deg_ = limit_deg; active_ = active;
        if (area_) gtk_widget_queue_draw(area_);
    }
};
}
