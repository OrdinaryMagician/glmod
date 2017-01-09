/*
	gltexdump.c : A library for dumping OpenGL textures.
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

ddsheader_t ddshead =
{
	.magic = 0x20534444, .size = 124, .flags = 0x0000100F, .height = 0,
	.width = 0, .pitch = 0, .depth = 0, .mipmaps = 0,
	.reserved1 = {0,0,0,0,0,0,0,0,0,0,0}, .pf_size = 32, .pf_flags = 0x40,
	.pf_fourcc = 0, .pf_bitcount = 24, .pf_rmask = 0xff,
	.pf_gmask = 0xff00, .pf_bmask = 0xff0000, .pf_amask = 0,
	.caps = {0x1000,0,0,0}, .reserved2 = 0
};

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

int getpixelsize( unsigned format )
{
	if ( format == GL_RED ) return 1;
	if ( format == GL_RG ) return 2;
	if ( format == GL_RGB ) return 3;
	if ( format == GL_BGR ) return 3;
	if ( format == GL_RGBA ) return 4;
	if ( format == GL_BGRA ) return 4;
	fprintf(stderr,"[gltxdump] unsupported format %x\n",format);
	return 0;
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

void dumptexture( unsigned format, int mip, int width, int height,
	const void* pixels )
{
	if ( !pixels ) return;
	int pitch = width*getpixelsize(format);
	int txsiz = pitch*height;
	if ( !txsiz ) return;
	/* change the dds header accordingly */
	ddshead.width = width;
	ddshead.height = height;
	ddshead.pitch = pitch;
	ddshead.flags = 0x0000100F;
	ddshead.pf_fourcc = 0;
	if ( format == GL_RED )
	{
		ddshead.pf_flags = 0x20000;
		ddshead.pf_bitcount = 8;
		ddshead.pf_rmask = 0xff;
	}
	else if ( format == GL_RG )
	{
		ddshead.pf_flags = 0x20001;
		ddshead.pf_bitcount = 16;
		ddshead.pf_rmask = 0x00ff;
		ddshead.pf_amask = 0xff00;
	}
	else if ( format == GL_RGB )
	{
		ddshead.pf_flags = 0x40;
		ddshead.pf_bitcount = 24;
		ddshead.pf_rmask = 0x0000ff;
		ddshead.pf_gmask = 0x00ff00;
		ddshead.pf_bmask = 0xff0000;
	}
	else if ( format == GL_BGR )
	{
		ddshead.pf_flags = 0x40;
		ddshead.pf_bitcount = 24;
		ddshead.pf_rmask = 0xff0000;
		ddshead.pf_gmask = 0x00ff00;
		ddshead.pf_bmask = 0x0000ff;
	}
	else if ( format == GL_RGBA )
	{
		ddshead.pf_flags = 0x41;
		ddshead.pf_bitcount = 32;
		ddshead.pf_rmask = 0x000000ff;
		ddshead.pf_gmask = 0x0000ff00;
		ddshead.pf_bmask = 0x00ff0000;
		ddshead.pf_amask = 0xff000000;
	}
	else if ( format == GL_BGRA )
	{
		ddshead.pf_flags = 0x41;
		ddshead.pf_bitcount = 32;
		ddshead.pf_rmask = 0x00ff0000;
		ddshead.pf_gmask = 0x0000ff00;
		ddshead.pf_bmask = 0x000000ff;
		ddshead.pf_amask = 0xff000000;
	}
	else
	{
		fprintf(stderr,"[gltxdump] unsupported format %X\n",format);
		return;
	}
	dword texcrc = crc(pixels,txsiz);
	char fname[256];
	mkdir("/tmp/gltxdump",0755);
	snprintf(fname,256,"/tmp/gltxdump/%u_%08X.dds",mip,texcrc);
	struct stat st;
	if ( !stat(fname,&st) )
	{
		fprintf(stderr,"[gltxdump] %08X already dumped, skipping\n",
			texcrc);
		return;
	}
	FILE *tx = fopen(fname,"w");
	if ( !tx )
	{
		fprintf(stderr,"[gltxdump] could not open file for %08X: %s\n",
			texcrc,strerror(errno));
		return;
	}
	fprintf(stderr,"[gltxdump] dumping texture with hash %08X\n",texcrc);
	fwrite(&ddshead,1,sizeof(ddsheader_t),tx);
	fwrite(pixels,1,txsiz,tx);
	fclose(tx);
}

