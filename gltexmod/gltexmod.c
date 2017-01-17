/*
	gltexmod.c : A library for replacing OpenGL textures.
	Part of GLMOD, the Linux OpenGL modding project.
	(C)2016-2017 Marisa Kirisame, UnSX Team.
	Released under the GNU GPLv3 (or later).
*/
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <dlfcn.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <sys/stat.h>
#include <stdarg.h>

#ifndef CFG_PATH
# define CFG_PATH "/etc/glmod/override.conf"
#endif
#ifndef OVERRIDE_PATH
# define OVERRIDE_PATH "/usr/share/glmod/override"
#endif

char texpath[256] = OVERRIDE_PATH;
int verbose = 0;

#define GL_UNSIGNED_BYTE 0x1401

#define GL_RED  0x1903
#define GL_RG   0x8227
#define GL_RGB  0x1907
#define GL_BGR  0x8020
#define GL_RGBA 0x1908
#define GL_BGRA 0x8021

#define GL_COMPRESSED_RGB_S3TC_DXT1_EXT 0x83F0
#define GL_COMPRESSED_RGBA_S3TC_DXT1_EXT 0x83F1
#define GL_COMPRESSED_RGBA_S3TC_DXT3_EXT 0x83F2
#define GL_COMPRESSED_RGBA_S3TC_DXT5_EXT 0x83F3

#define dword __UINT32_TYPE__
#define byte __UINT8_TYPE__

typedef struct
{
	dword magic, size, flags, height, width, pitch, depth, mipmaps,
	reserved1[11], pf_size, pf_flags, pf_fourcc, pf_bitcount, pf_rmask,
	pf_gmask, pf_bmask, pf_amask, caps[4], reserved2;
} __attribute__((packed)) ddsheader_t;

static char pname[256] = {0};
static void *handle = 0;
static void(*glteximage2d)(unsigned,int,int,int,int,int,unsigned,unsigned,
	const void*) = 0;
static void(*gltexsubimage2d)(unsigned,int,int,int,int,int,unsigned,
	unsigned,const void*) = 0;
static void(*(*glxgetprocaddress)(const unsigned char *procname))(void) = 0;
static void *(*dlsym_real)(void*,const char*) = 0;
static void(*glcompressedteximage2d)(unsigned,int,unsigned,int,int,int,int,
	const void*) = 0;
static void(*glcompressedtexsubimage2d)(unsigned,int,int,int,int,int,unsigned,
	int,const void*) = 0;

/* generic error print function */
#define B_ERR   0
#define B_WARN  1
#define B_INFO  2
#define B_DEBUG 3
int bail( int level, const char *fmt, ... )
{
	if ( level > verbose ) return 1;
	va_list args;
	va_start(args,fmt);
	vfprintf(stderr,fmt,args);
	va_end(args);
	return 1;
}

int getpixelsize( unsigned format )
{
	if ( format == GL_RED ) return 1;
	if ( format == GL_RG ) return 2;
	if ( format == GL_RGB ) return 3;
	if ( format == GL_BGR ) return 3;
	if ( format == GL_RGBA ) return 4;
	if ( format == GL_BGRA ) return 4;
	return bail(B_WARN,"[gltxmod] unsupported format %x\n",format)&0;
}

dword crc_tab[256];

void mkcrc( void )
{
	dword c;
	int n, k;
	for ( n=0; n<256; n++ )
	{
		c = (dword)n;
		for ( k=0; k<8; k++ ) c = (c&1)?(0xEDB88320UL^(c>>1)):(c>>1);
		crc_tab[n] = c;
	}
}

dword upcrc( dword crc, const byte *buf, int len)
{
	dword c = crc;
	int n;
	for ( n=0; n<len; n++ ) c = crc_tab[(c^buf[n])&0xFF]^(c>>8);
	return c;
}

dword crc( const byte *buf, int len )
{
	return upcrc(0xFFFFFFFFUL,buf,len)^0xFFFFFFFFUL;
}

