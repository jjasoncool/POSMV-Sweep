#pragma once
#include <gtk/gtk.h>
#include "transport/port_list.hpp"
#include <stdexcept>
namespace posmv {
inline std::string selected_port(GtkWidget* widget) {
    auto* item = gtk_drop_down_get_selected_item(GTK_DROP_DOWN(widget));
    if (!item) throw std::runtime_error("請選擇存在的 COM；建立配對後按重新掃描");
    return gtk_string_object_get_string(GTK_STRING_OBJECT(item));
}
inline void refresh_ports(GtkWidget* widget) {
    std::string previous;
    try { previous = selected_port(widget); } catch (...) {}
    auto ports = available_ports();
    auto* model = gtk_string_list_new(nullptr);
    guint selected = GTK_INVALID_LIST_POSITION;
    for (guint i = 0; i < ports.size(); ++i) {
        gtk_string_list_append(model, ports[i].c_str());
        if (ports[i] == previous) selected = i;
    }
    gtk_drop_down_set_model(GTK_DROP_DOWN(widget), G_LIST_MODEL(model));
    gtk_drop_down_set_selected(GTK_DROP_DOWN(widget), selected);
    g_object_unref(model);
}
// Moves `widget` off the port `other` shows, so a new form never offers the same port twice.
inline void avoid_port_of(GtkWidget* widget, GtkWidget* other) {
    auto* model = gtk_drop_down_get_model(GTK_DROP_DOWN(widget));
    std::string taken;
    try { taken = selected_port(other); if (selected_port(widget) != taken) return; } catch (...) { return; }
    for (guint i = 0; model && i < g_list_model_get_n_items(model); ++i) {
        auto* item = GTK_STRING_OBJECT(g_list_model_get_item(model, i));
        const bool free = taken != gtk_string_object_get_string(item);
        g_object_unref(item);
        if (free) { gtk_drop_down_set_selected(GTK_DROP_DOWN(widget), i); return; }
    }
}
// Why the two ports cannot be connected, or nothing when they can.
inline std::string port_conflict(GtkWidget* input, GtkWidget* output) {
    try {
        if (selected_port(input) == selected_port(output)) return "輸入與輸出不能是同一個 COM";
    } catch (...) { return "沒有可用的 COM：接上裝置後按「重新掃描 COM」"; }
    return {};
}
}