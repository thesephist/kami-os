#include "services/cine_camera.h"
#include "services/photo_storage.h"
#include "services/photo_print.h"
#include "hal/hal_sd.h"
#include "core/watchdog.h"
#include "core/cine_shutter.h"
#include "core/cine_frame.h"
#include <Arduino.h>
#include <esp_camera.h>
#include <esp_heap_caps.h>
#include <esp_timer.h>
#include <img_converters.h>
#include <new>
#include <string.h>
namespace services { namespace cine_camera {
namespace {
constexpr size_t kRaw=1024u*768,kBgr=640u*480*3,kJpeg=512u*1024;
void* allocate(size_t n){return heap_caps_malloc(n,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);}
struct Sink{explicit Sink(uint8_t* data):p(data){} uint8_t* p;size_t used=0;};
size_t output(void* arg,size_t index,const void* data,size_t n){
 auto& sink=*static_cast<Sink*>(arg);core::watchdog::feed();
 if(index!=sink.used || sink.used>kJpeg || n>kJpeg-sink.used || (!data&&n))return 0;
 if(n)memcpy(sink.p+sink.used,data,n);sink.used+=n;return n;
}
}
bool isIsp(Mode mode){return mode==Mode::Vanilla||mode==Mode::Detail||mode==Mode::IspVga;}
const char* modeName(Mode mode){return mode==Mode::FullRaw?"Cine RAW 3MP":mode==Mode::Detail?"Cine ISP 3MP":mode==Mode::IspVga?"Cine ISP VGA":mode==Mode::Vanilla?"Vanilla camera":mode==Mode::Pair?"RAW A/B pair":"Fast RAW";}
void Session::configure(bool neutral){if(tone_)tone_->configure(params_,neutral);}
bool Session::open(Mode mode,bool pattern,int wbOverride){
 const uint64_t startupAt=esp_timer_get_time();
 if(!close(false))return false;vendorGammaValid_=false;ispCurve_=-1;ispFitted_=false;ispVendorSaturation_=1;ispReadback_={};status_=Status{};shutter_={};shutterReadyUs_=0;mode_=mode;pattern_=pattern;outWidth_=mode==Mode::FullRaw?640:512;outHeight_=mode==Mode::FullRaw?480:384;params_=core::cine::Parameters{};params_.filmic=false;params_.color=(isIsp(mode)||pattern)?0:2;metering_={};meterPending_=!isIsp(mode)&&!pattern;
 if(!photo_print::readyForCapture()){snprintf(status_.message,sizeof(status_.message),"Printer task still finishing");close();return false;}
 if(!photos::initialize()||!hal::sd::ensureDir("/cine")){snprintf(status_.message,sizeof(status_.message),"SD unavailable");close();return false;}
 if(camera_.prepareCapture(false)!=camera::Session::PrepareResult::Ready){snprintf(status_.message,sizeof(status_.message),"Camera resources busy");close();return false;}
 hal::camera::Tuning tuning;
 int frameSize=FRAMESIZE_VGA,format=PIXFORMAT_JPEG;
 if(isIsp(mode)){vanilla_.load();tuning=vanilla_.tuning();if(mode==Mode::Detail)frameSize=FRAMESIZE_QXGA;if(mode!=Mode::Vanilla){tuning=hal::camera::Tuning{};tuning.wb_mode=1;tuning.allowNightMode=false;}tuning.testPattern=pattern;tuning.lenc=false;}
 else{
  tuning.bayer8=true;tuning.testPattern=pattern;tuning.wb_mode=1;tuning.allowNightMode=false;tuning.autofocus=false;
  tuning.hmirror=false;tuning.vflip=false;
  frameSize=mode==Mode::FullRaw?FRAMESIZE_QXGA:FRAMESIZE_XGA;format=PIXFORMAT_GRAYSCALE;
 }
 if(wbOverride>=1&&wbOverride<=4)tuning.wb_mode=wbOverride;wbPreset_=tuning.wb_mode;
 status_.startupPrepareUs=uint32_t(esp_timer_get_time()-startupAt);const uint64_t initAt=esp_timer_get_time();
 if(!camera_.start(frameSize,format,&tuning,(mode==Mode::Detail||mode==Mode::IspVga)?6:-1)){snprintf(status_.message,sizeof(status_.message),"Camera init failed: %s",camera_.lastError());close();return false;}
 status_.startupInitUs=uint32_t(esp_timer_get_time()-initAt);
 if(mode==Mode::Detail||mode==Mode::IspVga)vendorGammaValid_=hal::camera::readJpegGamma(vendorGamma_);
 const auto info=hal::camera::rawInfo();status_.raw=info.enabled;
 if(!isIsp(mode)&&!info.enabled){snprintf(status_.message,sizeof(status_.message),"RAW routing not verified");close();return false;}
 jpeg_=isIsp(mode)?nullptr:static_cast<uint8_t*>(allocate(kJpeg));
 preview_=static_cast<uint16_t*>(allocate(previewWidth*previewHeight*2));
 if(!isIsp(mode)){
  if(mode!=Mode::FullRaw)raw_=rawOwned_=static_cast<uint8_t*>(allocate(kRaw));bgr_=static_cast<uint8_t*>(allocate(kBgr));
  void* memory=allocate(sizeof(core::cine::ToneMap));if(memory)tone_=new(memory)core::cine::ToneMap;
  params_.black=info.black;for(auto& gain:params_.gains)gain=1024;configure();
 }
 if((!isIsp(mode)&&!jpeg_)||!preview_||(!isIsp(mode)&&((mode!=Mode::FullRaw&&!raw_)||!bgr_||!preview_||!tone_))){snprintf(status_.message,sizeof(status_.message),"Image buffer allocation failed");close();return false;}
 const uint64_t aeAt=esp_timer_get_time();
 if(mode==Mode::Detail&&!pattern){
  // Observe AE over distinct frames rather than always sleeping 1.1 seconds.
  // Require three stable transitions and >=450ms; retain the old maximum.
  hal::camera::ShutterInfo previous{};unsigned stable=0;
  while(esp_timer_get_time()-aeAt<1100000){
   hal::camera::CameraFrame frame{};if(!camera_.read(&frame))break;camera_.returnFrame();
   hal::camera::ShutterInfo value{};
   if(hal::camera::readJpegShutter(&value)&&value.valid){
    const uint64_t before=uint64_t(previous.actualUs)*previous.gainQ4,now=uint64_t(value.actualUs)*value.gainQ4;
    const uint64_t difference=before>now?before-now:now-before;
    stable=before&&difference*100<=before*3?stable+1:0;previous=value;
    if(stable>=3&&esp_timer_get_time()-aeAt>=450000){status_.startupAeStable=true;break;}
   }else stable=0;
  }
 }else camera_.settle(mode==Mode::FullRaw?1100:700);
 status_.startupAeUs=uint32_t(esp_timer_get_time()-aeAt);
 if(!isIsp(mode)&&!pattern){
  if(!camera_.lockRawShutter(&shutter_)){snprintf(status_.message,sizeof(status_.message),"Shutter control unavailable");close();return false;}
  // Discard transitional frames once at opening, never at the shutter button.
  camera_.settle((shutter_.frameUs*2+999)/1000+30);
 }
 const uint64_t ispAt=esp_timer_get_time();status_.open=true;
 if((mode==Mode::Detail||mode==Mode::IspVga)&&!configureIsp(0,0,16667,16,false,true))return false;
 status_.startupIspUs=uint32_t(esp_timer_get_time()-ispAt);const uint64_t previewAt=esp_timer_get_time();
 snprintf(status_.message,sizeof(status_.message),"C/RB: capture   Wheel: shutter");
 // Keep the two-frame freshness guard after ISP writes. Drain transitional
 // frames until an actual fresh preview is decoded, with a bounded deadline.
 bool first=false;do{first=preview();}while(!first&&shutterReadyUs_&&uint64_t(esp_timer_get_time())<shutterReadyUs_+uint64_t(shutter_.frameUs)*3+250000);
 if(!first){snprintf(status_.message,sizeof(status_.message),"Initial camera frame unavailable");close();return false;}
 status_.startupPreviewUs=uint32_t(esp_timer_get_time()-previewAt);status_.startupUs=uint32_t(esp_timer_get_time()-startupAt);
 Serial.printf("[cine-startup] total_us=%lu prepare_us=%lu init_us=%lu ae_us=%lu isp_us=%lu first_preview_us=%lu ae_stable=%u\n",(unsigned long)status_.startupUs,(unsigned long)status_.startupPrepareUs,(unsigned long)status_.startupInitUs,(unsigned long)status_.startupAeUs,(unsigned long)status_.startupIspUs,(unsigned long)status_.startupPreviewUs,status_.startupAeStable);
 return true;
}
bool Session::close(bool restoreUi){
 status_.open=false;cacheReady_=false;
 camera_.returnFrame();frameHeld_=false;camera_.stop();
 // A failed I2C teardown still owns the driver/DMA. Retain buffers and the
 // peripheral reservation until a later close/open can complete teardown.
 if(camera_.running()){snprintf(status_.message,sizeof(status_.message),"Camera cleanup pending; retry");return false;}
 releaseIspPreview();
 if(tone_){tone_->~ToneMap();heap_caps_free(tone_);tone_=nullptr;}
 heap_caps_free(rawOwned_);rawOwned_=nullptr;heap_caps_free(bgr_);heap_caps_free(jpeg_);heap_caps_free(preview_);
 cacheReady_=false;cachedFrameUs_=lastPreviewDoneUs_=0;referencePreview_=retainPreviewRaw_=false;
 raw_=bgr_=jpeg_=nullptr;preview_=nullptr;previewBytes_=rawBytes_=0;
 if(restoreUi)camera_.restoreUi(camera::Session::Resume::DeferredWithInputRecovery);
 return true;
}
bool Session::preview(){
 if(!status_.open)return false;hal::camera::CameraFrame frame{};
 if(frameHeld_){camera_.returnFrame();frameHeld_=false;raw_=nullptr;}
 const uint64_t readStarted=esp_timer_get_time();
 if(!camera_.read(&frame))return false;
 status_.previewReadUs=uint32_t(esp_timer_get_time()-readStarted);
 if(shutterReadyUs_&&frame.dmaStartUs<shutterReadyUs_){camera_.returnFrame();return false;}
 bool ok=false;
 if(isIsp(mode_)){
  ok=frame.format==PIXFORMAT_JPEG&&core::cine::validJpeg(frame.buf,frame.len,frame.width,frame.height,mode_==Mode::Detail?2048:640,mode_==Mode::Detail?1536:480);
  if(ok){width_=frame.width;height_=frame.height;cachedFrameUs_=frame.dmaStartUs;const uint64_t at=esp_timer_get_time();ok=renderIspPreview(frame);status_.previewUs=uint32_t(esp_timer_get_time()-at);}
 }else{
  const bool full=mode_==Mode::FullRaw;
  ok=frame.buf&&frame.format==PIXFORMAT_RAW&&frame.width==(full?2048:1024)&&frame.height==(full?1536:768)&&frame.len==(full?3145728:kRaw);
  if(ok){if(full){raw_=const_cast<uint8_t*>(frame.buf);frameHeld_=true;}else memcpy(raw_,frame.buf,frame.len);rawBytes_=frame.len;width_=frame.width;height_=frame.height;cachedFrameUs_=frame.dmaStartUs;}
  if(!frameHeld_)camera_.returnFrame();
  if(ok&&meterPending_){
   const uint64_t started=esp_timer_get_time();
   metering_=core::cine::meterBayer(raw_,rawBytes_,width_,height_,params_.black);
   status_.meterUs=uint32_t(esp_timer_get_time()-started);meterPending_=false;
   params_.exposureThirds=metering_.exposureThirds;configure();
  }
  const bool direct=full&&!referencePreview_;
  if(ok){const uint64_t began=esp_timer_get_time();ok=direct?tone_->preview565(raw_,rawBytes_,width_,height_,preview_,previewWidth,previewHeight):renderRaw();status_.previewUs=uint32_t(esp_timer_get_time()-began);}
  if(ok&&!direct){
   for(unsigned y=0;y<previewHeight;++y)for(unsigned x=0;x<previewWidth;++x){
    const size_t i=(size_t(y*outHeight_/previewHeight)*outWidth_+x*outWidth_/previewWidth)*3;
    const uint8_t b=bgr_[i],g=bgr_[i+1],r=bgr_[i+2];preview_[y*previewWidth+x]=uint16_t((r>>3)<<11|(g>>2)<<5|(b>>3));
   }
  }
  cacheReady_=ok;
  // Let the single DMA buffer refill while the UI composes the display and
  // handles input. Holding it until the next tick can miss a sensor VSYNC.
  if(direct&&!pattern_&&!retainPreviewRaw_&&frameHeld_){camera_.returnFrame();frameHeld_=false;raw_=nullptr;cacheReady_=false;}
  if(ok){++status_.frames;const uint64_t done=esp_timer_get_time();status_.previewAgeUs=done>=cachedFrameUs_?uint32_t(done-cachedFrameUs_):0;status_.previewIntervalUs=lastPreviewDoneUs_?uint32_t(done-lastPreviewDoneUs_):0;lastPreviewDoneUs_=done;}else snprintf(status_.message,sizeof(status_.message),"Unexpected sensor frame format");return ok;
 }
 camera_.returnFrame();if(ok){++status_.frames;const uint64_t done=esp_timer_get_time();status_.previewAgeUs=done>=cachedFrameUs_?uint32_t(done-cachedFrameUs_):0;status_.previewIntervalUs=lastPreviewDoneUs_?uint32_t(done-lastPreviewDoneUs_):0;lastPreviewDoneUs_=done;}else snprintf(status_.message,sizeof(status_.message),"Unexpected sensor frame format");return ok;
}
void Session::previewDrawn(){const uint64_t now=esp_timer_get_time();if(cachedFrameUs_&&now>=cachedFrameUs_)status_.previewDrawAgeUs=uint32_t(now-cachedFrameUs_);}
void Session::referencePreview(bool enabled){referencePreview_=enabled;cacheReady_=false;lastPreviewDoneUs_=0;}
void Session::adjustShutter(int delta){
 if(mode_==Mode::Vanilla||pattern_||!status_.open||!shutter_.valid||!delta)return;
 int step=int(core::cine::nearestShutter(shutter_.requestedUs));
 int next=delta< -int(core::cine::shutterCount)?0:delta>int(core::cine::shutterCount)?int(core::cine::shutterCount)-1:step+delta;
 if(next<0)next=0;if(next>=int(core::cine::shutterCount))next=core::cine::shutterCount-1;if(next==step&&shutter_.requestedUs==core::cine::shutterSpeedsUs[next])return;
 if(frameHeld_){camera_.returnFrame();frameHeld_=false;raw_=nullptr;}
 cacheReady_=false;
 if(isIsp(mode_)){configureIsp(0,0,core::cine::shutterSpeedsUs[next],shutter_.gainQ4,false);return;}
 if(!camera_.setRawShutter(core::cine::shutterSpeedsUs[next],&shutter_)){shutter_.valid=false;snprintf(status_.message,sizeof(status_.message),"Shutter update failed; reopen camera");return;}
 shutterReadyUs_=esp_timer_get_time()+uint64_t(shutter_.frameUs)*2;
 status_.saved=false;
}
void Session::adjustExposure(int delta){if(isIsp(mode_))return;int64_t ev=int64_t(params_.exposureThirds)+delta;params_.exposureThirds=ev< -6?-6:ev>6?6:ev;cacheReady_=false;configure();}
void Session::remeter(){if(isIsp(mode_)||pattern_)return;meterPending_=true;cacheReady_=false;}
void Session::setColor(unsigned color){if(isIsp(mode_)||pattern_)return;params_.color=color<=2?color:0;cacheReady_=false;configure();}
void Session::setFilmic(bool enabled){if(isIsp(mode_)||pattern_)return;params_.filmic=enabled;cacheReady_=false;configure();}
void Session::nextLook(){if(isIsp(mode_))return;params_.look=(params_.look+1)%3;cacheReady_=false;configure();}
bool Session::renderRaw(){
 return mode_==Mode::FullRaw?tone_->renderBgrArea(raw_,rawBytes_,width_,height_,bgr_,kBgr,outWidth_,outHeight_):tone_->renderBgr(raw_,rawBytes_,width_,height_,bgr_,kBgr);
}
bool Session::verifyPattern() const {
 if(!pattern_||!raw_||rawBytes_!=size_t(width_)*height_)return false;
 for(unsigned y=2;y<height_;++y)if(memcmp(raw_+size_t(y)*width_,raw_+size_t(y&1)*width_,width_)){Serial.printf("[cine-pattern] mismatch_row=%u\n",y);return false;}
 return true;
}
bool Session::saveRender(const char* path,bool neutral,bool reuse){
 uint64_t at=esp_timer_get_time();
 if(!reuse){
  if(mode_==Mode::Pair||neutral)configure(neutral);
  if(!renderRaw())return false;
  status_.toneUs+=uint32_t(esp_timer_get_time()-at);
 }
 at=esp_timer_get_time();Sink sink{jpeg_};
 if(!fmt2jpg_cb(bgr_,size_t(outWidth_)*outHeight_*3,outWidth_,outHeight_,PIXFORMAT_RGB888,90,output,&sink))return false;
 status_.encodeUs+=uint32_t(esp_timer_get_time()-at);at=esp_timer_get_time();
 const bool ok=hal::sd::writeFile(path,jpeg_,sink.used);status_.saveUs+=uint32_t(esp_timer_get_time()-at);
 if(!ok)hal::sd::removeFile(path);return ok;
}
bool Session::capture(bool saveRaw,bool compareNative){
 if(!status_.open)return false;const uint64_t started=esp_timer_get_time();
 if(mode_!=Mode::Vanilla&&!pattern_&&(!shutter_.valid||started<shutterReadyUs_)){snprintf(status_.message,sizeof(status_.message),shutter_.valid?"Updating shutter; wait for preview":"Reopen camera for shutter control");return false;}
 status_.saved=false;status_.toneUs=status_.encodeUs=status_.saveUs=status_.frameUs=0;
 status_.cinePath[0]=status_.neutralPath[0]=status_.rawPath[0]=0;
 auto target=photos::prepare();if(!target.valid()){snprintf(status_.message,sizeof(status_.message),"Could not create photo");return false;}
 status_.sequence=target.sequence;
 const bool cached=!isIsp(mode_)&&cacheReady_&&started>=int64_t(cachedFrameUs_)&&uint64_t(started)-cachedFrameUs_<=500000;
 status_.zeroShutterLag=cached;status_.frameAgeUs=cached?uint32_t(started-cachedFrameUs_):0;
 hal::camera::CameraFrame frame{};bool acquired=cached;
 if(!cached&&frameHeld_){camera_.returnFrame();frameHeld_=false;raw_=nullptr;}
 // Discard only stale queued frames; no sensor restart or AE/AWB retuning at shutter time.
 for(unsigned i=0;!cached&&i<4;++i){core::watchdog::feed();if(!camera_.read(&frame))break;if(frame.dmaStartUs+25000>=started&&frame.dmaStartUs>=shutterReadyUs_){acquired=true;break;}camera_.returnFrame();}
 status_.frameUs=cached?0:uint32_t(esp_timer_get_time()-started);
 if(!acquired){snprintf(status_.message,sizeof(status_.message),"Fresh sensor frame unavailable");return false;}
 bool ok=false;
 if(isIsp(mode_)){
  const uint64_t at=esp_timer_get_time();ok=frame.format==PIXFORMAT_JPEG&&core::cine::validJpeg(frame.buf,frame.len,frame.width,frame.height,mode_==Mode::Detail?2048:640,mode_==Mode::Detail?1536:480)&&photos::write(target,frame.buf,frame.len);width_=frame.width;height_=frame.height;
  camera_.returnFrame();status_.saveUs=uint32_t(esp_timer_get_time()-at);snprintf(status_.neutralPath,sizeof(status_.neutralPath),"%s",target.path);
 }else{
  const bool full=mode_==Mode::FullRaw;
  ok=cached||(frame.buf&&frame.format==PIXFORMAT_RAW&&frame.len==(full?3145728:kRaw)&&frame.width==(full?2048:1024)&&frame.height==(full?1536:768));
  if(ok&&!cached){if(full){raw_=const_cast<uint8_t*>(frame.buf);frameHeld_=true;}else memcpy(raw_,frame.buf,frame.len);rawBytes_=frame.len;width_=frame.width;height_=frame.height;cachedFrameUs_=frame.dmaStartUs;}
  if(!cached&&!frameHeld_)camera_.returnFrame();
  if(ok&&compareNative&&mode_!=Mode::Pair){
   const auto selected=params_.color;params_.color=0;configure();
   snprintf(status_.neutralPath,sizeof(status_.neutralPath),"%s",target.path);ok=saveRender(target.path,false);
   params_.color=selected;configure();
   if(ok)target=photos::prepare();ok=ok&&target.valid();
  }
  if(ok&&mode_==Mode::Pair){
   snprintf(status_.neutralPath,sizeof(status_.neutralPath),"%s",target.path);ok=saveRender(target.path,true);
   if(ok)target=photos::prepare();ok=ok&&target.valid();
  }
  if(ok){snprintf(status_.cinePath,sizeof(status_.cinePath),"%s",target.path);ok=saveRender(target.path,false,cached&&!compareNative&&mode_==Mode::Cine);}
  if(ok&&saveRaw){snprintf(status_.rawPath,sizeof(status_.rawPath),"/cine/RAW_%05lu.bayer",(unsigned long)status_.sequence);ok=hal::sd::writeFile(status_.rawPath,raw_,rawBytes_);}
  if(!ok)configure();cacheReady_=false;
 }
 status_.totalUs=uint32_t(esp_timer_get_time()-started);status_.saved=ok;
 snprintf(status_.metadataPath,sizeof(status_.metadataPath),"/cine/SHOT_%05lu.json",(unsigned long)status_.sequence);
 auto sensorInfo=hal::camera::rawInfo();if(ispCurve_>=0&&ispReadback_.valid)for(unsigned c=0;c<3;++c)sensorInfo.gains[c]=ispReadback_.wb[c];
 char metadata[2400];
 const int n=snprintf(metadata,sizeof(metadata),
  "{\"version\":1,\"mode\":\"%s\",\"source\":\"%s\",\"bayer\":\"%s\",\"bits\":%u,\"output_width\":%u,\"output_height\":%u,\"reconstruction\":\"%s\",\"raw_width\":%u,\"raw_height\":%u,\"black\":%u,\"wb_q10\":[%u,%u,%u],\"sensor_wb_q10\":[%u,%u,%u],\"preview_render_us\":%lu,\"preview_path\":\"%s\",\"preview_dma_age_us\":%lu,\"preview_draw_age_us\":%lu,\"preview_interval_us\":%lu,\"color_profile\":\"%s\",\"color_version\":%u,\"color_domain\":\"%s\",\"relative_wb_rgb\":%s,\"wb_preset\":%u,\"reference_profile\":\"%s\",\"shutter\":{\"manual\":%s,\"requested_us\":%lu,\"actual_us\":%lu,\"integration_ticks_q4\":%lu,\"gain_q4\":%u,\"hts\":%u,\"vts\":%u,\"clock_hz\":%lu},\"isp_experiment\":%d,\"isp_fitted_matrix\":%s,\"isp_vendor_saturation\":%d,\"software_filmic\":%s,\"tone_version\":2,\"exposure_thirds\":%d,\"meter\":{\"method\":\"%s\",\"valid\":%s,\"samples\":%u,\"auto_thirds\":%d,\"ec_thirds\":%d,\"geometric_mean\":%.6f,\"arithmetic_mean\":%.6f,\"highlight_p98\":%.6f,\"highlight_limited\":%s,\"time_us\":%lu},\"look\":\"%s\",\"zero_shutter_lag\":%s,\"frame_age_us\":%lu,\"software_mirror\":%s,\"pattern\":%s,\"cine\":\"%s\",\"neutral\":\"%s\",\"raw\":\"%s\",\"timing_us\":{\"frame\":%lu,\"tone\":%lu,\"encode\":%lu,\"save\":%lu,\"total\":%lu},\"vanilla_profile\":%u,\"saved\":%s}",
  modeName(mode_),status_.raw?"OV3660 RAW after DPC":"sensor ISP JPEG",status_.raw?"BGGR":"none",status_.raw?8:0,status_.raw?outWidth_:width_,status_.raw?outHeight_:height_,mode_==Mode::FullRaw?"Bayer cells + linear Q12 area before tone":"native",width_,height_,params_.black,params_.gains[0],params_.gains[1],params_.gains[2],sensorInfo.gains[0],sensorInfo.gains[1],sensorInfo.gains[2],(unsigned long)status_.previewUs,isIsp(mode_)?"scaled JPEG IDCT to RGB565":mode_==Mode::FullRaw&&!referencePreview_?"direct Bayer cell to RGB565":"full output then scale",(unsigned long)status_.previewAgeUs,(unsigned long)status_.previewDrawAgeUs,(unsigned long)status_.previewIntervalUs,isIsp(mode_)?"sensor-ISP":core::cine::colorName(params_.color),params_.color==2?2u:1u,isIsp(mode_)?"sensor-ISP":params_.color==2?"display-sRGB":"scene-linear",!isIsp(mode_)&&params_.color==1?"[0.92,1.0,1.12]":"[1.0,1.0,1.0]",wbPreset_,mode_==Mode::Pair?"native neutral sRGB":compareNative?"A1 with same tone/exposure":"none",shutter_.valid&&shutter_.manual?"true":"false",(unsigned long)shutter_.requestedUs,(unsigned long)shutter_.actualUs,(unsigned long)shutter_.ticks,shutter_.gainQ4,shutter_.hts,shutter_.vts,(unsigned long)shutter_.clockHz,ispCurve_,(ispCurve_>=0&&ispFitted_)?"true":"false",ispCurve_>=0&&!ispFitted_?int(ispVendorSaturation_):-1,(!isIsp(mode_)&&params_.filmic)?"true":"false",params_.exposureThirds,isIsp(mode_)||pattern_?"none":"trimmed-log-mean-startup",metering_.valid?"true":"false",metering_.samples,metering_.exposureThirds,int(params_.exposureThirds)-metering_.exposureThirds,metering_.geometricMean,metering_.arithmeticMean,metering_.highlight,metering_.highlightLimited?"true":"false",(unsigned long)status_.meterUs,(params_.filmic?core::cine::lookName(params_.look):"sRGB"),cached?"true":"false",(unsigned long)status_.frameAgeUs,status_.raw?"true":"false",pattern_?"true":"false",status_.cinePath,status_.neutralPath,status_.rawPath,(unsigned long)status_.frameUs,(unsigned long)status_.toneUs,(unsigned long)status_.encodeUs,(unsigned long)status_.saveUs,(unsigned long)status_.totalUs,unsigned(vanilla_.mode),ok?"true":"false");
 const bool metadataOk=n>0&&size_t(n)<sizeof(metadata)&&hal::sd::writeFile(status_.metadataPath,reinterpret_cast<const uint8_t*>(metadata),n);
 snprintf(status_.message,sizeof(status_.message),ok?(metadataOk?"Saved %lu ms | view %lu ms old":"Photo saved; metadata failed"):"Save failed; partial files retained",(unsigned long)(status_.totalUs/1000),(unsigned long)(status_.frameAgeUs/1000));
 Serial.printf("[cine] saved=%u mode=%s sequence=%lu frame_us=%lu tone_us=%lu encode_us=%lu save_us=%lu total_us=%lu cine=%s neutral=%s raw=%s meta=%s\n",ok,modeName(mode_),(unsigned long)status_.sequence,(unsigned long)status_.frameUs,(unsigned long)status_.toneUs,(unsigned long)status_.encodeUs,(unsigned long)status_.saveUs,(unsigned long)status_.totalUs,status_.cinePath,status_.neutralPath,status_.rawPath,status_.metadataPath);
 return ok;
}
} }