int replacetexture( unsigned target, int level, int internalFormat, int width,
	int height, int border, unsigned format, unsigned type,
	const void* pixels )
{
	int pitch = width*getpixelsize(format);
	int txsiz = pitch*height;
	if ( !pixels || !txsiz )
		return bail(B_DEBUG,"[gltxmod] skipping empty texture\n")&0;
	dword texcrc = crc(pixels,txsiz);
	char fname[256];
	snprintf(fname,256,"%s/%s",texpath,pname);
	mkdir(fname,0755);
	snprintf(fname,256,"%s/%s/%u_%08X.dds",texpath,pname,level,texcrc);
	struct stat st;
	if ( stat(fname,&st) == -1 ) return 0;
	FILE *tx = fopen(fname,"r");
	if ( !tx )
		return bail(B_ERR,"[gltxmod] could not open file for %08X:"
			" %s\n",texcrc,strerror(errno))&0;
	bail(B_INFO,"[gltxmod] replacing texture with hash %08X\n",texcrc);
	ddsheader_t ddshead;
	fread(&ddshead,1,sizeof(ddsheader_t),tx);
	if ( ddshead.magic != 0x20534444 )
	{
		fclose(tx);
		return bail(B_ERR,"[gltxmod] invalid DDS header magic %08X\n",
			ddshead.magic)&0;
	}
	int newsiz = st.st_size-sizeof(ddsheader_t);
	if ( ddshead.pf_fourcc == 0 )
	{
		void *newdata = malloc(newsiz);
		fread(newdata,1,newsiz,tx);
		fclose(tx);
		if ( (ddshead.pf_bitcount == 8) && (ddshead.pf_rmask == 0xff) )
			glteximage2d(target,level,internalFormat,ddshead.width,
				ddshead.height,border,GL_RED,GL_UNSIGNED_BYTE,
				newdata);
		else if ( (ddshead.pf_bitcount == 16)
			&& (ddshead.pf_rmask == 0xff)
			&& (ddshead.pf_amask == 0xff00) )
			glteximage2d(target,level,internalFormat,ddshead.width,
				ddshead.height,border,GL_RG,GL_UNSIGNED_BYTE,
				newdata);
		else if ( (ddshead.pf_bitcount == 24)
			&& (ddshead.pf_rmask == 0xff)
			&& (ddshead.pf_gmask == 0xff00)
			&& (ddshead.pf_bmask == 0xff0000) )
			glteximage2d(target,level,internalFormat,ddshead.width,
				ddshead.height,border,GL_RGB,GL_UNSIGNED_BYTE,
				newdata);
		else if ( (ddshead.pf_bitcount == 24)
			&& (ddshead.pf_rmask == 0xff0000)
			&& (ddshead.pf_gmask == 0xff00)
			&& (ddshead.pf_bmask == 0xff) )
			glteximage2d(target,level,internalFormat,ddshead.width,
				ddshead.height,border,GL_BGR,GL_UNSIGNED_BYTE,
				newdata);
		else if ( (ddshead.pf_bitcount == 32)
			&& (ddshead.pf_rmask == 0xff0000)
			&& (ddshead.pf_gmask == 0xff00)
			&& (ddshead.pf_bmask == 0xff)
			&& (ddshead.pf_amask == 0xff000000) )
			glteximage2d(target,level,internalFormat,ddshead.width,
				ddshead.height,border,GL_BGRA,GL_UNSIGNED_BYTE,
				newdata);
		else if ( (ddshead.pf_bitcount == 32)
			&& (ddshead.pf_rmask == 0xff)
			&& (ddshead.pf_gmask == 0xff00)
			&& (ddshead.pf_bmask == 0xff0000)
			&& (ddshead.pf_amask == 0xff000000) )
			glteximage2d(target,level,internalFormat,ddshead.width,
				ddshead.height,border,GL_RGBA,GL_UNSIGNED_BYTE,
				newdata);
		else
		{
			free(newdata);
			return bail(B_WARN,
				"[gltxmod] unsupported DDS texture\n")&0;
		}
		free(newdata);
		return 1;
	}
	if ( ddshead.pf_fourcc == 0x31545844 )
	{
		void *newdata = malloc(newsiz);
		fread(newdata,1,newsiz,tx);
		fclose(tx);
		glcompressedteximage2d(target,level,
			GL_COMPRESSED_RGBA_S3TC_DXT1_EXT,ddshead.width,
			ddshead.height,border,newsiz,newdata);
		free(newdata);
		return 1;
	}
	if ( ddshead.pf_fourcc == 0x33545844 )
	{
		void *newdata = malloc(newsiz);
		fread(newdata,1,newsiz,tx);
		fclose(tx);
		glcompressedteximage2d(target,level,
			GL_COMPRESSED_RGBA_S3TC_DXT3_EXT,ddshead.width,
			ddshead.height,border,newsiz,newdata);
		free(newdata);
		return 1;
	}
	if ( ddshead.pf_fourcc == 0x35545844 )
	{
		void *newdata = malloc(newsiz);
		fread(newdata,1,newsiz,tx);
		fclose(tx);
		glcompressedteximage2d(target,level,
			GL_COMPRESSED_RGBA_S3TC_DXT5_EXT,ddshead.width,
			ddshead.height,border,newsiz,newdata);
		free(newdata);
		return 1;
	}
	fclose(tx);
	return bail(B_WARN,"[gltxmod] unsupported DDS texture\n")&0;
}

