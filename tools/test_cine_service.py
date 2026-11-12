from pathlib import Path
import tempfile,subprocess
r=Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory(prefix='cine-service-') as directory:
 p=Path(directory)
 def put(name,s):
  f=p/name;f.parent.mkdir(parents=True,exist_ok=True);f.write_text(s)
 put('Arduino.h','''#pragma once
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
struct Log{template<class...T>void printf(const char*,T...){} };extern Log Serial;
''')
 put('esp_camera.h','#pragma once\nconstexpr int FRAMESIZE_VGA=8,FRAMESIZE_XGA=10,FRAMESIZE_QXGA=20,PIXFORMAT_JPEG=4,PIXFORMAT_GRAYSCALE=3,PIXFORMAT_RAW=6;\n')
 put('esp_heap_caps.h','#pragma once\n#include <stdlib.h>\n#define MALLOC_CAP_SPIRAM 1\n#define MALLOC_CAP_8BIT 2\nextern int allocations,failAllocation;inline void* heap_caps_malloc(size_t n,int){if(failAllocation==0)return nullptr;if(failAllocation>0)--failAllocation;void* p=malloc(n);if(p)++allocations;return p;}inline void heap_caps_free(void* p){if(p)--allocations;free(p);}\n')
 put('esp_timer.h','#pragma once\n#include <stdint.h>\nextern int64_t clockUs;inline int64_t esp_timer_get_time(){return clockUs+=100;}\n')
 put('core/watchdog.h','#pragma once\nnamespace core{namespace watchdog{inline void feed(){}}}\n')
 put('services/camera_settings.h','''#pragma once
#include "services/camera_session.h"
namespace services{namespace camera{struct Settings{int mode=0;void load(){}hal::camera::Tuning tuning(){return {};}};}}
''')
 put('services/camera_session.h','''#pragma once
#include "Arduino.h"
namespace hal{namespace camera{
struct Tuning{int8_t ae_level=0;bool lenc=true,bayer8=false,testPattern=false,allowNightMode=true,autofocus=true,hmirror=true,vflip=false;int wb_mode=0;};
struct ShutterInfo {bool valid=false,manual=false;uint32_t requestedUs=0,actualUs=0,ticks=0,clockHz=0,frameUs=0;uint16_t gainQ4=0,hts=0,vts=0;};

bool readJpegGamma(uint8_t*);bool readJpegShutter(ShutterInfo*);struct IspExperiment{};struct IspReadback{bool valid=false;uint16_t wb[3]={};};
struct RawInfo{bool enabled=false;uint8_t black=4;uint16_t gains[3]={1024,1024,1024};};RawInfo rawInfo();
struct CameraFrame{const uint8_t* buf=nullptr;size_t len=0;unsigned width=0,height=0;int format=0;uint64_t dmaStartUs=0;};}}
extern bool running,reserved;namespace services{namespace camera{class Session{public:enum class PrepareResult{Ready,BackgroundBusy};enum class Resume{DeferredWithInputRecovery};PrepareResult prepareCapture(bool);bool start(int,int,const hal::camera::Tuning*,int=-1);void stop();bool running()const{return ::running;}bool backgroundHeld()const{return ::reserved;}void restoreUi(Resume);bool read(hal::camera::CameraFrame*);void returnFrame();void settle(unsigned){}bool lockRawShutter(hal::camera::ShutterInfo*);bool setRawShutter(uint32_t,hal::camera::ShutterInfo*);const char* lastError(){return "fixture";}};}}
''')
 put('services/photo_print.h','#pragma once\nnamespace services{namespace photo_print{bool readyForCapture();}}\n')
 put('hal/hal_sd.h','''#pragma once
#include "Arduino.h"
namespace hal{namespace sd{bool ensureDir(const char*);bool writeFile(const char*,const uint8_t*,size_t);bool removeFile(const char*);}}
''')
 put('img_converters.h','''#pragma once
#include "Arduino.h"
using CB=size_t(*)(void*,size_t,const void*,size_t);
bool fmt2jpg_cb(uint8_t*,size_t,uint16_t,uint16_t,int,uint8_t,CB,void*);
''')
 put('test.cpp',r'''
#include <cassert>
#include <limits.h>
#include <map>
#include <string>
#include <vector>
#include "services/cine_camera.h"
#include "services/photo_storage.h"
#include "esp_camera.h"
#include "img_converters.h"
namespace services{namespace cine_camera{
 bool Session::configureBaseline(uint32_t us,unsigned gain,bool settled,bool automatic){ispConfigured_=true;ispConfig_=IspConfig{};ispConfig_.useSettledExposure=settled;ispConfig_.autoExposure=automatic;ispConfig_.shutterUs=us;ispConfig_.gainQ4=gain;shutter_.valid=true;shutter_.manual=!automatic;shutter_.requestedUs=shutter_.actualUs=us;shutter_.gainQ4=gain;shutter_.frameUs=100000;params_.exposureThirds=0;return true;}
 bool Session::refreshExposure(){if(!autoExposure())return false;hal::camera::ShutterInfo live;if(!hal::camera::readJpegShutter(&live)||!live.valid)return false;shutter_=live;return true;}
 bool Session::resumeAutoExposure(){return configureBaseline(16667,16,false,true);}
 void Session::releaseIspPreview(){}
 bool failDecode=false;bool Session::renderIspPreview(const hal::camera::CameraFrame&,bool(*abort)(void*),void* ctx){if(abort&&abort(ctx)){status_.previewInterrupted=true;return false;}return !failDecode;}
}}
bool failTelemetry=false;int allocations=0,failAllocation=-1;bool reserved=false,stopBlocked=false,failRead=false,nullFrame=false;Log Serial;int64_t clockUs=0;bool ready=true,rawMode=false,running=false,held=false,failWrite=false,allowRaw=true;unsigned sequence=0,reads=0;bool detailMode=false,fullRawMode=false,checkFullPixels=false;core::cine::Parameters expectedParams;
std::vector<uint8_t> raw(1024*768,100),jpg={255,216,1,2,255,217};std::map<std::string,std::vector<uint8_t>> files;
namespace hal{namespace camera{bool readJpegGamma(uint8_t* p){for(unsigned i=0;i<16;++i)p[i]=i;return true;}bool readJpegShutter(ShutterInfo* p){if(failTelemetry)return false;p->valid=true;p->manual=false;p->actualUs=10000;p->gainQ4=16;return true;}RawInfo rawInfo(){RawInfo info;info.enabled=rawMode&&allowRaw;return info;}}namespace sd{bool ensureDir(const char*){return true;}bool writeFile(const char* path,const uint8_t* b,size_t n){if(failWrite)return false;files[path].assign(b,b+n);return true;}bool removeFile(const char* p){files.erase(p);return true;}}}
namespace services{namespace photo_print{bool readyForCapture(){return ready;}}namespace photos{bool initialize(){return true;}Target prepare(){Target t;t.sequence=++sequence;snprintf(t.path,sizeof(t.path),"/photos/IMG_%05u.jpg",sequence);return t;}bool write(const Target& t,const uint8_t* p,size_t n){return hal::sd::writeFile(t.path,p,n);}}
namespace camera{bool Session::lockRawShutter(hal::camera::ShutterInfo* s){s->valid=s->manual=true;s->requestedUs=s->actualUs=10000;s->gainQ4=37;s->hts=2300;s->vts=1564;s->clockHz=24000000;s->frameUs=150000;return true;}bool Session::setRawShutter(uint32_t us,hal::camera::ShutterInfo* s){assert(!held);s->requestedUs=s->actualUs=us;return true;}Session::PrepareResult Session::prepareCapture(bool){reserved=true;return PrepareResult::Ready;}bool Session::start(int size,int,const hal::camera::Tuning* t,int quality){rawMode=t->bayer8;detailMode=size==FRAMESIZE_QXGA&&!rawMode;fullRawMode=size==FRAMESIZE_QXGA&&rawMode;if(detailMode)assert(quality==6);if(rawMode)raw.resize(fullRawMode?3145728:786432,16);::running=true;return true;}void Session::stop(){assert(!held);if(!stopBlocked)::running=false;}void Session::restoreUi(Resume){reserved=false;}bool Session::read(hal::camera::CameraFrame* f){assert(::running&&!held);if(failRead)return false;++reads;held=true;f->buf=nullFrame?nullptr:rawMode?raw.data():jpg.data();f->len=rawMode?raw.size():jpg.size();f->width=rawMode?(fullRawMode?2048:1024):detailMode?2048:640;f->height=rawMode?(fullRawMode?1536:768):detailMode?1536:480;f->format=rawMode?PIXFORMAT_RAW:PIXFORMAT_JPEG;f->dmaStartUs=clockUs;return true;}void Session::returnFrame(){held=false;}}}
bool fmt2jpg_cb(uint8_t* pixels,size_t n,uint16_t w,uint16_t h,int format,uint8_t,CB cb,void* ctx){assert(n==size_t(w)*h*3&&w==(fullRawMode?640:512)&&h==(fullRawMode?480:384)&&format==5);if(checkFullPixels){assert(fullRawMode);core::cine::ToneMap t;t.configure(expectedParams);std::vector<uint8_t> expected(n);assert(t.renderBgrArea(raw.data(),raw.size(),2048,1536,expected.data(),n,640,480));assert(!memcmp(pixels,expected.data(),n));checkFullPixels=false;}return cb(ctx,0,jpg.data(),jpg.size())==jpg.size();}
int main(){using namespace services::cine_camera;

 {Session scoped;assert(scoped.open(Mode::Isp3Mp));assert(allocations>0);}assert(!allocations&&!reserved&&!running);
 Session c;
 for(int budget=0;budget<5;++budget){failAllocation=budget;assert(!c.open(Mode::RawBinned));assert(!allocations&&!running&&!reserved&&!held);}failAllocation=-1;
 failDecode=true;assert(!c.open(Mode::Isp3Mp));assert(!allocations&&!running&&!reserved);failDecode=false;
 failRead=true;assert(!c.open(Mode::Isp3Mp));assert(!allocations&&!running&&!reserved);failRead=false;
 nullFrame=true;assert(!c.open(Mode::Isp3Mp));assert(!c.open(Mode::RawBinned));assert(!allocations&&!running&&!reserved&&!held);nullFrame=false;
 auto tail=jpg.back();jpg.back()=0;assert(!c.open(Mode::Isp3Mp));assert(!allocations&&!running&&!reserved);jpg.back()=tail;
 assert(c.open(Mode::Isp3Mp));assert(c.close(false)&&reserved&&!running&&!allocations);assert(c.open(Mode::Isp3Mp));stopBlocked=true;int owned=allocations;assert(!c.close()&&running&&reserved&&allocations==owned);assert(!c.open(Mode::Isp3Mp)&&allocations==owned);stopBlocked=false;assert(c.close()&&!running&&!reserved&&!allocations);
 ready=false;assert(!c.open(Mode::RawBinned)&&!running);ready=true;
 allowRaw=false;assert(!c.open(Mode::RawBinned)&&!running);allowRaw=true;
 assert(c.open(Mode::Stock)&&c.status().open&&!c.status().raw);assert(c.capture());assert(files.count(c.status().neutralPath));assert(c.status().cinePath[0]==0);c.close();assert(!running&&!held);
 assert(c.open(Mode::Isp3Mp)&&!c.status().raw&&c.whiteBalance()==1&&c.autoExposure()&&!c.shutter().manual);
 {unsigned before=reads;unsigned calls=0;auto abort=[](void* p){return ++*static_cast<unsigned*>(p)>=1;};assert(!c.preview(abort,&calls)&&c.status().previewInterrupted&&reads==before&&!held);}
 {unsigned calls=0;auto abort=[](void* p){return ++*static_cast<unsigned*>(p)>=2;};assert(!c.preview(abort,&calls)&&c.status().previewInterrupted&&!held);}
 {unsigned calls=0;auto abort=[](void* p){return ++*static_cast<unsigned*>(p)>=3;};clockUs+=1000000;assert(!c.preview(abort,&calls)&&c.status().previewInterrupted&&!held);c.previewDrawn();assert(c.status().previewDrawAgeUs>=1000000);assert(c.preview()&&!c.status().previewInterrupted);}
 failTelemetry=true;assert(c.capture()&&c.shutter().valid&&c.autoExposure());{auto& bytes=files.at(c.status().metadataPath);std::string metadata(bytes.begin(),bytes.end());assert(metadata.find("\"sample_valid\":false")!=std::string::npos&&metadata.find("\"automatic\":true")!=std::string::npos);}
 c.adjustShutter(1);assert(c.autoExposure());failTelemetry=false;
 assert(c.capture()&&c.autoExposure());{auto& bytes=files.at(c.status().metadataPath);std::string metadata(bytes.begin(),bytes.end());assert(metadata.find("\"sample_valid\":true")!=std::string::npos&&metadata.find("live-after-frame")!=std::string::npos);}
 c.adjustShutter(1);assert(!c.autoExposure()&&c.shutter().requestedUs==12500);assert(c.resumeAutoExposure());
c.adjustShutter(99);assert(!c.autoExposure()&&c.shutter().manual&&c.shutter().requestedUs==100000);c.adjustShutter(-99);assert(c.shutter().requestedUs==1000);assert(c.capture());assert(files.count(c.status().neutralPath));{const auto& metadata=files.at(c.status().metadataPath);std::string text(metadata.begin(),metadata.end());assert(text.find("2048")!=std::string::npos&&text.find("1536")!=std::string::npos&&text.find("\"isp_vendor_saturation\":1")!=std::string::npos&&text.find("\"isp_experiment\":0")!=std::string::npos&&text.find("\"reference_profile\":\"none\"")!=std::string::npos);}c.close();
 assert(c.open(Mode::RawBinned));assert(c.parameters().color==2&&!c.parameters().filmic);assert(c.metering().valid&&c.metering().exposureThirds<0);const int firstDefault=c.metering().exposureThirds;
 raw.assign(raw.size(),16);assert(c.preview());assert(c.metering().exposureThirds==firstDefault);c.remeter();assert(c.preview());assert(c.metering().exposureThirds==6&&c.parameters().exposureThirds==6);
 c.adjustExposure(-3);assert(c.preview());assert(c.metering().exposureThirds==6&&c.parameters().exposureThirds==3);const auto meterTime=c.status().meterUs;
 unsigned before=reads;assert(c.capture());assert(reads==before&&c.status().zeroShutterLag&&c.status().toneUs==0&&c.status().frameUs==0&&c.status().meterUs==meterTime);
 {const auto& bytes=files.at(c.status().metadataPath);std::string text(bytes.begin(),bytes.end());assert(text.find("\"auto_thirds\":6")!=std::string::npos&&text.find("\"ec_thirds\":-3")!=std::string::npos);}
 assert(c.preview());clockUs+=600000;before=reads;assert(c.capture());assert(reads>before&&!c.status().zeroShutterLag);
 assert(c.preview());c.adjustExposure(1);before=reads;assert(c.capture());assert(reads>before&&!c.status().zeroShutterLag);c.close();
 assert(c.open(Mode::RawBinned)&&c.status().raw);c.adjustExposure(INT_MAX);assert(c.parameters().exposureThirds==6);c.adjustExposure(INT_MIN);assert(c.parameters().exposureThirds==-6);c.adjustExposure(1);
 assert(c.capture(true));assert(files.count(c.status().cinePath)&&!c.status().neutralPath[0]);assert(files.at(c.status().rawPath).size()==raw.size());assert(files.count(c.status().metadataPath));
 assert(!held);failWrite=true;assert(!c.capture());assert(!c.status().saved);c.close();assert(!running&&!held);failWrite=false;
 assert(c.open(Mode::RawNative)&&!held&&!c.parameters().filmic);before=reads;assert(c.capture()&&held&&reads>before&&!c.status().zeroShutterLag&&c.status().toneUs>0);c.retainPreviewRaw(true);assert(c.preview()&&held);before=reads;assert(c.capture(false)&&held&&reads==before);assert(files.count(c.status().cinePath)&&!c.status().neutralPath[0]);{const auto& b=files.at(c.status().metadataPath);std::string text(b.begin(),b.end());assert(text.find("Sunny D")!=std::string::npos&&text.find("none")!=std::string::npos&&text.find("\"look\":\"sRGB\"")!=std::string::npos&&text.find("\"software_filmic\":false")!=std::string::npos);}assert(c.preview()&&held);before=reads;expectedParams=c.parameters();checkFullPixels=true;assert(c.capture(true)&&held&&reads==before&&!checkFullPixels&&c.status().toneUs>0&&c.status().frameUs==0);assert(files.at(c.status().rawPath).size()==3145728);assert(c.status().zeroShutterLag);
 assert(c.preview()&&held);c.adjustExposure(-1);assert(c.capture()&&held);c.close();assert(!held&&!running);
 assert(c.open(Mode::RawNative)&&!held&&!c.parameters().filmic);auto exposure=c.parameters().exposureThirds;auto baseline=c.metering().exposureThirds;c.adjustShutter(99);assert(c.shutter().actualUs==100000&&c.shutter().gainQ4==37&&!held&&c.parameters().exposureThirds==exposure&&c.metering().exposureThirds==baseline);assert(!c.capture());clockUs+=1000000;assert(c.preview());assert(c.capture());{auto& b=files.at(c.status().metadataPath);std::string text(b.begin(),b.end());assert(text.find("\"requested_us\":100000")!=std::string::npos);}c.adjustShutter(-99);assert(c.shutter().actualUs==1000);clockUs+=1000000;assert(c.preview());c.close();
 c.setFilmic(true);c.close();assert(c.open(Mode::RawNative)&&!c.parameters().filmic);c.close();
 assert(!allocations&&!reserved);assert(c.open(Mode::RawNative,true)&&held);assert(c.verifyPattern());c.close();assert(!held&&!running&&!allocations&&!reserved);
}
'''.replace('format==5','format==5'))
 # RGB888 is 5 in the actual pinned API.
 with (p/'esp_camera.h').open('a') as f:f.write('constexpr int PIXFORMAT_RGB888=5;\n')
 subprocess.run(['c++','-std=c++17','-O1','-fsanitize=address,undefined','-I'+str(p),'-I'+str(r/'include'),str(p/'test.cpp'),str(r/'src/services/cine_camera.cpp'),str(r/'src/core/cine_tone.cpp'),'-o',str(p/'test')],check=True)
 subprocess.run([str(p/'test')],check=True)
print('PASS actual Cine capture service: printer quiescence, RAW verification refusal, vanilla capture, optional Bayer retention, stable startup metering, re-metering and manual offsets, exposure bounds, file failures and resource cleanup under ASan/UBSan. Hardware and JPEG codec mocked.')
