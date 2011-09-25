/*
** LuaFileSystem
** Copyright Kepler Project 2003 (http://www.keplerproject.org/luafilesystem)
**
** $Id: lfs.h 1370 2011-09-04 12:30:40Z edvin.linge@gmail.com $
*/

/* Define 'chdir' for systems that do not implement it */
#ifdef NO_CHDIR
#define chdir(p)    (-1)
#define chdir_error    "Function 'chdir' not provided by system"
#else
#define chdir_error    strerror(errno)
#endif


int luaopen_lfs (lua_State *L);
int luaopen_lfs_ext (lua_State *L);
