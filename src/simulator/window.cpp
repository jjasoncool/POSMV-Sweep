#include <gtk/gtk.h>
#include "protocol/tss1.hpp"
#include "simulator/validation.hpp"
#include "simulator/fixed_frame_check.hpp"
#include "diagnostics/timing_stats.hpp"
#include <iomanip>
#include "motion/sweep.hpp"
#include "scheduler/output_scheduler.hpp"
#include "transport/serial_transport.hpp"
#include <thread>
#include <atomic>
#include "platform/environment_manager.hpp"
#include <filesystem>
#include "simulator/replay_source.hpp"
#include <mutex>
#include <fstream>
#include <array>
#include <span>
#include <sstream>
namespace {
struct State {
    GtkWidget *tx, *rx, *file, *baud, *rate, *loop, *tx_view, *rx_view, *strict, *environment, *proxy_ports, *off_check, *fixed_roll, *sine, *amplitude, *cycle;
    posmv::EnvironmentManager manager;
    std::filesystem::path state_dir;
    std::jthread sender, receiver;
    std::mutex mutex;
    std::string tx_status = "送出：已停止", rx_status = "接收：已停止";
    guint timer{};
    std::atomic<bool> rx_ready{false}, rx_failed{false};
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
void stop_tx(GtkButton*, gpointer p) { auto* s = static_cast<State*>(p); s->sender.request_stop(); if(s->sender.joinable()) s->sender.join(); s->message(true,"送出：已停止"); }
void stop_rx(GtkButton*, gpointer p) { auto* s = static_cast<State*>(p); s->pending_start=false; s->receiver.request_stop(); if(s->receiver.joinable()) s->receiver.join(); s->rx_ready=false; }
void start_tx(GtkButton*, gpointer p) {
    auto* s = static_cast<State*>(p);
    stop_tx(nullptr,p);
    const std::string port = gtk_editable_get_text(GTK_EDITABLE(s->tx));
    const std::string path = gtk_editable_get_text(GTK_EDITABLE(s->file));
    const unsigned baud = gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(s->baud));
    const unsigned rates[] = {25,50,100}; const unsigned rate = rates[gtk_drop_down_get_selected(GTK_DROP_DOWN(s->rate))];
    const bool loop = gtk_check_button_get_active(GTK_CHECK_BUTTON(s->loop));
    const double fixed=gtk_spin_button_get_value(GTK_SPIN_BUTTON(s->fixed_roll));
    const bool sine=gtk_check_button_get_active(GTK_CHECK_BUTTON(s->sine));
    posmv::SweepConfig sweep;
    sweep.amplitude_deg=gtk_spin_button_get_value(GTK_SPIN_BUTTON(s->amplitude));
    sweep.cycle_seconds=gtk_spin_button_get_value(GTK_SPIN_BUTTON(s->cycle));
    const bool strict = gtk_check_button_get_active(GTK_CHECK_BUTTON(s->strict));
    s->sender = std::jthread([s,port,path,baud,rate,loop,strict,fixed,sine,sweep](std::stop_token token) {
        try {
            posmv::validate_serial_throughput(baud,rate);
            std::vector<posmv::Tss1Frame> frames;
            std::string preflight;
            if(path.empty()) frames.push_back({":000000  0000G 0000  0000",0,0,0,'G'});
            else {
                std::ifstream file(std::filesystem::u8path(path),std::ios::binary);
                if(!file) throw std::runtime_error("無法讀取 Capture 檔案");
                auto source = posmv::read_replay(file, strict, token);
                frames = std::move(source.frames);
                preflight="預檢：有效 "+std::to_string(frames.size())+" 筆、異常 "+std::to_string(source.invalid)+"、垃圾 "+std::to_string(source.garbage)+" bytes\n";
                s->message(true,preflight);
            }
            if(frames.empty()) throw std::runtime_error("檔案沒有有效的 TSS1 Frame");
            posmv::SerialTransport transport(port,baud); posmv::OutputScheduler clock(rate);
            std::uint64_t count=0;
            do {
                for(const auto& frame:frames) {
                    if(token.stop_requested()) break;
                    clock.wait(); if(token.stop_requested()) break;
                    transport.write(frame.raw+"\r\n"); ++count;
                    s->message(true,preflight+"送出中：已送出 "+std::to_string(count)+" 筆\n最新："+frame.raw);
                }
            } while(loop && !token.stop_requested());
            s->message(true,preflight+"送出結束，共 "+std::to_string(count)+" 筆");
        } catch(const std::exception& e) { s->message(true,"送出錯誤："+std::string(e.what())); }
    });
}
void start_rx(GtkButton*, gpointer p) {
    auto* s = static_cast<State*>(p); stop_rx(nullptr,p);
    s->rx_ready=false; s->rx_failed=false;
    const std::string port = gtk_editable_get_text(GTK_EDITABLE(s->rx));
    const unsigned baud = gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(s->baud));
    const bool compare = gtk_check_button_get_active(GTK_CHECK_BUTTON(s->off_check));
    if(compare && (std::string(gtk_editable_get_text(GTK_EDITABLE(s->file))).size() ||
        gtk_check_button_get_active(GTK_CHECK_BUTTON(s->sine)) ||
        gtk_spin_button_get_value(GTK_SPIN_BUTTON(s->fixed_roll)) != 0)) {
        s->rx_failed=true;
        s->message(false,"OFF 自動比對只支援固定 0°：請清空 Capture 檔、關閉正弦並設定 Roll=0");
        return;
    }
    s->receiver = std::jthread([s,port,baud,compare](std::stop_token token) {
        try {
            posmv::SerialTransport transport(port,baud); s->rx_ready=true; posmv::StreamValidator validator;
            std::array<char,4096> bytes{}; const auto start=std::chrono::steady_clock::now();
            posmv::FixedFrameCheck off(std::string(":000000  0000G 0000  0000") + char(13) + char(10));
            std::string latest;
            posmv::TimingStats timing;
            while(!token.stop_requested()) {
                const auto n=transport.read(bytes); const std::string_view chunk(bytes.data(),n);
                const auto seconds=std::chrono::duration<double>(std::chrono::steady_clock::now()-start).count();
                const auto frames = validator.feed(chunk); if(compare) off.feed(chunk);
                for(const auto& frame:frames) {
                    timing.observe(seconds);
                    std::ostringstream motion; motion<<std::fixed<<std::setprecision(2)
                        <<"最新："<<frame.raw<<"\nRoll "<<frame.roll_deg<<"°　Pitch "<<frame.pitch_deg<<"°　Heave "<<frame.heave_m<<" m";
                    latest=motion.str();
                }
                const auto intervals=timing.intervals();
                std::ostringstream text; text<<std::fixed<<std::setprecision(1)
                    <<"接收中：有效 "<<validator.valid<<"　無效 "<<validator.invalid<<"　垃圾 "<<validator.garbage_bytes<<" bytes\n"
                    <<"速率：即時 "<<timing.current_rate(seconds)<<" Hz／平均 "<<timing.average_rate()<<" Hz　中斷 "<<timing.gaps()<<" 次\n"
                    <<"接收間隔（ms）：平均 "<<intervals.mean_ms<<"　最小 "<<intervals.min_ms<<"　最大 "<<intervals.max_ms<<"　p99 "<<intervals.p99_ms
                    <<"\n（一次讀到多筆時間隔記為 0）\n"<<latest<<(validator.last_error.empty()?"":"\n"+validator.last_error);
                s->message(false,text.str());
            }
            validator.finish(); off.finish();
            s->message(false,"接收結束：有效 "+std::to_string(validator.valid)+"　無效 "+std::to_string(validator.invalid)+
                (validator.last_error.empty()?"":"\n"+validator.last_error)+(compare ? (off.passed()?"\nOFF 比對：通過":"\nOFF 比對：未通過") : ""));
        } catch(const std::exception& e) { s->rx_failed=true; s->rx_ready=false; s->message(false,"接收錯誤："+std::string(e.what())); }
    });
}
void manage(GtkButton* button, gpointer p) {
    auto* s=static_cast<State*>(p);
    if(s->manager.active()) return;
    stop_tx(nullptr,p); stop_rx(nullptr,p); s->pending_start=false;
    const std::string action=static_cast<const char*>(g_object_get_data(G_OBJECT(button),"action"));
    try { s->manager.start(action,s->state_dir); }
    catch(const std::exception& e) { gtk_label_set_text(GTK_LABEL(s->environment),e.what()); return; }
    gtk_label_set_text(GTK_LABEL(s->environment),action=="inspect"||action=="test"?"處理中…":"處理中…請完成 Windows 權限提示（UAC）");
}
gboolean refresh(gpointer p) {
    auto* s=static_cast<State*>(p);
    if(auto completed=s->manager.poll()) {
        std::ifstream report(s->state_dir/"result.txt",std::ios::binary);
        std::string text, roles;
        for(std::string line; std::getline(report,line);) {
            if(line.starts_with("message=")) text+=(text.empty()?"":"\n")+line.substr(8);
            else if(line.starts_with("roles=")) roles=line.substr(6);
        }
        if(text.empty()) text="Virtual COM 管理操作沒有產生結果（結束代碼 "+std::to_string(*completed)+"）";
        gtk_label_set_text(GTK_LABEL(s->environment),text.c_str());
        std::istringstream fields(roles); std::string tx,input,output,rx;
        if(fields>>tx>>input>>output>>rx) {
            gtk_editable_set_text(GTK_EDITABLE(s->tx),tx.c_str()); gtk_editable_set_text(GTK_EDITABLE(s->rx),rx.c_str());
            const auto baud=std::to_string(gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(s->baud)));
            auto* markup=g_markup_printf_escaped("<big><b>正式程式：「輸入 COM」選 %s，「輸出 COM」選 %s（%s bps）</b></big>",
                input.c_str(),output.c_str(),baud.c_str());
            gtk_label_set_markup(GTK_LABEL(s->proxy_ports),markup); g_free(markup);
        } else gtk_label_set_text(GTK_LABEL(s->proxy_ports),"測試環境尚未就緒：完成後，這裡會顯示正式程式要選的兩個埠");
    }
    if(s->pending_start && s->rx_failed) { s->pending_start=false; s->message(true,"接收埠開啟失敗，未開始送出"); }
    else if(s->pending_start && s->rx_ready) { s->pending_start=false; start_tx(nullptr,p); }
    std::lock_guard lock(s->mutex);
    gtk_label_set_text(GTK_LABEL(s->tx_view),s->tx_status.c_str());
    gtk_label_set_text(GTK_LABEL(s->rx_view),s->rx_status.c_str());
    return G_SOURCE_CONTINUE;
}
void start_test(GtkButton*, gpointer p) { stop_tx(nullptr,p); start_rx(nullptr,p); static_cast<State*>(p)->pending_start=true; }
void stop_test(GtkButton*, gpointer p) { static_cast<State*>(p)->pending_start=false; stop_tx(nullptr,p); stop_rx(nullptr,p); }
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
void activate(GtkApplication* app,gpointer) {
    auto* window=gtk_application_window_new(app); gtk_window_set_title(GTK_WINDOW(window),"POSMV Simulator（測試控制台）"); gtk_window_set_default_size(GTK_WINDOW(window),800,600);
    auto* s=new State; s->state_dir=std::filesystem::path(g_get_user_data_dir())/"POSMV-Sweep"/"virtual-com"; auto* box=gtk_box_new(GTK_ORIENTATION_VERTICAL,12);
    gtk_widget_set_margin_start(box,20); gtk_widget_set_margin_end(box,20); gtk_widget_set_margin_top(box,20); gtk_widget_set_margin_bottom(box,20);
    auto entry=[&](const char* text) { auto* e=gtk_entry_new(); gtk_entry_set_placeholder_text(GTK_ENTRY(e),text); gtk_box_append(GTK_BOX(box),e); return e; };
    gtk_box_append(GTK_BOX(box),gtk_label_new("獨立串列測試工具：先開始接收，再連接正式程式，最後開始送出"));
    s->proxy_ports=gtk_label_new("測試環境尚未就緒：完成後，這裡會顯示正式程式要選的兩個埠");
    gtk_label_set_wrap(GTK_LABEL(s->proxy_ports),TRUE); gtk_box_append(GTK_BOX(box),s->proxy_ports);
    s->environment=status_label("正在檢查 Virtual COM 環境…");
    gtk_box_append(GTK_BOX(box),fixed_area(s->environment,110));
    const std::pair<const char*,const char*> driver_actions[]={{"inspect","檢查環境"},{"install","安裝 com0com 驅動（UAC）"},{"uninstall","解除安裝驅動（UAC）"}};
    const std::pair<const char*,const char*> pair_actions[]={{"create","建立兩組測試配對（UAC）"},{"test","配對雙向自測"},{"remove","移除本程式配對（UAC）"}};
    for(const auto& row:{std::span(driver_actions),std::span(pair_actions)}) {
        auto* management=gtk_box_new(GTK_ORIENTATION_HORIZONTAL,8); gtk_box_append(GTK_BOX(box),management);
        for(const auto& [action,title]:row) {
            auto* b=gtk_button_new_with_label(title); g_object_set_data(G_OBJECT(b),"action",const_cast<char*>(action));
            g_signal_connect(b,"clicked",G_CALLBACK(manage),s); gtk_box_append(GTK_BOX(management),b);
        }
    }
    s->tx=entry("TX COM：配對的來源端，例如 COM10（必須已存在）"); s->rx=entry("Validator RX COM：配對的接收端，例如 COM13");
    s->file=entry("Capture 檔案完整路徑；留空則送出下方的固定或正弦 Roll");
    s->baud=gtk_spin_button_new_with_range(1200,4000000,1200); gtk_spin_button_set_value(GTK_SPIN_BUTTON(s->baud),38400); gtk_box_append(GTK_BOX(box),s->baud);
    const char* rates[]={"25 Hz","50 Hz","100 Hz",nullptr}; s->rate=gtk_drop_down_new_from_strings(rates); gtk_box_append(GTK_BOX(box),s->rate);
    auto number=[&](const char* label,double low,double high,double step,double value) {
        gtk_box_append(GTK_BOX(box),gtk_label_new(label));
        auto* spin=gtk_spin_button_new_with_range(low,high,step); gtk_spin_button_set_value(GTK_SPIN_BUTTON(spin),value); gtk_box_append(GTK_BOX(box),spin); return spin;
    };
    s->fixed_roll=number("固定 Roll（°，未選 Capture 檔時使用）",-90,90,0.01,0);
    s->sine=gtk_check_button_new_with_label("改用正弦 Roll（未選 Capture 檔時）"); gtk_box_append(GTK_BOX(box),s->sine);
    s->amplitude=number("正弦幅度（±°）",0,90,0.1,10); s->cycle=number("正弦完整週期（秒）",0.1,3600,0.1,4);
    s->loop=gtk_check_button_new_with_label("循環播放直到停止"); gtk_check_button_set_active(GTK_CHECK_BUTTON(s->loop),TRUE); gtk_box_append(GTK_BOX(box),s->loop);
    s->strict=gtk_check_button_new_with_label("嚴格模式：異常或垃圾資料禁止發送"); gtk_box_append(GTK_BOX(box),s->strict);
    s->off_check=gtk_check_button_new_with_label("固定 0° Frame OFF 比對（不選 Capture 檔、正式程式 Sweep OFF）"); gtk_box_append(GTK_BOX(box),s->off_check);
    auto* buttons=gtk_box_new(GTK_ORIENTATION_HORIZONTAL,10); gtk_box_append(GTK_BOX(box),buttons);
    auto button=[&](const char* text,GCallback callback) { auto* b=gtk_button_new_with_label(text); gtk_box_append(GTK_BOX(buttons),b); g_signal_connect(b,"clicked",callback,s); };
    button("選擇 Capture 檔",G_CALLBACK(choose));
    button("開始測試",G_CALLBACK(start_test)); button("停止測試",G_CALLBACK(stop_test));
    button("開始送出",G_CALLBACK(start_tx)); button("停止送出",G_CALLBACK(stop_tx)); button("開始接收驗證",G_CALLBACK(start_rx)); button("停止接收",G_CALLBACK(stop_rx));
    auto* views=gtk_box_new(GTK_ORIENTATION_HORIZONTAL,24); gtk_box_set_homogeneous(GTK_BOX(views),TRUE);
    s->tx_view=status_label("送出：已停止"); s->rx_view=status_label("接收：已停止");
    gtk_box_append(GTK_BOX(views),s->tx_view); gtk_box_append(GTK_BOX(views),s->rx_view);
    gtk_box_append(GTK_BOX(box),fixed_area(views,170));
    s->timer=g_timeout_add(100,refresh,s);
    try { s->manager.start("inspect",s->state_dir); }
    catch(const std::exception& e) { gtk_label_set_text(GTK_LABEL(s->environment),e.what()); }
    g_object_set_data_full(G_OBJECT(window),"state",s,[](gpointer p){delete static_cast<State*>(p);});
    auto* scroll=gtk_scrolled_window_new(); gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scroll),box); gtk_window_set_child(GTK_WINDOW(window),scroll); gtk_window_present(GTK_WINDOW(window));
}
}
int launch_simulator_gui() {
    auto* app=gtk_application_new("org.posmvsweep.Simulator",G_APPLICATION_DEFAULT_FLAGS); g_signal_connect(app,"activate",G_CALLBACK(activate),nullptr);
    const int result=g_application_run(G_APPLICATION(app),0,nullptr); g_object_unref(app); return result;
}