int replacesubtexture( unsigned target, int level, int xoffset, int yoffset,
	int width, int height, unsigned format, unsigned type,
	const void* pixels )
{
	int pitch = width*getpixelsize(format);
	int txsiz = pitch*height;
	if ( !pixels || !txsiz )
		return bail(B_DEBUG,"[gltxmod] skipping empty texture\n")&0;
	dword texcrc = crc(pixels,txsiz);
	char fname[256];
	snprintf(fname,256,"%s/%s",texpath,pname);
	mkdir(fname,0755);
	snprintf(fname,256,"%s/%s/%u_%08X.dds",texpath,pname,level,texcrc);
	struct stat st;
	if ( stat(fname,&st) == -1 ) return 0;
	FILE *tx = fopen(fname,"r");
	if ( !tx )
		return bail(B_ERR,"[gltxmod] could not open file for %08X:"
			" %s\n",texcrc,strerror(errno))&0;
	bail(B_INFO,"[gltxmod] replacing texture with hash %08X\n",texcrc);
	ddsheader_t ddshead;
	fread(&ddshead,1,sizeof(ddsheader_t),tx);
	if ( ddshead.magic != 0x20534444 )
	{
		fclose(tx);
		return bail(B_ERR,"[gltxmod] invalid DDS header magic %08X\n",
			ddshead.magic)&0;
	}
	int newsiz = st.st_size-sizeof(ddsheader_t);
	if ( ddshead.pf_fourcc == 0 )
	{
		void *newdata = malloc(newsiz);
		fread(newdata,1,newsiz,tx);
		fclose(tx);
		if ( (ddshead.pf_bitcount == 8) && (ddshead.pf_rmask == 0xff) )
			gltexsubimage2d(target,level,xoffset*(ddshead.width
				/(float)width),yoffset*(ddshead.height
				/(float)height),ddshead.width,ddshead.height,
				GL_RED,GL_UNSIGNED_BYTE,newdata);
		else if ( (ddshead.pf_bitcount == 16)
			&& (ddshead.pf_rmask == 0xff)
			&& (ddshead.pf_amask == 0xff00) )
			gltexsubimage2d(target,level,xoffset*(ddshead.width
				/(float)width),yoffset*(ddshead.height
				/(float)height),ddshead.width,ddshead.height,
				GL_RG,GL_UNSIGNED_BYTE,newdata);
		else if ( (ddshead.pf_bitcount == 24)
			&& (ddshead.pf_rmask == 0xff)
			&& (ddshead.pf_gmask == 0xff00)
			&& (ddshead.pf_bmask == 0xff0000) )
			gltexsubimage2d(target,level,xoffset*(ddshead.width
				/(float)width),yoffset*(ddshead.height
				/(float)height),ddshead.width,ddshead.height,
				GL_RGB,GL_UNSIGNED_BYTE,newdata);
		else if ( (ddshead.pf_bitcount == 24)
			&& (ddshead.pf_rmask == 0xff0000)
			&& (ddshead.pf_gmask == 0xff00)
			&& (ddshead.pf_bmask == 0xff) )
			gltexsubimage2d(target,level,xoffset*(ddshead.width
				/(float)width),yoffset*(ddshead.height
				/(float)height),ddshead.width,ddshead.height,
				GL_BGR,GL_UNSIGNED_BYTE,newdata);
		else if ( (ddshead.pf_bitcount == 32)
			&& (ddshead.pf_rmask == 0xff0000)
			&& (ddshead.pf_gmask == 0xff00)
			&& (ddshead.pf_bmask == 0xff)
			&& (ddshead.pf_amask == 0xff000000) )
			gltexsubimage2d(target,level,xoffset*(ddshead.width
				/(float)width),yoffset*(ddshead.height
				/(float)height),ddshead.width,ddshead.height,
				GL_BGRA,GL_UNSIGNED_BYTE,newdata);
		else if ( (ddshead.pf_bitcount == 32)
			&& (ddshead.pf_rmask == 0xff)
			&& (ddshead.pf_gmask == 0xff00)
			&& (ddshead.pf_bmask == 0xff0000)
			&& (ddshead.pf_amask == 0xff000000) )
			gltexsubimage2d(target,level,xoffset*(ddshead.width
				/(float)width),yoffset*(ddshead.height
				/(float)height),ddshead.width,ddshead.height,
				GL_RGBA,GL_UNSIGNED_BYTE,newdata);
		else
		{
			free(newdata);
			return bail(B_WARN,
				"[gltxmod] unsupported DDS texture\n")&0;
		}
		free(newdata);
		return 1;
	}
	if ( ddshead.pf_fourcc == 0x31545844 )
	{
		void *newdata = malloc(newsiz);
		fread(newdata,1,newsiz,tx);
		fclose(tx);
		glcompressedtexsubimage2d(target,level,xoffset
			*(ddshead.width/(float)width),yoffset
			*(ddshead.height/(float)height),ddshead.width,
			ddshead.height,GL_COMPRESSED_RGBA_S3TC_DXT1_EXT,
			newsiz,newdata);
		free(newdata);
		return 1;
	}
	if ( ddshead.pf_fourcc == 0x33545844 )
	{
		void *newdata = malloc(newsiz);
		fread(newdata,1,newsiz,tx);
		fclose(tx);
		glcompressedtexsubimage2d(target,level,xoffset
			*(ddshead.width/(float)width),yoffset
			*(ddshead.height/(float)height),ddshead.width,
			ddshead.height,GL_COMPRESSED_RGBA_S3TC_DXT3_EXT,
			newsiz,newdata);
		free(newdata);
		return 1;
	}
	if ( ddshead.pf_fourcc == 0x35545844 )
	{
		void *newdata = malloc(newsiz);
		fread(newdata,1,newsiz,tx);
		fclose(tx);
		glcompressedtexsubimage2d(target,level,xoffset
			*(ddshead.width/(float)width),yoffset
			*(ddshead.height/(float)height),ddshead.width,
			ddshead.height,GL_COMPRESSED_RGBA_S3TC_DXT5_EXT,
			newsiz,newdata);
		free(newdata);
		return 1;
	}
	fclose(tx);
	return bail(B_WARN,"[gltxmod] unsupported DDS texture\n")&0;
}

