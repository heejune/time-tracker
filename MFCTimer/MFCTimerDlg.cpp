
// MFCTimerDlg.cpp : implementation file
//


#include "stdafx.h"
#include "MFCTimer.h"
#include "MFCTimerDlg.h"
#include "afxdialogex.h"

#include <chrono>

// use http://dx.codeplex.com/ modern cpp headers instead of built in macros
#undef ASSERT
#undef VERIFY
#undef TRACE

#include "..\libBimbap\handle.h"
#include "..\libBimbap\TimeEvent.h"

#include <sstream>
#include <deque>

#ifdef _DEBUG
#define new DEBUG_NEW
#endif

#define		ID_MY_TIMER		8080

// CAboutDlg dialog used for App About

class CAboutDlg : public CDialogEx
{
public:
	CAboutDlg();

// Dialog Data
	enum { IDD = IDD_ABOUTBOX };

	protected:
	virtual void DoDataExchange(CDataExchange* pDX);    // DDX/DDV support

// Implementation
protected:
	DECLARE_MESSAGE_MAP()
};

CAboutDlg::CAboutDlg() : CDialogEx(CAboutDlg::IDD)
{
}

void CAboutDlg::DoDataExchange(CDataExchange* pDX)
{
	CDialogEx::DoDataExchange(pDX);
}

BEGIN_MESSAGE_MAP(CAboutDlg, CDialogEx)
END_MESSAGE_MAP()


// CMFCTimerDlg dialog

// STATIC
CMFCTimerDlg* CMFCTimerDlg::InstancePtr = nullptr;

CMFCTimerDlg::CMFCTimerDlg(CWnd* pParent /*=NULL*/): 
	CDialogEx(CMFCTimerDlg::IDD, pParent), 
	m_bHookOnOff(false),
	m_bIdleDetected(false),
	m_hEventHook(nullptr)
{
	m_hIcon = AfxGetApp()->LoadIcon(IDR_MAINFRAME);

	// init
	// lame, but only allows this for test purpose
	InstancePtr = this;
}

void CMFCTimerDlg::DoDataExchange(CDataExchange* pDX)
{
	CDialogEx::DoDataExchange(pDX);
	DDX_Control(pDX, IDC_APPS_LIST, m_listCtrl);
}

BEGIN_MESSAGE_MAP(CMFCTimerDlg, CDialogEx)
	ON_WM_SYSCOMMAND()
	ON_WM_PAINT()
	ON_WM_QUERYDRAGICON()
	ON_WM_CLOSE()
	ON_BN_CLICKED(IDC_LOG_BTN, &CMFCTimerDlg::OnBnClickedLogBtn)
	ON_BN_CLICKED(IDC_DBRESET_BTN, &CMFCTimerDlg::OnBnClickedDbresetBtn)
	ON_BN_CLICKED(IDC_DBSAVE_BTN, &CMFCTimerDlg::OnBnClickedDbsaveBtn)
	ON_BN_CLICKED(IDC_REPORT_BTN, &CMFCTimerDlg::OnBnClickedReportBtn)
END_MESSAGE_MAP()


// CMFCTimerDlg message handlers

// http://social.msdn.microsoft.com/Forums/vstudio/en-US/c04e343f-f2e7-469a-8a54-48ca84f78c28/capture-switch-window-event-window-focus-alttab?forum=clr
// http://stackoverflow.com/questions/17222788/fire-event-when-user-changes-active-process
// http://stackoverflow.com/questions/4372055/detect-active-window-changed-using-c-sharp-without-polling
VOID CALLBACK WinEventProc(
	HWINEVENTHOOK hWinEventHook,
	DWORD         event,
	HWND          hwnd,
	LONG          idObject,
	LONG          idChild,
	DWORD         idEventThread,
	DWORD         dwmsEventTime)
{
	using namespace KennyKerr;
	
	if (event == EVENT_SYSTEM_FOREGROUND)
	{
		wchar_t windowName[512] = { 0 };
		GetWindowTextW(hwnd, windowName, 512);

		DWORD dwProcessId = 0;
		DWORD dwThreadId = GetWindowThreadProcessId(hwnd, &dwProcessId);

		CMFCTimerDlg::Instance().InsertTraceMessage(_T("Window Title: %ws,PID %d, TID %d \r\n"), 
			windowName, dwProcessId, dwThreadId);

		// Also, notifies CTimeEvent to know there's new active process context changed
		CMFCTimerDlg::Instance().GetEventManager()->OnActiveProcessChanged(hwnd);
	}
}

