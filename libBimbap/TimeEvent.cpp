#include "stdafx.h"
#include "TimeEvent.h"

// Kenny Kerr's dx library from http://dx.codeplex.com/
#include "handle.h"
#include "debug.h"

#include "sqlite3.h"

#include <codecvt>
#include <string>
#include <algorithm>
#include <chrono>
#include <ctime>
#include <iomanip>
#include <sstream>
#include <vector>

#include <Psapi.h>

// cpprest https://casablanca.codeplex.com/
#include <cpprest\json.h>
#include <cpprest\filestream.h>

#define		SAMPLE_DB_FILE_FULL_PATHNAME	L"C:\\Sample\\appslog.db"

CTimeEvent::CTimeEvent() :
	m_currentApp(nullptr),
	m_SqliteDBName(SAMPLE_DB_FILE_FULL_PATHNAME)
{
	if (!InitializeCriticalSectionAndSpinCount(&m_Sec, 0x00000400))
	{
		ASSERT(FALSE);
	}
}

CTimeEvent::~CTimeEvent()
{
	DeleteCriticalSection(&m_Sec);
}

struct connection_handle_traits
{
	using pointer = sqlite3 *;

	static auto invalid() throw() -> pointer
	{
		return nullptr;
	}

	static auto close(pointer value) throw() -> void
	{
		VERIFY_(SQLITE_OK, sqlite3_close(value));
	}
};

using connection_handle = KennyKerr::unique_handle<connection_handle_traits>;

struct statement_handle_traits
{
	using pointer = sqlite3_stmt *;

	static auto invalid() throw() -> pointer
	{
		return nullptr;
	}

	static auto close(pointer value) throw() -> void
	{
		VERIFY_(SQLITE_OK, sqlite3_finalize(value));
	}
};

using statement_handle = KennyKerr::unique_handle<statement_handle_traits>;


struct sql_exception
{
	int code;
	std::string message;

	// http://msdn.microsoft.com/en-us/library/dn793970.aspx
	sql_exception(int result, char const * text) :
		code( result ),	// changed due to https://connect.microsoft.com/VisualStudio/feedback/details/917150/compiler-error-c2797-on-code-that-previously-worked
		message( text )
	{}
};

struct connection
{
	connection_handle handle;

	auto open(char const * filename) -> void
	{
		auto local = connection_handle{};

		auto const result = sqlite3_open(filename,
			local.get_address_of());

		if (SQLITE_OK != result)
		{
			throw sql_exception{ result, sqlite3_errmsg(local.get()) };
		}

		handle = std::move(local);
	}

	auto execute(char const * text) -> void
	{
		ASSERT(handle);

		auto const result = sqlite3_exec(handle.get(),
			text,
			nullptr,
			nullptr,
			nullptr);

		if (SQLITE_OK != result)
		{
			throw sql_exception{ result, sqlite3_errmsg(handle.get()) };
		}
	}

	auto getlastid() -> int64_t
	{
		auto const result = sqlite3_last_insert_rowid(handle.get());

		return result;
	}
};

struct statement
{
	statement_handle handle;

	auto prepare(connection const & c,
		char const * text) -> void
	{
		handle.reset();

		auto const result = sqlite3_prepare_v2(c.handle.get(),
			text,
			static_cast<int>(strlen(text)),
			handle.get_address_of(),
			nullptr);

		if (SQLITE_OK != result)
		{
			throw sql_exception{ result, sqlite3_errmsg(c.handle.get()) };
		}
	}

	auto step() -> bool
	{
		ASSERT(handle);

		auto const result = sqlite3_step(handle.get());

		if (result == SQLITE_ROW) return true;

		if (result == SQLITE_DONE) return false;

		throw sql_exception{ result, sqlite3_errmsg(sqlite3_db_handle(handle.get())) };
	}

	auto get_int(int const column = 0) -> int
	{
		return sqlite3_column_int(handle.get(), column);
	}

	auto get_string(int const column = 0) -> char const *
	{
		return reinterpret_cast<char const *>(sqlite3_column_text(handle.get(), column));
	}

