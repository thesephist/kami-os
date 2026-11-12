"""Canonical service lifecycle/controls/capture with recording hardware adapters."""

from pathlib import Path
import tempfile, subprocess

R = Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory(prefix="cine-service-") as td:
    p = Path(td)

    def put(name, text):
        f = p / name
        f.parent.mkdir(parents=True, exist_ok=True)
        f.write_text(text)

    put(
        "Arduino.h",
        "#pragma once\n#include <stdint.h>\n#include <stddef.h>\n#include <stdio.h>\n#include <string.h>\n",
    )
    put(
        "esp_camera.h",
        "#pragma once\nconstexpr int FRAMESIZE_QXGA=20,PIXFORMAT_JPEG=4;\n",
    )
    put(
        "esp_heap_caps.h",
        "#pragma once\n#include <stdlib.h>\n#define MALLOC_CAP_SPIRAM 1\n#define MALLOC_CAP_8BIT 2\nextern int allocations,budget;inline void* heap_caps_malloc(size_t n,int caps){if(budget==0)return nullptr;if(budget>0)--budget;void* p=malloc(n);if(p)++allocations;return p;}inline void heap_caps_free(void* p){if(p)--allocations;free(p);}\n",
    )
    put(
        "esp_timer.h",
        "#pragma once\n#include <stdint.h>\nextern int64_t clockUs;inline int64_t esp_timer_get_time(){return clockUs+=100;}\n",
    )
    put(
        "core/watchdog.h",
        "#pragma once\nnamespace core{namespace watchdog{inline void feed(){}}}\n",
    )
    put(
        "services/camera_session.h",
        r"""#pragma once
#include "hal/hal_camera.h"
extern bool running,reserved;
namespace services{namespace camera{class Session{public:
 enum class PrepareResult{Ready,BackgroundBusy};enum class Resume{DeferredWithInputRecovery};
 PrepareResult prepareCapture(bool);bool start(int,int,const hal::camera::Tuning*,int);void stop();bool running()const{return ::running;}bool backgroundHeld()const{return ::reserved;}
 void restoreUi(Resume);bool read(hal::camera::CameraFrame*);void returnFrame();const char* lastError(){return "test";}
 bool configureCineIsp(const hal::camera::CineIspRequest&,hal::camera::CineIspReadback*);
};}}
""",
    )
    put(
        "services/photo_storage.h",
        r"""#pragma once
#include <stdint.h>
#include <stddef.h>
namespace services{namespace photos{struct Target{uint32_t sequence=0;char path[64]={};bool valid()const{return sequence!=0;}};bool initialize();Target prepare();bool write(const Target&,const uint8_t*,size_t);}}
""",
    )
    put(
        "services/photo_print.h",
        "#pragma once\nnamespace services{namespace photo_print{bool readyForCapture();}}\n",
    )
    put(
        "hal/hal_sd.h",
        "#pragma once\n#include <stddef.h>\n#include <stdint.h>\nnamespace hal{namespace sd{bool ensureDir(const char*);bool writeFile(const char*,const uint8_t*,size_t);}}\n",
    )
    put(
        "test.cpp",
        r"""
#include "services/cine_camera.h"
#include "services/photo_storage.h"
#include <cassert>
#include <climits>
#include <vector>
#include <map>
#include <string>
int allocations=0,budget=-1;int64_t clockUs=0;bool running=false,reserved=false,held=false,ready=true,stopBlocked=false,failInit=false,failRead=false,failDecode=false,failWrite=false,failMeta=false,failTelemetry=false,failIsp=false,stale=false,badFrame=false;
unsigned liveUs=10000,liveGain=32;
unsigned sequence=0,reads=0,configs=0;hal::camera::CineIspRequest last;std::vector<uint8_t> jpeg={255,216,1,2,255,217};std::map<std::string,std::vector<uint8_t>> files;
namespace hal{namespace camera{
bool readJpegShutter(ShutterInfo* p){if(failTelemetry)return false;p->valid=true;p->manual=false;p->actualUs=liveUs;p->gainQ4=liveGain;p->vts=1564;p->frameUs=75000;return true;}
}namespace sd{bool ensureDir(const char*){return true;}bool writeFile(const char* path,const uint8_t* data,size_t n){if(failMeta)return false;files[path].assign(data,data+n);return true;}}}
namespace services{namespace photo_print{bool readyForCapture(){return ready;}}namespace photos{
bool initialize(){return true;}Target prepare(){Target t;t.sequence=++sequence;snprintf(t.path,sizeof(t.path),"/photos/IMG_%05u.jpg",sequence);return t;}
bool write(const Target& t,const uint8_t* data,size_t n){if(failWrite)return false;files[t.path].assign(data,data+n);return true;}
}namespace camera{
Session::PrepareResult Session::prepareCapture(bool deep){assert(!deep);reserved=true;return PrepareResult::Ready;}
bool Session::start(int size,int format,const hal::camera::Tuning* t,int quality){assert(size==20&&format==4&&quality==6&&t->wb_mode==1&&t->ae_level==-1&&!t->lenc&&!t->allowNightMode);::running=!failInit;return ::running;}
void Session::stop(){assert(!held);if(!stopBlocked)::running=false;}
void Session::restoreUi(Resume){assert(!::running);reserved=false;}
bool Session::read(hal::camera::CameraFrame* f){assert(::running&&!held);if(failRead)return false;clockUs+=100000;++reads;held=true;f->buf=jpeg.data();f->len=jpeg.size();f->width=badFrame?640:2048;f->height=1536;f->format=4;f->dmaStartUs=stale?0:clockUs;return true;}
void Session::returnFrame(){held=false;}
bool Session::configureCineIsp(const hal::camera::CineIspRequest& r,hal::camera::CineIspReadback* p){assert(!held);++configs;last=r;if(failIsp)return false;*p={};p->valid=true;p->shutter.valid=true;p->shutter.manual=!r.autoExposure;p->shutter.requestedUs=r.autoExposure?0:r.shutterUs;p->shutter.actualUs=r.autoExposure?10000:r.shutterUs;p->shutter.gainQ4=r.autoExposure?32:r.gainQ4;p->shutter.vts=r.autoExposure?1564:r.shutterUs==100000?2095:1564;p->shutter.frameUs=75000;return true;}
}namespace cine_camera{
void Session::releaseIspPreview(){}
bool Session::renderIspPreview(const hal::camera::CameraFrame&,bool(*abort)(void*),void* p){if(abort&&abort(p)){status_.previewInterrupted=true;return false;}return !failDecode;}
}}
int main(){using services::cine_camera::Session;using services::cine_camera::WhiteBalance;
 {Session c;assert(c.open());}assert(!allocations&&!running&&!reserved);
 Session c;ready=false;assert(!c.open()&&!allocations);ready=true;
 failInit=true;assert(!c.open()&&!allocations&&!reserved);failInit=false;
 budget=0;assert(!c.open()&&!allocations&&!reserved);budget=-1;
 failRead=true;assert(!c.open()&&!allocations&&!reserved);failRead=false;
 failIsp=true;assert(!c.open()&&!allocations&&!reserved);failIsp=false;
 assert(c.open()&&c.autoExposure()&&c.whiteBalance()==WhiteBalance::Sunny&&!held&&allocations==1);
 auto n=reads;assert(!c.preview([](void*){return true;})&&reads==n&&!held);
 for(unsigned abortAt:{2u,3u}){struct Calls{unsigned n,at;}calls{0,abortAt};assert(!c.preview([](void* p){auto& c=*static_cast<Calls*>(p);return ++c.n==c.at;},&calls)&&!held&&c.status().previewInterrupted);}
 assert(c.preview()&&!held);failDecode=true;assert(!c.preview()&&!held&&!c.status().previewInterrupted);failDecode=false;
 badFrame=true;assert(!c.preview()&&!held);badFrame=false;
 for(int delta:{1,-1,INT_MAX,INT_MIN}){c.adjustWhiteBalance(delta);assert(unsigned(c.whiteBalance())>=1&&unsigned(c.whiteBalance())<=4&&c.autoExposure());}
 liveUs=500;liveGain=80;assert(c.refreshExposure());c.adjustWhiteBalance(1);assert(c.autoExposure()&&last.shutterUs==1000&&last.gainQ4==64);liveUs=10000;liveGain=32;
 c.adjustShutter(INT_MAX);assert(!c.autoExposure()&&c.shutter().requestedUs==100000&&last.gainQ4==32);auto count=configs;c.adjustShutter(1);assert(configs==count);
 c.adjustWhiteBalance(1);assert(!c.autoExposure()&&c.shutter().requestedUs==100000&&last.gainQ4==32);
 c.adjustShutter(INT_MIN);assert(c.shutter().requestedUs==1000);assert(c.resumeAutoExposure()&&c.autoExposure()&&last.autoVts==1564);count=configs;assert(c.resumeAutoExposure()&&configs==count);
 assert(!c.capture());clockUs+=1000000;assert(c.preview());stale=true;n=reads;assert(!c.capture()&&!held&&reads==n+4);stale=false;
 assert(c.capture()&&!held&&c.status().saved&&files[c.status().photoPath]==jpeg);std::string meta(files[c.status().metadataPath].begin(),files[c.status().metadataPath].end());assert(meta.find("\"automatic\":true")!=std::string::npos&&meta.find("\"sample_valid\":true")!=std::string::npos&&meta.find("\"output_width\":2048")!=std::string::npos);
 stale=true;assert(!c.capture());stale=false;
#if ORIGAMI_CINE_PROFILING
 assert(c.profile().saveUs==0&&c.profile().totalUs==0);
#endif
 failTelemetry=true;assert(c.capture()&&c.autoExposure());meta=std::string(files[c.status().metadataPath].begin(),files[c.status().metadataPath].end());assert(meta.find("\"sample_valid\":false")!=std::string::npos);failTelemetry=false;
 failMeta=true;assert(c.capture()&&strstr(c.status().message,"metadata failed"));failMeta=false;
 failWrite=true;assert(!c.capture()&&!held&&!c.status().saved);failWrite=false;
 stopBlocked=true;assert(!c.close()&&running&&reserved&&allocations==1);stopBlocked=false;assert(c.close()&&!running&&!reserved&&!allocations);assert(c.close());
 assert(c.open());failIsp=true;c.adjustWhiteBalance(1);assert(!c.status().open&&!running&&!reserved&&!allocations);failIsp=false;
}
""",
    )
    for profiling in [0, 1]:
        subprocess.run(
            [
                "c++",
                "-std=c++11",
                "-O1",
                "-fsanitize=address,undefined",
                "-DORIGAMI_CINE_PROFILING=" + str(profiling),
                "-I" + str(p),
                "-I" + str(R / "include"),
                str(p / "test.cpp"),
                str(R / "src/services/cine_camera.cpp"),
                str(R / "src/services/cine_camera_isp.cpp"),
                "-o",
                str(p / "test"),
            ],
            check=True,
        )
        subprocess.run([str(p / "test")], check=True)
print(
    "PASS canonical service with profiling off/on: startup/failures, loan cancellation, WB/manual/Auto, freshness, original JPEG save, metadata failures, blocked teardown and cleanup; ASan/UBSan"
)