void dumpcompressed( unsigned format, int mip, int width, int height,
	int datasize, const void* data )
{
	if ( !data ) return;
	if ( !datasize ) return;
	/* change the dds header accordingly */
	ddshead.width = width;
	ddshead.height = height;
	ddshead.flags = 0x00001007;
	ddshead.pf_flags = 0x4;
	if ( format == GL_COMPRESSED_RGB_S3TC_DXT1_EXT )
		ddshead.pf_fourcc = 0x31545844;
	else if ( format == GL_COMPRESSED_RGBA_S3TC_DXT1_EXT )
		ddshead.pf_fourcc = 0x31545844;
	else if ( format == GL_COMPRESSED_RGBA_S3TC_DXT3_EXT )
		ddshead.pf_fourcc = 0x33545844;
	else if ( format == GL_COMPRESSED_RGBA_S3TC_DXT5_EXT )
		ddshead.pf_fourcc = 0x35545844;
	else
	{
		fprintf(stderr,"[gltxdump] unsupported compressed format %X\n",
			format);
		return;
	}
	dword texcrc = crc(data,datasize);
	char fname[256];
	mkdir("/tmp/gltxdump",0755);
	snprintf(fname,256,"/tmp/gltxdump/%u_%08X.dds",mip,texcrc);
	struct stat st;
	if ( !stat(fname,&st) )
	{
		fprintf(stderr,"[gltxdump] %08X already dumped, skipping\n",
			texcrc);
		return;
	}
	FILE *tx = fopen(fname,"w");
	if ( !tx )
	{
		fprintf(stderr,"[gltxdump] could not open file for %08X: %s\n",
			texcrc,strerror(errno));
		return;
	}
	fprintf(stderr,"[gltxdump] dumping compressed texture with hash %08X\n"
		,texcrc);
	fwrite(&ddshead,1,sizeof(ddsheader_t),tx);
	fwrite(data,1,datasize,tx);
	fclose(tx);
}

void glTexImage2D( unsigned target, int level, int internalFormat,
	int width, int height, int border, unsigned format, unsigned type,
	const void *pixels )
{
//	fprintf(stderr,"[gltxdump] glTexImage2D(%u,%d,%x,%d,%d,%d,%x,%x,%p)\n",
//		target,level,internalFormat,width,height,border,format,type,
//		pixels);
	dumptexture(format,level,width,height,pixels);
	glteximage2d(target,level,internalFormat,width,height,border,format,
		type,pixels);
}

void glTexSubImage2D( unsigned target, int level, int xoffset, int yoffset,
	int width, int height, unsigned format, unsigned type,
	const void *pixels )
{
//	fprintf(stderr,"[gltxdump] glTexSubImage2D(%u,%d,%d,%d,%d,%d,%x,%x,"
//		"%p)\n",target,level,xoffset,yoffset,width,height,format,type,
//		pixels);
	dumptexture(format,level,width,height,pixels);
	gltexsubimage2d(target,level,xoffset,yoffset,width,height,format,
		type,pixels);
}

void glCompressedTexImage2D( unsigned target, int level,
	unsigned internalformat, int width, int height, int border,
	int imageSize, const void *data)
{
//	fprintf(stderr,"[gltxdump] glCompressedTexImage2D(%u,%d,%u,%d,%d,%d,%d"
//		",%p)\n",target,level,internalformat,width,height,border,
//		imageSize,data);
	dumpcompressed(internalformat,level,width,height,imageSize,data);
	glcompressedteximage2d(target,level,internalformat,width,height,border,
		imageSize,data);
}

