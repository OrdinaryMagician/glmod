/*
	notsanae.c : le sanae face, now with generic png image support.
	Now with generic OpenAL sound replacement support, too!
	And also compressed texture loading overrides!
	And dlsym overrides! Now everything can be Tim Allen, 100% guaranteed.
	Note that games with texture atlas will not quite look as expected.
	(C)2016-2017 Marisa Kirisame, UnSX Team.
	Released under the GNU GPLv3 (or later).
*/
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <dlfcn.h>
#include <unistd.h>
#include <string.h>
#include <png.h>	/* of course it requires libpng */
#include <sndfile.h>	/* for loading audio files (in wav) */

#define GL_UNSIGNED_BYTE   0x1401

#define GL_RGB             0x1907
#define GL_RGBA            0x1908

#define AL_FORMAT_MONO16   0x1101
#define AL_FORMAT_STEREO16 0x1103

static void *glhandle = 0, *alhandle = 0;
static void(*glteximage2d)(unsigned,int,int,int,int,int,unsigned,unsigned,
	const void*) = 0;
static void(*gltexsubimage2d)(unsigned,int,int,int,int,int,unsigned,
	unsigned,const void*) = 0;
static void(*(*glxgetprocaddress)(const char *))(void) = 0;
static void *(*dlsym_real)(void*,const char*) = 0;
static void(*glcompressedteximage2d)(unsigned,int,unsigned,int,int,int,int,
	const void*) = 0;
static void(*glcompressedtexsubimage2d)(unsigned,int,int,int,int,int,unsigned,
	int,const void*) = 0;
static void(*albufferdata)(unsigned,int,const void*,int,int) = 0;
static void(*(*algetprocaddress)(const char*))(void) = 0;

static unsigned char *sannie = 0;
static int sanniew = 0, sannieh = 0, sanniealpha = 0;

static short *sound = 0;
static int soundf = 0, soundr = 0, soundc = 0;

static int loadsanae( char *filename )
{
	if ( !filename ) return 0;
	png_structp pngp;
	png_infop infp;
	unsigned int sread = 0;
	int col, inter;
	FILE *pf;
	if ( !(pf = fopen(filename,"r")) ) return 0;
	pngp = png_create_read_struct(PNG_LIBPNG_VER_STRING,0,0,0);
	if ( !pngp )
	{
		fclose(pf);
		return 0;
	}
	infp = png_create_info_struct(pngp);
	if ( !infp )
	{
		fclose(pf);
		png_destroy_read_struct(&pngp,0,0);
		return 0;
	}
	if ( setjmp(png_jmpbuf(pngp)) )
	{
		png_destroy_read_struct(&pngp,&infp,0);
		fclose(pf);
		return 0;
	}
	png_init_io(pngp,pf);
	png_set_sig_bytes(pngp,sread);
	png_read_png(pngp,infp,PNG_TRANSFORM_STRIP_16|PNG_TRANSFORM_PACKING
		|PNG_TRANSFORM_EXPAND,0);
	png_uint_32 w,h;
	int dep;
	png_get_IHDR(pngp,infp,&w,&h,&dep,&col,&inter,0,0);
	sanniew = w;
	sannieh = h;
	sanniealpha = (col==6);	/* bad assumption? */
	int rbytes = png_get_rowbytes(pngp,infp);
	sannie = malloc(rbytes*h);
	png_bytepp rptr = png_get_rows(pngp,infp);
	for ( int i=0; i<h; i++ ) memcpy(sannie+(rbytes*i),rptr[i],rbytes);
	png_destroy_read_struct(&pngp,&infp,0);
	fclose(pf);
	return 1;
}

