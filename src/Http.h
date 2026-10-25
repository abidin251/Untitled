#pragma once

#include <string>

namespace mc
{
	struct HttpResponse
	{
		long Status = 0;
		std::string Body;
		std::string Error;

		bool Ok() const
		{
			return Error.empty() && Status >= 200 && Status < 300;
		}
	};

	HttpResponse HttpGet(const std::string& aUrl, const std::string& aBearer = {}, int aTimeoutMs = 20000);
	std::string UrlEncode(const std::string& aValue);
	std::string HttpStatusText(long aStatus);
}
