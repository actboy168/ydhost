# pragma once

#include <sstream>

namespace logging {
	class logger
	{
	public:
		logger();
		~logger();
		template <class T>
		logger& operator << (T const& v) { oss_ << v; return *this; }
		std::ostringstream oss_;
	private:
		logger(logger&);
		logger& operator=(logger&);
	};
}
