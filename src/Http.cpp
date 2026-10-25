#include "Http.h"

#ifdef _WIN32

#include <windows.h>
#include <winhttp.h>

#include <vector>

namespace mc
{
	namespace
	{
		std::wstring Widen(const std::string& aValue)
		{
			if (aValue.empty())
			{
				return {};
			}
			const int size = MultiByteToWideChar(CP_UTF8, 0, aValue.c_str(), static_cast<int>(aValue.size()), nullptr, 0);
			std::wstring result(static_cast<std::size_t>(size), L'\0');
			MultiByteToWideChar(CP_UTF8, 0, aValue.c_str(), static_cast<int>(aValue.size()), result.data(), size);
			return result;
		}

		struct InternetHandle
		{
			HINTERNET Handle = nullptr;

			explicit InternetHandle(HINTERNET aHandle = nullptr) : Handle(aHandle) {}
			InternetHandle(const InternetHandle&) = delete;
			InternetHandle& operator=(const InternetHandle&) = delete;

			~InternetHandle()
			{
				if (Handle)
				{
					WinHttpCloseHandle(Handle);
				}
			}

			operator HINTERNET() const { return Handle; }
		};
	}

	std::string HttpStatusText(long aStatus)
	{
		switch (aStatus)
		{
		case 400: return "Bad request";
		case 401: return "Unauthorized - check your API key";
		case 403: return "Forbidden - the API key is missing a required permission";
		case 404: return "Not found";
		case 429: return "Rate limited - try again in a minute";
		case 500: return "Guild Wars 2 API server error";
		case 502:
		case 503:
		case 504: return "API temporarily unavailable";
		default: return "HTTP error " + std::to_string(aStatus);
		}
	}

	HttpResponse HttpGet(const std::string& aUrl, const std::string& aBearer, int aTimeoutMs)
	{
		HttpResponse response;

		URL_COMPONENTS components{};
		components.dwStructSize = sizeof(components);
		components.dwSchemeLength = static_cast<DWORD>(-1);
		components.dwHostNameLength = static_cast<DWORD>(-1);
		components.dwUrlPathLength = static_cast<DWORD>(-1);
		components.dwExtraInfoLength = static_cast<DWORD>(-1);

		const std::wstring wideUrl = Widen(aUrl);
		if (wideUrl.empty() || !WinHttpCrackUrl(wideUrl.c_str(), 0, 0, &components))
		{
			response.Error = "Invalid URL: " + aUrl;
			return response;
		}

		const std::wstring host(components.lpszHostName, components.dwHostNameLength);
		std::wstring path(components.lpszUrlPath, components.dwUrlPathLength);
		if (components.dwExtraInfoLength > 0)
		{
			path.append(components.lpszExtraInfo, components.dwExtraInfoLength);
		}
		if (path.empty())
		{
			path = L"/";
		}

		InternetHandle session(WinHttpOpen(
			L"MysticCrafting-Nexus/1.0",
			WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
			WINHTTP_NO_PROXY_NAME,
			WINHTTP_NO_PROXY_BYPASS,
			0));
		if (!session.Handle)
		{
			response.Error = "Could not initialize WinHTTP.";
			return response;
		}

		WinHttpSetTimeouts(session, 5000, 5000, 10000, aTimeoutMs);

		InternetHandle connection(WinHttpConnect(session, host.c_str(), components.nPort, 0));
		if (!connection.Handle)
		{
			response.Error = "Could not connect to the data provider.";
			return response;
		}

		const DWORD flags = components.nScheme == INTERNET_SCHEME_HTTPS ? WINHTTP_FLAG_SECURE : 0;
		InternetHandle request(WinHttpOpenRequest(connection, L"GET", path.c_str(), nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, flags));
		if (!request.Handle)
		{
			response.Error = "Could not create the HTTP request.";
			return response;
		}

		std::wstring headers = L"Accept: application/json\r\nUser-Agent: MysticCrafting-Nexus/1.0\r\n";
		if (!aBearer.empty())
		{
			headers += L"Authorization: Bearer " + Widen(aBearer) + L"\r\n";
		}

		if (!WinHttpSendRequest(
			request,
			headers.c_str(),
			static_cast<DWORD>(headers.size()),
			WINHTTP_NO_REQUEST_DATA,
			0,
			0,
			0))
		{
			response.Error = "Could not send the HTTP request (error " + std::to_string(GetLastError()) + ").";
			return response;
		}

		if (!WinHttpReceiveResponse(request, nullptr))
		{
			response.Error = "No response from the data provider (error " + std::to_string(GetLastError()) + ").";
			return response;
		}

		DWORD statusCode = 0;
		DWORD statusSize = sizeof(statusCode);
		WinHttpQueryHeaders(request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX, &statusCode, &statusSize, WINHTTP_NO_HEADER_INDEX);
		response.Status = static_cast<long>(statusCode);

		DWORD available = 0;
		do
		{
			available = 0;
			if (!WinHttpQueryDataAvailable(request, &available))
			{
				response.Error = "Could not read the HTTP response.";
				return response;
			}
			if (available == 0)
			{
				break;
			}

			std::vector<char> buffer(available);
			DWORD read = 0;
			if (!WinHttpReadData(request, buffer.data(), available, &read))
			{
				response.Error = "Could not read the HTTP response body.";
				return response;
			}
			response.Body.append(buffer.data(), read);
		} while (available > 0);

		if (!response.Ok())
		{
			response.Error = HttpStatusText(response.Status);
			if (!response.Body.empty() && response.Body.size() < 512)
			{
				response.Error += ": " + response.Body;
			}
		}

		return response;
	}

	std::string UrlEncode(const std::string& aValue)
	{
		static const char* hex = "0123456789ABCDEF";
		std::string result;
		for (unsigned char c : aValue)
		{
			if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.' || c == '~')
			{
				result.push_back(static_cast<char>(c));
			}
			else
			{
				result.push_back('%');
				result.push_back(hex[c >> 4]);
				result.push_back(hex[c & 0x0F]);
			}
		}
		return result;
	}
}

#else

namespace mc
{
	HttpResponse HttpGet(const std::string&, const std::string&, int)
	{
		HttpResponse response;
		response.Error = "HTTP is only available on Windows.";
		return response;
	}

	std::string UrlEncode(const std::string& aValue)
	{
		return aValue;
	}

	std::string HttpStatusText(long aStatus)
	{
		return "HTTP error " + std::to_string(aStatus);
	}
}

#endif
