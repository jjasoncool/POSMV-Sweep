#pragma once
#include <gtk/gtk.h>
#include <pango/pangocairo.h>
#include "motion/sweep.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <utility>
namespace posmv {
// UI-thread only sketch, seen from the stern: the ship leans with Real Roll (positive to the
// right), and the MBES steers the beam relative to the hull by the Roll it receives, but only
// within its limit. Solid: the expected beam. Dashed: the target (Offset), drawn only when the
// limit keeps the beam from it. The port/starboard side still needs commissioning.
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
        // Both lines are angles from straight down, the same frame as the trend chart: dashed orange
        // is the target (the Offset), solid red where the beam should end up.
        const bool clipped = self.active_ && std::abs(output) > limit;
        const double target = output - roll, beam = expected_beam(roll, output, limit);
        const double red[] = {0.82, 0.1, 0.1}, orange[] = {0.9, 0.45, 0.0}, grey[] = {0.62, 0.62, 0.62};
        if (clipped) {  // only beyond the limit do target and beam part
            const auto [x, y] = at(target, length);
            cairo_set_source_rgb(cr, orange[0], orange[1], orange[2]);
            cairo_set_line_width(cr, 2);
            const double dashes[] = {6, 4};
            cairo_set_dash(cr, dashes, 2, 0);
            cairo_move_to(cr, cx, water);
            cairo_line_to(cr, x, y);
            cairo_stroke(cr);
            cairo_set_dash(cr, nullptr, 0, 0);
        }
        const double* colour = self.active_ ? red : grey;
        const auto [bx, by] = at(self.active_ ? beam : 0, length);
        cairo_set_source_rgb(cr, colour[0], colour[1], colour[2]);
        cairo_set_line_width(cr, 4);
        cairo_move_to(cr, cx, water);
        cairo_line_to(cr, bx, by);
        cairo_stroke(cr);
        char value[64];
        cairo_set_source_rgb(cr, 0.1, 0.1, 0.1);
        if (self.active_) std::snprintf(value, sizeof(value), "Roll %+.1f°", roll);
        else std::snprintf(value, sizeof(value), "Roll —");
        auto* layout = gtk_widget_create_pango_layout(widget, value);  // above the ship
        int w{}, h{};
        pango_layout_get_pixel_size(layout, &w, &h);
        cairo_move_to(cr, cx - w / 2.0, 2);
        pango_cairo_show_layout(cr, layout);
        g_object_unref(layout);
        // Bottom left, two short lines clear of the beam: how far the instrument turns relative to the hull.
        if (clipped) cairo_set_source_rgb(cr, orange[0], orange[1], orange[2]);
        else cairo_set_source_rgb(cr, grey[0] - 0.2, grey[1] - 0.2, grey[2] - 0.2);
        if (self.active_) {
            std::snprintf(value, sizeof(value), "MBES 轉 %+.1f°", std::clamp(output, -limit, limit));
            text(cr, widget, value, 2, height - 28, false, width);
        }
        if (clipped) std::snprintf(value, sizeof(value), "已達上限");
        else std::snprintf(value, sizeof(value), self.active_ ? "上限 ±%.0f°" : "MBES 上限 ±%.0f°", limit);
        text(cr, widget, value, 2, height - 10, false, width);
        if (!self.active_) return;
        // Labels sit beside their lines: the target's beyond it, the beam's on the other side.
        const double label = 0.55 * length, spread = 4;
        const bool right = clipped ? target > beam : beam >= 0;
        const auto [lx, ly] = at(beam + (clipped == right ? -spread : spread), label);
        cairo_set_source_rgb(cr, red[0], red[1], red[2]);
        std::snprintf(value, sizeof(value), "波束 %+.1f°", beam);
        text(cr, widget, value, lx, ly, clipped == right, width);
        if (clipped) {
            const auto [tx, ty] = at(target + (right ? spread : -spread), label);
            cairo_set_source_rgb(cr, orange[0], orange[1], orange[2]);
            std::snprintf(value, sizeof(value), "目標 %+.1f°", target);
            text(cr, widget, value, tx, ty, !right, width);
        }
    }
public:
    GtkWidget* create() {
        area_ = gtk_drawing_area_new();
        gtk_widget_set_size_request(area_, 260, 170);
        gtk_drawing_area_set_draw_func(GTK_DRAWING_AREA(area_), draw, this, nullptr);
        gtk_widget_set_tooltip_text(area_, "從船尾往船頭看。船依 Real Roll 傾斜；灰色扇形是 MBES 能補償的範圍，跟著船轉。"
            "角度都從垂直往下量。紅色實線：預期的波束方向，假設 MBES 依收到的 Roll 補償、最多轉到上限，不是 MBES 回報的值。"
            "橘色虛線：目標（Offset），只在 MBES 轉不到時出現。左右舷待現場確認");
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
