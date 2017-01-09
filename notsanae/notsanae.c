/*
	notsanae.c : le sanae face, now with generic png image support.
	The beginning of what could possibly be the first general purpose
	texture replacement modding library for Linux.
	(C)2016 Marisa Kirisame, UnSX Team.
	Released under the GNU GPLv3 (or later).
*/
#include <stdio.h>
#include <stdlib.h>
#include <dlfcn.h>
#include <string.h>
#include <unistd.h>
#include <png.h>	/* of course it requires libpng */

#define GL_UNSIGNED_BYTE   0x1401

#define GL_RGB             0x1907
#define GL_RGBA            0x1908

static void *handle = 0;
static void(*glteximage2d)(unsigned,int,int,int,int,int,unsigned,unsigned,
	const void*) = 0;
static void(*gltexsubimage2d)(unsigned,int,int,int,int,int,unsigned,
	unsigned,const void*) = 0;
static void(*(*glxgetprocaddress)(const unsigned char *procname))(void) = 0;

unsigned char *sannie = 0;
int sanniew = 0, sannieh = 0;
int sanniealpha = 0;

static int loadsanae( unsigned char *filename )
{
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

void* SDL_GL_GetProcAddress( const char* proc )
{
	fprintf(stderr,"[notsanae] program asks for \"%s\"\n",proc);
	if ( !strcmp(proc,"glTexImage2D") ) return glTexImage2D;
	if ( !strcmp(proc,"glTexSubImage2D") ) return glTexSubImage2D;
	return glxgetprocaddress(proc);
}

void* glXGetProcAddress( const char* proc )
{
	fprintf(stderr,"[notsanae] program asks for \"%s\"\n",proc);
	if ( !strcmp(proc,"glTexImage2D") ) return glTexImage2D;
	if ( !strcmp(proc,"glTexSubImage2D") ) return glTexSubImage2D;
	return glxgetprocaddress(proc);
}

void* glXGetProcAddressARB( const char* proc )
{
	fprintf(stderr,"[notsanae] program asks for \"%s\"\n",proc);
	if ( !strcmp(proc,"glTexImage2D") ) return glTexImage2D;
	if ( !strcmp(proc,"glTexSubImage2D") ) return glTexSubImage2D;
	return glxgetprocaddress(proc);
}

static void notsanae_init( void ) __attribute((constructor));
static void notsanae_exit( void ) __attribute((destructor));

static void notsanae_init( void )
{
	fprintf(stderr,"[notsanae] successfully hooked PID %u\n",getpid());
	char *err = 0;
	handle = dlopen("libGL.so",RTLD_LAZY);
	if ( !handle )
	{
		fprintf(stderr,"[notsanae] WE DONE FUCKED UP: %s\n",dlerror());
		exit(EXIT_FAILURE);
	}
	dlerror();
	*(void**)(&glteximage2d) = dlsym(handle,"glTexImage2D");
	err = dlerror();
	if ( err )
	{
		fprintf(stderr,"[notsanae] WE DONE FUCKED UP: %s\n",dlerror());
		exit(EXIT_FAILURE);
	}
	fprintf(stderr,"[notsanae] Found glTexImage2D at %p\n",glteximage2d);
	*(void**)(&gltexsubimage2d) = dlsym(handle,"glTexSubImage2D");
	err = dlerror();
	if ( err )
	{
		fprintf(stderr,"[notsanae] WE DONE FUCKED UP: %s\n",dlerror());
		exit(EXIT_FAILURE);
	}
	fprintf(stderr,"[notsanae] Found glTexSubImage2D at %p\n",
		gltexsubimage2d);
	*(void**)(&glxgetprocaddress) = dlsym(handle,"glXGetProcAddress");
	err = dlerror();
	if ( err )
	{
		fprintf(stderr,"[notsanae] WE DONE FUCKED UP: %s\n",dlerror());
		exit(EXIT_FAILURE);
	}
	fprintf(stderr,"[notsanae] Found glXGetProcAddress at %p\n",
		glxgetprocaddress);
	if ( !loadsanae(getenv("SANNIE")) )
		fprintf(stderr,"[notsanae] No image! Library useless!\n");
}

static void notsanae_exit( void )
{
	if ( sannie ) free(sannie);
	dlclose(handle);
	fprintf(stderr,"[notsanae] successfully unhooked PID %u\n",getpid());
}
