"""Check the native Cine preview adapter with a fault-injected vendor codec."""

from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
source = (ROOT / "src/services/cine_camera_native_preview.cpp").read_text()
methods = source[
    source.index("namespace {") : source.index("} // namespace cine_camera")
]
with tempfile.TemporaryDirectory() as directory:
    root = Path(directory)
    (root / "test.cpp").write_text(
        r"""
#include <stdint.h>
#include <stddef.h>
#include <limits.h>
#include <cassert>
#include <cstdlib>
#include <set>
#define MALLOC_CAP_SPIRAM 1
#define MALLOC_CAP_8BIT 2
#define JPEG_PIXEL_FORMAT_RGB565_LE 7
#define JPEG_ROTATE_0D 0
#define JPEG_ERR_OK 0
namespace core{namespace watchdog{void feed(){}}}
namespace hal{namespace camera{struct CameraFrame{const uint8_t* buf;size_t len;unsigned width,height;};}}
std::set<void*> owned;int fault=0,allocs=0,opens=0,processes=0,closes=0;
void* heap_caps_aligned_calloc(size_t alignment,size_t n,size_t bytes,int caps){assert(alignment==16&&caps==3);if(fault==1)return nullptr;void* p=nullptr;assert(!posix_memalign(&p,alignment,n*bytes));owned.insert(p);++allocs;return p;}
void heap_caps_free(void* p){if(p){assert(owned.erase(p)==1);free(p);}}
struct Resolution{unsigned width=0,height=0;};
struct jpeg_dec_config_t{int output_type=0;Resolution scale;int rotate=0;};
struct jpeg_dec_io_t{uint8_t* inbuf=nullptr;int inbuf_len=0;uint8_t* outbuf=nullptr;int out_size=0;};
struct jpeg_dec_header_info_t{unsigned width=0,height=0;};
int jpeg_dec_open(jpeg_dec_config_t* c,void** h){++opens;assert(c->output_type==7&&c->scale.width==256&&c->scale.height==192&&c->rotate==0);if(fault==2)return -1;*h=reinterpret_cast<void*>(16);return 0;}
int jpeg_dec_close(void*){++closes;return 0;}
int jpeg_dec_parse_header(void*,jpeg_dec_io_t* io,jpeg_dec_header_info_t* h){assert(io->inbuf&&io->inbuf_len>0);h->width=fault==4?255:256;h->height=192;return fault==3?-1:0;}
int jpeg_dec_get_outbuf_len(void*,int* n){*n=fault==6?100:256*192*2;return fault==5?-1:0;}
int jpeg_dec_get_process_count(void*,int* n){*n=fault==8?2:1;return fault==7?-1:0;}
int jpeg_dec_process(void*,jpeg_dec_io_t* io){++processes;assert(!(uintptr_t(io->outbuf)&15));auto* p=reinterpret_cast<uint16_t*>(io->outbuf);for(unsigned i=0;i<256*192;++i)p[i]=uint16_t(i*37);io->out_size=fault==10?100:256*192*2;return fault==9?-1:0;}
class Session{public:static constexpr unsigned previewWidth=224,previewHeight=168;void* nativeDecoder_=nullptr;uint16_t* nativePixels_=nullptr;bool nativeUnavailable_=false;uint16_t* previewWork_=nullptr;struct{bool previewInterrupted=false;}status_;void releaseNativePreview();bool renderNativePreview(const hal::camera::CameraFrame&,bool(*)(void*),void*);};
"""
        + methods
        + r"""
int main(){uint8_t jpeg[]={255,216,255,192,0,17,8,6,0,8,0,3,1,0x21,0,2,0x11,1,3,0x11,1,255,218,0,2,255,217};hal::camera::CameraFrame f{jpeg,sizeof(jpeg),2048,1536};uint16_t output[224*168+2];Session s;s.previewWork_=output+1;output[0]=0xabcd;output[224*168+1]=0xdcba;
for(int error=1;error<=10;++error){fault=error;for(auto& p:output)p=0xbeef;assert(!s.renderNativePreview(f,nullptr,nullptr));for(auto p:output)assert(p==0xbeef);s.releaseNativePreview();assert(owned.empty());}
fault=0;assert(nativeHeader(jpeg,sizeof(jpeg)));for(size_t n=0;n<25;++n)assert(!nativeHeader(jpeg,n));jpeg[16]=0x12;assert(!nativeHeader(jpeg,sizeof(jpeg)));jpeg[16]=0x11;jpeg[8]=1;assert(!nativeHeader(jpeg,sizeof(jpeg)));jpeg[8]=0;output[0]=0xabcd;output[224*168+1]=0xdcba;assert(s.renderNativePreview(f,nullptr,nullptr));const auto count=allocs,openCount=opens;for(int i=0;i<20;++i)assert(s.renderNativePreview(f,nullptr,nullptr));assert(allocs==count&&opens==openCount&&owned.size()==1);
for(unsigned y=0;y<168;++y)for(unsigned x=0;x<224;++x)assert(output[1+y*224+x]==uint16_t(((y*192/168)*256+x*256/224)*37));assert(output[0]==0xabcd&&output[224*168+1]==0xdcba);
for(unsigned at:{1u,2u,3u,4u,6u}){struct Calls{unsigned count,at;};Calls calls{0,at};s.status_.previewInterrupted=false;auto abort=[](void* p){auto& c=*static_cast<Calls*>(p);return ++c.count==c.at;};assert(!s.renderNativePreview(f,abort,&calls));assert(s.status_.previewInterrupted);}
s.status_.previewInterrupted=false;auto prior=processes;f.width=640;assert(!s.renderNativePreview(f,nullptr,nullptr));f.width=2048;f.len=size_t(INT_MAX)+1;assert(!s.renderNativePreview(f,nullptr,nullptr));f.len=4;f.buf=nullptr;assert(!s.renderNativePreview(f,nullptr,nullptr));assert(processes==prior);
s.releaseNativePreview();s.releaseNativePreview();assert(owned.empty());
}
"""
    )
    subprocess.run(
        [
            "c++",
            "-std=c++11",
            "-O1",
            "-fsanitize=address,undefined",
            str(root / "test.cpp"),
            "-o",
            str(root / "test"),
        ],
        check=True,
    )
    subprocess.run([str(root / "test")], check=True)
print(
    "PASS native Cine adapter: aligned bounded output, exact fitting, allocation/codec/header/size failures, cancellation, stable reuse and cleanup; ASan/UBSan, vendor codec mocked"
)
