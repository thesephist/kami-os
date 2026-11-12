
"""Actual ISP service with a recording HAL; no SDK, hardware or private fixtures."""
from pathlib import Path
import subprocess
import tempfile
ROOT = Path(__file__).resolve().parents[1]
header = (ROOT/'include/services/cine_camera.h').read_text()
types = header[header.index('enum class Mode'):header.index('struct Status')]
with tempfile.TemporaryDirectory(prefix='cine-isp-config-') as td:
    p = Path(td)
    (p/'services').mkdir()
    (p/'esp_timer.h').write_text('#pragma once\ninline unsigned long long esp_timer_get_time(){return 1000000;}\n')
    (p/'services/cine_camera.h').write_text(r"""
#pragma once
#include <stdint.h>
#include <stdio.h>
#include "hal/hal_camera.h"
#include "core/cine_tone.h"
namespace services { namespace cine_camera {
""" + types + r"""
inline bool isIsp(Mode m){return m==Mode::Isp3Mp||m==Mode::IspVga||m==Mode::Stock;}
extern bool fail;
extern unsigned writes;
extern hal::camera::IspExperiment last;
struct Camera {
 void returnFrame(){}
 bool configureIspExperiment(const hal::camera::IspExperiment& r,hal::camera::IspReadback* p){
  ++writes;last=r;if(fail)return false;p->valid=true;p->shutter.requestedUs=r.shutterUs;p->shutter.gainQ4=r.gainQ4;p->shutter.vts=r.autoVts?r.autoVts:1564;p->shutter.valid=true;p->shutter.manual=!r.autoExposure;p->shutter.actualUs=r.shutterUs;return true;
 }
};
struct Session {
 struct Status{bool open=true,saved=false;char message[80];}status_;
 Camera camera_;Mode mode_=Mode::Isp3Mp;
 bool frameHeld_=false,vendorGammaValid_=false,ispConfigured_=false,pattern_=false;
 uint8_t vendorGamma_[16]={};unsigned wbPreset_=1;
 uint64_t shutterReadyUs_=0;uint16_t autoFrameVts_=0;
 IspConfig ispConfig_;
 hal::camera::IspReadback ispReadback_;
 hal::camera::ShutterInfo shutter_;
 core::cine::Parameters params_;
 void close(){status_.open=false;}
 bool configureIsp(const IspConfig&);
 bool configureBaseline(uint32_t,unsigned,bool=false,bool=false);
 bool autoExposure() const {return ispConfigured_&&ispConfig_.autoExposure;}
 bool refreshExposure();bool resumeAutoExposure();
 void adjustWhiteBalance(int);
 static const char* whiteBalanceName(unsigned);
};
}}
""")
    (p/'Arduino.h').write_text('#pragma once\n#include <stddef.h>\n#include <stdint.h>\n')
    (p/'test.cpp').write_text(r"""
#include "services/cine_camera.h"
#include <cassert>
#include <cstring>
#include <climits>
#include <initializer_list>
namespace services { namespace cine_camera { bool fail=false;unsigned writes=0;hal::camera::IspExperiment last;}}
namespace hal{namespace camera{bool readJpegShutter(ShutterInfo* out){out->valid=true;out->manual=false;out->actualUs=12500;out->gainQ4=32;return true;}}}
int main(){
 using namespace services::cine_camera;
 const uint8_t neutral[]={29,96,3,12,120,132,125,107,18,1,152};
 const uint8_t selected[]={29,96,3,13,132,145,138,118,20,1,152};
 Session s;IspConfig config;
 assert(s.configureIsp(config)&&s.ispConfigured_&&!memcmp(last.cmx,selected,11));
 uint8_t gamma[16];memcpy(gamma,last.gamma,16);
 config.matrix=ColorMatrix::VendorNeutral;
 assert(s.configureIsp(config)&&!memcmp(last.cmx,neutral,11)&&!memcmp(last.gamma,gamma,16));
 config.matrix=ColorMatrix::FittedDiagnostic;assert(s.configureIsp(config));
 for(unsigned expected:{2,3,4,1,2,3,4,1}){
  s.adjustWhiteBalance(1);assert(s.wbPreset_==expected&&last.wbMode==expected);
  assert(s.ispConfig_.matrix==ColorMatrix::VendorSaturated&&!memcmp(last.cmx,selected,11)&&!memcmp(last.gamma,gamma,16));
 }
 s.adjustWhiteBalance(-1);assert(s.wbPreset_==4);s.adjustWhiteBalance(-3);assert(s.wbPreset_==1);
 unsigned before=writes;s.adjustWhiteBalance(0);s.adjustWhiteBalance(4);assert(writes==before);
 s.adjustWhiteBalance(INT_MAX);assert(s.wbPreset_==4);s.adjustWhiteBalance(INT_MIN);assert(s.wbPreset_==4);
 assert(s.resumeAutoExposure()&&s.autoExposure()&&last.autoExposure&&last.aeLevel==-1);
 assert(s.autoFrameVts_==1564&&last.autoVts==1564);
 s.adjustWhiteBalance(1);assert(s.autoExposure()&&last.autoExposure);
 assert(s.refreshExposure()&&s.shutter_.actualUs==12500&&s.shutter_.gainQ4==32);
 unsigned autoWrites=writes;assert(s.resumeAutoExposure()&&writes==autoWrites);
 assert(s.configureBaseline(100000,32)&&!s.autoExposure());
 assert(s.resumeAutoExposure()&&last.autoExposure&&last.autoVts==1564);
 assert(s.configureBaseline(100000,32,true)&&last.useSettledExposure&&last.shutterUs==100000&&last.gainQ4==32&&!memcmp(last.cmx,selected,11));
 config=IspConfig{};config.gamma=GammaCurve::Vendor;before=writes;
 assert(!s.configureIsp(config)&&writes==before);s.vendorGammaValid_=true;
 for(unsigned i=0;i<16;++i)s.vendorGamma_[i]=3*i+1;
 assert(s.configureIsp(config)&&!memcmp(last.gamma,s.vendorGamma_,16));
 config.exposureThirds=1;before=writes;assert(!s.configureIsp(config)&&writes==before);
 for(int field=0;field<5;++field){config=IspConfig{};
  if(field==0)config.gamma=static_cast<GammaCurve>(255);
  if(field==1)config.matrix=static_cast<ColorMatrix>(255);
  if(field==2)config.shutterUs=100001;
  if(field==3)config.gainQ4=65;
  if(field==4)config.exposureThirds=7;
  assert(!s.configureIsp(config)&&writes==before);
 }
 fail=true;unsigned previous=s.wbPreset_;s.adjustWhiteBalance(1);assert(s.wbPreset_==previous&&!s.status_.open);
}
""")
    subprocess.run(['c++','-std=c++17','-O1','-fsanitize=address,undefined','-fno-sanitize-recover=all',
                    '-I'+str(p),'-I'+str(ROOT/'include'),str(p/'test.cpp'),
                    str(ROOT/'src/services/cine_camera_isp.cpp'),str(ROOT/'src/core/cine_tone.cpp'),'-o',str(p/'test')],check=True)
    subprocess.run([str(p/'test')],check=True)
print('PASS typed ISP configuration, exact selected/neutral matrix and gamma, baseline reset, WB wrap, guards and rollback; ASan/UBSan')