/// append debug output string into edit ctrl
void CMFCTimerDlg::AppendStringToEditCtrl(wchar_t* msg)
{
	CEdit* pEditCtrl = reinterpret_cast<CEdit*>(CMFCTimerDlg::Instance().GetDlgItem(IDC_OUTPUT_EDIT));

	if (pEditCtrl != nullptr)
	{
		pEditCtrl->SetSel(pEditCtrl->GetWindowTextLengthW(), pEditCtrl->GetWindowTextLengthW());
		pEditCtrl->ReplaceSel(msg);
	}
}

void CMFCTimerDlg::InsertTraceMessage(wchar_t const * format, ...)
{
	wchar_t buffer[512] {};

	va_list args;
	va_start(args, format);

	vswprintf_s(buffer, format, args);

	//ASSERT(-1 != _snwprintf_s(buffer,
	//	_countof(buffer),
	//	_countof(buffer)- 1,
	//	format,
	//	args));

	va_end(args);

	CMFCTimerDlg::Instance().AppendStringToEditCtrl(buffer);
}

BOOL CMFCTimerDlg::OnInitDialog()
{
	CDialogEx::OnInitDialog();

	// Add "About..." menu item to system menu.

	// IDM_ABOUTBOX must be in the system command range.
	ASSERT((IDM_ABOUTBOX & 0xFFF0) == IDM_ABOUTBOX);
	ASSERT(IDM_ABOUTBOX < 0xF000);

	CMenu* pSysMenu = GetSystemMenu(FALSE);
	if (pSysMenu != NULL)
	{
		BOOL bNameValid;
		CString strAboutMenu;
		bNameValid = strAboutMenu.LoadString(IDS_ABOUTBOX);
		ASSERT(bNameValid);
		if (!strAboutMenu.IsEmpty())
		{
			pSysMenu->AppendMenu(MF_SEPARATOR);
			pSysMenu->AppendMenu(MF_STRING, IDM_ABOUTBOX, strAboutMenu);
		}
	}

	// Set the icon for this dialog.  The framework does this automatically
	//  when the application's main window is not a dialog
	SetIcon(m_hIcon, TRUE);			// Set big icon
	SetIcon(m_hIcon, FALSE);		// Set small icon

	// TODO: Add extra initialization here
	// m_list.SetExtendedStyle(LVS_EX_FULLROWSELECT | LVS_EX_CHECKBOXES | LVS_EX_GRIDLINES ); 
	m_listCtrl.InsertColumn(0, _T("id"), LVCFMT_LEFT, 100, -1);
	m_listCtrl.InsertColumn(1, _T("application"), LVCFMT_LEFT, 100, -1);
	m_listCtrl.InsertColumn(2, _T("elapsed"), LVCFMT_LEFT, 100, -1);

	eventManager = std::make_unique<CTimeEvent>();

	ASSERT(eventManager);
	if (eventManager == nullptr)
		return FALSE;

	// register app start/stop handler 
	GetEventManager()->AddOnStartAppHandler(std::bind(&CMFCTimerDlg::OnAppStarted, this, std::placeholders::_1));
	GetEventManager()->AddOnStopAppHandler(std::bind(&CMFCTimerDlg::OnAppStopped, this, std::placeholders::_1));
	//eventManager->AddOnStartAppHandler([](const std::shared_ptr<Application>& app){ TRACE(L"");  });
	//eventManager->AddOnStopAppHandler(std::bind(&CMFCTimerDlg::OnAppStopped, this));

	// set timer for detecting idle
	SetTimer(ID_MY_TIMER, 1000 * 5, [](HWND hwnd, UINT id, UINT_PTR ptr, DWORD d) {

		if (ptr == ID_MY_TIMER)
		{
			// check idle
			LASTINPUTINFO info {};
			info.cbSize = sizeof(LASTINPUTINFO);
			if (GetLastInputInfo(&info))
			{
				DWORD dwDiff = GetTickCount() - info.dwTime;
				//TRACE(_T("Idle Time %usec\n"), dwDiff / 1000);

				if ((dwDiff / 1000) > 60)
				{
					// it's been idle state over 60 sec 
					// then stop the current running app

					// !! IDLE 상태에서 다시 IDLE 상태 이전의 동일한 앱으로 돌아올 때,
					// 감지가 안되는 것 같다.. 그래서 m_bIdleDetected 추가함
					CMFCTimerDlg::Instance().eventManager->StopApplication(
						CMFCTimerDlg::Instance().eventManager->GetCurrentApplication()
						);

					// set idle to true
					CMFCTimerDlg::Instance().m_bIdleDetected = true;
				}
				else
				{
					// timer callback when it's not idle

					// is there any process has been marked as idle?
					if (CMFCTimerDlg::Instance().m_bIdleDetected == true)
					{
						// if so, set to free as non-idle
						CMFCTimerDlg::Instance().m_bIdleDetected = false;
						// and indicates new app has been started
						CMFCTimerDlg::Instance().eventManager->StartApplication(
							CMFCTimerDlg::Instance().eventManager->GetCurrentApplication()
							);
					}
				}
			}
		}
	});

	// start watching active process 
	EnableorDisable();

	// DB load
	eventManager->LoadRecordsFromSqlite();

	return TRUE;  // return TRUE  unless you set the focus to a control
}

