#ifndef __UD_ERROR_H_
#define __UD_ERROR_H_

enum usb0_log_level {
	UD_LOG_LEVEL_OFF,
	UD_LOG_LEVEL_ERROR,
	UD_LOG_LEVEL_WARNING,
	UD_LOG_LEVEL_INFO,
	UD_LOG_LEVEL_DEBUG,
};

#ifndef UD_LOG_NOP
#define UD_LOG_NOP do { } while(0)
#endif

#ifdef ENABLE_LOGGING
#define _usbi_log(mLogLevel, mStdOut, ...) if (mLogLevel <= usb_debug) fprintf(mStdOut,__VA_ARGS__)
#else
#define _usbi_log(mLogLevel, mStdOut, ...) UD_LOG_NOP
#endif

#ifdef ENABLE_DEBUG_LOGGING
#define UD_DBG(...) _usbi_log(UD_LOG_LEVEL_INFO,stdout,USB0_LOG_APPNAME" ["__FUNCTION__"]: " __VA_ARGS__)
#else
#define UD_DBG(...) UD_LOG_NOP
#endif
#define UD_INFO(...) _usbi_log(UD_LOG_LEVEL_INFO,stdout,USB0_LOG_APPNAME" info ["__FUNCTION__"]: " __VA_ARGS__)
#define UD_WRN(...) _usbi_log(UD_LOG_LEVEL_WARNING,stdout,USB0_LOG_APPNAME" warn ["__FUNCTION__"]: " __VA_ARGS__)
#define UD_ERR(...) _usbi_log(UD_LOG_LEVEL_ERROR,stderr,USB0_LOG_APPNAME" error ["__FUNCTION__"]: " __VA_ARGS__)

#define UD_INFO(...) UD_LOG_NOP
#define UD_DBG(...) UD_LOG_NOP
#define UD_WRN(...) UD_LOG_NOP
#define UD_ERR(...) UD_LOG_NOP

#endif