int replacecompressed( unsigned target, int level, unsigned internalFormat,
	int width, int height, int border, int imageSize, const void* data )
{
	if ( !data || !imageSize )
		return bail(B_DEBUG,
			"[gltxmod] skipping empty compressed texture\n")&0;
	dword texcrc = crc(data,imageSize);
	char fname[256];
	snprintf(fname,256,"%s/%s",texpath,pname);
	mkdir(fname,0755);
	snprintf(fname,256,"%s/%s/%u_%08X.dds",texpath,pname,level,texcrc);
	struct stat st;
	if ( stat(fname,&st) == -1 ) return 0;
	FILE *tx = fopen(fname,"r");
	if ( !tx )
		return bail(B_ERR,"[gltxmod] could not open file for %08X:"
			" %s\n",texcrc,strerror(errno))&0;
	bail(B_INFO,"[gltxmod] replacing compressed texture with hash %08X\n",
		texcrc);
	ddsheader_t ddshead;
	fread(&ddshead,1,sizeof(ddsheader_t),tx);
	if ( ddshead.magic != 0x20534444 )
	{
		fclose(tx);
		return bail(B_ERR,"[gltxmod] invalid DDS header magic %08X\n",
			ddshead.magic)&0;
	}
	int newsiz = st.st_size-sizeof(ddsheader_t);
	if ( ddshead.pf_fourcc == 0 )
	{
		void *newdata = malloc(newsiz);
		fread(newdata,1,newsiz,tx);
		fclose(tx);
		if ( (ddshead.pf_bitcount == 8) && (ddshead.pf_rmask == 0xff) )
			glteximage2d(target,level,internalFormat,ddshead.width,
				ddshead.height,border,GL_RED,GL_UNSIGNED_BYTE,
				newdata);
		else if ( (ddshead.pf_bitcount == 16)
			&& (ddshead.pf_rmask == 0xff)
			&& (ddshead.pf_amask == 0xff00) )
			glteximage2d(target,level,internalFormat,ddshead.width,
				ddshead.height,border,GL_RG,GL_UNSIGNED_BYTE,
				newdata);
		else if ( (ddshead.pf_bitcount == 24)
			&& (ddshead.pf_rmask == 0xff)
			&& (ddshead.pf_gmask == 0xff00)
			&& (ddshead.pf_bmask == 0xff0000) )
			glteximage2d(target,level,internalFormat,ddshead.width,
				ddshead.height,border,GL_RGB,GL_UNSIGNED_BYTE,
				newdata);
		else if ( (ddshead.pf_bitcount == 24)
			&& (ddshead.pf_rmask == 0xff0000)
			&& (ddshead.pf_gmask == 0xff00)
			&& (ddshead.pf_bmask == 0xff) )
			glteximage2d(target,level,internalFormat,ddshead.width,
				ddshead.height,border,GL_BGR,GL_UNSIGNED_BYTE,
				newdata);
		else if ( (ddshead.pf_bitcount == 32)
			&& (ddshead.pf_rmask == 0xff0000)
			&& (ddshead.pf_gmask == 0xff00)
			&& (ddshead.pf_bmask == 0xff)
			&& (ddshead.pf_amask == 0xff000000) )
			glteximage2d(target,level,internalFormat,ddshead.width,
				ddshead.height,border,GL_BGRA,GL_UNSIGNED_BYTE,
				newdata);
		else if ( (ddshead.pf_bitcount == 32)
			&& (ddshead.pf_rmask == 0xff)
			&& (ddshead.pf_gmask == 0xff00)
			&& (ddshead.pf_bmask == 0xff0000)
			&& (ddshead.pf_amask == 0xff000000) )
			glteximage2d(target,level,internalFormat,ddshead.width,
				ddshead.height,border,GL_RGBA,GL_UNSIGNED_BYTE,
				newdata);
		else
		{
			free(newdata);
			return bail(B_WARN,
				"[gltxmod] unsupported DDS texture\n")&0;
		}
		free(newdata);
		return 1;
	}
	if ( ddshead.pf_fourcc == 0x31545844 )
	{
		void *newdata = malloc(newsiz);
		fread(newdata,1,newsiz,tx);
		fclose(tx);
		glcompressedteximage2d(target,level,
			GL_COMPRESSED_RGBA_S3TC_DXT1_EXT,ddshead.width,
			ddshead.height,border,newsiz,newdata);
		free(newdata);
		return 1;
	}
	if ( ddshead.pf_fourcc == 0x33545844 )
	{
		void *newdata = malloc(newsiz);
		fread(newdata,1,newsiz,tx);
		fclose(tx);
		glcompressedteximage2d(target,level,
			GL_COMPRESSED_RGBA_S3TC_DXT3_EXT,ddshead.width,
			ddshead.height,border,newsiz,newdata);
		free(newdata);
		return 1;
	}
	if ( ddshead.pf_fourcc == 0x35545844 )
	{
		void *newdata = malloc(newsiz);
		fread(newdata,1,newsiz,tx);
		fclose(tx);
		glcompressedteximage2d(target,level,
			GL_COMPRESSED_RGBA_S3TC_DXT5_EXT,ddshead.width,
			ddshead.height,border,newsiz,newdata);
		free(newdata);
		return 1;
	}
	fclose(tx);
	return bail(B_WARN,"[gltxmod] unsupported DDS texture\n")&0;
}