void CMFCTimerDlg::OnSysCommand(UINT nID, LPARAM lParam)
{
	if ((nID & 0xFFF0) == IDM_ABOUTBOX)
	{
		CAboutDlg dlgAbout;
		dlgAbout.DoModal();
	}
	else
	{
		CDialogEx::OnSysCommand(nID, lParam);
	}
}

// If you add a minimize button to your dialog, you will need the code below
//  to draw the icon.  For MFC applications using the document/view model,
//  this is automatically done for you by the framework.

void CMFCTimerDlg::OnPaint()
{
	if (IsIconic())
	{
		CPaintDC dc(this); // device context for painting

		SendMessage(WM_ICONERASEBKGND, reinterpret_cast<WPARAM>(dc.GetSafeHdc()), 0);

		// Center icon in client rectangle
		int cxIcon = GetSystemMetrics(SM_CXICON);
		int cyIcon = GetSystemMetrics(SM_CYICON);
		CRect rect;
		GetClientRect(&rect);
		int x = (rect.Width() - cxIcon + 1) / 2;
		int y = (rect.Height() - cyIcon + 1) / 2;

		// Draw the icon
		dc.DrawIcon(x, y, m_hIcon);
	}
	else
	{
		CDialogEx::OnPaint();
	}
}

// The system calls this function to obtain the cursor to display while the user drags
//  the minimized window.
HCURSOR CMFCTimerDlg::OnQueryDragIcon()
{
	return static_cast<HCURSOR>(m_hIcon);
}



void CMFCTimerDlg::OnClose()
{
	ASSERT(GetEventManager());

	if (GetEventManager() == nullptr) return;

	// DB SAVE
	GetEventManager()->SaveRecordsFromSqlite();

	// TODO: Add your message handler code here and/or call default
	if (m_bHookOnOff == true && m_hEventHook != nullptr)
	{
		UnhookWinEvent(m_hEventHook);
		m_bHookOnOff = true;
	}

	CDialogEx::OnClose();
}

void CMFCTimerDlg::InsertItem()
{

}

void CMFCTimerDlg::RemoveItem(int n)
{

}

void CMFCTimerDlg::UpdateItem(int n, void*)
{

}

void CMFCTimerDlg::OnAppStarted(const std::shared_ptr<Application>& app)
{
	// new app started, set idle to false 
	m_bIdleDetected = false;

	// IDC_STATIC_APPNAME
	static_cast<CStatic*>(GetDlgItem(IDC_STATIC_APPNAME))->SetWindowTextW(app->applicationName.c_str());

	std::chrono::seconds elapsedSec(app->elapsedSeconds);

	// if the elapsedTime is over than 60 sec, then it shows as miniutes unit
	if (app->elapsedSeconds > 60)
	{
		auto mins = std::chrono::duration_cast<std::chrono::minutes>(elapsedSec);

		// IDC_STATIC_APPTIME
		// boost::lexical_cast or std::to_string
		std::wstringstream ostr;	// output string stream
		ostr << mins.count();
		std::wstring thenumber = ostr.str();

		static_cast<CStatic*>(GetDlgItem(IDC_STATIC_APPTIME))->SetWindowTextW((ostr.str() + std::wstring(L" minutes")).c_str());
	}
	else
	{
		// IDC_STATIC_APPTIME
		// boost::lexical_cast or std::to_string
		std::wstringstream ostr;	// output string stream
		ostr << app->elapsedSeconds;
		std::wstring thenumber = ostr.str();

		static_cast<CStatic*>(GetDlgItem(IDC_STATIC_APPTIME))->SetWindowTextW((ostr.str() + std::wstring(L" seconds")).c_str());

	}
}