void glCompressedTexSubImage2D( unsigned target, int level,
	int xoffset, int yoffset, int width, int height, unsigned format,
	int imageSize, const void *data)
{
//	fprintf(stderr,"[gltxdump] glCompressedTexSubImage2D(%u,%d,%d,%d,%d,%d"
//		",%u,%d,%p)\n",target,level,xoffset,yoffset,width,height,
//		format,imageSize,data);
	dumpcompressed(format,level,width,height,imageSize,data);
	glcompressedtexsubimage2d(target,level,xoffset,yoffset,width,height,
		format,imageSize,data);
}

void* SDL_GL_GetProcAddress( const char* proc )
{
//	fprintf(stderr,"[gltxdump] program asks for \"%s\" through"
//		" SDL_GL_GetProcAddress\n",proc);
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
//	fprintf(stderr,"[gltxdump] program asks for \"%s\" through"
//		" glXGetProcAddress\n",proc);
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
//	fprintf(stderr,"[gltxdump] program asks for \"%s\" through"
//		" glXGetProcAddressARB\n",proc);
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

void* dlsym( void *handle, const char *symbol )
{
//	fprintf(stderr,"[gltxdump] program asks for \"%s\" through dlsym\n",
//		symbol);
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

static void gltxdump_init( void ) __attribute((constructor));
static void gltxdump_exit( void ) __attribute((destructor));

static void gltxdump_init( void )
{
	mkcrc();
	fprintf(stderr,"[gltxdump] successfully hooked PID %u\n",getpid());
	dlsym_real = _dl_sym(RTLD_NEXT,"dlsym",gltxdump_init);
	fprintf(stderr,"[gltxdump] real dlsym at %p\n",dlsym_real);
	char *err = 0;
	handle = dlopen("libGL.so",RTLD_LAZY);
	if ( !handle )
	{
		fprintf(stderr,"[gltxdump] WE DONE FUCKED UP: %s\n",dlerror());
		exit(EXIT_FAILURE);
	}
	dlerror();
	*(void**)(&glteximage2d) = dlsym_real(handle,"glTexImage2D");
	err = dlerror();
	if ( err )
	{
		fprintf(stderr,"[gltxdump] WE DONE FUCKED UP: %s\n",dlerror());
		exit(EXIT_FAILURE);
	}
	fprintf(stderr,"[gltxdump] Found glTexImage2D at %p\n",glteximage2d);
	*(void**)(&gltexsubimage2d) = dlsym_real(handle,"glTexSubImage2D");
	err = dlerror();
	if ( err )
	{
		fprintf(stderr,"[gltxdump] WE DONE FUCKED UP: %s\n",dlerror());
		exit(EXIT_FAILURE);
	}
	fprintf(stderr,"[gltxdump] Found glTexSubImage2D at %p\n",
		gltexsubimage2d);
	*(void**)(&glcompressedteximage2d) = dlsym_real(handle,
		"glCompressedTexImage2D");
	err = dlerror();
	if ( err )
	{
		fprintf(stderr,"[gltxdump] WE DONE FUCKED UP: %s\n",dlerror());
		exit(EXIT_FAILURE);
	}
	fprintf(stderr,"[gltxdump] Found glCompressedTexImage2D at %p\n",
		glcompressedteximage2d);
	*(void**)(&glcompressedtexsubimage2d) = dlsym_real(handle,
		"glCompressedTexSubImage2D");
	err = dlerror();
	if ( err )
	{
		fprintf(stderr,"[gltxdump] WE DONE FUCKED UP: %s\n",dlerror());
		exit(EXIT_FAILURE);
	}
	fprintf(stderr,"[gltxdump] Found glCompressedTexSubImage2D at %p\n",
		glcompressedtexsubimage2d);
	*(void**)(&glxgetprocaddress) = dlsym_real(handle,"glXGetProcAddress");
	err = dlerror();
	if ( err )
	{
		fprintf(stderr,"[gltxdump] WE DONE FUCKED UP: %s\n",dlerror());
		exit(EXIT_FAILURE);
	}
	fprintf(stderr,"[gltxdump] Found glXGetProcAddress at %p\n",
		glxgetprocaddress);
}

static void gltxdump_exit( void )
{
	fprintf(stderr,"[gltxdump] successfully unhooked PID %u\n",getpid());
	dlclose(handle);
}
