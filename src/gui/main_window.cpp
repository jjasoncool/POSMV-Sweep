#include <gtk/gtk.h>
#include "gui/port_selector.hpp"
#include "gui/roll_chart.hpp"
#include "gui/beam_view.hpp"
#include "app/proxy_engine.hpp"
#include <algorithm>
#include <iterator>
#include <sstream>
#include <iomanip>
namespace {
struct Controller {
    posmv::ProxyEngine engine;
    posmv::RollChart chart;
    posmv::BeamView beam;
    GtkWidget *input, *output, *scan, *baud, *timeout, *connect, *state;
    GtkWidget *enable, *mode, *mode_fields, *angle, *cycle, *fixed_offset, *invert, *finish, *rate_info;
    GtkWidget *limit, *rate_limit, *notice, *apply, *motion, *diagnostics;
    posmv::SweepConfig applied;  // what the last Apply or Connect handed to the engine
    std::string connect_error;
    bool syncing{};
    guint timer{};
};
posmv::SweepConfig settings(Controller* c) {
    posmv::SweepConfig config;
    config.enabled = gtk_check_button_get_active(GTK_CHECK_BUTTON(c->enable));
    config.amplitude_deg = gtk_spin_button_get_value(GTK_SPIN_BUTTON(c->angle));
    config.cycle_seconds = gtk_spin_button_get_value(GTK_SPIN_BUTTON(c->cycle));
    config.direction = gtk_check_button_get_active(GTK_CHECK_BUTTON(c->invert)) ? posmv::SweepDirectionConvention::PositiveFirst : posmv::SweepDirectionConvention::NegativeFirst;
    config.mode = gtk_drop_down_get_selected(GTK_DROP_DOWN(c->mode)) == 1 ? posmv::SweepMode::Fixed : posmv::SweepMode::Sine;
    config.fixed_offset_deg = gtk_spin_button_get_value(GTK_SPIN_BUTTON(c->fixed_offset));
    return config;
}
void configure(Controller* c) { c->applied = settings(c); c->engine.configure(c->applied); }
// Settings that need Apply differ from what was applied (only the fields the planned mode uses).
bool unapplied(const posmv::SweepConfig& planned, const posmv::SweepConfig& applied) {
    if (planned.mode != applied.mode) return true;
    if (planned.mode == posmv::SweepMode::Fixed) return planned.fixed_offset_deg != applied.fixed_offset_deg;
    return planned.amplitude_deg != applied.amplitude_deg || planned.cycle_seconds != applied.cycle_seconds ||
        planned.direction != applied.direction;
}
unsigned selected_baud(Controller* c) {
    const auto index = std::min<std::size_t>(gtk_drop_down_get_selected(GTK_DROP_DOWN(c->baud)), std::size(posmv::standard_bauds) - 1);
    return posmv::standard_bauds[index];
}
void finish_toggled(GtkWidget*, gpointer data) {
    auto* c = static_cast<Controller*>(data);
    c->engine.finish_at_center(gtk_check_button_get_active(GTK_CHECK_BUTTON(c->finish)));
}
void limit_changed(GtkWidget*, gpointer data) {
    auto* c = static_cast<Controller*>(data);
    c->engine.stabilisation_limit(gtk_spin_button_get_value(GTK_SPIN_BUTTON(c->limit)));
}
void changed(GtkWidget*, gpointer data) {
    auto* c = static_cast<Controller*>(data);
    if (c->syncing) return;
    try { c->engine.enable(gtk_check_button_get_active(GTK_CHECK_BUTTON(c->enable))); gtk_label_set_text(GTK_LABEL(c->notice), ""); }
    catch (const std::exception& e) { gtk_label_set_text(GTK_LABEL(c->notice), e.what()); }
}
// Each mode shows only its own fields: Angle and Cycle for a sine, one angle for a fixed Offset.
void mode_changed(GObject*, GParamSpec*, gpointer data) {
    auto* c = static_cast<Controller*>(data);
    const bool fixed = gtk_drop_down_get_selected(GTK_DROP_DOWN(c->mode)) == 1;
    gtk_stack_set_visible_child_name(GTK_STACK(c->mode_fields), fixed ? "fixed" : "sine");
}
void apply(GtkButton*, gpointer data) {
    auto* c = static_cast<Controller*>(data);
    try {
        const auto config = settings(c);
        c->engine.configure(config);
        c->applied = config;
        const auto s = c->engine.snapshot();
        const bool fixed = config.mode == posmv::SweepMode::Fixed;
        const char* text = !s.connected ? "設定已更新，連線後生效"
            : !config.enabled ? (fixed ? "已更新固定 Offset，開啟 Sweep 時生效" : "設定已更新，開啟 Sweep 時從 CENTER 開始")
            : s.apply_pending ? (fixed ? "固定 Offset 將在下一個 CENTER 生效" : "新設定將在下一個 CENTER 生效，並從 CENTER 開始")
            : fixed ? "已套用固定 Offset；offset 可能跳變" : "設定已套用，Sweep 從 CENTER 重啟；offset 可能跳變";
        gtk_label_set_text(GTK_LABEL(c->notice), text);
    } catch (const std::exception& e) { gtk_label_set_text(GTK_LABEL(c->notice), e.what()); }
}
// Connection problems show beside the Connect button, in the state label.
void connect_clicked(GtkButton*, gpointer data) {
    auto* c = static_cast<Controller*>(data);
    gtk_label_set_text(GTK_LABEL(c->notice), "");
    c->connect_error.clear();
    if (c->engine.snapshot().connected) { c->engine.stop(); return; }
    try {
        configure(c);
        c->engine.stale_timeout(std::chrono::milliseconds(gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(c->timeout))));
        c->engine.start(posmv::selected_port(c->input), posmv::selected_port(c->output), selected_baud(c));
    } catch (const std::exception& e) { c->connect_error = e.what(); }
}
void scan_ports(GtkButton*, gpointer data) {
    auto* c = static_cast<Controller*>(data);
    if (c->engine.snapshot().connected) return;
    posmv::refresh_ports(c->input); posmv::refresh_ports(c->output); posmv::avoid_port_of(c->output, c->input);
}
std::string state_text(const posmv::ProxySnapshot& s) {
    switch (s.state) {
    case posmv::ProxyState::Disconnected: return "未連線";
    case posmv::ProxyState::Waiting: return "已連線，等待輸入資料";
    case posmv::ProxyState::Active:
        if (s.sweep_finishing) return "運作中：Sweep 收尾中，到下一個 CENTER 停止";
        if (s.apply_pending) return "運作中：新設定將在下一個 CENTER 生效";
        return s.sweep_stopped_by_stale ? "運作中（輸入曾過期，Sweep 已自動關閉）" : "運作中";
    case posmv::ProxyState::Stale: return "資料過期：已暫停輸出並關閉 Sweep";
    case posmv::ProxyState::Error: return "錯誤：" + s.error;
    }
    return {};
}
const char* state_color(posmv::ProxyState state) {
    switch (state) {
    case posmv::ProxyState::Active: return "#2e9e44";
    case posmv::ProxyState::Waiting: case posmv::ProxyState::Stale: return "#d08a00";
    case posmv::ProxyState::Error: return "#d0342c";
    case posmv::ProxyState::Disconnected: break;
    }
    return "#888888";
}
gboolean refresh(gpointer data) {
    auto* c = static_cast<Controller*>(data); const auto s = c->engine.snapshot();
    for (auto* widget : {c->input, c->output, c->scan, c->baud, c->timeout}) gtk_widget_set_sensitive(widget, !s.connected);
    gtk_button_set_label(GTK_BUTTON(c->connect), s.connected ? "中斷連線" : "連線");
    // Foolproof: the same port for input and output, or no port at all, cannot be connected.
    const auto conflict = s.connected ? std::string() : posmv::port_conflict(c->input, c->output);
    gtk_widget_set_sensitive(c->connect, conflict.empty());
    if (s.connected) {
        c->syncing = true;
        gtk_check_button_set_active(GTK_CHECK_BUTTON(c->enable), s.sweep_enabled && !s.sweep_finishing);
        c->syncing = false;
    }
    auto* state = !conflict.empty() ? g_markup_printf_escaped("<span foreground=\"#c25e00\">● %s</span>", conflict.c_str())
        : !s.connected && !c->connect_error.empty()
            ? g_markup_printf_escaped("<span foreground=\"#d0342c\">● 無法連線：%s</span>", c->connect_error.c_str())
            : g_markup_printf_escaped("<span foreground=\"%s\">●</span> %s", state_color(s.state), state_text(s).c_str());
    gtk_label_set_markup(GTK_LABEL(c->state), state); g_free(state);
    const auto planned = settings(c);
    // Blue Apply means some settings on screen are not in effect yet.
    if (unapplied(planned, c->applied)) gtk_widget_add_css_class(c->apply, "suggested-action");
    else gtk_widget_remove_css_class(c->apply, "suggested-action");
    const bool sine = planned.mode == posmv::SweepMode::Sine;
    std::ostringstream rate;
    rate << std::fixed << std::setprecision(1);
    if (sine && posmv::peak_sweep_rate(planned) > gtk_spin_button_get_value(GTK_SPIN_BUTTON(c->rate_limit)))
        rate << "<span foreground=\"#c25e00\">最大擺動速度 " << posmv::peak_sweep_rate(planned) << "°/s ⚠ 超過上限</span>";
    else if (sine) rate << "最大擺動速度 " << posmv::peak_sweep_rate(planned) << "°/s";
    gtk_label_set_markup(GTK_LABEL(c->rate_info), rate.str().c_str());
    const double limit = gtk_spin_button_get_value(GTK_SPIN_BUTTON(c->limit));
    const bool sending = s.state == posmv::ProxyState::Active && s.output_count;
    if (sending) c->chart.add(s.real, s.offset, s.output);
    else c->chart.break_line();
    c->beam.update(s.real, s.output, limit, sending);
    std::ostringstream motion, diagnostics;
    motion << std::fixed << std::setprecision(2) << "<span size=\"x-large\">";
    if (s.state == posmv::ProxyState::Active) {
        motion << "Real Roll " << s.real << "°　　Offset " << s.offset << "°　　";
        // Output turns orange while it exceeds the MBES limit.
        if (sending && s.over_limit) motion << "<span foreground=\"#c25e00\">Output " << s.output << "°</span>";
        else motion << "Output " << s.output << "°";
    } else if (s.state == posmv::ProxyState::Stale && s.output_count)
        motion << "最後值（已停止輸出）　Real Roll " << s.real << "°　Offset " << s.offset << "°　Output " << s.output << "°";
    else motion << "Real Roll —　　Offset —　　Output —";
    motion << "</span>";
    const auto& in = s.input_timing; const auto& out = s.output_timing;
    diagnostics << std::fixed << std::setprecision(1)
        << "輸入：即時 " << in.current_hz << " Hz／平均 " << in.average_hz << " Hz　收到 " << s.input_count
        << "　無效 " << s.invalid << "　中斷 " << in.gaps << " 次　｜　"
        << "輸出：即時 " << out.current_hz << " Hz／平均 " << out.average_hz << " Hz　送出 " << s.output_count
        << "　截斷 " << s.clipped << "\n"
        << "輸出間隔（ms）：最小 " << out.intervals.min_ms << "　平均 " << out.intervals.mean_ms
        << "　p99 " << out.intervals.p99_ms << "　最大 " << out.intervals.max_ms << "　｜　"
        << std::setprecision(2) << "轉送延遲：平均 " << s.forward_mean_ms << " ms　最大 " << s.forward_max_ms << " ms\n"
        << "Output 超出 MBES 上限：累計 " << s.over_limit_count << " 筆";
    gtk_label_set_markup(GTK_LABEL(c->motion), motion.str().c_str());
    gtk_label_set_text(GTK_LABEL(c->diagnostics), diagnostics.str().c_str());
    return G_SOURCE_CONTINUE;
}
void destroy(gpointer data) { auto* c = static_cast<Controller*>(data); g_source_remove(c->timer); delete c; }
GtkWidget* row(std::initializer_list<GtkWidget*> widgets) {
    auto* line = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    for (auto* widget : widgets) gtk_box_append(GTK_BOX(line), widget);
    return line;
}
GtkWidget* number(double low, double high, double step, double value, int width) {
    auto* spin = gtk_spin_button_new_with_range(low, high, step);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(spin), value);
    gtk_editable_set_width_chars(GTK_EDITABLE(spin), width);
    return spin;
}
GtkWidget* text(const char* label) { return gtk_label_new(label); }
void activate(GtkApplication* application, gpointer) {
    auto* window = gtk_application_window_new(application);
    gtk_window_set_title(GTK_WINDOW(window), "POSMV-Sweep");
    gtk_window_set_default_size(GTK_WINDOW(window), 900, 620);
    auto* box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    gtk_widget_set_margin_start(box, 16);
    gtk_widget_set_margin_end(box, 16);
    gtk_widget_set_margin_top(box, 16);
    gtk_widget_set_margin_bottom(box, 16);
    auto* c = new Controller;
    auto add = [&](GtkWidget* widget) { gtk_box_append(GTK_BOX(box), widget); return widget; };
    auto button = [&](const char* label, GCallback callback) {
        auto* b = gtk_button_new_with_label(label); g_signal_connect(b, "clicked", callback, c); return b;
    };
    // Connection
    c->input = gtk_drop_down_new(nullptr, nullptr); posmv::refresh_ports(c->input); gtk_widget_set_hexpand(c->input, TRUE);
    c->output = gtk_drop_down_new(nullptr, nullptr); posmv::refresh_ports(c->output); gtk_widget_set_hexpand(c->output, TRUE);
    posmv::avoid_port_of(c->output, c->input);
    c->scan = button("重新掃描 COM", G_CALLBACK(scan_ports));
    add(row({text("輸入 COM"), c->input, text("輸出 COM"), c->output, c->scan}));
    // Foolproof baud: standard values only, starting at POS MV's recommended 9600 for 25 Hz.
    auto* bauds = gtk_string_list_new(nullptr);
    for (const unsigned baud : posmv::standard_bauds) gtk_string_list_append(bauds, std::to_string(baud).c_str());
    c->baud = gtk_drop_down_new(G_LIST_MODEL(bauds), nullptr);
    gtk_drop_down_set_selected(GTK_DROP_DOWN(c->baud),
        static_cast<guint>(std::ranges::find(posmv::standard_bauds, 9600u) - std::begin(posmv::standard_bauds)));
    gtk_widget_set_tooltip_text(c->baud, "與 POS MV、MBES 的設定一致；手冊建議 25 Hz 至少 9600，200 Hz 需要 57600 以上");
    c->timeout = number(10, 60000, 10, 500, 5);
    gtk_widget_set_tooltip_text(c->timeout, "超過這個時間沒收到有效資料就判定過期：暫停輸出並關閉 Sweep；恢復後 Sweep 維持關閉");
    c->connect = button("連線", G_CALLBACK(connect_clicked));
    c->state = gtk_label_new(""); gtk_label_set_xalign(GTK_LABEL(c->state), 0); gtk_widget_set_hexpand(c->state, TRUE);
    add(row({text("鮑率（8N1）"), c->baud, text("過期門檻"), c->timeout, text("ms"), c->connect, c->state}));
    add(gtk_separator_new(GTK_ORIENTATION_HORIZONTAL));
    // Sweep
    c->enable = gtk_check_button_new_with_label("Sweep 開啟");
    gtk_widget_set_tooltip_text(c->enable, "關閉時照常轉送原始資料，不加 Offset");
    const char* modes[] = {"正弦 Sweep", "固定 Offset（驗證用）", nullptr};
    c->mode = gtk_drop_down_new_from_strings(modes);
    gtk_widget_set_size_request(c->mode, 190, -1);  // wide enough for either mode, so the fields never move
    c->angle = number(0, 90, 0.1, 10, 5);
    c->cycle = number(0.1, 3600, 0.1, 4, 6);
    gtk_widget_set_tooltip_text(c->cycle, "一個 Cycle：CENTER → 負峰 → CENTER → 正峰 → CENTER（反轉時先正）");
    c->fixed_offset = number(-90, 90, 0.1, 5, 5);
    c->mode_fields = gtk_stack_new();
    gtk_stack_add_named(GTK_STACK(c->mode_fields), row({text("幅度 Angle ±"), c->angle, text("°　週期 Cycle"), c->cycle, text("秒")}), "sine");
    gtk_stack_add_named(GTK_STACK(c->mode_fields), row({text("Offset"), c->fixed_offset, text("°")}), "fixed");
    add(row({c->enable, text("模式"), c->mode, c->mode_fields}));
    c->invert = gtk_check_button_new_with_label("反轉（先正後負）");
    c->finish = gtk_check_button_new_with_label("關閉與套用時收尾到 CENTER（不立即跳回 0）");
    gtk_widget_set_tooltip_text(c->finish, "關閉 Sweep 或套用新設定時，Offset 沿正弦走到下一個 CENTER 才停止或切換");
    // Apply's feedback sits right above the button.
    c->notice = gtk_label_new(""); gtk_widget_set_hexpand(c->notice, TRUE);
    gtk_label_set_xalign(GTK_LABEL(c->notice), 1); gtk_label_set_ellipsize(GTK_LABEL(c->notice), PANGO_ELLIPSIZE_END);
    add(row({c->invert, c->finish, c->notice}));
    c->limit = number(0.5, 90, 0.5, 10, 5);
    gtk_widget_set_tooltip_text(c->limit, "下游 MBES 能補償的最大 |Roll|，請依機型設定（例如 NORBIT ±10°；R2Sonic 約 ±(160° − sector)/2）。"
        "只用來警告，不會限制輸出");
    c->rate_limit = number(1, 360, 1, 20, 4);
    gtk_widget_set_tooltip_text(c->rate_limit, "Offset 變化速度的警告門檻；部分 MBES 會標記 Roll 變化太快的 ping。"
        "下游是 RESON 時請改成 10°/s");
    c->rate_info = gtk_label_new("");
    // Apply closes the Sweep block at its bottom right.
    c->apply = button("套用", G_CALLBACK(apply));
    gtk_widget_set_tooltip_text(c->apply, "套用模式、幅度、週期、固定 Offset 與反轉；有未套用的變更時按鈕是藍色。"
        "Sweep 開啟、收尾到 CENTER 與兩個上限一改就生效，不需要套用");
    auto* spacer = gtk_label_new(""); gtk_widget_set_hexpand(spacer, TRUE);
    add(row({text("MBES 穩定上限 ±"), c->limit, text("°　擺動速度上限"), c->rate_limit, text("°/s"), c->rate_info, spacer, c->apply}));
    c->engine.stabilisation_limit(gtk_spin_button_get_value(GTK_SPIN_BUTTON(c->limit)));
    g_signal_connect(c->enable, "toggled", G_CALLBACK(changed), c);
    g_signal_connect(c->mode, "notify::selected", G_CALLBACK(mode_changed), c);
    g_signal_connect(c->finish, "toggled", G_CALLBACK(finish_toggled), c);
    g_signal_connect(c->limit, "value-changed", G_CALLBACK(limit_changed), c);
    add(gtk_separator_new(GTK_ORIENTATION_HORIZONTAL));
    c->motion = add(gtk_label_new(""));
    c->diagnostics = add(gtk_label_new("")); gtk_label_set_xalign(GTK_LABEL(c->diagnostics), 0);
    auto* development = add(gtk_label_new(nullptr));
    gtk_label_set_markup(GTK_LABEL(development), "<small>開發版：實體 Roll 方向尚未驗證，現場請先用固定 Offset 確認</small>");
    gtk_label_set_xalign(GTK_LABEL(development), 0); gtk_widget_add_css_class(development, "dim-label");
    // Beam sketch on the left, trend on the right.
    auto* legend = gtk_label_new(nullptr);
    gtk_label_set_markup(GTK_LABEL(legend), "最近 10 秒：<span foreground=\"#1fa8e0\">■ Real</span>　"
        "<span foreground=\"#e89a00\">■ Offset</span>　<span foreground=\"#2db84a\">■ Output</span>");
    gtk_label_set_xalign(GTK_LABEL(legend), 0);
    auto* chart = c->chart.create(); gtk_widget_set_vexpand(chart, TRUE); gtk_widget_set_hexpand(chart, TRUE);
    auto* trend = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
    gtk_box_append(GTK_BOX(trend), legend); gtk_box_append(GTK_BOX(trend), chart);
    auto* bottom = add(row({c->beam.create(), trend})); gtk_widget_set_vexpand(bottom, TRUE);
    gtk_widget_set_hexpand(trend, TRUE);
    c->applied = settings(c);
    c->timer = g_timeout_add(100, refresh, c);
    refresh(c);
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
