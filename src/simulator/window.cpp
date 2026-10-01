#include <gtk/gtk.h>
#include "protocol/tss1.hpp"
#include "simulator/validation.hpp"
#include "scheduler/output_scheduler.hpp"
#include "transport/serial_transport.hpp"
#include <thread>
#include <atomic>
#include <windows.h>
#include <shellapi.h>
#include <filesystem>
#include "simulator/replay_source.hpp"
#include <mutex>
#include <fstream>
#include <array>
#include <sstream>
namespace {
struct State {
    GtkWidget *tx, *rx, *file, *baud, *rate, *loop, *status, *strict, *environment;
    HANDLE manager{};
    std::filesystem::path state_dir;
    std::jthread sender, receiver;
    std::mutex mutex;
    std::string tx_status = "TX 停止", rx_status = "RX 停止";
    guint timer{};
    std::atomic<bool> rx_ready{false}, rx_failed{false};
    bool pending_start{};
    ~State() {
        sender.request_stop(); receiver.request_stop();
        if (sender.joinable()) sender.join();
        if (receiver.joinable()) receiver.join();
        if (timer) g_source_remove(timer);
        if (manager) CloseHandle(manager);
    }
    void message(bool transmit, std::string text) { std::lock_guard lock(mutex); (transmit ? tx_status : rx_status) = std::move(text); }
};
void stop_tx(GtkButton*, gpointer p) { auto* s = static_cast<State*>(p); s->sender.request_stop(); if(s->sender.joinable()) s->sender.join(); s->message(true,"TX 停止"); }
void stop_rx(GtkButton*, gpointer p) { auto* s = static_cast<State*>(p); s->receiver.request_stop(); if(s->receiver.joinable()) s->receiver.join(); s->message(false,"RX 停止"); }
void start_tx(GtkButton*, gpointer p) {
    auto* s = static_cast<State*>(p);
    stop_tx(nullptr,p);
    const std::string port = gtk_editable_get_text(GTK_EDITABLE(s->tx));
    const std::string path = gtk_editable_get_text(GTK_EDITABLE(s->file));
    const unsigned baud = gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(s->baud));
    const unsigned rates[] = {25,50,100}; const unsigned rate = rates[gtk_drop_down_get_selected(GTK_DROP_DOWN(s->rate))];
    const bool loop = gtk_check_button_get_active(GTK_CHECK_BUTTON(s->loop));
    const bool strict = gtk_check_button_get_active(GTK_CHECK_BUTTON(s->strict));
    s->sender = std::jthread([s,port,path,baud,rate,loop,strict](std::stop_token token) {
        try {
            posmv::validate_serial_throughput(baud,rate);
            std::vector<posmv::Tss1Frame> frames; posmv::Tss1Parser parser;
            if(path.empty()) frames.push_back({":000000  0000G 0000  0000",0,0,0,'G'});
            else {
                std::ifstream file(std::filesystem::u8path(path),std::ios::binary);
                if(!file) throw std::runtime_error("無法讀取 replay 檔案");
                auto source = posmv::read_replay(file, strict, token);
                frames = std::move(source.frames);
                s->message(true,"預檢有效="+std::to_string(frames.size())+" 異常="+std::to_string(source.invalid)+" 垃圾="+std::to_string(source.garbage));
            }
            if(frames.empty()) throw std::runtime_error("檔案沒有有效 TSS1");
            posmv::SerialTransport transport(port,baud); posmv::OutputScheduler clock(rate);
            std::uint64_t count=0;
            do {
                for(const auto& frame:frames) {
                    if(token.stop_requested()) break;
                    clock.wait(); if(token.stop_requested()) break;
                    transport.write(frame.raw+"\r\n"); ++count;
                    s->message(true,"TX="+std::to_string(count)+" 略過異常="+std::to_string(parser.invalid_frames())+"\n"+frame.raw);
                }
            } while(loop && !token.stop_requested());
            s->message(true,"TX 結束，筆數="+std::to_string(count));
        } catch(const std::exception& e) { s->message(true,"TX Error: "+std::string(e.what())); }
    });
}
void start_rx(GtkButton*, gpointer p) {
    auto* s = static_cast<State*>(p); stop_rx(nullptr,p);
    s->rx_ready=false; s->rx_failed=false;
    const std::string port = gtk_editable_get_text(GTK_EDITABLE(s->rx));
    const unsigned baud = gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(s->baud));
    s->receiver = std::jthread([s,port,baud](std::stop_token token) {
        try {
            posmv::SerialTransport transport(port,baud); s->rx_ready=true; posmv::StreamValidator validator; posmv::Tss1Parser parser;
            std::array<char,4096> bytes{}; const auto start=std::chrono::steady_clock::now();
            std::string latest;
            while(!token.stop_requested()) {
                const auto n=transport.read(bytes); const std::string_view chunk(bytes.data(),n);
                validator.feed(chunk);
                for(const auto& frame:parser.feed(chunk)) latest=frame.raw+"\nRoll="+std::to_string(frame.roll_deg)+" Pitch="+std::to_string(frame.pitch_deg)+" Heave="+std::to_string(frame.heave_m);
                const auto seconds=std::chrono::duration<double>(std::chrono::steady_clock::now()-start).count();
                s->message(false,"RX valid="+std::to_string(validator.valid)+" invalid="+std::to_string(validator.invalid)+" garbage="+std::to_string(validator.garbage_bytes)+" Hz="+std::to_string(seconds>0?validator.valid/seconds:0)+"\n"+latest+"\n"+validator.last_error);
            }
            validator.finish();
            s->message(false,"RX 結束 valid="+std::to_string(validator.valid)+" invalid="+std::to_string(validator.invalid)+"\n"+validator.last_error);
        } catch(const std::exception& e) { s->rx_failed=true; s->rx_ready=false; s->message(false,"RX Error: "+std::string(e.what())); }
    });
}
void manage(GtkButton* button, gpointer p) {
    auto* s=static_cast<State*>(p);
    if(s->manager) return;
    stop_tx(nullptr,p); stop_rx(nullptr,p); s->pending_start=false;
    wchar_t exe[32768]; GetModuleFileNameW(nullptr,exe,32768);
    auto script=std::filesystem::path(exe).parent_path()/"Manage-VirtualCom.ps1";
    const auto* action=static_cast<const char*>(g_object_get_data(G_OBJECT(button),"action"));
    std::wstring verb;
    for(const char* c=action; *c; ++c) verb+=wchar_t(*c);
    std::wstring args=L"-NoProfile -File \""+script.wstring()+L"\" -Action "+verb+L" -StateDirectory \""+s->state_dir.wstring()+L"\"";
    SHELLEXECUTEINFOW info{}; info.cbSize=sizeof(info); info.fMask=SEE_MASK_NOCLOSEPROCESS;
    info.lpVerb=verb==L"Inspect"?L"open":L"runas";
    info.lpFile=L"powershell.exe"; info.lpParameters=args.c_str(); info.nShow=SW_HIDE;
    if(!ShellExecuteExW(&info)) { gtk_label_set_text(GTK_LABEL(s->environment),"管理操作未啟動／權限要求被取消"); return; }
    s->manager=info.hProcess;
    gtk_label_set_text(GTK_LABEL(s->environment),"正在檢查／管理配對，請完成 Windows 權限提示…");
}
gboolean refresh(gpointer p) {
    auto* s=static_cast<State*>(p);
    if(s->manager && WaitForSingleObject(s->manager,0)==WAIT_OBJECT_0) {
        DWORD code=1; GetExitCodeProcess(s->manager,&code); CloseHandle(s->manager); s->manager=nullptr;
        std::ifstream report(s->state_dir/"result.json",std::ios::binary);
        std::string text((std::istreambuf_iterator<char>(report)),{});
        gtk_label_set_text(GTK_LABEL(s->environment),text.empty()?"管理操作失敗，請確認驅動及 PowerShell 執行政策":text.c_str());
        if(code==0) {
            std::ifstream map(s->state_dir/"ports.txt"); std::string tx,input,output,rx;
            if(map>>tx>>input>>output>>rx) {
                gtk_editable_set_text(GTK_EDITABLE(s->tx),tx.c_str()); gtk_editable_set_text(GTK_EDITABLE(s->rx),rx.c_str());
                std::string hint="Sweep Input: "+input+" / Output: "+output+" (pair listed; connectivity not tested)";
                gtk_label_set_text(GTK_LABEL(s->environment),hint.c_str());
            }
        }
    }
    if(s->pending_start && s->rx_failed) { s->pending_start=false; s->message(true,"RX 開啟失敗，未啟動 TX"); }
    else if(s->pending_start && s->rx_ready) { s->pending_start=false; start_tx(nullptr,p); }
    std::lock_guard lock(s->mutex);
    gtk_label_set_text(GTK_LABEL(s->status),(s->tx_status+"\n\n"+s->rx_status).c_str()); return G_SOURCE_CONTINUE;
}
void start_test(GtkButton*, gpointer p) { stop_tx(nullptr,p); start_rx(nullptr,p); static_cast<State*>(p)->pending_start=true; }
void stop_test(GtkButton*, gpointer p) { static_cast<State*>(p)->pending_start=false; stop_tx(nullptr,p); stop_rx(nullptr,p); }
void selected(GtkNativeDialog* dialog, int response, gpointer p) {
    if (response == GTK_RESPONSE_ACCEPT) {
        auto* file = gtk_file_chooser_get_file(GTK_FILE_CHOOSER(dialog));
        auto* path = g_file_get_path(file);
        if (path) gtk_editable_set_text(GTK_EDITABLE(static_cast<State*>(p)->file), path);
        g_free(path); g_object_unref(file);
    }
    g_object_unref(dialog);
}
void choose(GtkButton* button, gpointer p) {
    auto* dialog = gtk_file_chooser_native_new("選擇 TSS1 capture", GTK_WINDOW(gtk_widget_get_root(GTK_WIDGET(button))), GTK_FILE_CHOOSER_ACTION_OPEN, "選擇", "取消");
    g_signal_connect(dialog, "response", G_CALLBACK(selected), p);
    gtk_native_dialog_show(GTK_NATIVE_DIALOG(dialog));
}
void activate(GtkApplication* app,gpointer) {
    auto* window=gtk_application_window_new(app); gtk_window_set_title(GTK_WINDOW(window),"POSMV Simulator — TX / Validator"); gtk_window_set_default_size(GTK_WINDOW(window),800,600);
    auto* s=new State; s->state_dir=std::filesystem::path(g_get_user_data_dir())/"POSMV-Sweep"/"virtual-com"; auto* box=gtk_box_new(GTK_ORIENTATION_VERTICAL,12);
    gtk_widget_set_margin_start(box,20); gtk_widget_set_margin_end(box,20); gtk_widget_set_margin_top(box,20); gtk_widget_set_margin_bottom(box,20);
    auto entry=[&](const char* text) { auto* e=gtk_entry_new(); gtk_entry_set_placeholder_text(GTK_ENTRY(e),text); gtk_box_append(GTK_BOX(box),e); return e; };
    gtk_box_append(GTK_BOX(box),gtk_label_new("獨立串列測試工具：先啟動 RX，再連接正式程式，最後啟動 TX"));
    s->environment=gtk_label_new("Virtual COM：尚未檢查；建立需要已安裝 com0com。取消 UAC 不會建立配對。");
    gtk_label_set_wrap(GTK_LABEL(s->environment),TRUE); gtk_box_append(GTK_BOX(box),s->environment);
    auto* management=gtk_box_new(GTK_ORIENTATION_HORIZONTAL,8); gtk_box_append(GTK_BOX(box),management);
    for(auto action:{"Inspect","Create","Remove"}) {
        const char* title=std::string(action)=="Inspect"?"檢查環境":std::string(action)=="Create"?"建立兩組測試配對（UAC）":"移除本程式配對（UAC）";
        auto* b=gtk_button_new_with_label(title); g_object_set_data(G_OBJECT(b),"action",const_cast<char*>(action));
        g_signal_connect(b,"clicked",G_CALLBACK(manage),s); gtk_box_append(GTK_BOX(management),b);
    }
    s->tx=entry("TX COM：配對的來源端，例如 COM10（必須已存在）"); s->rx=entry("Validator RX COM：配對的接收端，例如 COM13");
    s->file=entry("Replay 檔案完整路徑；留空產生固定 0° Roll");
    s->baud=gtk_spin_button_new_with_range(1200,4000000,1200); gtk_spin_button_set_value(GTK_SPIN_BUTTON(s->baud),38400); gtk_box_append(GTK_BOX(box),s->baud);
    const char* rates[]={"25 Hz","50 Hz","100 Hz",nullptr}; s->rate=gtk_drop_down_new_from_strings(rates); gtk_box_append(GTK_BOX(box),s->rate);
    s->loop=gtk_check_button_new_with_label("循環播放直到 Stop"); gtk_check_button_set_active(GTK_CHECK_BUTTON(s->loop),TRUE); gtk_box_append(GTK_BOX(box),s->loop);
    s->strict=gtk_check_button_new_with_label("嚴格模式：異常或垃圾資料禁止發送"); gtk_box_append(GTK_BOX(box),s->strict);
    auto* buttons=gtk_box_new(GTK_ORIENTATION_HORIZONTAL,10); gtk_box_append(GTK_BOX(box),buttons);
    auto button=[&](const char* text,GCallback callback) { auto* b=gtk_button_new_with_label(text); gtk_box_append(GTK_BOX(buttons),b); g_signal_connect(b,"clicked",callback,s); };
    button("選擇文字檔",G_CALLBACK(choose));
    button("開始測試",G_CALLBACK(start_test)); button("停止測試",G_CALLBACK(stop_test));
    button("Start TX",G_CALLBACK(start_tx)); button("Stop TX",G_CALLBACK(stop_tx)); button("Start RX / Validator",G_CALLBACK(start_rx)); button("Stop RX",G_CALLBACK(stop_rx));
    s->status=gtk_label_new(""); gtk_label_set_wrap(GTK_LABEL(s->status),TRUE); gtk_box_append(GTK_BOX(box),s->status);
    gtk_box_append(GTK_BOX(box),gtk_label_new("Virtual COM 驅動／配對尚未自動安裝。埠不存在會回報錯誤，不會使用假的 COM。"));
    s->timer=g_timeout_add(100,refresh,s);
    g_object_set_data_full(G_OBJECT(window),"state",s,[](gpointer p){delete static_cast<State*>(p);});
    gtk_window_set_child(GTK_WINDOW(window),box); gtk_window_present(GTK_WINDOW(window));
}
}
int launch_simulator_gui() {
    auto* app=gtk_application_new("org.posmvsweep.Simulator",G_APPLICATION_DEFAULT_FLAGS); g_signal_connect(app,"activate",G_CALLBACK(activate),nullptr);
    const int result=g_application_run(G_APPLICATION(app),0,nullptr); g_object_unref(app); return result;
}