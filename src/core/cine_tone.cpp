#include "core/cine_tone.h"
#include <math.h>
namespace core { namespace cine {
namespace {
float clamp(float x,float lo,float hi){return x<lo?lo:x>hi?hi:x;}
float displayLinear(float x){return x<=.04045f?x/12.92f:powf((x+.055f)/1.055f,2.4f);}
// Scene-linear anchors and display brightness targets. All looks hold 18% gray
// at sRGB 0.46; only the toe, contrast and shoulder change. No scene adaptation.
class Curve {
 float xx[10],vv[10],m[10];
 static constexpr float x[10]={0,.005f,.018f,.05f,.18f,.4f,.7f,1.f,2.f,8.f};
public:
 explicit Curve(unsigned look){
  const float v[3][10]={
   {0,.095f,.185f,.285f,.46f,.62f,.76f,.86f,.95f,1.f},
   {0,.025f,.095f,.22f,.46f,.70f,.85f,.94f,.985f,1.f},
   {0,.008f,.035f,.13f,.46f,.76f,.92f,.985f,.998f,1.f}};
  float d[9];
  for(unsigned i=0;i<10;++i){xx[i]=log2f(x[i]+.001f);vv[i]=displayLinear(v[look][i]);}
  for(unsigned i=0;i<9;++i)d[i]=(vv[i+1]-vv[i])/(xx[i+1]-xx[i]);
  m[0]=d[0];m[9]=d[8];
  for(unsigned i=1;i<9;++i){float left=xx[i]-xx[i-1],right=xx[i+1]-xx[i];float w1=2*right+left,w2=right+2*left;m[i]=(d[i-1]<=0||d[i]<=0)?0:(w1+w2)/(w1/d[i-1]+w2/d[i]);}
 }
 float map(float y) const {
  unsigned k=0;while(k<8&&y>x[k+1])++k;
  const float h=xx[k+1]-xx[k],t=clamp((log2f(y+.001f)-xx[k])/h,0,1),t2=t*t,t3=t2*t;
  return (2*t3-3*t2+1)*vv[k]+(t3-2*t2+t)*h*m[k]+(-2*t3+3*t2)*vv[k+1]+(t3-t2)*h*m[k+1];
 }
};
constexpr float Curve::x[10];

bool valid(const uint8_t* p,size_t n,unsigned w,unsigned h){return p&&w>=2&&h>=2&&!(w&1)&&!(h&1)&&w<=4096&&h<=4096&&n==size_t(w)*h;}
}
Metering meterBayer(const uint8_t* raw,size_t n,unsigned w,unsigned h,uint8_t black){
 Metering result;
 if(!valid(raw,n,w,h))return result;
 if(black>64)black=64;
 const unsigned denominator=255-black;
 const unsigned gw=w/2<64?w/2:64,gh=h/2<48?h/2:48;
 uint16_t luminance[256]={},peaks[256]={};uint32_t sum=0,signal=0;
 // Sparse, evenly spaced Bayer cells; no allocation or per-sample logarithm.
 for(unsigned y=0;y<gh;++y)for(unsigned x=0;x<gw;++x){
  const unsigned sx=((2*x+1)*(w/2)/(2*gw))*2,sy=((2*y+1)*(h/2)/(2*gh))*2;
  const size_t i=size_t(sy)*w+sx;
  unsigned r=raw[i+w+1],g=(unsigned(raw[i+1])+raw[i+w]+1)/2,b=raw[i];
  r=r>black?r-black:0;g=g>black?g-black:0;b=b>black?b-black:0;
  const unsigned l=(54*r+183*g+19*b+128)>>8;
  const unsigned peak=r>g?(r>b?r:b):(g>b?g:b);
  const unsigned lb=(l*255+denominator/2)/denominator,pb=(peak*255+denominator/2)/denominator;
  ++luminance[lb];++peaks[pb];sum+=lb;if(peak>2)++signal;++result.samples;
 }
 result.arithmeticMean=float(sum)/(result.samples*255.f);
 // Do not normalize an almost black frame into amplified quantization/noise.
 if(signal*20<result.samples)return result;
 const unsigned low=result.samples/10,high=result.samples-low;
 unsigned before=0,used=0,cumulative=0;double logs=0;
 const float floor=2.f/denominator;
 for(unsigned i=0;i<256;++i){
  const unsigned after=before+luminance[i],begin=before>low?before:low,end=after<high?after:high;
  if(end>begin){const unsigned count=end-begin;const float value=i/255.f;logs+=count*log2f(value>floor?value:floor);used+=count;}
  before=after;cumulative+=peaks[i];
  if(!result.highlight&&cumulative>=(unsigned(result.samples)*98+99)/100)result.highlight=i/255.f;
 }
 if(!used)return result;
 result.geometricMean=exp2f(float(logs/used));
 float ev=clamp(log2f(.18f/result.geometricMean),-2.f,2.f);
 // The 98th percentile ignores tiny speculars. Existing clipped highlights
 // must not force a new darkening adjustment; the guard only limits lift.
 float limit=result.highlight>0?log2f(.85f/result.highlight):2.f;if(limit<0)limit=0;
 result.highlightLimited=ev>limit;if(result.highlightLimited)ev=limit;
 int thirds=int(lroundf(ev*3));
 if(ev>0&&thirds>int(floorf(limit*3)))thirds=int(floorf(limit*3));
 result.exposureThirds=thirds< -6?-6:thirds>6?6:thirds;
 result.valid=true;return result;
}
bool makeIspGamma(bool filmic,int thirds,uint8_t output[16]){
 if(!output||thirds< -6||thirds>6)return false;
 const unsigned refs[15]={4,8,16,32,40,48,56,64,72,80,96,112,144,176,208};
 const Curve curve(1);const float exposure=exp2f(thirds/3.f);
 auto encode=[](float x){return x<=.0031308f?12.92f*x:1.055f*powf(x,1.f/2.4f)-.055f;};
 for(unsigned i=0;i<15;++i){float x=refs[i]/256.f*exposure;float y=filmic?curve.map(x):x;output[i]=uint8_t(clamp(encode(y)*255.f+.5f,0,255));}
 float x=exposure;const unsigned end=unsigned(clamp(encode(filmic?curve.map(x):x)*255.f+.5f,0,255));
 // Datasheet highest slope = (255-GAM15+1)*40/30 for a 255 endpoint.
 // Here the requested endpoint follows Filmic (about 240 at 0EV), so this
 // is an intentional programmable-slope experiment, pending hardware checks.
 output[15]=uint8_t(clamp((float(end)+1-output[14])*40.f/30.f+.5f,0,255));return true;
}
const char* lookName(unsigned look){return look==0?"Soft":look==2?"Bold":"Filmic";}
const char* colorName(unsigned color){return color==2?"Sunny D":color==1?"D2":"A1";}
void ToneMap::configure(const Parameters& p,bool neutral){
 filmic_=!neutral&&p.filmic;d2_=!neutral&&p.color==1;sunny_=!neutral&&p.color==2;
 if(sunny_&&!displayTablesReady_){
  for(unsigned i=0;i<4865;++i){const float x=i<=1024?i/1024.f:1.f+(i-1024)/256.f;displayEncode_[i]=x<=.0031308f?12.92f*x:1.055f*powf(x,1.f/2.4f)-.055f;}
  for(unsigned i=0;i<2817;++i){const float x=i<=1024?i/1024.f:1.f+(i-1024)/256.f;displayDecode_[i]=x<=.04045f?x/12.92f:powf((x+.055f)/1.055f,2.4f);}
  displayTablesReady_=true;
 }
 if(d2_||sunny_){
  // Exact D2 grid recipe: vendor combined signed-Q7 matrix and BT.601-style
  // inverse. Provisional artistic transform, not a calibrated sensor profile.
  const float yuv[3][3]={{29/128.f,96/128.f,3/128.f},{-12/128.f,-120/128.f,132/128.f},{125/128.f,-107/128.f,-18/128.f}};
  const float inverse[3][3]={{1,0,1.402f},{1,-.344136f,-.714136f},{1,1.772f,0}};
  for(unsigned i=0;i<9;++i)colorMatrix_[i]=0;
  for(unsigned i=0;i<3;++i)for(unsigned j=0;j<3;++j)for(unsigned k=0;k<3;++k)colorMatrix_[i*3+j]+=inverse[i][k]*yuv[k][j];
 }
 const uint8_t black=p.black>64?64:p.black;
 const float ev=clamp(p.exposureThirds,-6,6)/3.f,exposure=exp2f(ev);
 for(unsigned c=0;c<3;++c)for(unsigned i=0;i<256;++i){
  const float x=float(i>black?i-black:0)/float(255-black);
  linear_[c][i]=uint16_t(clamp(x*clamp(p.gains[c]/1024.f,.5f,3.f)*exposure*4096.f,0,32760));
 }
 if(filmic_){
  const Curve curve(p.look>2?1:p.look);
  for(unsigned i=0;i<4096;++i)luminance_[i]=uint16_t(clamp(curve.map(i*8.f/4096.f)*4096.f,0,32760));
 }
 for(unsigned i=0;i<4096;++i){
  const float v=i/4095.f;
  srgb_[i]=uint8_t(clamp((v<=.0031308f?12.92f*v:1.055f*powf(v,1.f/2.4f)-.055f)*255.f+.5f,0,255));
 }
}
__attribute__((optimize("O3"))) float ToneMap::encodeDisplay(float value) const {
 // Sunny input is nonnegative and bounded below 16 by the tone/gain limits.
 const float x=value;
 const float at=x<=1.f?x*1024.f:1024.f+(x-1.f)*256.f;const unsigned i=unsigned(at);const float f=at-i;
 return displayEncode_[i]+(displayEncode_[i<4864?i+1:i]-displayEncode_[i])*f;
}
__attribute__((optimize("O3"))) float ToneMap::decodeDisplay(float value) const {
 // The matrix positive/negative row sums bound this to |value| < 6.4.
 const float x=fabsf(value);
 const float at=x<=1.f?x*1024.f:1024.f+(x-1.f)*256.f;const unsigned i=unsigned(at);const float f=at-i;
 const float decoded=displayDecode_[i]+(displayDecode_[i<2816?i+1:i]-displayDecode_[i])*f;return value<0?-decoded:decoded;
}
__attribute__((optimize("O3"))) void ToneMap::pixel(uint8_t r,uint8_t g,uint8_t b,uint8_t& ro,uint8_t& go,uint8_t& bo) const {
 linearPixel(linear_[0][r],linear_[1][g],linear_[2][b],ro,go,bo);
}
__attribute__((optimize("O3"))) void ToneMap::linearPixel(uint32_t R,uint32_t G,uint32_t B,uint8_t& ro,uint8_t& go,uint8_t& bo) const {
 const uint32_t y=(54*R+183*G+19*B)>>8;
 uint32_t mapped=y;
 if(filmic_){const unsigned index=y>>3,frac=y&7;mapped=(uint32_t(luminance_[index])*(8-frac)+uint32_t(luminance_[index<4095?index+1:4095])*frac+4)>>3;}
 if(sunny_){
  // Selected lab candidate D: unity relative WB, full vendor-derived matrix
  // after sRGB encoding (optional diagnostic Filmic), then restore linear luminance.
  const float original[3]={float(R),float(G),float(B)};
  const float sourceY=(54*original[0]+183*original[1]+19*original[2])/256.f;
  const float factor=y?(filmic_?mapped/float(y):1.f):0.f;float base[3],adjusted[3]={};
  const float scale=factor/4096.f;for(unsigned c=0;c<3;++c)base[c]=encodeDisplay(original[c]*scale);
  for(unsigned c=0;c<3;++c){float value=0;for(unsigned j=0;j<3;++j)value+=colorMatrix_[c*3+j]*base[j];adjusted[c]=decodeDisplay(value)*4096.f;}
  const float newY=(54*adjusted[0]+183*adjusted[1]+19*adjusted[2])/256.f;
  uint8_t* channels[3]={&ro,&go,&bo};
  const float offset=sourceY*factor-newY;
  for(unsigned c=0;c<3;++c){float value=adjusted[c]+offset;unsigned code=value<=0?0:value>=4095?4095:unsigned(value+.5f);*channels[c]=srgb_[code];}
  return;
 }
 if(d2_){
  const float original[3]={float(R),float(G),float(B)},wb[3]={.92f,1.f,1.12f};
  float adjusted[3]={};
  for(unsigned c=0;c<3;++c)for(unsigned j=0;j<3;++j)adjusted[c]+=colorMatrix_[c*3+j]*original[j]*wb[j];
  // Preserve the same pseudo-luminance used in the approved grid. Exposure
  // metering and the Filmic luminance curve remain independent of color.
  const float sourceY=(54*original[0]+183*original[1]+19*original[2])/256.f;
  const float newY=(54*adjusted[0]+183*adjusted[1]+19*adjusted[2])/256.f;
  const float factor=y?mapped/float(y):0;
  uint8_t* channels[3]={&ro,&go,&bo};
  for(unsigned c=0;c<3;++c){float value=(adjusted[c]+sourceY-newY)*factor;unsigned code=unsigned(clamp(roundf(value),0,4095));*channels[c]=srgb_[code];}
  return;
 }
 const uint32_t rr=y?(R*mapped+y/2)/y:0,gg=y?(G*mapped+y/2)/y:0,bb=y?(B*mapped+y/2)/y:0;
 // Interpolated output luminance keeps neutral ramps monotonic, including near black.
 // One luminance ratio preserves hue until the final display gamut limit.
 ro=srgb_[rr>4095?4095:rr];go=srgb_[gg>4095?4095:gg];bo=srgb_[bb>4095?4095:bb];
}
__attribute__((optimize("O3"))) bool ToneMap::renderBgr(const uint8_t* raw,size_t n,unsigned w,unsigned h,uint8_t* out,size_t cap) const {
 if(!valid(raw,n,w,h)||!out||cap<size_t(w/2)*(h/2)*3)return false;
 for(unsigned y=0;y<h;y+=2)for(unsigned x=0;x<w;x+=2){
  const size_t i=size_t(y)*w+(w-2-x);uint8_t r,g,b;
  pixel(raw[i+w+1],uint8_t((unsigned(raw[i+1])+raw[i+w]+1)/2),raw[i],r,g,b);
  *out++=b;*out++=g;*out++=r;
 }
 return true;
}
// Keep the native resampling hot loop optimized even in the size-focused firmware build.
__attribute__((optimize("O3"))) bool ToneMap::renderBgrArea(const uint8_t* raw,size_t n,unsigned w,unsigned h,uint8_t* out,size_t cap,unsigned ow,unsigned oh) const {
 if(!valid(raw,n,w,h)||!out||!ow||!oh||ow>w/2||oh>h/2||cap<size_t(ow)*oh*3)return false;
 auto gcd=[](unsigned a,unsigned b){while(b){unsigned t=a%b;a=b;b=t;}return a;};
 // Native VGA specialization: 8x8 Bayer cells -> 5x5 RGB outputs. Cache each
 // photosite once per tile and use the exact 8/5 area kernels; /64 is a shift.
 if(w==2048&&h==1536&&ow==640&&oh==480){
  static constexpr unsigned start[5]={0,1,3,4,6},count[5]={2,3,2,3,2};
  static constexpr unsigned weights[5][3]={{5,3,0},{2,5,1},{4,4,0},{1,5,2},{3,5,0}};
  uint16_t cells[8][8][3];
  for(unsigned by=0;by<96;++by)for(unsigned bx=0;bx<128;++bx){
   for(unsigned cy=0;cy<8;++cy)for(unsigned cx=0;cx<8;++cx){
    const size_t i=size_t(by*16+cy*2)*2048+bx*16+cx*2;
    cells[cy][cx][0]=linear_[0][raw[i+2049]];cells[cy][cx][2]=linear_[2][raw[i]];
    cells[cy][cx][1]=(unsigned(linear_[1][raw[i+1]])+linear_[1][raw[i+2048]]+1)/2;
   }
   for(unsigned oy=0;oy<5;++oy)for(unsigned ox=0;ox<5;++ox){
    uint32_t sums[3]={};
    for(unsigned ky=0;ky<count[oy];++ky)for(unsigned kx=0;kx<count[ox];++kx){
     const unsigned weight=weights[oy][ky]*weights[ox][kx];const auto& cell=cells[start[oy]+ky][start[ox]+kx];
     for(unsigned c=0;c<3;++c)sums[c]+=cell[c]*weight;
    }
    uint8_t r,g,b;linearPixel((sums[0]+32)>>6,(sums[1]+32)>>6,(sums[2]+32)>>6,r,g,b);
    const size_t dst=(size_t(by*5+oy)*640+639-(bx*5+ox))*3;out[dst]=b;out[dst+1]=g;out[dst+2]=r;
   }
  }
  return true;
 }
 const unsigned rw=w/2,rh=h/2,gx=gcd(rw,ow),gy=gcd(rh,oh);
 const unsigned spanX=rw/gx,unitX=ow/gx,spanY=rh/gy,unitY=oh/gy;
 if(spanX>16||spanY>16)return false; // 32-bit accumulator bound: 32760 * 256.
 const unsigned area=spanX*spanY;
 for(unsigned y=0;y<oh;++y)for(unsigned x=0;x<ow;++x){
  const unsigned left=x*spanX,right=left+spanX,top=y*spanY,bottom=top+spanY;
  uint32_t sumR=0,sumG=0,sumB=0;
  for(unsigned sy=top/unitY;sy<(bottom+unitY-1)/unitY;++sy){
   const unsigned ya=top>sy*unitY?top:sy*unitY,yb=bottom<(sy+1)*unitY?bottom:(sy+1)*unitY;
   for(unsigned sx=left/unitX;sx<(right+unitX-1)/unitX;++sx){
    const unsigned xa=left>sx*unitX?left:sx*unitX,xb=right<(sx+1)*unitX?right:(sx+1)*unitX;
    const unsigned weight=(xb-xa)*(yb-ya);const size_t i=size_t(sy*2)*w+sx*2;
    sumR+=linear_[0][raw[i+w+1]]*weight;sumB+=linear_[2][raw[i]]*weight;
    sumG+=((unsigned(linear_[1][raw[i+1]])+linear_[1][raw[i+w]]+1)/2)*weight;
   }
  }
  uint8_t r,g,b;linearPixel((sumR+area/2)/area,(sumG+area/2)/area,(sumB+area/2)/area,r,g,b);
  const size_t dst=(size_t(y)*ow+(ow-1-x))*3;out[dst]=b;out[dst+1]=g;out[dst+2]=r;
 }
 return true;
}
__attribute__((optimize("O3"))) bool ToneMap::renderRgbRow(const uint8_t* raw,size_t n,unsigned w,unsigned h,unsigned y,bool native,uint8_t* out,size_t cap) const {
 if(!valid(raw,n,w,h)||!out||y>=(native?h:h/2)||cap<size_t(native?w:w/2)*3)return false;
 const unsigned ow=native?w:w/2;
 for(unsigned ox=0;ox<ow;++ox){
  uint32_t R,G,B;
  if(!native){const size_t i=size_t(y*2)*w+(w-2-ox*2);R=linear_[0][raw[i+w+1]];G=(unsigned(linear_[1][raw[i+1]])+linear_[1][raw[i+w]]+1)/2;B=linear_[2][raw[i]];}
  else{
   const unsigned x=w-1-ox,xl=x?x-1:1,xr=x+1<w?x+1:w-2,yu=y?y-1:1,yd=y+1<h?y+1:h-2;
   const size_t i=size_t(y)*w+x;
   if((y&1)==(x&1)){
    G=(unsigned(linear_[1][raw[size_t(y)*w+xl]])+linear_[1][raw[size_t(y)*w+xr]]+linear_[1][raw[size_t(yu)*w+x]]+linear_[1][raw[size_t(yd)*w+x]]+2)/4;
    const unsigned c=(y&1)?2:0;
    const unsigned opposite=(unsigned(linear_[c][raw[size_t(yu)*w+xl]])+linear_[c][raw[size_t(yu)*w+xr]]+linear_[c][raw[size_t(yd)*w+xl]]+linear_[c][raw[size_t(yd)*w+xr]]+2)/4;
    if(y&1){R=linear_[0][raw[i]];B=opposite;}else{B=linear_[2][raw[i]];R=opposite;}
   }else{
    G=linear_[1][raw[i]];
    if(y&1){R=(unsigned(linear_[0][raw[size_t(y)*w+xl]])+linear_[0][raw[size_t(y)*w+xr]]+1)/2;B=(unsigned(linear_[2][raw[size_t(yu)*w+x]])+linear_[2][raw[size_t(yd)*w+x]]+1)/2;}
    else{B=(unsigned(linear_[2][raw[size_t(y)*w+xl]])+linear_[2][raw[size_t(y)*w+xr]]+1)/2;R=(unsigned(linear_[0][raw[size_t(yu)*w+x]])+linear_[0][raw[size_t(yd)*w+x]]+1)/2;}
   }
  }
  linearPixel(R,G,B,out[ox*3],out[ox*3+1],out[ox*3+2]);
 }
 return true;
}
__attribute__((optimize("O3"))) bool ToneMap::preview565(const uint8_t* raw,size_t n,unsigned w,unsigned h,uint16_t* out,unsigned ow,unsigned oh) const {
 if(!valid(raw,n,w,h)||!out||!ow||!oh||ow>1024||oh>1024)return false;
 for(unsigned y=0;y<oh;++y)for(unsigned x=0;x<ow;++x){
  // One centered Bayer cell per display pixel. Match capture's order:
  // subtract black/apply linear gains before averaging the two green sites.
  unsigned sy=(((2*y+1)*h)/(2*oh))&~1u,sx=(((2*(ow-1-x)+1)*w)/(2*ow))&~1u;
  size_t i=size_t(sy)*w+sx;uint8_t r,g,b;
  linearPixel(linear_[0][raw[i+w+1]],(unsigned(linear_[1][raw[i+1]])+linear_[1][raw[i+w]]+1)/2,linear_[2][raw[i]],r,g,b);
  *out++=uint16_t((r>>3)<<11|(g>>2)<<5|(b>>3));
 }
 return true;
}
} }
