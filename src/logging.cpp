#include "logging.h"

#ifdef WIN32
#	pragma warning(disable:4996)
#	include <windows.h>
#	include <direct.h>
#else
#	include <unistd.h>
#	include <sys/types.h> 
#	include <sys/stat.h>
#	include <sys/time.h>
#endif

#include <stdio.h>
#include <time.h>
#include <string.h>
#include <stdint.h>

namespace logging {

	namespace date {
#ifdef WIN32
		/*
		* Number of micro-seconds between the beginning of the Windows epoch
		* (Jan. 1, 1601) and the Unix epoch (Jan. 1, 1970).
		*
		* This assumes all Win32 compilers have 64-bit support.
		*/
#	if defined(_MSC_VER) || defined(_MSC_EXTENSIONS) || defined(__WATCOMC__)
#		define DELTA_EPOCH_IN_USEC 11644473600000000Ui64
#	else
#		define DELTA_EPOCH_IN_USEC 11644473600000000ULL
#	endif

		static uint64_t filetime_to_unix_epoch(const FILETIME *ft)
		{
			uint64_t res = (uint64_t)ft->dwHighDateTime << 32;

			res |= ft->dwLowDateTime;
			res /= 10; /* from 100 nano-sec periods to usec */
			res -= DELTA_EPOCH_IN_USEC; /* from Win epoch to Unix epoch */
			return (res);
		}

		static int gettimeofday(struct timeval *tv, void *tz)
		{
			FILETIME ft;
			uint64_t tim;

			if (!tv) {
				//errno = EINVAL;
				return (-1);
			}
			::GetSystemTimeAsFileTime(&ft);
			tim = filetime_to_unix_epoch(&ft);
			tv->tv_sec = (long)(tim / 1000000L);
			tv->tv_usec = (long)(tim % 1000000L);
			return (0);
		}
#endif

		static void localtime(time_t* now, tm* tm_now)
		{
#ifdef WIN32
			::localtime_s(tm_now, now);
#else
			::localtime_r(now, tm_now);
#endif
		}

		struct tm now(long* usec = 0)
		{
			struct tm tm_now;
			struct timeval tv;
			gettimeofday(&tv, 0);
			time_t now = tv.tv_sec;
			localtime(&now, &tm_now);
			if (usec) *usec = tv.tv_usec;
			return tm_now;
		}

		int now_day()
		{
			struct tm t = now();
			return 10000 * (t.tm_year + 1900) + 100 * (t.tm_mon + 1) + t.tm_mday;
		}
	}

#ifdef WIN32
	const char separator = '\\';
#else
	const char separator = '/';
#endif

	class auto_logger_file
	{
	public:
		auto_logger_file() { }
		~auto_logger_file() { flush(); }
		std::ostringstream& stream() { return m_oss; }

		void flush()
		{
			rotate("ydhost.log");
			std::string tmp = m_oss.str();
			m_oss.str("");
			if (!tmp.empty())
			{
				FILE *f = fopen(m_last_filename.c_str(), "a");
				if (f)
				{
#ifdef _WIN32
					fputs(tmp.c_str(), f);
#else
					int fd = fileno(f);
					lockf(fd, F_LOCK, 0l);
					fputs(tmp.c_str(), f);
					lockf(fd, F_ULOCK, 0l);
#endif
					fclose(f);
				}
			}
		}

		void rotate(const std::string& log_name)
		{
			if (m_last_day != date::now_day())
			{
				m_last_day = date::now_day();
				m_last_filename = make_filename(log_name);
			}
		}

		std::string make_filename(const std::string &p) const
		{
			char cwd[260] = { 0 };
#ifdef WIN32
			_getcwd(cwd, sizeof(cwd));
#else
			getcwd(cwd, sizeof(cwd));
#endif
			struct tm t = date::now();
			char date[20] = { 0 };
			sprintf(date, "%02d-%02d", t.tm_mon + 1, t.tm_mday);
			return std::string(cwd) + separator + "log" + separator + p + "-" + date + ".log";
		}

	private:
		std::ostringstream m_oss;
		std::string m_log_name;
		mutable int m_last_day;
		mutable std::string m_last_filename;
	};								

	template<class Writer>
	Writer& writer_single()
	{
		static Writer writer_instance;
		return writer_instance;
	}

	static const char* time_now_string()
	{
		static char str[64] = { 0 };
		long usec = 0;
		struct tm t = date::now(&usec);
		sprintf(str, "%04d-%02d-%02d %02d:%02d:%02d.%3ld", t.tm_year + 1900, t.tm_mon + 1, t.tm_mday, t.tm_hour, t.tm_min, t.tm_sec, usec / 1000);
		return str;
	}

	logger::logger()
	{ }

	logger::~logger()
	{
		auto_logger_file& lg = writer_single<auto_logger_file>();
		lg.stream() << time_now_string() << oss_.str() << std::endl;
		lg.flush();
	}
}
