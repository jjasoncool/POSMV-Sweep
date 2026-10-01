#include <gtk/gtk.h>
namespace {
void activate(GtkApplication* application, gpointer) {
    auto* window = gtk_application_window_new(application);
    gtk_window_set_title(GTK_WINDOW(window), "POSMV-Sweep");
    gtk_window_set_default_size(GTK_WINDOW(window), 560, 220);
    auto* box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
    gtk_widget_set_margin_start(box, 24);
    gtk_widget_set_margin_end(box, 24);
    gtk_widget_set_margin_top(box, 24);
    gtk_widget_set_margin_bottom(box, 24);
    gtk_box_append(GTK_BOX(box), gtk_label_new("POS MV TSS1 Motion Sweep Proxy"));
    gtk_box_append(GTK_BOX(box), gtk_label_new("Offline development build — not connected to hardware"));
    gtk_box_append(GTK_BOX(box), gtk_label_new("Replay is currently available through --replay / --output CLI options."));
    gtk_box_append(GTK_BOX(box), gtk_label_new("Operator controls and live diagnostics are not implemented yet."));
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