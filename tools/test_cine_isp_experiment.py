"""Exercise the actual HAL register transaction with a failure-injected bus."""
from pathlib import Path
import subprocess,tempfile
R=Path(__file__).resolve().parents[1];s=(R/'src/hal/hal_camera.cpp').read_text();a=s.index('namespace {\nbool jpegShutterRead(');b=s.index('\nbool triggerAutofocus(',a);code=s[a:b]
with tempfile.TemporaryDirectory() as td:
 p=Path(td);(p/'Arduino.h').write_text('#pragma once\n#include <stdint.h>\n#include <stddef.h>\n')
 (p/'test.cpp').write_text(r'''
#include "hal/hal_camera.h"
#include "core/cine_shutter.h"
#include "core/cine_tone.h"
#include <cassert>
#include <array>
#include <vector>
#include <cstring>
constexpr int PIXFORMAT_JPEG=4;struct Sensor{int pixformat=PIXFORMAT_JPEG;};Sensor sensor;
Sensor* esp_camera_sensor_get(){return &sensor;}
namespace hal {namespace bus{bool lockI2c(unsigned){return true;}void unlockI2c(){}}namespace camera{
bool s_initialized=true;unsigned s_detectedPid=0x3660;RawInfo s_rawInfo;void* s_lastFb=nullptr;constexpr unsigned kI2cLockTimeout=10;
std::array<uint8_t,65536> registers{};std::vector<unsigned> writes;int failRegister=-1,failReadRegister=-1;
bool ovReadReg(uint16_t a,uint8_t* out){if(a==failReadRegister)return false;*out=registers[a];return true;}
bool ovWriteReg(uint16_t a,uint8_t v){writes.push_back(a);if(a==failRegister){failRegister=-1;return false;}registers[a]=v;return true;}
''' +code+r'''
}}
int main(){using namespace hal::camera;
 registers[0x303b]=24;registers[0x303c]=0x11;registers[0x303d]=0x30;
 registers[0x380c]=2300>>8;registers[0x380d]=2300&255;registers[0x380e]=1564>>8;registers[0x380f]=1564&255;
 registers[0x350b]=16;registers[0x3821]=0x20;registers[0x5000]=0xa7;registers[0x5001]=0xa3;
 IspExperiment request;request.shutterUs=80000;request.gainQ4=16;assert(core::cine::makeIspGamma(true,0,request.gamma));const uint8_t cmx[11]={29,96,3,12,120,132,125,107,18,1,152};memcpy(request.cmx,cmx,11);IspReadback output;
 assert(configureIspExperiment(request,&output));assert(output.valid&&output.shutter.manual&&output.shutter.clockHz==48000000);
 assert(output.shutter.actualUs>=79997&&output.shutter.actualUs<=80003&&output.shutter.gainQ4==16&&output.shutter.vts>1564);
 assert(!memcmp(output.gamma,request.gamma,16)&&!memcmp(output.cmx,request.cmx,11));assert(output.wb[0]==1504&&output.wb[1]==1040&&output.wb[2]==1344);
 assert((output.flags[0]&0x80)==0&&(output.flags[0]&0x21)==0x21&&(output.flags[1]&0x80)==0&&(output.flags[1]&3)==3);
 for(auto a:writes)assert((a>=0x3500&&a<=0x3503)||a==0x350a||a==0x350b||a==0x380e||a==0x380f||(a>=0x3400&&a<=0x3406)||a==0x5000||a==0x5001||a==0x5308||a==0x5302||a==0x5303||a==0x530b||a==0x530c||(a>=0x5481&&a<=0x5490)||(a>=0x5381&&a<=0x538b));
 writes.clear();auto unchanged=registers;ShutterInfo measured;assert(readJpegShutter(&measured)&&measured.manual&&measured.actualUs==output.shutter.actualUs&&measured.gainQ4==16&&writes.empty()&&registers==unchanged);
 registers[0x3503]=0;unchanged=registers;assert(readJpegShutter(&measured)&&!measured.manual&&writes.empty()&&registers==unchanged);registers[0x3503]=3;
 uint8_t gamma[16];memset(gamma,0xaa,16);writes.clear();auto savedRegs=registers;
 assert(readJpegGamma(gamma)&&!memcmp(gamma,output.gamma,16)&&writes.empty()&&registers==savedRegs);
 uint8_t priorGamma[16];memcpy(priorGamma,gamma,16);failReadRegister=0x5488;
 assert(!readJpegGamma(gamma)&&!memcmp(priorGamma,gamma,16)&&writes.empty());failReadRegister=-1;
 s_lastFb=(void*)1;assert(!readJpegGamma(gamma));s_lastFb=nullptr;
 auto snapshot=registers;writes.clear();assert(core::cine::makeIspGamma(false,3,request.gamma));request.shutterUs=50000;failRegister=0x5488;assert(!configureIspExperiment(request,&output));assert(registers==snapshot);
 writes.clear();s_rawInfo.enabled=true;assert(!configureIspExperiment(request,&output)&&writes.empty());s_rawInfo.enabled=false;
 request.gamma[1]=0;request.gamma[0]=20;assert(!configureIspExperiment(request,&output)&&writes.empty());assert(core::cine::makeIspGamma(false,0,request.gamma));
 s_lastFb=(void*)1;assert(!configureIspExperiment(request,&output)&&writes.empty());s_lastFb=nullptr;
 for(unsigned wb=0;wb<5;++wb){request.wbMode=wb;assert(configureIspExperiment(request,&output));assert(registers[0x3406]==(wb?1:0)&&output.wbMode==wb);assert(!(output.flags[0]&0x80));}
 const uint16_t expected[4][3]={{0x5e0,0x410,0x540},{0x650,0x410,0x4f0},{0x520,0x410,0x660},{0x420,0x3f0,0x710}};
 for(unsigned wb=1;wb<5;++wb){request.wbMode=wb;assert(configureIspExperiment(request,&output));for(unsigned c=0;c<3;++c)assert(output.wb[c]==expected[wb-1][c]);}
 request.wbMode=0;request.useSettledExposure=true;assert(configureIspExperiment(request,&output));assert(output.shutter.requestedUs>=1000&&output.shutter.requestedUs<=100000);request.useSettledExposure=false;
 snapshot=registers;request.wbMode=3;failRegister=0x3403;assert(!configureIspExperiment(request,&output)&&registers==snapshot);request.wbMode=0;
 writes.clear();request.wbMode=5;assert(!configureIspExperiment(request,&output)&&writes.empty());request.wbMode=0;
 registers[0x303d]=2;assert(!configureIspExperiment(request,&output)&&writes.empty());registers[0x303d]=0x30;
 // Auto is a bounded hardware loop; gamma/color/WB remain the Cine preset.
 request.autoExposure=true;request.autoVts=1564;request.aeLevel=-1;
 assert(configureIspExperiment(request,&output)&&output.valid&&!output.shutter.manual);
 assert(output.shutter.requestedUs==0&&output.shutter.vts==1564&&(registers[0x3503]&3)==0);
 assert((registers[0x3a00]&5)==0&&registers[0x3a18]==0&&registers[0x3a19]==64);
 assert(registers[0x3a0f]==48&&registers[0x3a10]==41&&registers[0x3a1b]==48&&registers[0x3a1e]==41&&registers[0x3a11]==96&&registers[0x3a1f]==20);
 assert(((registers[0x3a02]<<8)|registers[0x3a03])==1560&&((registers[0x3a14]<<8)|registers[0x3a15])==1560);
 for(int reg:{0x3a02,0x3a03,0x3a14,0x3a15,0x3a18,0x3a19,0x3a0f,0x3a10,0x3a1b,0x3a1e,0x3a11,0x3a1f,0x3503}){
  snapshot=registers;failRegister=reg;assert(!configureIspExperiment(request,&output)&&registers==snapshot);
 }
 // An Auto WB/target update retains current integration even below the manual ladder.
 registers[0x3500]=0;registers[0x3501]=0;registers[0x3502]=80;registers[0x350b]=32;
 assert(configureIspExperiment(request,&output)&&output.shutter.ticks==80&&output.shutter.actualUs<1000&&output.shutter.gainQ4==32);
 request.aeLevel=-3;writes.clear();assert(!configureIspExperiment(request,&output)&&writes.empty());request.aeLevel=-1;
 request.autoExposure=false;request.shutterUs=100000;assert(configureIspExperiment(request,&output)&&output.shutter.manual&&output.shutter.vts>1564);
 request.autoExposure=true;assert(configureIspExperiment(request,&output)&&!output.shutter.manual&&output.shutter.vts==1564);
 for(int ev=-6;ev<=6;ev++)for(bool film:{false,true}){assert(core::cine::makeIspGamma(film,ev,request.gamma));for(int i=1;i<15;i++)assert(request.gamma[i]>=request.gamma[i-1]);}
 assert(!core::cine::makeIspGamma(true,7,request.gamma));
 core::cine::Parameters p;p.color=2;p.filmic=false;for(auto& g:p.gains)g=1024;core::cine::ToneMap t;t.configure(p);uint8_t r,g,b;t.pixel(49,49,49,r,g,b);assert(r==117&&g==117&&b==117);t.pixel(255,255,255,r,g,b);assert(r==255&&g==255&&b==255);
}
''')
 subprocess.run(['c++','-std=c++17','-O2','-fsanitize=address,undefined','-fno-sanitize-recover=all','-I'+str(p),'-I'+str(R/'include'),str(p/'test.cpp'),str(R/'src/core/cine_tone.cpp'),'-o',str(p/'test')],check=True);subprocess.run([str(p/'test')],check=True)
print('PASS actual ISP transaction: matched shutter/gain, gamma/matrix/WB readback, finite writes, failure rollback, RAW/clock/loan guards; all gamma exposure seeds; Sunny D no-Filmic neutral values; ASan/UBSan')
