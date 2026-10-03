// Deterministic test of the real decoder/player, without WASAPI or a fake DSP.
#include "../player/player.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <thread>
static void check(bool ok, const char *message) { if (!ok) throw std::runtime_error(message); }
static void loaded(Player &p, const char *path) {
  check(p.load(path), "load refused");
  for (int i=0; i<2000 && p.status()==1; ++i)
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
}
int main(int argc, char **argv) {
 try {
  check(argc==6,"expected wav, mp3, m4a, 600s, 601s fixtures");
  Player p;
  float l[480],r[480];
  auto tick=[&]{p.process(l,r,480);};
  for(int file=1;file<=3;++file){
   loaded(p,argv[file]);check(p.status()==2,"format decode failed");
   check(p.duration()>=2900 && p.duration()<=3100,"wrong duration");
   tick();check(p.position()==0,"load must not autoplay");
   check(p.resume(),"play refused");for(int i=0;i<40;++i)tick();
   check(p.position()>=390 && p.position()<=410,"position not advancing");
   check(*std::max_element(l,l+480)>0.05f,"no audio");
   check(p.pause(),"pause refused");tick();const auto frozen=p.position();
   for(int i=0;i<20;++i)tick();
   check(p.status()==5 && p.position()==frozen,"pause advanced cursor");
   check(std::all_of(l,l+480,[](float v){return v==0;}),"pause not silent");
   check(p.seek(1200),"seek refused");tick();check(p.position()==1200,"paused seek wrong");
   check(p.resume(),"resume refused");tick();check(p.position()==1210,"resume rewound");
   check(!p.seek(p.duration()+1),"out-of-range seek accepted");
   check(p.seek(p.duration()),"EOF seek refused");tick();check(p.status()==4,"EOF missing");
   // Seek then Play before the next callback must preserve the chosen position.
   check(p.seek(700) && p.resume(),"seek/play refused");tick();check(p.position()==710,"EOF seek/play rewound");
   check(p.play(),"legacy restart refused");tick();check(p.position()==10,"legacy restart changed");
   p.reset();tick();check(p.status()==5 && p.position()==10,"reset lost position");
  }
  loaded(p,argv[4]);check(p.status()==2 && p.duration()==600000,"ten minutes rejected");
  loaded(p,argv[5]);check(p.status()<0,"over ten minutes accepted");
  std::cout<<"PASS: MP3/WAV/M4A, pause/resume/seek/EOF/reset, 600s accepted and 601s rejected.\n";
  return 0;
 } catch(const std::exception &e){std::cerr<<e.what()<<'\n';return 1;}
}