	// SQLite expected date string format is "YYYY-MM-DD HH:MM:SS" (there are others too)
	//strftime(buffer, TIME_STRING_LENGTH, "%Y-%m-%d %H:%M:%S", currentTime);
	auto bind_text(int index, char const * text)-> void
	{
		auto const result = sqlite3_bind_text(handle.get(), index, text, static_cast<int>(strlen(text)), SQLITE_TRANSIENT);

		if (SQLITE_OK != result)
		{
			throw sql_exception{ result, sqlite3_errmsg( sqlite3_db_handle( handle.get() )) };
		}
	}

	auto bind_int64(int index, int64_t value) -> void
	{
		auto const result = sqlite3_bind_int64(handle.get(), index, value);

		if (SQLITE_OK != result)
		{
			throw sql_exception{ result, sqlite3_errmsg(sqlite3_db_handle(handle.get())) };
		}
	}

	auto bind_int(int index, int value) -> void
	{
		auto const result = sqlite3_bind_int(handle.get(), index, value);

		if (SQLITE_OK != result)
		{
			throw sql_exception{ result, sqlite3_errmsg(sqlite3_db_handle(handle.get())) };
		}
	}

	auto reset() -> void
	{
		auto const result = sqlite3_reset(handle.get());

		if (SQLITE_OK != result)
		{
			throw sql_exception
			{
				result,
				sqlite3_errmsg(sqlite3_db_handle(handle.get()))
			};
		}
	}
};

/// Find Application object corresponding to given hwnd from running process list 
std::shared_ptr<Application> CTimeEvent::GetApplication(const HWND hwnd)
{
	// pid, tid
	DWORD dwProcessId = 0;
	DWORD dwThreadId = GetWindowThreadProcessId(hwnd, &dwProcessId);

	if (dwThreadId == 0)
	{
		// something went wrong
		return nullptr;
	}

	std::wstring modPath = GetModuleFileNameFromHwnd(hwnd);

	if (modPath.empty())
	{
		return nullptr;
	}

	//auto pos = std::find(m_Applications.begin(), m_Applications.end(), dwProcessId);
	auto pos = std::find_if(m_Applications.begin(), m_Applications.end(),
		[modPath](std::shared_ptr<Application> app){ return modPath == app->procExePath; });

	if (pos != m_Applications.end())
	{
		TRACE(L"hwnd %x -> %s, found from existing list\n", hwnd, modPath.c_str());

		return *pos;
	}
	else
	{
		TRACE(L"hwnd %x -> %s, not found, creating one...\n", hwnd, modPath.c_str());
		return CreateAppRunningInfo(dwProcessId, dwThreadId, hwnd);
	}
}

/// auto lock
/// simple approach..
template <typename T>
struct Lock
{
	T* lock;

	Lock(T* p) :
		lock(p)
	{
		// Request ownership of the critical section.
		EnterCriticalSection(lock);
	}

	~Lock()
	{
		// Release ownership of the critical section.
		LeaveCriticalSection(lock);
	}
};

/// active process context has been changed to hwnd param
void CTimeEvent::OnActiveProcessChanged(const HWND hwnd)
{
	Lock<CRITICAL_SECTION> lock(&m_Sec);

	// it happens
	if (hwnd == nullptr)
	{
		TRACE(L"CTimeEvent::OnActiveProcessChanged received NULL HWND\n");

		// active process has been changed anyway. stop the current app
		return StopApplication(GetCurrentApplication());
	}

	auto newApp = GetApplication(hwnd);
	auto oldApp = GetCurrentApplication();

	// ignore the notification if it's getting called for same hwnd twice
	if (newApp == oldApp)
	{
		return;	// do nothing
	}

	// failed to get an associated pid with the given hwnd. error
	if (!newApp)
	{
		// stop the existing app
		if (oldApp != nullptr)
		{
			StopApplication(oldApp);
		}
		
		return;
	}

	// oldApp is null when it's notified by starting newly
	if (oldApp == nullptr)
	{
		// create one
		StartApplication(newApp);
	}
	else
	{
		StopApplication(oldApp);
		StartApplication(newApp);
	}
}