int replacesubcompressed( unsigned target, int level, int xoffset, int yoffset,
	int width, int height, unsigned format, int imageSize,
	const void* data )
{
	if ( !data || !imageSize )
		return bail(B_DEBUG,
			"[gltxmod] skipping empty compressed texture\n")&0;
	dword texcrc = crc(data,imageSize);
	char fname[256];
	snprintf(fname,256,"%s/%s",texpath,pname);
	mkdir(fname,0755);
	snprintf(fname,256,"%s/%s/%u_%08X.dds",texpath,pname,level,texcrc);
	struct stat st;
	if ( stat(fname,&st) == -1 ) return 0;
	FILE *tx = fopen(fname,"r");
	if ( !tx )
		return bail(B_ERR,"[gltxmod] could not open file for %08X:"
			" %s\n",texcrc,strerror(errno))&0;
	bail(B_INFO,"[gltxmod] replacing compressed texture with hash %08X\n",
		texcrc);
	ddsheader_t ddshead;
	fread(&ddshead,1,sizeof(ddsheader_t),tx);
	if ( ddshead.magic != 0x20534444 )
	{
		fclose(tx);
		return bail(B_ERR,"[gltxmod] invalid DDS header magic %08X\n",
			ddshead.magic)&0;
	}
	int newsiz = st.st_size-sizeof(ddsheader_t);
	if ( ddshead.pf_fourcc == 0 )
	{
		void *newdata = malloc(newsiz);
		fread(newdata,1,newsiz,tx);
		fclose(tx);
		if ( (ddshead.pf_bitcount == 8) && (ddshead.pf_rmask == 0xff) )
			gltexsubimage2d(target,level,xoffset*(ddshead.width
				/(float)width),yoffset*(ddshead.height
				/(float)height),ddshead.width,ddshead.height,
				GL_RED,GL_UNSIGNED_BYTE,newdata);
		else if ( (ddshead.pf_bitcount == 16)
			&& (ddshead.pf_rmask == 0xff)
			&& (ddshead.pf_amask == 0xff00) )
			gltexsubimage2d(target,level,xoffset*(ddshead.width
				/(float)width),yoffset*(ddshead.height
				/(float)height),ddshead.width,ddshead.height,
				GL_RG,GL_UNSIGNED_BYTE,newdata);
		else if ( (ddshead.pf_bitcount == 24)
			&& (ddshead.pf_rmask == 0xff)
			&& (ddshead.pf_gmask == 0xff00)
			&& (ddshead.pf_bmask == 0xff0000) )
			gltexsubimage2d(target,level,xoffset*(ddshead.width
				/(float)width),yoffset*(ddshead.height
				/(float)height),ddshead.width,ddshead.height,
				GL_RGB,GL_UNSIGNED_BYTE,newdata);
		else if ( (ddshead.pf_bitcount == 24)
			&& (ddshead.pf_rmask == 0xff0000)
			&& (ddshead.pf_gmask == 0xff00)
			&& (ddshead.pf_bmask == 0xff) )
			gltexsubimage2d(target,level,xoffset*(ddshead.width
				/(float)width),yoffset*(ddshead.height
				/(float)height),ddshead.width,ddshead.height,
				GL_BGR,GL_UNSIGNED_BYTE,newdata);
		else if ( (ddshead.pf_bitcount == 32)
			&& (ddshead.pf_rmask == 0xff0000)
			&& (ddshead.pf_gmask == 0xff00)
			&& (ddshead.pf_bmask == 0xff)
			&& (ddshead.pf_amask == 0xff000000) )
			gltexsubimage2d(target,level,xoffset*(ddshead.width
				/(float)width),yoffset*(ddshead.height
				/(float)height),ddshead.width,ddshead.height,
				GL_BGRA,GL_UNSIGNED_BYTE,newdata);
		else if ( (ddshead.pf_bitcount == 32)
			&& (ddshead.pf_rmask == 0xff)
			&& (ddshead.pf_gmask == 0xff00)
			&& (ddshead.pf_bmask == 0xff0000)
			&& (ddshead.pf_amask == 0xff000000) )
			gltexsubimage2d(target,level,xoffset*(ddshead.width
				/(float)width),yoffset*(ddshead.height
				/(float)height),ddshead.width,ddshead.height,
				GL_RGBA,GL_UNSIGNED_BYTE,newdata);
		else
		{
			free(newdata);
			return bail(B_WARN,
				"[gltxmod] unsupported DDS texture\n")&0;
		}
		free(newdata);
		return 1;
	}
	if ( ddshead.pf_fourcc == 0x31545844 )
	{
		void *newdata = malloc(newsiz);
		fread(newdata,1,newsiz,tx);
		fclose(tx);
		glcompressedtexsubimage2d(target,level,xoffset
			*(ddshead.width/(float)width),yoffset
			*(ddshead.height/(float)height),ddshead.width,
			ddshead.height,GL_COMPRESSED_RGBA_S3TC_DXT1_EXT,
			newsiz,newdata);
		free(newdata);
		return 1;
	}
	if ( ddshead.pf_fourcc == 0x33545844 )
	{
		void *newdata = malloc(newsiz);
		fread(newdata,1,newsiz,tx);
		fclose(tx);
		glcompressedtexsubimage2d(target,level,xoffset
			*(ddshead.width/(float)width),yoffset
			*(ddshead.height/(float)height),ddshead.width,
			ddshead.height,GL_COMPRESSED_RGBA_S3TC_DXT3_EXT,
			newsiz,newdata);
		free(newdata);
		return 1;
	}
	if ( ddshead.pf_fourcc == 0x35545844 )
	{
		void *newdata = malloc(newsiz);
		fread(newdata,1,newsiz,tx);
		fclose(tx);
		glcompressedtexsubimage2d(target,level,xoffset
			*(ddshead.width/(float)width),yoffset
			*(ddshead.height/(float)height),ddshead.width,
			ddshead.height,GL_COMPRESSED_RGBA_S3TC_DXT5_EXT,
			newsiz,newdata);
		free(newdata);
		return 1;
	}
	fclose(tx);
	return bail(B_WARN,"[gltxmod] unsupported DDS texture\n")&0;
}

