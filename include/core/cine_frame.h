#pragma once
#include <stddef.h>
#include <stdint.h>
#include <limits.h>
namespace core { namespace cine {
inline bool validJpeg(const uint8_t* data,size_t bytes,unsigned width,unsigned height,unsigned expectedWidth,unsigned expectedHeight){
 return data&&expectedWidth&&expectedHeight&&expectedWidth<=4096&&expectedHeight<=4096&&width==expectedWidth&&height==expectedHeight&&
  bytes>=4&&bytes<=size_t(INT_MAX)&&bytes<=size_t(expectedWidth)*expectedHeight&&
  data[0]==0xff&&data[1]==0xd8&&data[bytes-2]==0xff&&data[bytes-1]==0xd9;
}
} }