template <typename T>
struct ListrCtrlHandler
{
	static int FindItem(CListCtrl* plist, T* key)
	{
		for (int i = 0; i < plist->GetItemCount(); i++)
		{
			if (plist->GetItemData(i) == reinterpret_cast<DWORD_PTR>(key))
			{
				return i;
			}
		}

		return -1;
	}

	static void UpdateItem(CListCtrl* plist, int pos, int col, std::wstring str, T* key)
	{
		LVITEM lv = {};

		lv.iItem = pos;
		lv.iSubItem = col;
		lv.pszText = const_cast<LPWSTR>(str.c_str());
		lv.mask = LVIF_TEXT;

		plist->SetItemData(pos, reinterpret_cast<DWORD_PTR>(key));
		plist->SetItem(&lv);
	}

	static void CreateItem(CListCtrl* plist, std::deque<std::wstring> strings, T* data)
	{
		LVITEM lv = {};

		lv.iItem = plist->GetItemCount(); //row;
		lv.iSubItem = 0;	//col;
		lv.pszText = const_cast<LPWSTR>( std::to_wstring( plist->GetItemCount() ).c_str() );
		lv.mask = LVIF_TEXT;
		int pos = plist->InsertItem(&lv);
		plist->SetItemData(pos, reinterpret_cast<DWORD_PTR>(data));

		int index = 1;
		for (auto& wstr : strings)
		{
			lv.iSubItem = index;	//col;
			lv.pszText = const_cast<LPWSTR>(wstr.c_str());
			lv.mask = LVIF_TEXT;

			plist->SetItem(&lv);

			index++;
		}
	}
};

void CMFCTimerDlg::OnAppStopped(const std::shared_ptr<Application>& app)
{
	// find the existing item and update 
	int pos = ListrCtrlHandler<Application>::FindItem(&m_listCtrl, app.get());

	if (pos != -1) 		// found
	{
		// then update the existing one
		std::wstringstream ostr;	// output string stream
		ostr << app->elapsedSeconds;
		std::wstring thenumber = ostr.str();

		// update the one with new app
		ListrCtrlHandler<Application>::UpdateItem(&m_listCtrl, pos, 2, thenumber, app.get());
	}
	else
	{
		// if not found, then insert new one
		std::wstringstream ostr;	// output string stream
		ostr << app->elapsedSeconds;
		std::wstring thenumber = ostr.str();

		std::deque<std::wstring> strs = { app->applicationName, thenumber };
		ListrCtrlHandler<Application>::CreateItem(&m_listCtrl, strs, app.get());
	}
}

/// enable or disable watching the system window context changing state
bool CMFCTimerDlg::EnableorDisable()
{
	if (m_bHookOnOff == false)
	{
		m_hEventHook = SetWinEventHook(
			EVENT_SYSTEM_FOREGROUND,
			EVENT_SYSTEM_FOREGROUND,
			nullptr,
			WinEventProc, 0, 0,
			//WINEVENT_OUTOFCONTEXT | WINEVENT_SKIPOWNPROCESS);
			WINEVENT_OUTOFCONTEXT);

		CMFCTimerDlg::Instance().InsertTraceMessage(_T("Start watching\n"));

		CButton* btn = (CButton*)GetDlgItem(IDC_LOG_BTN);
		ASSERT(btn);
		
		if (btn)
			btn->SetWindowTextW(L"Stop");

		m_bHookOnOff = true;
	}
	else
	{
		UnhookWinEvent(m_hEventHook);
		CMFCTimerDlg::Instance().InsertTraceMessage(_T("Stop watching\r\n"));

		m_bHookOnOff = false;

		CButton* btn = (CButton*)GetDlgItem(IDC_LOG_BTN);
		btn->SetWindowTextW(L"Start");
	}

	return m_bHookOnOff;
}


auto CMFCTimerDlg::OnBnClickedLogBtn() -> void
{
	EnableorDisable();
}


void CMFCTimerDlg::OnBnClickedDbresetBtn()
{
	GetEventManager()->ResetSqlite();
}


void CMFCTimerDlg::OnBnClickedDbsaveBtn()
{
	GetEventManager()->SaveRecordsFromSqlite();
}


void CMFCTimerDlg::OnBnClickedReportBtn()
{
	// TODO
	// File open dialog for saving json
	GetEventManager()->MakeReports(L"runninglist.json");
}