void glTexImage2D( unsigned target, int level, int internalFormat,
	int width, int height, int border, unsigned format, unsigned type,
	const void *pixels )
{
	bail(B_DEBUG,"[gltxmod] glTexImage2D(%u,%d,%x,%d,%d,%d,%x,%x,%p)\n",
		target,level,internalFormat,width,height,border,format,type,
		pixels);
	if ( replacetexture(target,level,internalFormat,width,height,
		border,format,type,pixels) )
		return;
	glteximage2d(target,level,internalFormat,width,height,border,format,
		type,pixels);
}

void glTexSubImage2D( unsigned target, int level, int xoffset, int yoffset,
	int width, int height, unsigned format, unsigned type,
	const void *pixels )
{
	bail(B_DEBUG,"[gltxmod] glTexSubImage2D(%u,%d,%d,%d,%d,%d,%x,%x,"
		"%p)\n",target,level,xoffset,yoffset,width,height,format,type,
		pixels);
	if ( replacesubtexture(target,level,xoffset,yoffset,width,height,
		format,type,pixels) )
		return;
	gltexsubimage2d(target,level,xoffset,yoffset,width,height,format,
		type,pixels);
}

void glCompressedTexImage2D( unsigned target, int level,
	unsigned internalformat, int width, int height, int border,
	int imageSize, const void *data)
{
	bail(B_DEBUG,"[gltxmod] glCompressedTexImage2D(%u,%d,%u,%d,%d,%d,%d"
		",%p)\n",target,level,internalformat,width,height,border,
		imageSize,data);
	if ( replacecompressed(target,level,internalformat,width,height,border,
		imageSize,data) )
		return;
	glcompressedteximage2d(target,level,internalformat,width,height,border,
		imageSize,data);
}

