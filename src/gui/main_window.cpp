#include <gtk/gtk.h>
#include "gui/port_selector.hpp"
#include "gui/roll_chart.hpp"
#include "app/proxy_engine.hpp"
#include <sstream>
#include <iomanip>
namespace {
struct Controller {
    posmv::ProxyEngine engine;
    posmv::RollChart chart;
    GtkWidget *input, *output, *baud, *rate, *angle, *cycle, *enable, *simulation, *invert, *status, *motion, *diagnostics, *timeout;
    bool syncing{};
    guint timer{};
};
void configure(Controller* c) {
    posmv::SweepConfig config;
    config.enabled = gtk_check_button_get_active(GTK_CHECK_BUTTON(c->enable));
    config.amplitude_deg = gtk_spin_button_get_value(GTK_SPIN_BUTTON(c->angle));
    config.cycle_seconds = gtk_spin_button_get_value(GTK_SPIN_BUTTON(c->cycle));
    config.direction = gtk_check_button_get_active(GTK_CHECK_BUTTON(c->invert)) ? posmv::SweepDirectionConvention::PositiveFirst : posmv::SweepDirectionConvention::NegativeFirst;
    c->engine.configure(config);
}
void changed(GtkWidget*, gpointer data) {
    auto* c = static_cast<Controller*>(data);
    if (c->syncing) return;
    try { c->engine.enable(gtk_check_button_get_active(GTK_CHECK_BUTTON(c->enable))); }
    catch (const std::exception& e) { gtk_label_set_text(GTK_LABEL(c->status), e.what()); }
}
void apply(GtkButton*, gpointer data) {
    auto* c = static_cast<Controller*>(data);
    try { configure(c); gtk_label_set_text(GTK_LABEL(c->status), "設定已套用，Sweep 從 CENTER 重啟；offset 可能跳變"); }
    catch (const std::exception& e) { gtk_label_set_text(GTK_LABEL(c->status), e.what()); }
}
void connect_clicked(GtkButton*, gpointer data) {
    auto* c = static_cast<Controller*>(data);
    try {
        configure(c); const unsigned rates[] = {25, 50, 100};
        c->engine.stale_timeout(std::chrono::milliseconds(gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(c->timeout))));
        c->engine.start(posmv::selected_port(c->input), posmv::selected_port(c->output),
            gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(c->baud)), rates[gtk_drop_down_get_selected(GTK_DROP_DOWN(c->rate))],
            false);
    } catch (const std::exception& e) { gtk_label_set_text(GTK_LABEL(c->status), e.what()); }
}
void scan_ports(GtkButton*, gpointer data) {
    auto* c = static_cast<Controller*>(data);
    if (c->engine.snapshot().connected) return;
    posmv::refresh_ports(c->input); posmv::refresh_ports(c->output);
}
void disconnect_clicked(GtkButton*, gpointer data) {
    auto* c = static_cast<Controller*>(data); c->engine.stop(); gtk_label_set_text(GTK_LABEL(c->status), "Disconnected");
}
gboolean refresh(gpointer data) {
    auto* c = static_cast<Controller*>(data); const auto s = c->engine.snapshot();
    for (auto* widget : {c->input, c->output, c->baud, c->rate, c->timeout}) gtk_widget_set_sensitive(widget, !s.connected);
    if (s.connected) {
        c->syncing = true;
        gtk_check_button_set_active(GTK_CHECK_BUTTON(c->enable), s.sweep_enabled);
        c->syncing = false;
    }
    if (s.connected || s.status.starts_with("Error:")) gtk_label_set_text(GTK_LABEL(c->status), s.status.c_str());
    if(s.connected && !s.stale && s.output_count) c->chart.add(s.real,s.offset,s.output);
    std::ostringstream motion, diagnostics;
    motion << std::fixed << std::setprecision(2) << "Real Roll: " << s.real << "°   Offset: " << s.offset << "°   Output: " << s.output << "°";
    diagnostics << std::fixed << std::setprecision(2) << "Input: " << s.input_rate << " Hz   Output: " << s.output_rate << " Hz\nRX: " << s.input_count << "   TX: " << s.output_count << "   Invalid: " << s.invalid << "   Clipped: " << s.clipped << "\nLate (>1ms): " << s.late << "   Maximum lateness: " << s.lateness_ms << " ms";
    gtk_label_set_text(GTK_LABEL(c->motion), motion.str().c_str()); gtk_label_set_text(GTK_LABEL(c->diagnostics), diagnostics.str().c_str());
    diagnostics << "\nInterval avg/min/max: " << s.average_ms << " / " << s.minimum_ms << " / " << s.maximum_ms << " ms";
    gtk_label_set_text(GTK_LABEL(c->diagnostics), diagnostics.str().c_str());
    return G_SOURCE_CONTINUE;
}
void destroy(gpointer data) { auto* c = static_cast<Controller*>(data); g_source_remove(c->timer); delete c; }
void activate(GtkApplication* application, gpointer) {
    auto* window = gtk_application_window_new(application);
    gtk_window_set_title(GTK_WINDOW(window), "POSMV-Sweep");
    gtk_window_set_default_size(GTK_WINDOW(window), 760, 640);
    auto* box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
    gtk_widget_set_margin_start(box, 24);
    gtk_widget_set_margin_end(box, 24);
    gtk_widget_set_margin_top(box, 24);
    gtk_widget_set_margin_bottom(box, 24);
    gtk_box_append(GTK_BOX(box), gtk_label_new("POS MV TSS1 Motion Sweep Proxy"));
    auto* c = new Controller;
    auto row = [&](const char* label, GtkWidget* widget) {
        auto* line = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
        auto* text = gtk_label_new(label); gtk_widget_set_size_request(text, 170, -1);
        gtk_label_set_xalign(GTK_LABEL(text), 0); gtk_box_append(GTK_BOX(line), text);
        gtk_widget_set_hexpand(widget, TRUE); gtk_box_append(GTK_BOX(line), widget);
        gtk_box_append(GTK_BOX(box), line); return widget;
    };
    auto number = [&](const char* label, double low, double high, double step, double value) {
        auto* spin = gtk_spin_button_new_with_range(low, high, step); gtk_spin_button_set_value(GTK_SPIN_BUTTON(spin), value); return row(label, spin);
    };
    c->input = row("輸入 COM", gtk_drop_down_new(nullptr, nullptr)); posmv::refresh_ports(c->input);
    c->output = row("輸出 COM", gtk_drop_down_new(nullptr, nullptr)); posmv::refresh_ports(c->output);
    c->baud = number("Baud (8N1)", 1200, 4000000, 1200, 38400);
    c->timeout = number("資料過期 timeout ms", 10, 60000, 10, 500);
    const char* rates[] = {"25 Hz", "50 Hz", "100 Hz", nullptr}; c->rate = row("Output rate", gtk_drop_down_new_from_strings(rates));
    c->angle = number("Angle ± degrees", 0, 90, 0.1, 10); c->cycle = number("Full cycle seconds", 0.1, 3600, 0.1, 4);
    c->enable = row("Sweep", gtk_check_button_new_with_label("ON / OFF (OFF continues output)"));
    c->invert = row("Direction", gtk_check_button_new_with_label("Invert Roll direction"));
    g_signal_connect(c->enable, "toggled", G_CALLBACK(changed), c);
    auto* buttons = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8); gtk_box_append(GTK_BOX(box), buttons);
    auto button = [&](const char* label, GCallback cb) { auto* b = gtk_button_new_with_label(label); gtk_box_append(GTK_BOX(buttons), b); g_signal_connect(b, "clicked", cb, c); };
    button("重新掃描 COM", G_CALLBACK(scan_ports));
    button("連線 COM", G_CALLBACK(connect_clicked)); button("中斷連線", G_CALLBACK(disconnect_clicked));
    button("套用 Sweep 設定", G_CALLBACK(apply));
    c->status = gtk_label_new("Disconnected"); c->motion = gtk_label_new(""); c->diagnostics = gtk_label_new("");
    for (auto* widget : {c->status, c->motion, c->diagnostics}) gtk_box_append(GTK_BOX(box), widget);
    gtk_box_append(GTK_BOX(box), gtk_label_new("CENTER → LEFT → CENTER → RIGHT → CENTER"));
    gtk_box_append(GTK_BOX(box), gtk_label_new("開發版：輸入過期停止輸出；恢復後 Sweep OFF。實體 Roll 方向尚未驗證。"));
    gtk_box_append(GTK_BOX(box),gtk_label_new("最近 10 秒：藍 Real ／ 黃 Offset ／ 綠 Output"));
    gtk_box_append(GTK_BOX(box),c->chart.create());
    c->timer = g_timeout_add(100, refresh, c);
    g_object_set_data_full(G_OBJECT(window), "controller", c, destroy);
    gtk_window_set_child(GTK_WINDOW(window), box);
    gtk_window_present(GTK_WINDOW(window));
}
}
int launch_gui() {
    auto* application = gtk_application_new("org.posmvsweep.App", G_APPLICATION_DEFAULT_FLAGS);
    g_signal_connect(application, "activate", G_CALLBACK(activate), nullptr);
    const int result = g_application_run(G_APPLICATION(application), 0, nullptr);
    g_object_unref(application);
    return result;
}
