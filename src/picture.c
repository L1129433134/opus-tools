/* Copyright (C)2007-2013 Xiph.Org Foundation
   File: picture.c

   Redistribution and use in source and binary forms, with or without
   modification, are permitted provided that the following conditions
   are met:

   - Redistributions of source code must retain the above copyright
   notice, this list of conditions and the following disclaimer.

   - Redistributions in binary form must reproduce the above copyright
   notice, this list of conditions and the following disclaimer in the
   documentation and/or other materials provided with the distribution.

   THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
   ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
   LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
   A PARTICULAR PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE FOUNDATION OR
   CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
   EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
   PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
   PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
   LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
   NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
   SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "picture.h"

static const char BASE64_TABLE[64]={
  'A','B','C','D','E','F','G','H','I','J','K','L','M','N','O','P',
  'Q','R','S','T','U','V','W','X','Y','Z','a','b','c','d','e','f',
  'g','h','i','j','k','l','m','n','o','p','q','r','s','t','u','v',
  'w','x','y','z','0','1','2','3','4','5','6','7','8','9','+','/'
};

/*Utility function for base64 encoding METADATA_BLOCK_PICTURE tags.
  Stores BASE64_LENGTH(len)+1 bytes in dst (including a terminating NUL).*/
void base64_encode(char *dst, const char *src, int len){
  unsigned s0;
  unsigned s1;
  unsigned s2;
  int      ngroups;
  int      i;
  ngroups=len/3;
  for(i=0;i<ngroups;i++){
    s0=(unsigned char)src[3*i+0];
    s1=(unsigned char)src[3*i+1];
    s2=(unsigned char)src[3*i+2];
    dst[4*i+0]=BASE64_TABLE[s0>>2];
    dst[4*i+1]=BASE64_TABLE[(s0&3)<<4|s1>>4];
    dst[4*i+2]=BASE64_TABLE[(s1&15)<<2|s2>>6];
    dst[4*i+3]=BASE64_TABLE[s2&63];
  }
  len-=3*i;
  if(len==1){
    s0=(unsigned char)src[3*i+0];
    dst[4*i+0]=BASE64_TABLE[s0>>2];
    dst[4*i+1]=BASE64_TABLE[(s0&3)<<4];
    dst[4*i+2]='=';
    dst[4*i+3]='=';
    i++;
  }
  else if(len==2){
    s0=(unsigned char)src[3*i+0];
    s1=(unsigned char)src[3*i+1];
    dst[4*i+0]=BASE64_TABLE[s0>>2];
    dst[4*i+1]=BASE64_TABLE[(s0&3)<<4|s1>>4];
    dst[4*i+2]=BASE64_TABLE[(s1&15)<<2];
    dst[4*i+3]='=';
    i++;
  }
  dst[4*i]='\0';
}

/*A version of strncasecmp() that is guaranteed to only ignore the case of
   ASCII characters.*/
int oi_strncasecmp(const char *a, const char *b, int n){
  int i;
  for(i=0;i<n;i++){
    int aval;
    int bval;
    int diff;
    aval=a[i];
    bval=b[i];
    if(aval>='a'&&aval<='z') {
      aval-='a'-'A';
    }
    if(bval>='a'&&bval<='z'){
      bval-='a'-'A';
    }
    diff=aval-bval;
    if(diff){
      return diff;
    }
  }
  return 0;
}

int is_jpeg(const unsigned char *buf, size_t length){
  return length>=11&&memcmp(buf,"\xFF\xD8\xFF\xE0",4)==0
   &&(buf[4]<<8|buf[5])>=16&&memcmp(buf+6,"JFIF",5)==0;
}

int is_png(const unsigned char *buf, size_t length){
  return length>=8&&memcmp(buf,"\x89PNG\x0D\x0A\x1A\x0A",8)==0;
}

int is_gif(const unsigned char *buf, size_t length){
  return length>=6
   &&(memcmp(buf,"GIF87a",6)==0||memcmp(buf,"GIF89a",6)==0);
}