void glCompressedTexSubImage2D( unsigned target, int level,
	int xoffset, int yoffset, int width, int height, unsigned format,
	int imageSize, const void *data)
{
	bail(B_DEBUG,"[gltxmod] glCompressedTexSubImage2D(%u,%d,%d,%d,%d,%d"
		",%u,%d,%p)\n",target,level,xoffset,yoffset,width,height,
		format,imageSize,data);
	if ( replacesubcompressed(target,level,xoffset,yoffset,width,height,
		format,imageSize,data) )
		return;
	glcompressedtexsubimage2d(target,level,xoffset,yoffset,width,height,
		format,imageSize,data);
}

void* SDL_GL_GetProcAddress( const char* proc )
{
	bail(B_DEBUG,"[gltxmod] program asks for \"%s\" through"
		" SDL_GL_GetProcAddress\n",proc);
	if ( !strcmp(proc,"glTexImage2D") ) return glTexImage2D;
	if ( !strcmp(proc,"glTexSubImage2D") ) return glTexSubImage2D;
	if ( !strcmp(proc,"glCompressedTexImage2D") )
		return glCompressedTexImage2D;
	if ( !strcmp(proc,"glCompressedTexSubImage2D") )
		return glCompressedTexSubImage2D;
	if ( !strcmp(proc,"dlsym") ) return dlsym;
	return glxgetprocaddress(proc);
}

void* glXGetProcAddress( const char* proc )
{
	bail(B_DEBUG,"[gltxmod] program asks for \"%s\" through"
		" glXGetProcAddress\n",proc);
	if ( !strcmp(proc,"glTexImage2D") ) return glTexImage2D;
	if ( !strcmp(proc,"glTexSubImage2D") ) return glTexSubImage2D;
	if ( !strcmp(proc,"glCompressedTexImage2D") )
		return glCompressedTexImage2D;
	if ( !strcmp(proc,"glCompressedTexSubImage2D") )
		return glCompressedTexSubImage2D;
	if ( !strcmp(proc,"dlsym") ) return dlsym;
	return glxgetprocaddress(proc);
}

void* glXGetProcAddressARB( const char* proc )
{
	bail(B_DEBUG,"[gltxmod] program asks for \"%s\" through"
		" glXGetProcAddressARB\n",proc);
	if ( !strcmp(proc,"glTexImage2D") ) return glTexImage2D;
	if ( !strcmp(proc,"glTexSubImage2D") ) return glTexSubImage2D;
	if ( !strcmp(proc,"glCompressedTexImage2D") )
		return glCompressedTexImage2D;
	if ( !strcmp(proc,"glCompressedTexSubImage2D") )
		return glCompressedTexSubImage2D;
	if ( !strcmp(proc,"dlsym") ) return dlsym;
	return glxgetprocaddress(proc);
}