/* libsndfile sure requires less boilerplate */
static int loadsound( char *filename )
{
	if ( !filename ) return 0;
	SNDFILE *sf;
	SF_INFO inf;
	if ( !(sf = sf_open(filename,SFM_READ,&inf)) ) return 0;
	soundf = inf.frames;
	soundr = inf.samplerate;
	soundc = inf.channels;
	sound = malloc(soundf*soundc*2); /* always 16-bit audio */
	sf_read_short(sf,sound,soundf*soundc);
	sf_close(sf);
	return 1;
}

void alBufferData( unsigned int buffer, int format, const void *data,
	int size, int freq )
{
	fprintf(stderr,"[notsanae] alBufferData(%d,%x,%p,%d,%d)\n",buffer,
		format,data,size,freq);
	/*
	   skip some formats to avoid a buffer allocation failure
	   notable cases include games that use MS ADPCM audio
	*/
	if ( (format < 0x1100) || (format > 0x1103) )
	{
		fprintf(stderr,"[notsanae] format %x not supported\n",format);
		albufferdata(buffer,format,data,size,freq);
		return;
	}
	else if ( !sound )
	{
		albufferdata(buffer,format,data,size,freq);
		return;
	}
	albufferdata(buffer,(soundc==2)?AL_FORMAT_STEREO16:AL_FORMAT_MONO16,
		sound,soundf*soundc*2,soundr);
}

void glTexImage2D( unsigned target, int level, int internalFormat,
	int width, int height, int border, unsigned format, unsigned type,
	const void *pixels )
{
	fprintf(stderr,"[notsanae] glTexImage2D(%u,%d,%x,%d,%d,%d,%x,%x,%p)\n",
		target,level,internalFormat,width,height,border,format,type,
		pixels);
	if ( type != GL_UNSIGNED_BYTE )
	{
		fprintf(stderr,"[notsanae] pixel data type %x not supported\n",
			type);
		glteximage2d(target,level,internalFormat,width,height,border,
			format,type,pixels);
		return;
	}
	if ( !sannie )
	{
		glteximage2d(target,level,internalFormat,width,height,border,
			format,type,pixels);
		return;
	}
	glteximage2d(target,level,internalFormat,sanniew,sannieh,
		border,sanniealpha?GL_RGBA:GL_RGB,GL_UNSIGNED_BYTE,sannie);
}

void glTexSubImage2D( unsigned target, int level, int xoffset, int yoffset,
	int width, int height, unsigned format, unsigned type,
	const void *pixels )
{
	fprintf(stderr,"[notsanae] glTexSubImage2D(%u,%d,%d,%d,%d,%d,%x,%x,"
		"%p)\n",target,level,xoffset,yoffset,width,height,format,type,
		pixels);
	if ( type != GL_UNSIGNED_BYTE )
	{
		fprintf(stderr,"[notsanae] pixel data type %x not supported\n",
			type);
		gltexsubimage2d(target,level,xoffset,yoffset,width,height,
			format,type,pixels);
		return;
	}
	if ( !sannie )
	{
		gltexsubimage2d(target,level,xoffset,yoffset,width,height,
			format,type,pixels);
		return;
	}
	gltexsubimage2d(target,level,xoffset,yoffset,sanniew,sannieh,
		sanniealpha?GL_RGBA:GL_RGB,GL_UNSIGNED_BYTE,sannie);
}

void glCompressedTexImage2D( unsigned target, int level,
	unsigned internalformat, int width, int height, int border,
	int imageSize, const void *data )
{
	fprintf(stderr,"[notsanae] glCompressedTexImage2D(%u,%d,%x,%d,%d,%d,%d"
		",%p)\n",target,level,internalformat,width,height,border,
		imageSize,data);
	if ( !sannie )
	{
		glcompressedteximage2d(target,level,internalformat,width,
			height,border,imageSize,data);
		return;
	}
	glteximage2d(target,level,internalformat,sanniew,sannieh,border,
		sanniealpha?GL_RGBA:GL_RGB,GL_UNSIGNED_BYTE,sannie);
}

