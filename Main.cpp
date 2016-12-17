#include "SyringeDebugger.h"
#include "Log.h"

#include <string>

inline auto GetLastErrorMessage(DWORD const error = GetLastError())
{
	struct lasterror {
		DWORD error;
		std::string message;

		explicit operator bool() const noexcept {
			return this->error != ERROR_SUCCESS;
		}
	} ret{ error };

	LocalAllocHandle handle;

	auto count = FormatMessage(
		FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM |
		FORMAT_MESSAGE_IGNORE_INSERTS, nullptr, error,
		MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
		reinterpret_cast<LPTSTR>(handle.set()), 0u, nullptr);

	auto const message = static_cast<LPTSTR>(handle.get());
	while(count && isspace(static_cast<unsigned char>(message[count - 1]))) {
		--count;
	}

	ret.message.assign(message, count);

	return ret;
}

int Run(char* const lpCmdLine) {
	constexpr auto const VersionString = "Syringe 0.7.0.6";

	Log::Open("syringe.log");

	Log::WriteLine(VersionString);
	Log::WriteLine("===============");
	Log::WriteLine();
	Log::WriteLine("WinMain: lpCmdLine = \"%s\"", lpCmdLine);

	if(lpCmdLine && *lpCmdLine == '\"')
	{
		auto const pFilenameBegin = lpCmdLine + 1;

		if(auto const pFilenameEnd = strstr(pFilenameBegin, "\""))
		{
			std::string file(pFilenameBegin, pFilenameEnd);
			
			Log::WriteLine("WinMain: Trying to load executable file \"%s\"...", file.c_str());
			Log::WriteLine();
			SyringeDebugger Debugger;
			if(Debugger.RetrieveInfo(file))
			{
				Log::WriteLine("WinMain: SyringeDebugger::FindDLLs();");
				Log::WriteLine();
				Debugger.FindDLLs();

				auto const pArgs = &pFilenameEnd[1 + strspn(pFilenameEnd + 1, " ")];
				Log::WriteLine("WinMain: SyringeDebugger::Run(\"%s\");", pArgs);
				Log::WriteLine();

				if(Debugger.Run(pArgs)) {
					Log::WriteLine("WinMain: SyringeDebugger::Run finished.");
					Log::WriteLine("WinMain: Exiting on success.");
					return 0;
				}

				if(auto const lasterror = GetLastErrorMessage()) {
					Log::WriteLine("WinMain: %s (%d)", lasterror.message.c_str(), lasterror.error);

					auto const msg = "Could not run executable.\n\n\"" + file + "\"\n\n" + lasterror.message;
					MessageBoxA(nullptr, msg.c_str(), VersionString, MB_OK | MB_ICONERROR);

					Log::WriteLine("WinMain: Exiting on failure.");
					return lasterror.error;
				}
			}
			else
			{
				char msg[0x280];
				sprintf_s(msg, "Could not load executable file.\n\n\"%s\"", file.c_str());

				MessageBoxA(nullptr, msg, VersionString, MB_OK | MB_ICONERROR);

				Log::WriteLine("WinMain: ERROR: Could not load executable file, exiting...");
			}
		}
		else
		{
			char msg[0x280];
			sprintf_s(msg, "Could not evaluate command line arguments.\n\n\"%s\"", lpCmdLine);

			MessageBoxA(nullptr, msg, VersionString, MB_OK | MB_ICONERROR);

			Log::WriteLine("WinMain: ERROR: Command line arguments could not be evaluated, exiting...");
		}
	}
	else
	{
		MessageBoxA(nullptr, "Syringe cannot be run just like that.\n\nUsage:\nSyringe.exe \"<exe name>\" <arguments>",
			VersionString, MB_OK | MB_ICONINFORMATION);

		Log::WriteLine("WinMain: ERROR: No command line arguments given, exiting...");
	}

	Log::WriteLine("WinMain: Exiting on failure.");
	return 0;
}

int WINAPI WinMain(_In_ HINSTANCE hInstance, _In_opt_ HINSTANCE hPrevInstance, _In_ LPSTR lpCmdLine, _In_ int nCmdShow)
{
	UNREFERENCED_PARAMETER(hInstance);
	UNREFERENCED_PARAMETER(hPrevInstance);
	UNREFERENCED_PARAMETER(nCmdShow);

	return Run(lpCmdLine);
}
