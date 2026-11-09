// Gated, session-local ISP comparison. No user preference or stock setting writes.
#include "services/cine_camera.h"
#include "core/cine_isp_seed.h"
#include "hal/hal_sd.h"
#include <esp_timer.h>
#include <string.h>
namespace services { namespace cine_camera {
bool Session::configureIsp(unsigned curve,int thirds,uint32_t us,unsigned gain,bool fitted,bool settled,unsigned vendorSaturation){
 if(!status_.open||!isIsp(mode_)||curve>2||vendorSaturation>1||(fitted&&vendorSaturation!=0)||(curve==2&&(!vendorGammaValid_||thirds!=0))||thirds< -6||thirds>6||us<1000||us>100000||gain<16||gain>64)return false;
 camera_.returnFrame();frameHeld_=false;
 hal::camera::IspExperiment request;request.shutterUs=us;request.gainQ4=gain;request.wbMode=wbPreset_;request.useSettledExposure=settled;
 if(curve==2)memcpy(request.gamma,vendorGamma_,16);else if(!core::cine::makeIspGamma(curve==1,thirds,request.gamma))return false;
 // Exact pinned OV3660 sensor_saturation_levels rows 0 and +1.
 // C0/B0 bypass SDE; saturation is implemented by the vendor CMX table.
 static const uint8_t vendor[2][11]={{29,96,3,12,120,132,125,107,18,1,152},{29,96,3,13,132,145,138,118,20,1,152}};
 memcpy(request.cmx,fitted?core::cine::ispFittedCmx:vendor[vendorSaturation],11);
 if(!camera_.configureIspExperiment(request,&ispReadback_)){close();snprintf(status_.message,sizeof(status_.message),"ISP experiment rejected; camera closed");return false;}
 ispCurve_=curve;ispFitted_=fitted;ispVendorSaturation_=vendorSaturation;shutter_=ispReadback_.shutter;params_.exposureThirds=thirds;
 shutterReadyUs_=esp_timer_get_time()+uint64_t(shutter_.frameUs)*2;status_.saved=false;previewBytes_=0;
 return true;
}
const char* Session::whiteBalanceName(unsigned preset){static const char* names[]={"Auto","Sunny","Cloudy","Office","Home"};return names[preset<=4?preset:0];}
void Session::nextWhiteBalance(){adjustWhiteBalance(1);}
void Session::adjustWhiteBalance(int delta){
 if(!status_.open||(mode_!=Mode::Detail&&mode_!=Mode::IspVga)||!delta)return;
 const auto previous=wbPreset_;const int64_t step=int64_t(wbPreset_>=1&&wbPreset_<=4?wbPreset_-1:0)+delta;wbPreset_=1+unsigned((step%4+4)%4);if(wbPreset_==previous)return;
 if(!configureIsp(0,0,shutter_.requestedUs,shutter_.gainQ4,false))wbPreset_=previous;
}
} }