void glCompressedTexSubImage2D( unsigned target, int level,
	int xoffset, int yoffset, int width, int height, unsigned format,
	int imageSize, const void *data )
{
	fprintf(stderr,"[notsanae] glCompressedTexSubImage2D(%u,%d,%d,%d,%d,%d"
		",%x,%d,%p)\n",target,level,xoffset,yoffset,width,height,
		format,imageSize,data);
	if ( !sannie )
	{
		glcompressedtexsubimage2d(target,level,xoffset,yoffset,width,
			height,format,imageSize,data);
		return;
	}
	gltexsubimage2d(target,level,xoffset,yoffset,sanniew,sannieh,
		sanniealpha?GL_RGBA:GL_RGB,GL_UNSIGNED_BYTE,sannie);
}

void* SDL_GL_GetProcAddress( const char* proc )
{
	fprintf(stderr,"[notsanae] program asks for \"%s\"\n",proc);
	if ( !strcmp(proc,"glTexImage2D") ) return glTexImage2D;
	if ( !strcmp(proc,"glTexSubImage2D") ) return glTexSubImage2D;
	if ( !strcmp(proc,"glCompressedTexImage2D") )
		return glCompressedTexImage2D;
	if ( !strcmp(proc,"glCompressedTexSubImage2D") )
		return glCompressedTexSubImage2D;
	return glxgetprocaddress(proc);
}

void* glXGetProcAddress( const char* proc )
{
	fprintf(stderr,"[notsanae] program asks for \"%s\"\n",proc);
	if ( !strcmp(proc,"glTexImage2D") ) return glTexImage2D;
	if ( !strcmp(proc,"glTexSubImage2D") ) return glTexSubImage2D;
	if ( !strcmp(proc,"glCompressedTexImage2D") )
		return glCompressedTexImage2D;
	if ( !strcmp(proc,"glCompressedTexSubImage2D") )
		return glCompressedTexSubImage2D;
	return glxgetprocaddress(proc);
}

void* glXGetProcAddressARB( const char* proc )
{
	fprintf(stderr,"[notsanae] program asks for \"%s\"\n",proc);
	if ( !strcmp(proc,"glTexImage2D") ) return glTexImage2D;
	if ( !strcmp(proc,"glTexSubImage2D") ) return glTexSubImage2D;
	if ( !strcmp(proc,"glCompressedTexImage2D") )
		return glCompressedTexImage2D;
	if ( !strcmp(proc,"glCompressedTexSubImage2D") )
		return glCompressedTexSubImage2D;
	return glxgetprocaddress(proc);
}

void* alGetProcAddress( const char* proc )
{
	fprintf(stderr,"[notsanae] program asks for \"%s\"\n",proc);
	if ( !strcmp(proc,"alBufferData") ) return alBufferData;
	return algetprocaddress(proc);
}

extern void *_dl_sym( void*, const char*, void* );

