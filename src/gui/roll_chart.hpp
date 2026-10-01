#pragma once
#include <gtk/gtk.h>
#include <array>
#include <deque>
#include <chrono>
#include <algorithm>
#include <cmath>
namespace posmv {
// UI-thread only, bounded ten-second monitoring history; never schedules output.
class RollChart {
    struct Point { double time; std::array<double,3> values; };
    std::deque<Point> points_;
    GtkWidget* area_{};
    static double now() {
        return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
    }
    static void draw(GtkDrawingArea*, cairo_t* cr, int width, int height, gpointer data) {
        auto& self=*static_cast<RollChart*>(data);
        cairo_set_source_rgb(cr,0.08,0.09,0.11); cairo_paint(cr);
        const double end=now();
        double limit=10;
        for(const auto& p:self.points_) for(double v:p.values) limit=std::max(limit,std::abs(v)*1.1);
        cairo_set_source_rgb(cr,0.4,0.4,0.4); cairo_move_to(cr,0,height/2.0); cairo_line_to(cr,width,height/2.0); cairo_stroke(cr);
        const double colors[3][3]={{0.2,0.8,1},{1,0.7,0.1},{0.3,1,0.4}};
        for(unsigned series=0;series<3;++series) {
            cairo_set_source_rgb(cr,colors[series][0],colors[series][1],colors[series][2]);
            cairo_set_line_width(cr,1.5); bool first=true;
            for(const auto& p:self.points_) {
                const double x=(p.time-end+10)/10*width;
                const double y=height/2.0-p.values[series]/limit*(height/2.0-8);
                if(first) cairo_move_to(cr,x,y); else cairo_line_to(cr,x,y); first=false;
            }
            cairo_stroke(cr);
        }
    }
public:
    GtkWidget* create() {
        area_=gtk_drawing_area_new(); gtk_widget_set_size_request(area_, -1, 160);
        gtk_drawing_area_set_draw_func(GTK_DRAWING_AREA(area_),draw,this,nullptr);
        return area_;
    }
    void add(double real,double offset,double output) {
        const auto time=now(); points_.push_back({time,{real,offset,output}});
        while(!points_.empty() && (points_.front().time<time-10 || points_.size()>200)) points_.pop_front();
        if(area_) gtk_widget_queue_draw(area_);
    }
};
}