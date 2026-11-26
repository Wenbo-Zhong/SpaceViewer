#include "view/icons.hpp"
#include "view/raster.hpp"

#include <algorithm>
#include <cmath>

void toolbarIcon(Frame& frame, int x, int y, int size, HotKind kind, uint32_t ink, uint32_t tint) {
  auto line = [&](double ax, double ay, double bx, double by) {
    int steps = std::max(1, static_cast<int>(std::hypot(bx - ax, by - ay) * size));
    int weight = std::max(1, size / 13);
    for (int i = 0; i <= steps; ++i) {
      double t = static_cast<double>(i) / steps;
      fillRect(frame, x + static_cast<int>((ax + (bx - ax) * t) * size) - weight / 2,
               y + static_cast<int>((ay + (by - ay) * t) * size) - weight / 2, weight, weight, ink);
    }
  };
  auto box = [&](double a, double b, double w, double h) {
    line(a,b,a+w,b); line(a+w,b,a+w,b+h); line(a+w,b+h,a,b+h); line(a,b+h,a,b);
  };
  switch (kind) {
    case HotKind::Open:
      roundRect(frame,x+size/10,y+size/3,size*4/5,size/2,size/12,tint);
      line(.1,.8,.1,.22); line(.1,.22,.4,.22); line(.4,.22,.5,.34); line(.5,.34,.88,.34);
      line(.1,.8,.78,.8); line(.78,.8,.94,.42); line(.94,.42,.27,.42); line(.27,.42,.1,.8); break;
    case HotKind::Drives:
      roundRect(frame,x+size/8,y+size/5,size*3/4,size*3/5,size/10,tint);
      box(.13,.2,.74,.6); line(.13,.59,.87,.59); line(.66,.7,.73,.7); break;
    case HotKind::Back: case HotKind::Forward: {
      bool back = kind == HotKind::Back;
      double a = back ? .2 : .8, b = back ? .8 : .2;
      line(a,.5,b,.5); line(a,.5,.5,.2); line(a,.5,.5,.8); break;
    }
    case HotKind::Up:
      line(.5,.83,.5,.17); line(.5,.17,.2,.47); line(.5,.17,.8,.47); break;
    case HotKind::FocusRescan:
    case HotKind::Rescan:
      for (int i=0;i<32;++i) {
        double a=(35+i*9)*3.141592653589793/180, b=(35+(i+1)*9)*3.141592653589793/180;
        line(.5+.33*std::cos(a),.5+.33*std::sin(a),.5+.33*std::cos(b),.5+.33*std::sin(b));
      }
      line(.81,.18,.81,.42); line(.81,.42,.57,.42); break;
    case HotKind::Stop:
      roundRect(frame,x+size/4,y+size/4,size/2,size/2,size/12,ink); break;
    case HotKind::Export:
      line(.18,.51,.18,.84); line(.18,.84,.83,.84); line(.83,.84,.83,.51);
      line(.5,.65,.5,.12); line(.5,.12,.27,.35); line(.5,.12,.73,.35); break;
    case HotKind::Home:
      line(.12,.43,.5,.13); line(.5,.13,.88,.43); box(.24,.43,.52,.42); break;
    case HotKind::LessDetail: case HotKind::MoreDetail:
      box(.14,.14,.72,.72); line(.3,.5,.7,.5);
      if (kind == HotKind::MoreDetail) line(.5,.3,.5,.7); break;
    case HotKind::FileGroups:
      box(.12,.12,.34,.34); box(.54,.12,.34,.34); box(.12,.54,.34,.34); box(.54,.54,.34,.34); break;
    case HotKind::ShowFree:
      box(.15,.2,.7,.6); line(.5,.2,.5,.8); break;
    case HotKind::ShowUnknown:
      box(.15,.2,.7,.6); line(.38,.38,.6,.38); line(.6,.38,.5,.53); line(.5,.68,.5,.7); break;
    case HotKind::Configure:
      for (int i=0;i<3;++i) { double yy=.25+i*.25; line(.12,yy,.88,yy); box(.3+i*.12,yy-.08,.12,.16); } break;
    default: break;
  }
}
