#pragma once

#include <list>
#include <memory>
#include <functional>

#ifdef LIBBIMBAP_EXPORTS
#define DllExport   __declspec( dllexport ) 
#else
#define DllExport   __declspec( dllimport ) 
#endif

struct DllExport Application
{
	int64_t			rowid;
	DWORD			pid;
	std::wstring	procExePath;
	std::wstring	titleName;
	std::wstring	applicationName;
	int64_t			elapsedSeconds;
	time_t			lastStartTime;
	time_t			lastActiveTime;
};

class DllExport CTimeEvent
{
public:
	CTimeEvent();
	~CTimeEvent();

	// db
	void	SaveRecordsFromSqlite() const;
	void	LoadRecordsFromSqlite();
	void	ResetSqlite() const;
	int		GetTodayApplication(const std::wstring& AppExePath) const;

	// event
	void StartApplication(std::shared_ptr<Application> const);
	void StopApplication(std::shared_ptr<Application> const);

	// event callback register
	void AddOnStartAppHandler(std::function<void(const std::shared_ptr<Application>& app)> const & param) { onAppStarted = param; }
	void AddOnStopAppHandler(std::function<void(const std::shared_ptr<Application>& app)> const & param) { onAppStopped = param; }

	// build json report
	void MakeReports(const wchar_t* json_name) const;

	void OnActiveProcessChanged(const HWND hwnd);

	std::shared_ptr<Application> GetCurrentApplication() const;

protected:

	std::wstring GetModuleFileNameFromHwnd(const HWND hwnd) const;

	std::shared_ptr<Application> CreateAppRunningInfo(const DWORD dwPID, const DWORD dwTID, const HWND hwnd);
	std::shared_ptr<Application> GetApplication(const HWND hwnd);

	std::function<void(const std::shared_ptr<Application>& app)>	onAppStarted;
	std::function<void(const std::shared_ptr<Application>& app)>	onAppStopped;

private:
	std::shared_ptr<Application> m_currentApp;
	std::list<std::shared_ptr<Application>>	m_Applications;

	std::wstring m_SqliteDBName;

	CRITICAL_SECTION m_Sec;
};

