from pathlib import Path
import subprocess,tempfile
r=Path(__file__).resolve().parents[1];header=(r/'include/hal/hal_camera.h').read_text();types=header[header.index('struct ShutterInfo'):header.index('bool lockRawShutter')]
source=(r/'src/hal/hal_camera.cpp').read_text();section=source[source.index('namespace {\nShutterInfo s_shutter;'):source.index('namespace {\nbool jpegShutterRead(')]
with tempfile.TemporaryDirectory() as t:
 p=Path(t);(p/'test.cpp').write_text('''#include <stdint.h>
#include <map>
#include <cassert>
#include "core/cine_shutter.h"
'''+types+'''
std::map<uint16_t,uint8_t> registers;bool busAvailable=true;int locks=0,writes=0,failAt=0;
bool s_initialized=true;uint16_t s_detectedPid=0x3660;struct {bool enabled=true;}s_rawInfo;
constexpr int kI2cLockTimeout=1;
namespace hal{namespace bus{bool lockI2c(int){if(!busAvailable)return false;++locks;return true;}void unlockI2c(){--locks;}}}
bool ovReadReg(uint16_t a,uint8_t* v){*v=registers[a];return true;}
bool ovWriteReg(uint16_t a,uint8_t v){if(++writes==failAt)return false;registers[a]=v;return true;}
'''+section+'''
void setup(int multiplier){registers={{0x303a,0},{0x303b,uint8_t(multiplier)},{0x303c,0x11},{0x303d,2},{0x380c,8},{0x380d,0xfc},{0x380e,6},{0x380f,0x1c},{0x3500,0},{0x3501,0x20},{0x3502,0},{0x350a,0},{0x350b,37},{0x3503,0},{0x3509,0}};writes=failAt=0;}
int main(){
 for(int multiplier:{4,8}){setup(multiplier);ShutterInfo s;assert(lockRawShutter(&s)&&s.valid&&s.manual&&s.gainQ4==37&&locks==0);assert(s.clockHz==unsigned(multiplier)*3000000);
  uint32_t last=0;for(auto us:core::cine::shutterSpeedsUs){assert(setRawShutter(us,&s));assert(s.actualUs>last&&s.actualUs<=100000&&s.gainQ4==37&&s.manual);assert(s.ticks<=(s.vts-4)*16u);last=s.actualUs;}assert(last>=99980);
  assert(!setRawShutter(100001,&s)&&!setRawShutter(999,&s));busAvailable=false;assert(!setRawShutter(10000,&s)&&locks==0);busAvailable=true;
  auto old=registers;failAt=writes+3;assert(!setRawShutter(10000,&s)&&locks==0);for(unsigned a=0x3500;a<=0x3503;++a)assert(registers[a]==old[a]);assert(registers[0x350a]==old[0x350a]&&registers[0x350b]==old[0x350b]);
 }
 setup(8);registers[0x303d]=3;ShutterInfo s;int before=writes;assert(!lockRawShutter(&s)&&writes==before&&locks==0);
 setup(8);s_detectedPid=0x5640;assert(!lockRawShutter(&s));s_detectedPid=0x3660;
 setup(8);registers[0x3509]=8;assert(!lockRawShutter(&s));
 setup(8);registers[0x350b]=127;assert(lockRawShutter(&s)&&s.gainQ4==64);
}
''');subprocess.run(['c++','-std=c++17','-O1','-fsanitize=address,undefined','-I'+str(r/'include'),str(p/'test.cpp'),'-o',str(p/'test')],check=True);subprocess.run([str(p/'test')],check=True)
print('PASS actual HAL shutter code: both RAW clock profiles, monotone hardware ticks, 100ms cap, exact locked gain, readback, bus failures, unsupported profile refusal and failed-write rollback under ASan/UBSan')