void* dlsym( void *handle, const char *symbol )
{
	fprintf(stderr,"[notsanae] program asks for \"%s\" through dlsym\n",
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
	if ( !strcmp(symbol,"alBufferData") )
		return alBufferData;
	if ( !strcmp(symbol,"alGetProcAddress") )
		return alGetProcAddress;
	if ( !strcmp(symbol,"dlsym") ) return dlsym;
	return dlsym_real(handle,symbol);
}

static void notsanae_init( void ) __attribute((constructor));
static void notsanae_exit( void ) __attribute((destructor));

static void notsanae_init( void )
{
	dlsym_real = _dl_sym(RTLD_NEXT,"dlsym",notsanae_init);
	fprintf(stderr,"[notsanae] successfully hooked PID %u\n",getpid());
	char *err = 0;
	glhandle = dlopen("libGL.so",RTLD_LAZY);
	if ( !glhandle )
	{
		fprintf(stderr,"[notsanae] WE DONE FUCKED UP: %s\n",dlerror());
		exit(EXIT_FAILURE);
	}
	dlerror();
	*(void**)(&glteximage2d) = dlsym_real(glhandle,"glTexImage2D");
	err = dlerror();
	if ( err )
	{
		fprintf(stderr,"[notsanae] WE DONE FUCKED UP: %s\n",dlerror());
		exit(EXIT_FAILURE);
	}
	fprintf(stderr,"[notsanae] Found glTexImage2D at %p\n",glteximage2d);
	*(void**)(&gltexsubimage2d) = dlsym_real(glhandle,"glTexSubImage2D");
	err = dlerror();
	if ( err )
	{
		fprintf(stderr,"[notsanae] WE DONE FUCKED UP: %s\n",dlerror());
		exit(EXIT_FAILURE);
	}
	fprintf(stderr,"[notsanae] Found glTexSubImage2D at %p\n",
		gltexsubimage2d);
	*(void**)(&glcompressedteximage2d) = dlsym_real(glhandle,
		"glCompressedTexImage2D");
	err = dlerror();
	if ( err )
	{
		fprintf(stderr,"[notsanae] WE DONE FUCKED UP: %s\n",dlerror());
		exit(EXIT_FAILURE);
	}
	fprintf(stderr,"[notsanae] Found glCompressedTexImage2D at %p\n",
		glcompressedteximage2d);
	*(void**)(&glcompressedtexsubimage2d) = dlsym_real(glhandle,
		"glCompressedTexSubImage2D");
	err = dlerror();
	if ( err )
	{
		fprintf(stderr,"[notsanae] WE DONE FUCKED UP: %s\n",dlerror());
		exit(EXIT_FAILURE);
	}
	fprintf(stderr,"[notsanae] Found glCompressedTexSubImage2D at %p\n",
		glcompressedtexsubimage2d);
	*(void**)(&glxgetprocaddress) = dlsym_real(glhandle,
		"glXGetProcAddress");
	err = dlerror();
	if ( err )
	{
		fprintf(stderr,"[notsanae] WE DONE FUCKED UP: %s\n",dlerror());
		exit(EXIT_FAILURE);
	}
	fprintf(stderr,"[notsanae] Found glXGetProcAddress at %p\n",
		glxgetprocaddress);
	alhandle = dlopen("libopenal.so",RTLD_LAZY);
	if ( !alhandle )
	{
		fprintf(stderr,"[notsanae] WE DONE FUCKED UP: %s\n",dlerror());
		exit(EXIT_FAILURE);
	}
	dlerror();
	*(void**)(&albufferdata) = dlsym_real(alhandle,"alBufferData");
	err = dlerror();
	if ( err )
	{
		fprintf(stderr,"[notsanae] WE DONE FUCKED UP: %s\n",dlerror());
		exit(EXIT_FAILURE);
	}
	fprintf(stderr,"[notsanae] Found alBufferData at %p\n",albufferdata);
	*(void**)(&algetprocaddress) = dlsym_real(alhandle,"alGetProcAddress");
	err = dlerror();
	if ( err )
	{
		fprintf(stderr,"[notsanae] WE DONE FUCKED UP: %s\n",dlerror());
		exit(EXIT_FAILURE);
	}
	fprintf(stderr,"[notsanae] Found alGetProcAddress at %p\n",
		algetprocaddress);
	/* if no variables, fall back to generic files in working directory */
	if ( !loadsanae(getenv("SANNIE_IMAGE")) ) loadsanae("sannie.png");
	if ( !loadsound(getenv("SANNIE_SOUND")) ) loadsound("sannie.wav");
	if ( !sannie && !sound )
		fprintf(stderr,"[notsanae] nothing loaded, library useless\n");
}

static void notsanae_exit( void )
{
	if ( sound ) free(sound);
	if ( sannie ) free(sannie);
	dlclose(alhandle);
	dlclose(glhandle);
	fprintf(stderr,"[notsanae] successfully unhooked PID %u\n",getpid());
}
