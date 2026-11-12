from pathlib import Path
import tempfile,subprocess
R=Path(__file__).resolve().parents[1];s=(R/'src/services/cine_camera_resize.cpp').read_text();callbacks=s[s.index('struct Decode {'):s.index('struct Encode')];methods=s[s.index('void Session::releaseIspPreview()'):s.index('bool Session::benchmarkIspResize()')]
with tempfile.TemporaryDirectory() as td:
 p=Path(td);(p/'test.cpp').write_text(r'''
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <cassert>
#include <cstdlib>
#include <new>
#include <set>
#include "core/cine_frame.h"
#define MAX_BUFFERED_PIXELS 2048
#define MALLOC_CAP_SPIRAM 1
#define MALLOC_CAP_8BIT 2
#define RGB565_LITTLE_ENDIAN 0
#define JPEG_SCALE_EIGHTH 8
#define JPEG_SCALE_HALF 2
#define JPG_SCALE_8X 8
#define JPG_SCALE_2X 2
#define ESP_OK 0
namespace core{namespace watchdog{void feed(){}}}
namespace hal{namespace camera{struct CameraFrame{const uint8_t* buf;size_t len;unsigned width,height;};}}
std::set<void*> owned;int budget=-1,behavior=0;unsigned opens=0;
void* heap_caps_malloc(size_t n,int caps){assert(caps==(MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT));if(budget==0)return nullptr;if(budget>0)--budget;auto* p=malloc(n);if(p)assert(owned.insert(p).second);return p;}
void heap_caps_free(void* p){if(p)assert(owned.erase(p)==1);free(p);}
struct JPEGDRAW{void* pUser;int x,y,iWidthUsed,iHeight,iWidth;uint16_t* pPixels;};
class JPEGDEC{void* user=nullptr;int(*callback)(JPEGDRAW*)=nullptr;public:
 int openRAM(uint8_t*,int,int(*cb)(JPEGDRAW*)){++opens;callback=cb;return behavior!=1;}
 int getWidth(){return behavior==2?640:2048;}int getHeight(){return 1536;}
 void setUserPointer(void* p){user=p;}void setPixelType(int){}
 int decode(int,int,int){uint16_t pixels[2048];for(auto& v:pixels)v=0x1234;for(int y=0;y<192;y+=8){JPEGDRAW b{user,0,y,256,8,256,pixels};if(!callback(&b))return 1;if(behavior==3)return 0;}return 1;}
 void close(){}
};
class Session{public:static constexpr unsigned previewWidth=224,previewHeight=168;uint16_t *preview_=nullptr,*previewWork_=nullptr;JPEGDEC* ispDecoder_=nullptr;bool referencePreview_=false;struct {bool previewInterrupted=false;} status_;void releaseIspPreview();bool renderIspPreview(const hal::camera::CameraFrame&,bool(*)(void*)=nullptr,void* =nullptr);};
int esp_jpg_decode(size_t,int,size_t(*)(void*,size_t,uint8_t*,size_t),bool(*write)(void*,uint16_t,uint16_t,uint16_t,uint16_t,uint8_t*),void* ctx){assert(write(ctx,0,0,256,192,nullptr));uint8_t pixels[256*8*3]={};assert(write(ctx,0,0,256,8,pixels));return -1;}
'''+callbacks+methods+r'''
int main(){uint8_t jpeg[]={255,216,255,217};hal::camera::CameraFrame f{jpeg,4,2048,1536};Session s;s.preview_=static_cast<uint16_t*>(heap_caps_malloc(224*168*2,3));for(unsigned i=0;i<224*168;++i)s.preview_[i]=0xbeef;
 budget=0;assert(!s.renderIspPreview(f)&&owned.size()==1);budget=1;assert(!s.renderIspPreview(f)&&owned.size()==2);budget=-1;
 for(int b:{1,2,3}){behavior=b;auto* front=s.preview_;assert(!s.renderIspPreview(f)&&s.preview_==front);for(unsigned i=0;i<224*168;++i)assert(front[i]==0xbeef);assert(owned.size()==3);}
 behavior=0;{auto* front=s.preview_;unsigned calls=0;auto abort=[](void* p){return ++*static_cast<unsigned*>(p)>=2;};assert(!s.renderIspPreview(f,abort,&calls)&&s.status_.previewInterrupted&&s.preview_==front);for(unsigned i=0;i<224*168;++i)assert(front[i]==0xbeef);assert(owned.size()==3);}
 behavior=0;assert(s.renderIspPreview(f)&&owned.size()==3);for(unsigned i=0;i<224*168;++i)assert(s.preview_[i]==0x1234);
 for(int i=0;i<20;++i){assert(s.renderIspPreview(f));assert(owned.size()==3);}s.referencePreview_=true;auto* front=s.preview_;assert(!s.renderIspPreview(f)&&front==s.preview_);for(unsigned i=0;i<224*168;++i)assert(front[i]==0x1234);
 auto count=opens;f.buf=nullptr;assert(!s.renderIspPreview(f)&&opens==count);f.buf=jpeg;f.len=0;assert(!s.renderIspPreview(f));assert(!core::cine::validJpeg(jpeg,4,UINT32_MAX,UINT32_MAX,UINT32_MAX,UINT32_MAX));
 s.releaseIspPreview();s.releaseIspPreview();assert(owned.size()==1);heap_caps_free(s.preview_);assert(owned.empty());
}
''');subprocess.run(['c++','-std=c++11','-O1','-fsanitize=address,undefined','-I'+str(R/'include'),str(p/'test.cpp'),'-o',str(p/'test')],check=True);subprocess.run([str(p/'test')],check=True)
print('PASS actual preview lifecycle under ASan/UBSan: PSRAM-only persistent decoder, OOM, open/dimension/partial-decode failures, atomic frame swap, malformed frames, repeated reuse and idempotent cleanup; codec mocked.')
