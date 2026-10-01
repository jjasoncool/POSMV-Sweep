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
}