std::shared_ptr<Application> CTimeEvent::GetCurrentApplication() const
{
	return m_currentApp;
}


std::wstring CTimeEvent::GetModuleFileNameFromHwnd(const HWND hwnd) const
{
	DWORD dwProcessId = 0, dwThreadId = 0;

	dwThreadId = GetWindowThreadProcessId(hwnd, &dwProcessId);
	//GetWindowModuleFileName(hwnd, path, MAX_PATH); only works for calling process

	// get module name
	auto hProc = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, FALSE, dwProcessId);
	if (!hProc)
	{
		ASSERT(!"Can't get process handle!");

		return L"Unknown";
	}

	TCHAR path[MAX_PATH] {  };
	if (!GetModuleFileNameEx(hProc, nullptr, path, MAX_PATH))
	{
		// fail
		ASSERT(!"coudn't get module file name");

		return L"Unknown";
	}

	CloseHandle(hProc);

	return path;
}

std::shared_ptr<Application> CTimeEvent::CreateAppRunningInfo(const DWORD dwPID, const DWORD dwTID, const HWND hwnd)
{
	UNREFERENCED_PARAMETER(dwTID);

	// window title name
	wchar_t windowName[512] {  };
	GetWindowTextW(hwnd, windowName, 512);

	auto currentApp = std::make_shared<Application>();

	ASSERT(currentApp);
	if (currentApp == nullptr)
		return currentApp;

	currentApp->pid			= dwPID;
	currentApp->titleName	= windowName;
	currentApp->procExePath = GetModuleFileNameFromHwnd(hwnd);

	// split exe name only
	currentApp->applicationName = 
		currentApp->procExePath.substr(
			currentApp->procExePath.find_last_of(L'\\')+1);

	currentApp->elapsedSeconds	= 0;
	currentApp->lastActiveTime	= time(nullptr);
	currentApp->lastStartTime	= time(nullptr);

	m_Applications.push_front(currentApp);

	return currentApp;
}

void CTimeEvent::StartApplication(std::shared_ptr<Application> const app)
{
	ASSERT(app);
	if (app == nullptr)
		return;

	TRACE(L"StartApplication: %s\n", app->applicationName.c_str());

	m_currentApp = app;
	app->lastStartTime = time(nullptr);

	onAppStarted(app);
}

void CTimeEvent::StopApplication(std::shared_ptr<Application> const app)
{
	// app expected to be null in case it started and reported at first
	if (app == nullptr || app->lastStartTime == 0)
	{
		return;
	}

	TRACE(L"StopApplication: %s, used %llu\n", app->applicationName.c_str(), app->elapsedSeconds);
	ASSERT(app->lastStartTime != 0);

	auto now = std::chrono::system_clock::now();

	// get duration
	auto seconds = std::chrono::duration_cast<std::chrono::seconds>(
		now - std::chrono::system_clock::from_time_t(app->lastStartTime)).count();

	app->elapsedSeconds += seconds;

	// update the last active time
	app->lastActiveTime = std::chrono::system_clock::to_time_t(now);

	// reset start time
	app->lastStartTime = 0;

	onAppStopped(app);
}

void CTimeEvent::ResetSqlite() const
{
	try
	{
		// ref http://www.dreamincode.net/forums/topic/122300-sqlite-in-c/
		connection c;

		std::wstring_convert<std::codecvt_utf8<wchar_t>> myconv;
		std::string dbname = myconv.to_bytes(m_SqliteDBName);

		c.open(dbname.c_str());	// throws exception when it fails

		// check the table exist
		statement s;
		s.prepare(c, "select count(type) from sqlite_master where type='table' and name='Apps';");

		if (s.step())
		{
			if (s.get_int() != 0)	// table exists
			{
				// drop table if exists TableName
				c.execute("drop table Apps");
			}
		}

		// https://sqlite.org/c3ref/last_insert_rowid.html
		// or simply we can use if exist - create table if not exists TableName (col1 typ1, ..., colN typN)
		// const char* sql = "CREATE TABLE IF NOT EXISTS blocks(id text primary_key,length numeric);";

		c.execute("create table Apps ( \
				  Id INTEGER PRIMARY KEY autoincrement, \
				  Pid INTEGER NOT NULL, \
				  ProcExePath nvarchar(600), \
				  TitleName nvarchar(600), \
				  ApplicationName nvarchar(100), \
				  ElapsedSeconds INTEGER, \
				  LastActiveTime nvarchar(100), \
				  Today nvarchar(40))");
	}
	catch (sql_exception const & e)
	{
		TRACE(L"%d %S\n", e.code, e.message.c_str());
	}
}

