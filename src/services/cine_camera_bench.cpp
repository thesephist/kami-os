// Gated resolution experiments. Production captures remain the selected VGA path.
#include "services/cine_camera.h"
#include "core/cine_bench_jpeg.h"
#include "core/watchdog.h"
#include "hal/hal_sd.h"
#include <esp_heap_caps.h>
#include <esp_timer.h>
#include <string.h>
#include <stdio.h>
namespace services { namespace cine_camera {
namespace {
constexpr size_t chunkBytes=64*1024;
class BenchStream final : public cine_bench_jpge::output_stream {
 uint8_t* buffer_;const char* path_;size_t used_=0,total_=0;
public:
 uint32_t writeUs=0;
 BenchStream(uint8_t* buffer,const char* path):buffer_(buffer),path_(path){}
 bool flush(){if(!used_)return true;const auto at=esp_timer_get_time();bool ok=hal::sd::appendFile(path_,buffer_,used_);writeUs+=uint32_t(esp_timer_get_time()-at);if(ok)used_=0;return ok;}
 bool put_buf(const void* data,int len) override {
  if(len<0||(!data&&len))return false;auto* src=static_cast<const uint8_t*>(data);size_t left=len;
  while(left){const size_t n=left<chunkBytes-used_?left:chunkBytes-used_;memcpy(buffer_+used_,src,n);used_+=n;total_+=n;left-=n;src+=n;if(used_==chunkBytes&&!flush())return false;}return true;
 }
 cine_bench_jpge::uint get_size() const override{return total_;}
};
struct Result{bool ok=false;unsigned width=0,height=0;uint32_t processing=0,encoding=0,write=0,atomicWrite=0,total=0;size_t bytes=0;char path[80]={};};
}
bool Session::benchmarkResolution(){
 if(!status_.open||pattern_||params_.color!=2||(params_.filmic&&params_.look!=1)||mode_!=Mode::FullRaw||!frameHeld_||!raw_||rawBytes_!=3145728||!tone_||!status_.saved)return false;
 const auto sequence=status_.sequence;const uint32_t productionProcessing=status_.toneUs;
 auto* row=static_cast<uint8_t*>(heap_caps_malloc(2048*3,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT));if(!row)return false;
 Result results[3];const char* names[]={"vga","cells","native"};bool all=true;
 for(unsigned kind=0;kind<3;++kind){
  auto& result=results[kind];result.width=kind==0?640:kind==1?1024:2048;result.height=kind==0?480:kind==1?768:1536;
  snprintf(result.path,sizeof(result.path),"/cine/BENCH_%05lu_%s.jpg",(unsigned long)sequence,names[kind]);char part[90];snprintf(part,sizeof(part),"%s.part",result.path);
  if(hal::sd::exists(result.path)||hal::sd::exists(part)){all=false;break;}
  const auto started=esp_timer_get_time();BenchStream stream(jpeg_,part);cine_bench_jpge::jpeg_encoder encoder;cine_bench_jpge::params options;options.m_quality=90;options.m_subsampling=cine_bench_jpge::H2V2;
  auto at=esp_timer_get_time();bool ok=encoder.init(&stream,result.width,result.height,3,options);result.encoding=uint32_t(esp_timer_get_time()-at);
  for(unsigned y=0;ok&&y<result.height;++y){
   core::watchdog::feed();at=esp_timer_get_time();
   if(kind==0){const auto* src=bgr_+size_t(y)*640*3;for(unsigned x=0;x<640;++x){row[x*3]=src[x*3+2];row[x*3+1]=src[x*3+1];row[x*3+2]=src[x*3];}}
   else ok=tone_->renderRgbRow(raw_,rawBytes_,width_,height_,y,kind==2,row,2048*3);
   result.processing+=uint32_t(esp_timer_get_time()-at);if(!ok)break;
   at=esp_timer_get_time();const uint32_t priorWrite=stream.writeUs;ok=encoder.process_scanline(row);result.encoding+=uint32_t(esp_timer_get_time()-at)-(stream.writeUs-priorWrite);
  }
  at=esp_timer_get_time();const uint32_t priorWrite=stream.writeUs;if(ok)ok=encoder.process_scanline(nullptr);encoder.deinit();result.encoding+=uint32_t(esp_timer_get_time()-at)-(stream.writeUs-priorWrite);
  if(ok)ok=stream.flush();result.bytes=stream.get_size();result.write=stream.writeUs;
  if(ok){size_t bytes=0;uint32_t modified=0;ok=hal::sd::fileInfo(part,bytes,modified)&&bytes==result.bytes;}
  at=esp_timer_get_time();if(ok)ok=hal::sd::replaceFile(part,result.path);result.write+=uint32_t(esp_timer_get_time()-at);
  if(!ok)hal::sd::removeFile(part);result.ok=ok;result.total=uint32_t(esp_timer_get_time()-started);
  if(kind==0){result.processing+=productionProcessing;result.total+=productionProcessing;}
  all=all&&ok;
  Serial.printf("[cine-bench] output=%s ok=%u width=%u height=%u processing_us=%lu encode_us=%lu write_us=%lu total_us=%lu bytes=%u path=%s\n",names[kind],ok,result.width,result.height,(unsigned long)result.processing,(unsigned long)result.encoding,(unsigned long)result.write,(unsigned long)result.total,unsigned(result.bytes),result.path);
 }
 heap_caps_free(row);
 // Separately time the existing one-buffer SD writer using identical compressed
 // bytes, where they fit in the already allocated VGA work buffer. Read time
 // is excluded. The temporary duplicate is removed after its size is verified.
 for(unsigned i=0;i<3;++i){auto& v=results[i];if(!v.ok||v.bytes>640u*480*3)continue;
  hal::sd::StreamReader reader{};if(!hal::sd::openStream(v.path,&reader))continue;size_t done=0;
  while(done<v.bytes){size_t n=hal::sd::readStream(&reader,bgr_+done,v.bytes-done);if(!n)break;done+=n;}hal::sd::closeStream(&reader);if(done!=v.bytes)continue;
  char copy[90];snprintf(copy,sizeof(copy),"%s.write-test",v.path);if(hal::sd::exists(copy))continue;
  const auto at=esp_timer_get_time();bool ok=hal::sd::writeFile(copy,bgr_,v.bytes);v.atomicWrite=uint32_t(esp_timer_get_time()-at);
  size_t bytes=0;uint32_t modified=0;ok=ok&&hal::sd::fileInfo(copy,bytes,modified)&&bytes==v.bytes;hal::sd::removeFile(copy);if(!ok){v.atomicWrite=0;all=false;}
 }
 char metadata[2600];int used=snprintf(metadata,sizeof(metadata),"{\"version\":1,\"sequence\":%lu,\"source\":\"same held 2048x1536 RAW8 frame\",\"color\":\"Sunny D\",\"look\":\"%s\",\"software_filmic\":%s,\"exposure_thirds\":%d,\"shutter_us\":%lu,\"gain_q4\":%u,\"quality\":90,\"codec\":\"namespaced esp32-camera v2.0.4 JPGE O3; identical for all outputs\",\"common_sensor_read_us\":%lu,\"production_vga_total_us\":%lu,\"production_vga_processing_us\":%lu,\"production_vga_encode_us\":%lu,\"production_vga_write_us\":%lu,\"outputs\":[",(unsigned long)sequence,params_.filmic?"Filmic":"sRGB",params_.filmic?"true":"false",params_.exposureThirds,(unsigned long)shutter_.actualUs,shutter_.gainQ4,(unsigned long)status_.frameUs,(unsigned long)status_.totalUs,(unsigned long)status_.toneUs,(unsigned long)status_.encodeUs,(unsigned long)status_.saveUs);
 for(unsigned i=0;i<3&&used>0&&size_t(used)<sizeof(metadata);++i){auto& v=results[i];used+=snprintf(metadata+used,sizeof(metadata)-used,"%s{\"name\":\"%s\",\"ok\":%s,\"width\":%u,\"height\":%u,\"processing_us\":%lu,\"encode_us\":%lu,\"write_us\":%lu,\"atomic_write_us\":%lu,\"total_us\":%lu,\"bytes\":%u,\"path\":\"%s\"}",i?",":"",names[i],v.ok?"true":"false",v.width,v.height,(unsigned long)v.processing,(unsigned long)v.encoding,(unsigned long)v.write,(unsigned long)v.atomicWrite,(unsigned long)v.total,unsigned(v.bytes),v.path);}
 if(used>0&&size_t(used)+3<sizeof(metadata)){metadata[used++]=']';metadata[used++]='}';metadata[used]=0;char path[70];snprintf(path,sizeof(path),"/cine/BENCH_%05lu.json",(unsigned long)sequence);all=hal::sd::writeFile(path,reinterpret_cast<const uint8_t*>(metadata),used)&&all;Serial.printf("[cine-bench] manifest=%s ok=%u\n",path,all);}else all=false;
 return all;
}
} }
