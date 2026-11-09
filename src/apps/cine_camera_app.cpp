#include "apps/cine_camera_app.h"
#include "services/cine_camera.h"
#include "core/cine_controls.h"
#include "services/photo_print.h"
#include "hal/hal_display.h"
#include "nav_handler.h"
#include "scene/screen.h"
#include "ui/scene.h"
#include "ui/edit_field.h"
#include "ui/cine_camera_view.h"
#include "ui/draw.h"
#include "ui/nav_controls.h"
#include "ui/list_view.h"
#include "config/ui_theme.h"
#include <Arduino.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>
namespace apps { namespace cine_camera {
namespace {
namespace cine=services::cine_camera;
cine::Session session;
core::cine::WheelControl wheel;
ui::ListView settingsList;bool menuEditing=false;
enum class View{Menu,Opening,Preview,Error};
cine::Mode requestedMode=cine::Mode::Detail;
View current=View::Menu;
unsigned feedbackStage=0;uint32_t feedbackAt=0;
bool parseInteger(const char* text,long minimum,long maximum,long& value){
 if(!text||!*text)return false;char* end=nullptr;errno=0;value=strtol(text,&end,10);
 return !errno&&end&&!*end&&value>=minimum&&value<=maximum;
}
bool active=false,dirty=true,pattern=false,saveRaw=false;
// Native preview samples directly to screen resolution. Keep the idle gap
// short; the borrowed RAW frame retains the unchanged 500 ms capture limit.
uint32_t requestedAt=0,lastPreview=0;int wbOverride=-1;
constexpr uint16_t bg=0xFFFF,fg=0x0000,muted=0x632C;
const char* viewName(){return current==View::Preview?"preview":current==View::Opening?"opening":current==View::Error?"error":"menu";}
void invalidate(){dirty=true;hal::display::markAllDirty();}
bool close(bool keepReservation=false){const bool ok=session.close(!keepReservation);current=ok?View::Menu:View::Error;feedbackStage=0;invalidate();return ok;}
void begin();
void draw(TFT_eSprite&);
void activate(){active=true;requestedMode=cine::Mode::Detail;pattern=saveRaw=false;feedbackStage=0;wbOverride=-1;begin();}
void exit(){close();active=false;}
void cover(){close();}
void begin(){wheel.reset();services::photo_print::cancelEndpointPoll();current=View::Opening;requestedAt=millis();invalidate();ui::showAppOpeningSplash("Cine camera");}
void snap(bool compare=false){
 if(current!=View::Preview)return;
 feedbackStage=1;feedbackAt=millis();invalidate();draw(hal::display::canvas());hal::display::flush();
 const bool saved=session.capture(saveRaw,compare);feedbackStage=saved?5:6;feedbackAt=millis();
 invalidate();draw(hal::display::canvas());hal::display::flush();
}
void tick(uint32_t){
 const uint32_t now=millis();
 if(wheel.expire(now))invalidate();
 if(feedbackStage&&feedbackStage!=1&&uint32_t(now-feedbackAt)>=1800u){feedbackStage=0;invalidate();}
 if(current==View::Opening){
  if(services::photo_print::readyForCapture()){
   current=session.open(requestedMode,pattern,wbOverride)?View::Preview:View::Error;
   if(current==View::Error)session.close();lastPreview=millis();invalidate();
  }else if(uint32_t(now-requestedAt)>15000){current=View::Error;invalidate();}
 }else if(current==View::Preview&&uint32_t(now-lastPreview)>=1u){
  if(session.preview())invalidate();lastPreview=millis();
 }
}
void shutterValue(char* text,size_t bytes){
 if(session.shutter().valid&&session.shutter().actualUs)snprintf(text,bytes,"1/%lu",(unsigned long)((1000000+session.shutter().actualUs/2)/session.shutter().actualUs));
 else snprintf(text,bytes,"Auto");
}
ui::ListItem settingsItem(int index,void*){
 static char label[64];char shutter[20];shutterValue(shutter,sizeof(shutter));
 if(index==0)snprintf(label,sizeof(label),"Shutter  %s",shutter);
 else if(index==1)snprintf(label,sizeof(label),"WB  %s",cine::Session::whiteBalanceName(session.whiteBalance()));
 else snprintf(label,sizeof(label),"Back to camera");
 ui::ListItem item;item.label=label;item.fg=fg;item.opensScreen=index==2;item.editing=menuEditing&&index==settingsList.selected;return item;
}
void wheelValue(char* text,size_t bytes){
 if(wheel.whiteBalance())snprintf(text,bytes,"%s",cine::Session::whiteBalanceName(session.whiteBalance()));
 else if(session.shutter().valid&&session.shutter().actualUs)snprintf(text,bytes,"1/%lu",(unsigned long)((1000000+session.shutter().actualUs/2)/session.shutter().actualUs));
 else snprintf(text,bytes,"AUTO");
}
void build(ui::scene::Scene& view,int16_t w,int16_t h){
 view.clear();ui::scene::setCurrentScene(&view);
 if(current==View::Preview){
  view.addRect("cine.preview",{0,0,w,h},0);
  auto* gear=view.addBitmap("cine.settings_button",{18,14,20,20},ui::cine_view::settingsMask,20,20,0xffff,0);if(gear)gear->bitmapTransparent=true;
  view.addLabel("cine.controls",{52,196,120,20},"B: PHOTO",0xffff,0,1);
  if(wheel.opacity(millis())){
   const uint16_t ss=wheel.whiteBalance()?0xffff:kUiEdit,wb=wheel.whiteBalance()?kUiEdit:0xffff;
   char shutter[20];shutterValue(shutter,sizeof(shutter));
   view.addLabel("cine.wheel_shutter_label",{142,52,26,12},"SS",ss,0,1);
   view.addLabel("cine.wheel_wb_label",{142,82,26,12},"WB",wb,0,1);
   view.addLabel("cine.wheel_shutter_value",{178,46,90,26},shutter,ss,0,2);
   view.addLabel("cine.wheel_wb_value",{178,76,90,26},cine::Session::whiteBalanceName(session.whiteBalance()),wb,0,2);
  }
  if(feedbackStage)view.addLabel("cine.feedback",{52,196,180,20},feedbackStage==1?"CAPTURE":feedbackStage==5?"SAVED":"SAVE FAILED",0xffff,0,1);
  return;
 }
 view.addRect("cine.bg",{0,38,w,(int16_t)(h-38)},bg);
 if(current==View::Menu){
  settingsList.totalItems=3;
  settingsList.buildScene(view,0,38,w,h-38,settingsItem,nullptr,kUiSelBg,kUiSelFg,bg,muted,fg);return;
 }
 view.addLabel("cine.title",{0,55,w,26},"Cine camera",fg,bg,2,ui::scene::TextAlign::MiddleCenter);
 view.addLabel("cine.status",{8,96,(int16_t)(w-16),50},current==View::Opening?"Preparing camera...":session.status().message,fg,bg,1,ui::scene::TextAlign::MiddleCenter);
 view.addLabel("cine.controls",{0,212,w,18},"A: return to settings",muted,bg,1,ui::scene::TextAlign::MiddleCenter);
}
struct PreviewCanvas{
 TFT_eSprite& surface;
 uint16_t at(int x,int y)const{return surface.readPixel(x,y);}
 uint16_t readPixel(int x,int y)const{return surface.readPixel(x,y);}
 void fillRect(int x,int y,int w,int h,uint16_t ink){surface.fillRect(x,y,w,h,ink);}
 void pushImage(int x,int y,int w,int h,uint16_t* pixels){const bool swap=surface.getSwapBytes();surface.setSwapBytes(true);surface.pushImage(x,y,w,h,pixels);surface.setSwapBytes(swap);}
};
void draw(TFT_eSprite& canvas){
 if(!dirty)return;auto& view=ui::scene::scratchScene();build(view,canvas.width(),canvas.height());
 if(current==View::Preview){
  PreviewCanvas target{canvas};
  ui::cine_view::image(target,session.pixels(),cine::Session::previewWidth,cine::Session::previewHeight,feedbackStage==1);
  char shutter[20];shutterValue(shutter,sizeof(shutter));
  ui::cine_view::chrome(target,cine::Session::whiteBalanceName(session.whiteBalance()),shutter,feedbackStage*1000+(millis()-feedbackAt<999?millis()-feedbackAt:999),session.mode()!=cine::Mode::Vanilla,wheel.whiteBalance(),wheel.opacity(millis()));
  session.previewDrawn();
 }else view.render(canvas);
 dirty=false;
}
AppPresentation presentation(){return current==View::Preview?AppPresentation{true,true,AppPresentation::StatusBar::Hidden}:AppPresentation{};}

void input(hal::input::InputEvent e){
 using ET=hal::input::EventType;
 if(e.type==ET::ButtonA_Tap&&current==View::Preview){current=View::Menu;menuEditing=false;settingsList.totalItems=3;settingsList.selected=wheel.whiteBalance()?1:0;invalidate();return;}
 if(current==View::Menu){
  if(ui::nav::isBackTap(e)){if(menuEditing)menuEditing=false;else if(session.status().open)current=View::Preview;else begin();invalidate();return;}
  if(e.type==ET::Scroll){
   if(menuEditing){if(settingsList.selected==0)session.adjustShutter(-int(e.delta));else session.adjustWhiteBalance(int(e.delta));if(!session.status().open)current=View::Error;}
   else settingsList.scroll(e.delta);invalidate();
  }else if(ui::nav::isEnterTap(e)){
   if(settingsList.selected==2){menuEditing=false;if(session.status().open)current=View::Preview;else begin();}
   else menuEditing=!menuEditing;invalidate();
  }return;
 }
 if(ui::nav::isBackTap(e)){NavHandler::instance().pop();return;}
 if(current!=View::Preview)return;
 if(e.type==ET::ButtonB_Tap)snap();
 else if(ui::nav::isEnterTap(e)){wheel.toggle(millis());invalidate();}
 else if(e.type==ET::Scroll){if(!e.delta)return;if(wheel.whiteBalance())session.adjustWhiteBalance(int(e.delta));else session.adjustShutter(-int(e.delta));wheel.show(millis());if(!session.status().open)current=View::Error;invalidate();}
}
core::health::RuntimePolicy policy(){bool busy=current==View::Preview||current==View::Opening||session.holdsResources();auto p=core::health::whileBusy(busy);p.allowBackgroundUploads=!busy;p.allowAutomaticUpdate=!busy;return p;}
const ::scene::Screen screen={"Cine camera",invalidate,nullptr,tick,input,nullptr,draw,"main",build,&bg};
const ::scene::Screen* const screens[]={&screen};
}
App app=[] {App a=SCENE_APP_CATALOG_LIFECYCLE_HOOKS("Cine camera","cine-camera",&screen,activate,invalidate,exit,"src/apps/cine_camera_app.cpp",screens,presentation,policy);a.onCover=cover;return a;}();
bool handleSerialCommand(const char* line,Print& output){
 if(strncmp(line,"cine ",5))return false;
 if(!strcmp(line,"cine open")){NavHandler::instance().push(&app);requestedMode=cine::Mode::Detail;begin();return true;}
 if(!strcmp(line,"cine status")){
  const auto& s=session.status();output.printf("[cine] status active=%u state=%s raw=%u frames=%u sequence=%lu saved=%u total_us=%lu cine=%s neutral=%s raw_path=%s meta=%s\n",active,viewName(),s.raw,s.frames,(unsigned long)s.sequence,s.saved,(unsigned long)s.totalUs,s.cinePath,s.neutralPath,s.rawPath,s.metadataPath);return true;
 }
 if(!strcmp(line,"cine sensor")){
  hal::camera::ShutterInfo value;const bool ok=hal::camera::readJpegShutter(&value);
  output.printf("[cine-sensor] ok=%u manual=%u actual_us=%lu gain_q4=%u hts=%u vts=%u clock_hz=%lu frame_us=%lu\n",ok,value.manual,(unsigned long)value.actualUs,value.gainQ4,value.hts,value.vts,(unsigned long)value.clockHz,(unsigned long)value.frameUs);return true;
 }
 if(!active){output.println("[cine] rejected: open Cine camera first");return true;}
 if(!strcmp(line,"cine controls")){output.printf("[cine-controls] mode=%s popup=%u\n",wheel.whiteBalance()?"wb":"shutter",wheel.visible());return true;}
 if(!strcmp(line,"cine startup")){const auto& s=session.status();output.printf("[cine-startup] total_us=%lu prepare_us=%lu init_us=%lu ae_us=%lu isp_us=%lu first_preview_us=%lu ae_stable=%u\n",(unsigned long)s.startupUs,(unsigned long)s.startupPrepareUs,(unsigned long)s.startupInitUs,(unsigned long)s.startupAeUs,(unsigned long)s.startupIspUs,(unsigned long)s.startupPreviewUs,s.startupAeStable);return true;}
 if(!strcmp(line,"cine latency")){const auto& s=session.status();output.printf("[cine] latency reference=%u frames=%u read_us=%lu render_us=%lu dma_age_us=%lu drawn_age_us=%lu interval_us=%lu\n",session.usesReferencePreview(),s.frames,(unsigned long)s.previewReadUs,(unsigned long)s.previewUs,(unsigned long)s.previewAgeUs,(unsigned long)s.previewDrawAgeUs,(unsigned long)s.previewIntervalUs);return true;}
 if(!strncmp(line,"cine preview ",13)){session.referencePreview(!strcmp(line+13,"reference"));session.retainPreviewRaw(!strcmp(line+13,"held"));output.printf("[cine] preview reference=%u\n",session.usesReferencePreview());return true;}
 if(!strcmp(line,"cine verify")){output.printf("[cine] pattern_check ok=%u\n",session.verifyPattern());return true;}
 if(!strcmp(line,"cine meter")){
  const auto& m=session.metering();output.printf("[cine] meter valid=%u samples=%u auto_thirds=%d total_thirds=%d ec_thirds=%d geometric=%.6f arithmetic=%.6f p98=%.6f limited=%u time_us=%lu\n",m.valid,m.samples,m.exposureThirds,session.parameters().exposureThirds,int(session.parameters().exposureThirds)-m.exposureThirds,m.geometricMean,m.arithmeticMean,m.highlight,m.highlightLimited,(unsigned long)session.status().meterUs);return true;
 }
 if(!strcmp(line,"cine shutter")){const auto& s=session.shutter();output.printf("[cine] shutter valid=%u manual=%u requested_us=%lu actual_us=%lu ticks_q4=%lu gain_q4=%u hts=%u vts=%u clock_hz=%lu frame_us=%lu\n",s.valid,s.manual,(unsigned long)s.requestedUs,(unsigned long)s.actualUs,(unsigned long)s.ticks,s.gainQ4,s.hts,s.vts,(unsigned long)s.clockHz,(unsigned long)s.frameUs);return true;}
 if(!strcmp(line,"cine wb")){output.printf("[cine] wb=%u name=%s\n",session.whiteBalance(),cine::Session::whiteBalanceName(session.whiteBalance()));return true;}
 if(!strcmp(line,"cine bench resize")){const bool ok=session.benchmarkIspResize();output.printf("[cine-resize] done=%u\n",ok);invalidate();return true;}
 if(!strcmp(line,"cine snap")){snap();return true;}
 if(!strncmp(line,"cine raw-save ",14)){saveRaw=!strcmp(line+14,"on");output.printf("[cine] raw-save=%u\n",saveRaw);return true;}
 if(!strncmp(line,"cine pattern ",13)){if(!close(true))return true;pattern=!strcmp(line+13,"on");output.printf("[cine] pattern=%u\n",pattern);return true;}
 if(!strncmp(line,"cine mode ",10)){
  if(strcmp(line+10,"sunny")){output.println("[cine] Cine is the only app mode; use bench mode for diagnostics");return true;}
  if(!close(true))return true;requestedMode=cine::Mode::Detail;begin();return true;
 }
 if(!strncmp(line,"cine bench mode ",16)){
  if(strcmp(line+16,"stock")&&strcmp(line+16,"isp3mp")&&strcmp(line+16,"binned")&&strcmp(line+16,"ispvga")&&strcmp(line+16,"native")){output.println("[cine] invalid bench mode");return true;}
  if(!close(true))return true;requestedMode=!strcmp(line+16,"stock")?cine::Mode::Vanilla:!strcmp(line+16,"isp3mp")?cine::Mode::Detail:!strcmp(line+16,"binned")?cine::Mode::Cine:!strcmp(line+16,"ispvga")?cine::Mode::IspVga:cine::Mode::FullRaw;begin();return true;
 }
 if(!strncmp(line,"cine bench ev ",14)){long target=0;if(parseInteger(line+14,-6,6,target))session.adjustExposure(int(target)-session.parameters().exposureThirds);output.printf("[cine] bench exposure_thirds=%d\n",session.parameters().exposureThirds);return true;}
 if(!strncmp(line,"cine bench isp ",15)){
  char curve[12]={},matrix[12]={},evText[16]={},usText[16]={},gainText[16]={},extra=0;long ev=0,us=0,gain=0;
  if(sscanf(line+15,"%11s %11s %15s %15s %15s %c",curve,matrix,evText,usText,gainText,&extra)!=5||!parseInteger(evText,-6,6,ev)||!parseInteger(usText,1000,100000,us)||!parseInteger(gainText,16,64,gain)||(strcmp(curve,"srgb")&&strcmp(curve,"filmic")&&strcmp(curve,"vendor"))||(strcmp(matrix,"vendor")&&strcmp(matrix,"vendor+1")&&strcmp(matrix,"fitted"))||ev< -6||ev>6||us<1000||us>100000||gain<16||gain>64){output.println("[cine-isp] use bench isp srgb|filmic|vendor vendor|vendor+1|fitted EV_THIRDS SHUTTER_US GAIN_Q4");return true;}
  const bool ok=session.configureIsp(!strcmp(curve,"vendor")?2:!strcmp(curve,"filmic"),ev,us,gain,!strcmp(matrix,"fitted"),false,!strcmp(matrix,"vendor+1")?1:0);if(!ok&&!session.status().open)current=View::Error;const auto& r=session.ispReadback();
  output.printf("[cine-isp] ok=%u curve=%s matrix=%s ev_thirds=%d requested_us=%u actual_us=%lu gain_q4=%u clock_hz=%lu hts=%u vts=%u gamma=",ok,curve,matrix,int(ev),unsigned(us),(unsigned long)r.shutter.actualUs,r.shutter.gainQ4,(unsigned long)r.shutter.clockHz,r.shutter.hts,r.shutter.vts);
  for(auto v:r.gamma)output.printf("%02x",v);output.print(" cmx=");for(auto v:r.cmx)output.printf("%02x",v);output.printf(" flags=%02x%02x wb=%u/%u/%u\n",r.flags[0],r.flags[1],r.wb[0],r.wb[1],r.wb[2]);invalidate();return true;
 }
 if(!strncmp(line,"cine bench tone ",16)){if(strcmp(line+16,"filmic")&&strcmp(line+16,"srgb")){output.println("[cine] use bench tone filmic|srgb");return true;}session.setFilmic(!strcmp(line+16,"filmic"));output.printf("[cine] software_filmic=%u\n",session.parameters().filmic);invalidate();return true;}
 if(!strcmp(line,"cine bench save")){const bool ok=session.capture()&&session.benchmarkResolution();output.printf("[cine] bench done=%u\n",ok);return true;}
 output.println("[cine] use open/status/snap/shutter/latency/meter/mode sunny; gated research: bench mode native|binned|isp3mp|stock, bench ev -6..6, bench save");return true;
}
} }