void CTimeEvent::SaveRecordsFromSqlite() const
{
	try
	{
		connection c;

		std::wstring_convert<std::codecvt_utf8<wchar_t>> myconv;
		std::string dbname = myconv.to_bytes(m_SqliteDBName);

		c.open(dbname.c_str());

		for (auto& app : m_Applications)
		{
			statement s;

			time_t now = time(nullptr);
			std::stringstream today;
			today << std::put_time(std::localtime(&now), "%Y-%m-%d ") << std::ends;

			std::stringstream fmt;
			fmt << "INSERT OR REPLACE INTO Apps ";
			fmt << "(Id, Pid,	ProcExePath,	TitleName,	ApplicationName,	ElapsedSeconds,		LastActiveTime,		Today) values ( ";
			fmt << "(SELECT Id FROM Apps WHERE Today = '" << std::put_time(std::localtime(&now), "%Y-%m-%d ") << "' ";
			fmt << "AND ProcExePath ='" << myconv.to_bytes(app->procExePath) << "' ), ";

			fmt << "? , ? , ? , ? , ? , ? , ? ); " << std::ends;

			s.prepare(c, fmt.str().c_str());
			
			s.bind_int(1, app->pid);
			s.bind_text(2, myconv.to_bytes(app->procExePath).c_str());
			s.bind_text(3, myconv.to_bytes(app->titleName).c_str());
			s.bind_text(4, myconv.to_bytes(app->applicationName).c_str());
			s.bind_int64(5, app->elapsedSeconds);

			std::tm _tm = *std::localtime(&app->lastActiveTime);
			std::stringstream lastActiveTimeStream;
			lastActiveTimeStream << std::put_time(&_tm, "%Y-%m-%d %H:%M:%S") << std::ends;
			s.bind_text(6, lastActiveTimeStream.str().c_str());

			std::stringstream todayss;
			todayss << std::put_time(std::localtime(&now), "%Y-%m-%d ") << std::ends;

			s.bind_text(7, todayss.str().c_str());

			s.step();

			app->rowid = c.getlastid();	// get index
			s.reset();
		}
	}
	catch (sql_exception const & e)
	{
		TRACE(L"%d %S\n", e.code, e.message.c_str());
	}
}

int CTimeEvent::GetTodayApplication(const std::wstring& AppExePath) const
{
	connection c;

	std::wstring_convert<std::codecvt_utf8<wchar_t>> myconv;
	std::string dbname = myconv.to_bytes(m_SqliteDBName);
	std::string dbcsAppExePath = myconv.to_bytes(AppExePath);

	c.open(dbname.c_str());

	statement s;

	s.prepare(c, "select Id from Apps where Today = ? AND ProcExePath = ?;");
	
	time_t now = time(nullptr);
	std::stringstream today;
	today << std::put_time(std::localtime(&now), "%Y-%m-%d ") << std::ends;

	s.bind_text(1, today.str().c_str());
	s.bind_text(2, dbcsAppExePath.c_str());

	if (s.step())
	{
		// returned a row
		return s.get_int(0);
	}
	else
	{
		// returned 0 or error
		return -1;
	}
}

# if 1
using namespace concurrency::streams;