#define READ_U32_BE(buf) \
    (((buf)[0]<<24)|((buf)[1]<<16)|((buf)[2]<<8)|((buf)[3]&0xff))

/*Tries to extract the width, height, bits per pixel, and palette size of a
   PNG.
  On failure, simply leaves its outputs unmodified.*/
void extract_png_params(const unsigned char *data, size_t data_length,
                        ogg_uint32_t *width, ogg_uint32_t *height,
                        ogg_uint32_t *depth, ogg_uint32_t *colors,
                        int *has_palette){
  if(is_png(data,data_length)){
    size_t offs;
    offs=8;
    while(data_length-offs>=12){
      ogg_uint32_t chunk_len;
      chunk_len=READ_U32_BE(data+offs);
      if(chunk_len>data_length-(offs+12))break;
      else if(chunk_len==13&&memcmp(data+offs+4,"IHDR",4)==0){
        int color_type;
        *width=READ_U32_BE(data+offs+8);
        *height=READ_U32_BE(data+offs+12);
        color_type=data[offs+17];
        if(color_type==3){
          *depth=24;
          *has_palette=1;
        }
        else{
          int sample_depth;
          sample_depth=data[offs+16];
          if(color_type==0)*depth=sample_depth;
          else if(color_type==2)*depth=sample_depth*3;
          else if(color_type==4)*depth=sample_depth*2;
          else if(color_type==6)*depth=sample_depth*4;
          *colors=0;
          *has_palette=0;
          break;
        }
      }
      else if(*has_palette>0&&memcmp(data+offs+4,"PLTE",4)==0){
        *colors=chunk_len/3;
        break;
      }
      offs+=12+chunk_len;
    }
  }
}

/*Tries to extract the width, height, bits per pixel, and palette size of a
   GIF.
  On failure, simply leaves its outputs unmodified.*/
void extract_gif_params(const unsigned char *data, size_t data_length,
                        ogg_uint32_t *width, ogg_uint32_t *height,
                        ogg_uint32_t *depth, ogg_uint32_t *colors,
                        int *has_palette){
  if(is_gif(data,data_length)&&data_length>=14){
    *width=data[6]|data[7]<<8;
    *height=data[8]|data[9]<<8;
    /*libFLAC hard-codes the depth to 24.*/
    *depth=24;
    *colors=1<<((data[10]&7)+1);
    *has_palette=1;
  }
}


/*Tries to extract the width, height, bits per pixel, and palette size of a
   JPEG.
  On failure, simply leaves its outputs unmodified.*/
void extract_jpeg_params(const unsigned char *data, size_t data_length,
                         ogg_uint32_t *width, ogg_uint32_t *height,
                         ogg_uint32_t *depth, ogg_uint32_t *colors,
                         int *has_palette){
  if(is_jpeg(data,data_length)){
    size_t offs;
    offs=2;
    for(;;){
      size_t segment_len;
      int    marker;
      while(offs<data_length&&data[offs]!=0xFF)offs++;
      while(offs<data_length&&data[offs]==0xFF)offs++;
      marker=data[offs];
      offs++;
      /*If we hit EOI* (end of image), or another SOI* (start of image),
         or SOS (start of scan), then stop now.*/
      if(offs>=data_length||(marker>=0xD8&&marker<=0xDA))break;
      /*RST* (restart markers): skip (no segment length).*/
      else if(marker>=0xD0&&marker<=0xD7)continue;
      /*Read the length of the marker segment.*/
      if(data_length-offs<2)break;
      segment_len=data[offs]<<8|data[offs+1];
      if(segment_len<2||data_length-offs<segment_len)break;
      if(marker==0xC0||(marker>0xC0&&marker<0xD0&&(marker&3)!=0)){
        /*Found a SOFn (start of frame) marker segment:*/
        if(segment_len>=8){
          *height=data[offs+3]<<8|data[offs+4];
          *width=data[offs+5]<<8|data[offs+6];
          *depth=data[offs+2]*data[offs+7];
          *colors=0;
          *has_palette=0;
        }
        break;
      }
      /*Other markers: skip the whole marker segment.*/
      offs+=segment_len;
    }
  }
}