extern void *_dl_sym( void*, const char*, void* );

/* TODO thread safety (is that needed in this case?) */
void* dlsym( void *handle, const char *symbol )
{
	bail(B_DEBUG,"[gltxmod] program asks for \"%s\" through dlsym\n",
		symbol);
	if ( !strcmp(symbol,"glTexImage2D") ) return glTexImage2D;
	if ( !strcmp(symbol,"glTexSubImage2D") ) return glTexSubImage2D;
	if ( !strcmp(symbol,"glCompressedTexImage2D") )
		return glCompressedTexImage2D;
	if ( !strcmp(symbol,"glCompressedTexSubImage2D") )
		return glCompressedTexSubImage2D;
	if ( !strcmp(symbol,"SDL_GL_GetProcAddress") )
		return SDL_GL_GetProcAddress;
	if ( !strcmp(symbol,"glXGetProcAddress") ) return glXGetProcAddress;
	if ( !strcmp(symbol,"glXGetProcAddressARB") )
		return glXGetProcAddressARB;
	if ( !strcmp(symbol,"dlsym") ) return dlsym;
	return dlsym_real(handle,symbol);
}

static void gltxmod_init( void ) __attribute((constructor));
static void gltxmod_exit( void ) __attribute((destructor));

static void gltxmod_init( void )
{
	mkcrc();
	bail(B_INFO,"[gltxmod] successfully hooked PID %u\n",getpid());
	FILE *comm = fopen("/proc/self/comm","r");
	if ( comm )
	{
		fscanf(comm,"%255[^\n]",&pname);
		fclose(comm);
	}
	if ( !*pname )
		bail(B_WARN,"[gltxmod] could not retrieve program name\n");
	dlsym_real = _dl_sym(RTLD_NEXT,"dlsym",gltxmod_init);
	bail(B_INFO,"[gltxmod] real dlsym at %p\n",dlsym_real);
	char *err = 0;
	handle = dlopen("libGL.so",RTLD_LAZY);
	if ( !handle )
	{
		bail(B_ERR,"[gltxmod] WE DONE FUCKED UP: %s\n",dlerror());
		exit(EXIT_FAILURE);
	}
	dlerror();
	*(void**)(&glteximage2d) = dlsym_real(handle,"glTexImage2D");
	err = dlerror();
	if ( err )
	{
		bail(B_ERR,"[gltxmod] WE DONE FUCKED UP: %s\n",dlerror());
		exit(EXIT_FAILURE);
	}
	bail(B_INFO,"[gltxmod] Found glTexImage2D at %p\n",glteximage2d);
	*(void**)(&gltexsubimage2d) = dlsym_real(handle,"glTexSubImage2D");
	err = dlerror();
	if ( err )
	{
		bail(B_ERR,"[gltxmod] WE DONE FUCKED UP: %s\n",dlerror());
		exit(EXIT_FAILURE);
	}
	bail(B_INFO,"[gltxmod] Found glTexSubImage2D at %p\n",
		gltexsubimage2d);
	*(void**)(&glcompressedteximage2d) = dlsym_real(handle,
		"glCompressedTexImage2D");
	err = dlerror();
	if ( err )
	{
		bail(B_ERR,"[gltxmod] WE DONE FUCKED UP: %s\n",dlerror());
		exit(EXIT_FAILURE);
	}
	bail(B_INFO,"[gltxmod] Found glCompressedTexImage2D at %p\n",
		glcompressedteximage2d);
	*(void**)(&glcompressedtexsubimage2d) = dlsym_real(handle,
		"glCompressedTexSubImage2D");
	err = dlerror();
	if ( err )
	{
		bail(B_ERR,"[gltxmod] WE DONE FUCKED UP: %s\n",dlerror());
		exit(EXIT_FAILURE);
	}
	bail(B_INFO,"[gltxmod] Found glCompressedTexSubImage2D at %p\n",
		glcompressedtexsubimage2d);
	*(void**)(&glxgetprocaddress) = dlsym_real(handle,"glXGetProcAddress");
	err = dlerror();
	if ( err )
	{
		bail(B_ERR,"[gltxmod] WE DONE FUCKED UP: %s\n",dlerror());
		exit(EXIT_FAILURE);
	}
	bail(B_INFO,"[gltxmod] Found glXGetProcAddress at %p\n",
		glxgetprocaddress);
}

static void gltxmod_exit( void )
{
	bail(B_INFO,"[gltxmod] successfully unhooked PID %u\n",getpid());
	dlclose(handle);
}
