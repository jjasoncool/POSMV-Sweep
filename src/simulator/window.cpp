#include <gtk/gtk.h>
#include "protocol/tss1.hpp"
#include "simulator/validation.hpp"
#include "simulator/fixed_frame_check.hpp"
#include "diagnostics/timing_stats.hpp"
#include "motion/sweep.hpp"
#include "scheduler/output_scheduler.hpp"
#include "transport/serial_transport.hpp"
#include "platform/environment_manager.hpp"
#include "simulator/replay_source.hpp"
#include "simulator/test_signal.hpp"
#include "simulator/frame_pairing.hpp"
#include "gui/port_selector.hpp"
#include <algorithm>
#include <array>
#include <atomic>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <mutex>
#include <optional>
#include <set>
#include <sstream>
#include <thread>
#include <utility>
#include <vector>
namespace {
enum Source : guint { FixedRoll, SineRoll, Capture };
// What the last Virtual COM report said about this machine.
struct Environment {
    bool known{};                                     // a report has arrived
    std::optional<std::string> driver;                // com0com version when installed
    std::optional<std::array<std::string, 4>> roles;  // TX, Proxy Input, Proxy Output, RX
    std::string test, test_message;                   // self-test: passed, busy or failed
    std::string failure;                              // why the last action failed
    std::string actions;                              // actions that make sense now, space separated
};
struct State {
    // 測試 page
    GtkWidget *install, *create, *environment, *proxy_ports, *source, *source_fields, *fixed_roll, *amplitude, *cycle;
    GtkWidget *file, *strict, *loop, *rate, *baud, *off_check, *run, *tx_view, *rx_view;
    // 環境與進階 page
    GtkWidget *details, *tx, *rx, *tag_check;
    std::vector<GtkWidget*> environment_buttons, locked_while_running;
    // TX records every frame it sends; RX pairs what comes back (shared clock below).
    std::mutex pairing_mutex;
    posmv::FramePairing pairing;
    const std::chrono::steady_clock::time_point epoch = std::chrono::steady_clock::now();
    double since_epoch() const { return std::chrono::duration<double>(std::chrono::steady_clock::now() - epoch).count(); }
    posmv::EnvironmentManager manager;
    std::filesystem::path state_dir;
    std::string action;  // environment action in progress
    Environment env;
    std::string shown_ports;
    std::jthread sender, receiver;
    std::mutex mutex;
    std::string tx_status = "送出：已停止", rx_status = "接收：已停止";
    guint timer{};
    std::atomic<bool> tx_running{false}, rx_running{false}, rx_ready{false}, rx_failed{false};
    bool pending_start{};
    GtkNativeDialog* chooser{};
    ~State() {
        if (chooser) {
            g_signal_handlers_disconnect_by_data(chooser, this);
            gtk_native_dialog_destroy(chooser);
            g_object_unref(chooser);
        }
        sender.request_stop(); receiver.request_stop();
        if (sender.joinable()) sender.join();
        if (receiver.joinable()) receiver.join();
        if (timer) g_source_remove(timer);
    }
    void message(bool transmit, std::string text) { std::lock_guard lock(mutex); (transmit ? tx_status : rx_status) = std::move(text); }
};
// Clears a worker's running flag however its thread ends.
struct RunningFlag {
    std::atomic<bool>& flag;
    ~RunningFlag() { flag = false; }
};
std::optional<std::string> chosen_port(GtkWidget* dropdown) {
    try { return posmv::selected_port(dropdown); } catch (const std::exception&) { return std::nullopt; }
}
bool running(const State* s) { return s->pending_start || s->tx_running || s->rx_running; }
unsigned selected_rate(const State* s) {
    const auto index=std::min<std::size_t>(gtk_drop_down_get_selected(GTK_DROP_DOWN(s->rate)),std::size(posmv::supported_rates)-1);
    return posmv::supported_rates[index];
}
unsigned selected_baud(const State* s) {
    auto* item=gtk_drop_down_get_selected_item(GTK_DROP_DOWN(s->baud));
    return item ? static_cast<unsigned>(std::stoul(gtk_string_object_get_string(GTK_STRING_OBJECT(item)))) : posmv::minimum_baud(selected_rate(s));
}
// Foolproof baud: only standard bauds that carry the rate are offered, starting at the lowest.
void rate_changed(GObject*, GParamSpec*, gpointer p) {
    auto* s=static_cast<State*>(p);
    const unsigned lowest=posmv::minimum_baud(selected_rate(s));
    auto* model=gtk_string_list_new(nullptr);
    for(const unsigned baud:posmv::standard_bauds) if(baud>=lowest) gtk_string_list_append(model,std::to_string(baud).c_str());
    gtk_drop_down_set_model(GTK_DROP_DOWN(s->baud),G_LIST_MODEL(model));
    gtk_drop_down_set_selected(GTK_DROP_DOWN(s->baud),0);
    g_object_unref(model);
}
void stop_tx(GtkButton*, gpointer p) { auto* s = static_cast<State*>(p); s->sender.request_stop(); if(s->sender.joinable()) s->sender.join(); s->message(true,"送出：已停止"); }
void stop_rx(GtkButton*, gpointer p) { auto* s = static_cast<State*>(p); s->pending_start=false; s->receiver.request_stop(); if(s->receiver.joinable()) s->receiver.join(); s->rx_ready=false; }
void start_tx(GtkButton*, gpointer p) {
    auto* s = static_cast<State*>(p);
    stop_tx(nullptr,p);
    const auto port = chosen_port(s->tx);
    if(!port) { s->message(true,"送出：請先在「環境與進階」選擇 TX COM"); return; }
    const auto source = gtk_drop_down_get_selected(GTK_DROP_DOWN(s->source));
    const std::string path = source == Capture ? gtk_editable_get_text(GTK_EDITABLE(s->file)) : "";
    if(source == Capture && path.empty()) { s->message(true,"送出：請先選擇 Capture 檔"); return; }
    const unsigned baud = selected_baud(s), rate = selected_rate(s);
    // Generated Roll runs until Stop; a capture can also play once.
    const bool loop = source != Capture || gtk_check_button_get_active(GTK_CHECK_BUTTON(s->loop));
    posmv::TestSignal signal;
    signal.fixed_roll_deg=gtk_spin_button_get_value(GTK_SPIN_BUTTON(s->fixed_roll));
    signal.sine=source == SineRoll;
    signal.sine_roll.amplitude_deg=gtk_spin_button_get_value(GTK_SPIN_BUTTON(s->amplitude));
    signal.sine_roll.cycle_seconds=gtk_spin_button_get_value(GTK_SPIN_BUTTON(s->cycle));
    // The fixed-frame OFF comparison needs identical frames, so it never tags.
    signal.tag=gtk_check_button_get_active(GTK_CHECK_BUTTON(s->tag_check)) && !gtk_check_button_get_active(GTK_CHECK_BUTTON(s->off_check));
    const bool strict = source == Capture && gtk_check_button_get_active(GTK_CHECK_BUTTON(s->strict));
    s->tx_running = true;
    s->sender = std::jthread([s,port=*port,path,baud,rate,loop,strict,signal](std::stop_token token) {
        RunningFlag running{s->tx_running};
        try {
            posmv::validate_serial_throughput(baud,rate);
            std::vector<posmv::Tss1Frame> frames;
            std::string preflight;
            if(path.empty()) frames.push_back({":000000  0000G 0000  0000",0,0,0,'G'});
            else {
                std::ifstream file(std::filesystem::path(reinterpret_cast<const char8_t*>(path.c_str())),std::ios::binary);
                if(!file) throw std::runtime_error("無法讀取 Capture 檔案");
                auto source = posmv::read_replay(file, strict, token);
                frames = std::move(source.frames);
                preflight="預檢：有效 "+std::to_string(frames.size())+" 筆、異常 "+std::to_string(source.invalid)+"、垃圾 "+std::to_string(source.garbage)+" bytes\n";
                s->message(true,preflight);
            }
            if(frames.empty()) throw std::runtime_error("檔案沒有有效的 TSS1 Frame");
            posmv::SerialTransport transport(port,baud); posmv::OutputScheduler clock(rate);
            std::uint64_t count=0;
            const auto started=std::chrono::steady_clock::now();
            do {
                for(const auto& frame:frames) {
                    if(token.stop_requested()) break;
                    clock.wait(); if(token.stop_requested()) break;
                    std::string packet=frame.raw+"\r\n";
                    auto sent=frame;
                    if(path.empty()) {
                        packet=posmv::test_frame(signal,std::chrono::duration<double>(std::chrono::steady_clock::now()-started).count(),count);
                        sent=posmv::Tss1Parser().feed(packet).at(0);
                    }
                    {  // record before writing so a fast return can still pair
                        std::lock_guard lock(s->pairing_mutex); s->pairing.sent(sent,s->since_epoch());
                    }
                    transport.write(packet); ++count;
                    s->message(true,preflight+"送出中：已送出 "+std::to_string(count)+" 筆\n最新："+packet.substr(0,packet.size()-2));
                }
            } while(loop && !token.stop_requested());
            s->message(true,preflight+"送出結束，共 "+std::to_string(count)+" 筆");
        } catch(const std::exception& e) { s->message(true,"送出錯誤："+std::string(e.what())); }
    });
}
void start_rx(GtkButton*, gpointer p) {
    auto* s = static_cast<State*>(p); stop_rx(nullptr,p);
    s->rx_ready=false; s->rx_failed=false;
    const auto port = chosen_port(s->rx);
    if(!port) { s->rx_failed=true; s->message(false,"接收：請先在「環境與進階」選擇 RX COM"); return; }
    const unsigned baud = selected_baud(s);
    const bool compare = gtk_check_button_get_active(GTK_CHECK_BUTTON(s->off_check));
    if(compare && (gtk_drop_down_get_selected(GTK_DROP_DOWN(s->source)) != FixedRoll ||
        gtk_spin_button_get_value(GTK_SPIN_BUTTON(s->fixed_roll)) != 0)) {
        s->rx_failed=true;
        s->message(false,"OFF 比對只支援固定 Roll 0.00°：請把資料來源設為固定 Roll，Roll 填 0");
        return;
    }
    { std::lock_guard lock(s->pairing_mutex); s->pairing=posmv::FramePairing{}; }
    s->rx_running = true;
    s->receiver = std::jthread([s,port=*port,baud,compare](std::stop_token token) {
        RunningFlag running{s->rx_running};
        try {
            posmv::SerialTransport transport(port,baud); s->rx_ready=true; posmv::StreamValidator validator;
            double offset_now{};
            std::array<char,4096> bytes{}; const auto start=std::chrono::steady_clock::now();
            posmv::FixedFrameCheck off(std::string(":000000  0000G 0000  0000") + char(13) + char(10));
            std::string latest;
            posmv::TimingStats timing;
            while(!token.stop_requested()) {
                const auto n=transport.read(bytes); const std::string_view chunk(bytes.data(),n);
                const auto seconds=std::chrono::duration<double>(std::chrono::steady_clock::now()-start).count();
                const auto frames = validator.feed(chunk); if(compare) off.feed(chunk);
                const double received_at=s->since_epoch();
                for(const auto& frame:frames) {
                    timing.observe(seconds);
                    {
                        std::lock_guard lock(s->pairing_mutex);
                        if(const auto pair=s->pairing.received(frame,received_at)) offset_now=pair->offset_deg;
                    }
                    std::ostringstream motion; motion<<std::fixed<<std::setprecision(2)
                        <<"最新："<<frame.raw<<"\nRoll "<<frame.roll_deg<<"°　Pitch "<<frame.pitch_deg<<"°　Heave "<<frame.heave_m<<" m";
                    latest=motion.str();
                }
                const auto intervals=timing.intervals();
                std::ostringstream text; text<<std::fixed<<std::setprecision(1)
                    <<"接收中：有效 "<<validator.valid<<"　無效 "<<validator.invalid<<"　垃圾 "<<validator.garbage_bytes<<" bytes\n"
                    <<"速率：即時 "<<timing.current_rate(seconds)<<" Hz／平均 "<<timing.average_rate()<<" Hz　中斷 "<<timing.gaps()<<" 次\n"
                    <<"接收間隔（ms）：平均 "<<intervals.mean_ms<<"　最小 "<<intervals.min_ms<<"　最大 "<<intervals.max_ms<<"　p99 "<<intervals.p99_ms<<"\n";
                {
                    std::lock_guard lock(s->pairing_mutex);
                    const auto& pairing=s->pairing;
                    text<<"比對：對上 "<<pairing.paired()<<"　相同 "<<pairing.identical()<<"　遺漏 "<<pairing.missing()
                        <<"　對不上 "<<pairing.unmatched()<<"\n";
                    const auto delay=pairing.latency(); const auto shape=pairing.offset_estimate();
                    if(pairing.paired()) text<<"延遲（送出→收到，ms）：平均 "<<delay.mean_ms<<"　最大 "<<delay.max_ms<<"　p99 "<<delay.p99_ms<<"\n";
                    text<<std::setprecision(2);
                    // Below the 0.01° resolution a centre prints as 0.00, never -0.00.
                    const auto centre=shape && std::abs(shape->center_deg)>=0.005 ? shape->center_deg : 0.0;
                    if(shape && !shape->period_seconds && shape->amplitude_deg<0.05) text<<"量到的 Offset：固定 "<<centre<<"°\n";
                    else if(shape) {
                        text<<"量到的 Offset：目前 "<<offset_now<<"°　幅度 ±"<<shape->amplitude_deg<<"°　週期 ";
                        if(shape->period_seconds) text<<*shape->period_seconds<<" 秒"; else text<<"—";
                        text<<"　中心 "<<centre<<"°\n";
                    }
                }
                text<<latest<<(validator.last_error.empty()?"":"\n"+validator.last_error);
                s->message(false,text.str());
            }
            validator.finish(); off.finish();
            s->message(false,"接收結束：有效 "+std::to_string(validator.valid)+"　無效 "+std::to_string(validator.invalid)+
                (validator.last_error.empty()?"":"\n"+validator.last_error)+(compare ? (off.passed()?"\nOFF 比對：通過":"\nOFF 比對：未通過") : ""));
        } catch(const std::exception& e) { s->rx_failed=true; s->rx_ready=false; s->message(false,"接收錯誤："+std::string(e.what())); }
    });
}
void start_test(GtkButton*, gpointer p) {
    auto* s=static_cast<State*>(p);
    const auto tx=chosen_port(s->tx), rx=chosen_port(s->rx);
    if(tx && rx && *tx==*rx) { s->message(false,"TX 與 RX 不能是同一個 COM：請到「環境與進階」修改"); return; }
    stop_tx(nullptr,p); start_rx(nullptr,p); s->pending_start=true;
}
void stop_test(GtkButton*, gpointer p) { static_cast<State*>(p)->pending_start=false; stop_tx(nullptr,p); stop_rx(nullptr,p); }
// One button: RX opens first and TX starts once it is ready (see refresh).
void run_clicked(GtkButton*, gpointer p) { if(running(static_cast<State*>(p))) stop_test(nullptr,p); else start_test(nullptr,p); }

// --- Virtual COM environment ------------------------------------------------------------------
std::string driver_name(const std::string& version) { return version == "未知" ? "com0com（版本未知）" : "com0com " + version; }
std::string action_title(const std::string& action) {
    if(action=="inspect") return "檢查測試環境";
    if(action=="install") return "安裝 com0com 驅動";
    if(action=="create") return "建立虛擬 COM 埠";
    if(action=="test") return "雙向自測";
    if(action=="remove") return "移除本程式配對";
    if(action=="uninstall") return "解除安裝驅動";
    return "Virtual COM 管理";
}
// The main page shows the install button, this status, then the create button; each button
// appears only when it is the next step.
void show_environment(State* s) {
    const auto& e=s->env;
    const bool busy=s->manager.active();
    std::string text;
    if(busy) text = s->action=="inspect" ? "正在檢查測試環境…" : s->action=="test" ? "正在自測虛擬 COM 埠…" : "處理中…請完成 Windows 權限提示（UAC）";
    else if(!e.known) text = "尚未檢查測試環境";
    else if(!e.failure.empty()) text = "✘ "+e.failure;
    else if(!e.driver) text = "✘ 尚未安裝 com0com 驅動：請按「安裝 com0com 驅動」（需要系統管理員權限）";
    else if(!e.roles) text = driver_name(*e.driver)+" 已安裝，還沒有可用的虛擬 COM 埠：按「建立虛擬 COM 埠」會建立兩組相通的埠（從 COM10 起），讓 Simulator 和正式程式互相連線";
    else {
        text = driver_name(*e.driver)+" 已安裝，虛擬 COM 已就緒";
        if(e.test=="passed") text = "✔ "+text+"，自測通過";
        else if(e.test=="busy") text = "⚠ "+text+"；"+e.test_message;
        else if(e.test=="failed") text = "✘ "+text+"，但自測失敗："+e.test_message;
    }
    gtk_label_set_text(GTK_LABEL(s->environment),text.c_str());
    // Each button appears only while its action makes sense, so no step can be taken out of order.
    std::istringstream offered(e.actions); std::set<std::string> actions;
    for(std::string action; offered>>action;) actions.insert(action);
    for(auto* button:s->environment_buttons) {
        const std::string action=static_cast<const char*>(g_object_get_data(G_OBJECT(button),"action"));
        gtk_widget_set_visible(button, action=="inspect" || actions.contains(action));
        gtk_widget_set_sensitive(button,!busy);
    }
}
void run_action(State* s, const std::string& action) {
    if(s->manager.active()) return;
    // Pairs, the driver and the self-test itself all need the test ports closed.
    if(action!="inspect") stop_test(nullptr,s);
    try { s->manager.start(action,s->state_dir); }
    catch(const std::exception& e) { s->env.failure=e.what(); show_environment(s); return; }
    s->action=action;
    show_environment(s);
}
void action_clicked(GtkButton* button, gpointer p) {
    run_action(static_cast<State*>(p),static_cast<const char*>(g_object_get_data(G_OBJECT(button),"action")));
}
void select_port(GtkWidget* dropdown, const std::string& port) {
    posmv::refresh_ports(dropdown);
    auto* model=gtk_drop_down_get_model(GTK_DROP_DOWN(dropdown));
    for(guint i=0;i<g_list_model_get_n_items(model);++i) {
        auto* item=GTK_STRING_OBJECT(g_list_model_get_item(model,i));
        const bool match=port==gtk_string_object_get_string(item);
        g_object_unref(item);
        if(match) { gtk_drop_down_set_selected(GTK_DROP_DOWN(dropdown),i); return; }
    }
}
void environment_finished(State* s, unsigned long code) {
    const auto action=std::exchange(s->action,std::string{});
    bool reported=false, ok=false; std::string roles, test, actions; std::optional<std::string> driver; std::vector<std::string> messages;
    {  // closed before the next action, which deletes the file
        std::ifstream report(s->state_dir/"result.txt",std::ios::binary);
        reported=static_cast<bool>(report);
        for(std::string line; std::getline(report,line);) {
            if(!line.empty() && line.back()=='\r') line.pop_back();
            if(line=="status=ok") ok=true;
            else if(line.starts_with("message=")) messages.push_back(line.substr(8));
            else if(line.starts_with("roles=")) roles=line.substr(6);
            else if(line.starts_with("driver=")) driver=line.substr(7);
            else if(line.starts_with("test=")) test=line.substr(5);
            else if(line.starts_with("actions=")) actions=line.substr(8);
        }
    }
    if(!reported) {
        s->env.failure=action_title(action)+"沒有產生結果（結束代碼 "+std::to_string(code)+"）";
        gtk_label_set_text(GTK_LABEL(s->details),s->env.failure.c_str());
        show_environment(s);
        return;
    }
    std::string details=action_title(action)+(ok?"：完成":"：失敗");
    for(const auto& message:messages) details+="\n"+message;
    gtk_label_set_text(GTK_LABEL(s->details),details.c_str());
    auto& e=s->env;
    e.known=true; e.driver=driver; e.actions=actions;
    std::istringstream fields(roles); std::array<std::string,4> found;
    if(fields>>found[0]>>found[1]>>found[2]>>found[3]) e.roles=found; else e.roles.reset();
    const std::string last=messages.empty() ? "" : messages.back();
    e.failure = ok || action=="test" ? "" : action_title(action)+"失敗："+last;
    if(action=="test") { e.test=test.empty() ? (ok?"passed":"failed") : test; e.test_message=ok ? "" : last; }
    else e.test.clear();
    // Simulator TX and RX follow the environment, but never change under a running test.
    if(e.roles && !running(s)) { select_port(s->tx,(*e.roles)[0]); select_port(s->rx,(*e.roles)[3]); }
    show_environment(s);
    // Ready pairs get a self-test right away, unless a test is using them.
    if(ok && e.roles && (action=="inspect"||action=="install"||action=="create") && !running(s)) run_action(s,"test");
}
gboolean refresh(gpointer p) {
    auto* s=static_cast<State*>(p);
    if(const auto code=s->manager.poll()) environment_finished(s,*code);
    if(s->pending_start && s->rx_failed) { s->pending_start=false; s->message(true,"接收端沒有開始，未開始送出"); }
    else if(s->pending_start && s->rx_ready) { s->pending_start=false; start_tx(nullptr,p); }
    std::string ports="測試環境就緒後，這裡會顯示正式程式要選的兩個埠";
    if(s->env.roles) {
        const auto baud=std::to_string(selected_baud(s));
        auto* markup=g_markup_printf_escaped("<big><b>正式程式：「輸入 COM」選 %s，「輸出 COM」選 %s（%s bps）</b></big>",
            (*s->env.roles)[1].c_str(),(*s->env.roles)[2].c_str(),baud.c_str());
        ports=markup; g_free(markup);
    }
    if(ports!=s->shown_ports) { gtk_label_set_markup(GTK_LABEL(s->proxy_ports),ports.c_str()); s->shown_ports=ports; }
    const bool active=running(s);
    gtk_button_set_label(GTK_BUTTON(s->run),active?"停止測試":"開始測試");
    gtk_widget_set_sensitive(s->run,!s->manager.active());
    for(auto* widget:s->locked_while_running) gtk_widget_set_sensitive(widget,!active);
    std::lock_guard lock(s->mutex);
    gtk_label_set_text(GTK_LABEL(s->tx_view),s->tx_status.c_str());
    gtk_label_set_text(GTK_LABEL(s->rx_view),s->rx_status.c_str());
    return G_SOURCE_CONTINUE;
}
void selected(GtkNativeDialog* dialog, int response, gpointer p) {
    static_cast<State*>(p)->chooser=nullptr;
    if (response == GTK_RESPONSE_ACCEPT) {
        auto* file = gtk_file_chooser_get_file(GTK_FILE_CHOOSER(dialog));
        auto* path = g_file_get_path(file);
        if (path) gtk_editable_set_text(GTK_EDITABLE(static_cast<State*>(p)->file), path);
        g_free(path); g_object_unref(file);
    }
    g_object_unref(dialog);
}
void choose(GtkButton* button, gpointer p) {
    auto* state=static_cast<State*>(p);
    if(state->chooser) { gtk_native_dialog_show(state->chooser); return; }
    auto* dialog = gtk_file_chooser_native_new("選擇 TSS1 capture", GTK_WINDOW(gtk_widget_get_root(GTK_WIDGET(button))), GTK_FILE_CHOOSER_ACTION_OPEN, "選擇", "取消");
    g_signal_connect(dialog, "response", G_CALLBACK(selected), p);
    state->chooser=GTK_NATIVE_DIALOG(dialog);
    gtk_native_dialog_show(GTK_NATIVE_DIALOG(dialog));
}
// Each data source shows only its own fields.
void source_changed(GObject*, GParamSpec*, gpointer p) {
    auto* s=static_cast<State*>(p);
    const char* names[]={"fixed","sine","capture"};
    gtk_stack_set_visible_child_name(GTK_STACK(s->source_fields),names[std::min<guint>(gtk_drop_down_get_selected(GTK_DROP_DOWN(s->source)),2)]);
}
void rescan(GtkButton*, gpointer p) { auto* s=static_cast<State*>(p); posmv::refresh_ports(s->tx); posmv::refresh_ports(s->rx); }

// --- Layout -----------------------------------------------------------------------------------
// Status text lives in a fixed-height area so its changing length never shifts the page.
GtkWidget* fixed_area(GtkWidget* child,int height) {
    auto* scroll=gtk_scrolled_window_new();
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll),GTK_POLICY_NEVER,GTK_POLICY_AUTOMATIC);
    gtk_widget_set_size_request(scroll,-1,height);
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scroll),child);
    return scroll;
}
GtkWidget* status_label(const char* text) {
    auto* label=gtk_label_new(text); gtk_label_set_wrap(GTK_LABEL(label),TRUE);
    gtk_label_set_xalign(GTK_LABEL(label),0); gtk_label_set_yalign(GTK_LABEL(label),0); gtk_widget_set_hexpand(label,TRUE);
    return label;
}
GtkWidget* row(std::initializer_list<GtkWidget*> widgets) {
    auto* line=gtk_box_new(GTK_ORIENTATION_HORIZONTAL,8);
    for(auto* widget:widgets) gtk_box_append(GTK_BOX(line),widget);
    return line;
}
GtkWidget* number(double low,double high,double step,double value,int width) {
    auto* spin=gtk_spin_button_new_with_range(low,high,step);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(spin),value);
    gtk_editable_set_width_chars(GTK_EDITABLE(spin),width);
    return spin;
}
GtkWidget* text(const char* label) { return gtk_label_new(label); }
GtkWidget* heading(const char* title) {
    auto* label=gtk_label_new(nullptr);
    auto* markup=g_markup_printf_escaped("<b>%s</b>",title); gtk_label_set_markup(GTK_LABEL(label),markup); g_free(markup);
    gtk_label_set_xalign(GTK_LABEL(label),0);
    return label;
}
GtkWidget* page() {
    auto* box=gtk_box_new(GTK_ORIENTATION_VERTICAL,10);
    gtk_widget_set_margin_start(box,16); gtk_widget_set_margin_end(box,16); gtk_widget_set_margin_top(box,12); gtk_widget_set_margin_bottom(box,16);
    return box;
}
GtkWidget* scrolled(GtkWidget* content) {
    auto* scroll=gtk_scrolled_window_new(); gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scroll),content); return scroll;
}
GtkWidget* environment_button(State* s,const char* action,const char* title) {
    auto* b=gtk_button_new_with_label(title); g_object_set_data(G_OBJECT(b),"action",const_cast<char*>(action));
    g_signal_connect(b,"clicked",G_CALLBACK(action_clicked),s); s->environment_buttons.push_back(b);
    return b;
}
GtkWidget* test_page(State* s) {
    auto* box=page();
    s->install=environment_button(s,"install","安裝 com0com 驅動（UAC）");
    s->create=environment_button(s,"create","建立虛擬 COM 埠（UAC）");
    s->environment=status_label("尚未檢查測試環境"); gtk_label_set_yalign(GTK_LABEL(s->environment),0.5);
    auto* environment=row({text("測試環境"),s->install,s->environment,s->create});
    gtk_widget_set_size_request(environment,-1,44); gtk_box_append(GTK_BOX(box),environment);
    s->proxy_ports=gtk_label_new(""); gtk_label_set_xalign(GTK_LABEL(s->proxy_ports),0); gtk_box_append(GTK_BOX(box),s->proxy_ports);
    gtk_box_append(GTK_BOX(box),gtk_separator_new(GTK_ORIENTATION_HORIZONTAL));
    const char* sources[]={"固定 Roll","正弦 Roll","Capture 檔",nullptr};
    s->source=gtk_drop_down_new_from_strings(sources);
    gtk_widget_set_size_request(s->source,130,-1);  // the same width for every source, so the fields never move
    s->fixed_roll=number(-90,90,0.01,0,7);
    // A plausible vessel roll, so a tagged run shows the Offset is measured independently of it.
    s->amplitude=number(0,90,0.1,5,5); s->cycle=number(0.1,3600,0.1,7,6);
    s->file=gtk_entry_new(); gtk_entry_set_placeholder_text(GTK_ENTRY(s->file),"Capture 檔完整路徑"); gtk_widget_set_hexpand(s->file,TRUE);
    auto* choose_button=gtk_button_new_with_label("選擇…"); g_signal_connect(choose_button,"clicked",G_CALLBACK(choose),s);
    s->strict=gtk_check_button_new_with_label("嚴格模式"); gtk_widget_set_tooltip_text(s->strict,"預檢發現異常或垃圾資料時就不送出");
    s->loop=gtk_check_button_new_with_label("循環播放"); gtk_check_button_set_active(GTK_CHECK_BUTTON(s->loop),TRUE);
    gtk_widget_set_tooltip_text(s->loop,"播完從頭再送，直到停止；不勾選則只播一次");
    s->source_fields=gtk_stack_new(); gtk_widget_set_hexpand(s->source_fields,TRUE);
    gtk_stack_add_named(GTK_STACK(s->source_fields),row({text("Roll"),s->fixed_roll,text("°")}),"fixed");
    gtk_stack_add_named(GTK_STACK(s->source_fields),row({text("幅度 ±"),s->amplitude,text("°　週期"),s->cycle,text("秒")}),"sine");
    gtk_stack_add_named(GTK_STACK(s->source_fields),row({s->file,choose_button,s->strict,s->loop}),"capture");
    g_signal_connect(s->source,"notify::selected",G_CALLBACK(source_changed),s);
    gtk_box_append(GTK_BOX(box),row({text("資料來源"),s->source,s->source_fields}));
    const char* rates[]={"25 Hz","50 Hz","100 Hz","200 Hz",nullptr}; s->rate=gtk_drop_down_new_from_strings(rates);
    s->baud=gtk_drop_down_new(nullptr,nullptr);
    gtk_widget_set_tooltip_text(s->baud,"依速率自動帶入最低可用的標準鮑率（每筆 27 bytes × 10 bits × 速率）；可以選更高，但不會低於這個值");
    rate_changed(nullptr,nullptr,s);
    g_signal_connect(s->rate,"notify::selected",G_CALLBACK(rate_changed),s);
    s->off_check=gtk_check_button_new_with_label("OFF 比對（固定 0°）");
    gtk_widget_set_tooltip_text(s->off_check,"正式程式 Sweep 關閉時，逐 byte 比對收到的 Frame；資料來源要設為固定 Roll 0°");
    s->run=gtk_button_new_with_label("開始測試"); gtk_widget_add_css_class(s->run,"suggested-action");
    g_signal_connect(s->run,"clicked",G_CALLBACK(run_clicked),s);
    auto* spacer=gtk_label_new(""); gtk_widget_set_hexpand(spacer,TRUE);
    gtk_box_append(GTK_BOX(box),row({text("速率"),s->rate,text("鮑率"),s->baud,s->off_check,spacer,s->run}));
    s->locked_while_running={s->source,s->source_fields,s->rate,s->baud,s->off_check};
    gtk_box_append(GTK_BOX(box),gtk_separator_new(GTK_ORIENTATION_HORIZONTAL));
    auto* views=gtk_box_new(GTK_ORIENTATION_HORIZONTAL,24); gtk_box_set_homogeneous(GTK_BOX(views),TRUE);
    s->tx_view=status_label("送出：已停止"); s->rx_view=status_label("接收：已停止");
    gtk_widget_set_tooltip_text(s->rx_view,"比對：以 Roll 以外的內容，把收到的 Frame 對回送出的那一筆。相同：與送出的逐 byte 相同，正式程式 Sweep 關閉時應等於對上的筆數；"
        "遺漏：送出後沒有收到；對不上：收到的 Frame 不是本程式送出的。一次讀到多筆 Frame 時，間隔記為 0");
    gtk_box_append(GTK_BOX(views),s->tx_view); gtk_box_append(GTK_BOX(views),s->rx_view);
    gtk_box_append(GTK_BOX(box),fixed_area(views,200));
    return box;
}
GtkWidget* advanced_page(State* s) {
    auto* box=page();
    gtk_box_append(GTK_BOX(box),heading("虛擬 COM 環境"));
    gtk_box_append(GTK_BOX(box),row({environment_button(s,"inspect","重新檢查"),environment_button(s,"test","雙向自測"),
        environment_button(s,"create","建立虛擬 COM 埠（UAC）"),environment_button(s,"remove","移除本程式配對（UAC）")}));
    gtk_box_append(GTK_BOX(box),row({environment_button(s,"install","安裝 com0com 驅動（UAC）"),environment_button(s,"uninstall","解除安裝驅動（UAC）")}));
    s->details=status_label("尚未檢查測試環境"); gtk_box_append(GTK_BOX(box),fixed_area(s->details,150));
    gtk_box_append(GTK_BOX(box),gtk_separator_new(GTK_ORIENTATION_HORIZONTAL));
    gtk_box_append(GTK_BOX(box),heading("Simulator 使用的埠（依測試環境自動選好）"));
    s->tx=gtk_drop_down_new(nullptr,nullptr); posmv::refresh_ports(s->tx);
    s->rx=gtk_drop_down_new(nullptr,nullptr); posmv::refresh_ports(s->rx); posmv::avoid_port_of(s->rx,s->tx);
    auto* rescan_button=gtk_button_new_with_label("重新掃描 COM"); g_signal_connect(rescan_button,"clicked",G_CALLBACK(rescan),s);
    gtk_box_append(GTK_BOX(box),row({text("TX（送往正式程式）"),s->tx,text("RX（接收正式程式輸出）"),s->rx,rescan_button}));
    gtk_box_append(GTK_BOX(box),gtk_separator_new(GTK_ORIENTATION_HORIZONTAL));
    gtk_box_append(GTK_BOX(box),heading("單獨控制"));
    auto* controls=gtk_box_new(GTK_ORIENTATION_HORIZONTAL,8);
    const std::pair<const char*,GCallback> buttons[]={{"開始送出",G_CALLBACK(start_tx)},{"停止送出",G_CALLBACK(stop_tx)},
        {"開始接收驗證",G_CALLBACK(start_rx)},{"停止接收",G_CALLBACK(stop_rx)}};
    for(const auto& [title,callback]:buttons) {
        auto* b=gtk_button_new_with_label(title); g_signal_connect(b,"clicked",callback,s); gtk_box_append(GTK_BOX(controls),b);
    }
    gtk_box_append(GTK_BOX(box),controls);
    s->tag_check=gtk_check_button_new_with_label("固定／正弦 Roll 在 Heave 寫入序號，讓每筆 Frame 都不同，遺漏筆數更準（OFF 比對時自動關閉）");
    gtk_check_button_set_active(GTK_CHECK_BUTTON(s->tag_check),TRUE); gtk_box_append(GTK_BOX(box),s->tag_check);
    for(auto* widget:{s->tx,s->rx,s->tag_check}) s->locked_while_running.push_back(widget);
    return box;
}
void activate(GtkApplication* app,gpointer) {
    auto* window=gtk_application_window_new(app); gtk_window_set_title(GTK_WINDOW(window),"POSMV Simulator（測試控制台）"); gtk_window_set_default_size(GTK_WINDOW(window),960,560);
    auto* s=new State;  // GLib paths are UTF-8, which std::filesystem would read as the ANSI code page
    s->state_dir=std::filesystem::path(reinterpret_cast<const char8_t*>(g_get_user_data_dir()))/"POSMV-Sweep"/"virtual-com";
    auto* notebook=gtk_notebook_new();
    gtk_notebook_append_page(GTK_NOTEBOOK(notebook),scrolled(test_page(s)),gtk_label_new("測試"));
    gtk_notebook_append_page(GTK_NOTEBOOK(notebook),scrolled(advanced_page(s)),gtk_label_new("環境與進階"));
    s->timer=g_timeout_add(100,refresh,s);
    g_object_set_data_full(G_OBJECT(window),"state",s,[](gpointer p){delete static_cast<State*>(p);});
    gtk_window_set_child(GTK_WINDOW(window),notebook);
    show_environment(s);
    run_action(s,"inspect");
    gtk_window_present(GTK_WINDOW(window));
}
}
int launch_simulator_gui() {
    auto* app=gtk_application_new("org.posmvsweep.Simulator",G_APPLICATION_DEFAULT_FLAGS); g_signal_connect(app,"activate",G_CALLBACK(activate),nullptr);
    const int result=g_application_run(G_APPLICATION(app),0,nullptr); g_object_unref(app); return result;
}
