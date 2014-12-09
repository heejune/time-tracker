
// MFCTimerDlg.h : header file
//

#pragma once
#include "afxcmn.h"

#include <memory>

#include "../libBimbap/TimeEvent.h"

// CMFCTimerDlg dialog
class CMFCTimerDlg : public CDialogEx
{
// Construction
public:
	CMFCTimerDlg(CWnd* pParent = NULL);	// standard constructor

// Dialog Data
	enum { IDD = IDD_MFCTIMER_DIALOG };

	protected:
	virtual void DoDataExchange(CDataExchange* pDX);	// DDX/DDV support

	// make it simple
	// static accessor for thread/global
	static CMFCTimerDlg* InstancePtr;
public:
	static CMFCTimerDlg& Instance()
	{
		return *InstancePtr;
	}

	static void InsertTraceMessage(wchar_t const * format, ...);
	bool EnableorDisable();
	void AppendStringToEditCtrl(wchar_t* msg);

	void OnAppStarted(const std::shared_ptr<Application>& app);
	void OnAppStopped(const std::shared_ptr<Application>& app);

	void InsertItem();
	void RemoveItem(int n);
	void UpdateItem(int n, void*);

// Implementation
protected:
	HICON m_hIcon;

	// Generated message map functions
	virtual BOOL OnInitDialog();
	afx_msg void OnSysCommand(UINT nID, LPARAM lParam);
	afx_msg void OnPaint();
	afx_msg HCURSOR OnQueryDragIcon();
	DECLARE_MESSAGE_MAP()
public:
	afx_msg void OnClose();
	afx_msg void OnBnClickedLogBtn();

	CTimeEvent* GetEventManager() const { return eventManager.get(); }

private:
	bool	m_bHookOnOff;
	HWINEVENTHOOK m_hEventHook;

	std::unique_ptr<CTimeEvent> eventManager;

	CListCtrl m_listCtrl;
	afx_msg void OnBnClickedDbresetBtn();
	afx_msg void OnBnClickedDbsaveBtn();
	afx_msg void OnBnClickedReportBtn();

	bool m_bIdleDetected;
};

// http://dx.codeplex.com/