void CTimeEvent::MakeReports(const wchar_t* json_name) const
{
	web::json::value root;

	// Open stream to file.
	auto ofs = std::ofstream(json_name, std::ofstream::out | std::ofstream::trunc);
	if (ofs.bad())
	{
		return;
	}

	ofs.seekp(0);	// overwrite existing
	std::vector<web::json::value> vtJsonValues;

	try
	{
		connection c;

		std::wstring_convert<std::codecvt_utf8<wchar_t>> myconv;
		std::string dbname = myconv.to_bytes(m_SqliteDBName);

		c.open(dbname.c_str());

		statement s;
		s.prepare(c, "select * from Apps where Today = ?;");
		time_t now = time(nullptr);
		std::stringstream today;
		today << std::put_time(std::localtime(&now), "%Y-%m-%d ") << std::ends;

		s.bind_text(1, today.str().c_str());

		while (s.step())
		{
			web::json::value element;
			element[L"rowid"] = web::json::value::number(s.get_int(0));	//web::json::value::string(U(""));
			element[L"procExePath"] = web::json::value::string(myconv.from_bytes(s.get_string(2)));	//
			element[L"titleName"] = web::json::value::string(myconv.from_bytes(s.get_string(3)));	//
			element[L"applicationName"] = web::json::value::string(myconv.from_bytes(s.get_string(4)));	//
			element[L"elapsedSeconds"] = web::json::value::number(s.get_int(5));
			element[L"Date"] = web::json::value::string(myconv.from_bytes(s.get_string(7)));

			vtJsonValues.emplace_back(element);
		}
	}
	catch (sql_exception const & e)
	{
		TRACE(L"%d %S\n", e.code, e.message.c_str());
	}

	root = web::json::value::array(vtJsonValues);

	root.serialize(ofs);

	ofs.close();
}
#endif

void CTimeEvent::LoadRecordsFromSqlite()
{
	try
	{
		connection c;

		std::wstring_convert<std::codecvt_utf8<wchar_t>> myconv;
		std::string dbname = myconv.to_bytes(m_SqliteDBName);

		c.open(dbname.c_str());

		statement s;
		
		s.prepare(c, "select * from Apps where Today = ?;");
		time_t now = time(nullptr);
		std::stringstream today;
		today << std::put_time(std::localtime(&now), "%Y-%m-%d ") << std::ends;

		s.bind_text(1, today.str().c_str());

		while (s.step())
		{
			auto ptr = std::make_shared<Application>();
			ASSERT(ptr);
			if (ptr == nullptr) continue;

			//Id INTEGER PRIMARY KEY autoincrement, \
			//	Pid INTEGER NOT NULL, \		1
			//	ProcExePath nvarchar(600), \ 2
			//	TitleName nvarchar(600), \ 3
			//	ApplicationName nvarchar(100), \ 4
			//	ElapsedSeconds INTEGER, \5
			//	LastActiveTime nvarchar(100), \ 6
			//	Today nvarchar(40))"); 7

			ptr->rowid = s.get_int(0);
			ptr->pid = s.get_int(1);
			ptr->procExePath = myconv.from_bytes(s.get_string(2));
			ptr->titleName = myconv.from_bytes(s.get_string(3));
			ptr->applicationName = myconv.from_bytes(s.get_string(4));
			ptr->elapsedSeconds = s.get_int(5);
			
			// init
			ptr->lastStartTime = 0;
			
			std::stringstream iss;
			iss << s.get_string(6) << std::ends;
			std::tm t{ 0 };
			iss >> std::get_time(&t, "%Y-%m-%d %H:%M:%S");

			ptr->lastActiveTime = std::mktime(&t);

			//std::stringstream todayss;
			//todayss << s.get_string(7) << std::ends;
			//std::tm today{};

			//todayss >> std::get_time(&today, "%Y-%m-%d ");

			ptr->rowid = s.get_int(0);

			//--------------------------------------------------------------------------

			auto start = std::chrono::system_clock::now();
			std::time_t ts = std::chrono::system_clock::to_time_t(start);

			std::tm time_out = *std::localtime(&ts);
			std::stringstream ss;
			ss << std::put_time(&time_out, "%Y-%m-%d %H:%M:%S %Z") << std::ends;
			
			std::tm time_in{0};
			ss >> std::get_time(&time_in, "%Y-%m-%d %H:%M:%S %Z");

			m_Applications.push_back(ptr);

			onAppStopped(ptr);
		}
	}
	catch (sql_exception const & e)
	{
		TRACE(L"%d %S\n", e.code, e.message.c_str());
	}
}

