
// ElevatorSimulationDlg.cpp: 实现文件
//

#include "pch.h"
#include "framework.h"
#include "ElevatorSimulation.h"
#include "ElevatorSimulationDlg.h"
#include "afxdialogex.h"

#include <algorithm>
#include <cerrno>
#include <cmath>
#include <cwchar>
#include <limits>
#include <tuple>

#ifdef _DEBUG
#define new DEBUG_NEW
#endif

namespace
{
	constexpr int ParameterControlIds[] = {
		IDC_EDIT_FLOOR_COUNT, IDC_EDIT_ELEVATOR_COUNT, IDC_EDIT_CAPACITY,
		IDC_EDIT_MOVE_TIME, IDC_EDIT_PERSON_TIME, IDC_EDIT_PASSENGER_RATE,
		IDC_COMBO_TRAFFIC_SCENARIO, IDC_COMBO_TRAFFIC_PATTERN,
		IDC_EDIT_DURATION, IDC_EDIT_SEED, IDC_EDIT_SPEED,
		IDC_CHECK_PREDICTIVE_REBALANCING
	};

	constexpr int ParameterLabelIds[] = {
		IDC_PARAMETER_LABEL_FIRST, IDC_PARAMETER_LABEL_FIRST + 1,
		IDC_PARAMETER_LABEL_FIRST + 2, IDC_PARAMETER_LABEL_FIRST + 3,
		IDC_PARAMETER_LABEL_FIRST + 4, IDC_PARAMETER_LABEL_FIRST + 5,
		IDC_PARAMETER_LABEL_SCENARIO, IDC_PARAMETER_LABEL_FIRST + 6,
		IDC_PARAMETER_LABEL_FIRST + 7, IDC_PARAMETER_LABEL_FIRST + 8,
		IDC_PARAMETER_LABEL_FIRST + 9
	};

	constexpr const wchar_t* ParameterLabels[] = {
		L"楼层数", L"电梯数量", L"容量", L"每层时间（秒）",
		L"上下客时间（秒）", L"客流率（人/仿真秒）", L"客流场景", L"客流模式",
		L"总时长（秒）", L"随机种子", L"仿真倍速"
	};

	// 倍速滑块：位置区间映射到 [0.1x, 20.0x] 的线性刻度，滑块每格 0.1x。
	constexpr int kSpeedSliderMin = 1;
	constexpr int kSpeedSliderMax = 200;
	constexpr double kSpeedSliderStep = 0.1;
	constexpr int kSpeedSliderDefaultPos = 10; // 1.0x。

	const wchar_t* DirectionText(Direction direction)
	{
		switch (direction)
		{
		case Direction::Up: return L"↑";
		case Direction::Down: return L"↓";
		default: return L"空闲";
		}
	}

	const wchar_t* ElevatorStateText(ElevatorState state)
	{
		switch (state)
		{
		case ElevatorState::MovingUp: return L"上行中";
		case ElevatorState::MovingDown: return L"下行中";
		case ElevatorState::Boarding: return L"上客中";
		case ElevatorState::Alighting: return L"下客中";
		case ElevatorState::Stopped: return L"停站中";
		default: return L"空闲";
		}
	}

	const wchar_t* SimulationStateText(SimulationState state)
	{
		switch (state)
		{
		case SimulationState::Ready: return L"就绪";
		case SimulationState::Running: return L"运行中";
		case SimulationState::Paused: return L"已暂停";
		case SimulationState::Finished: return L"已结束";
		default: return L"未初始化";
		}
	}

	const wchar_t* TrafficPatternText(TrafficPattern pattern)
	{
		switch (pattern)
		{
		case TrafficPattern::UpPeak: return L"上行高峰";
		case TrafficPattern::DownPeak: return L"下行高峰";
		case TrafficPattern::InterFloor: return L"层间交通";
		default: return L"均匀随机";
		}
	}

	const wchar_t* OfficePhaseText(std::size_t phaseIndex)
	{
		if (phaseIndex == 0) return L"早高峰";
		if (phaseIndex == 1) return L"日间层间";
		return L"晚高峰";
	}

	CString Utf8ToCString(const std::string& text)
	{
		if (text.empty()) return CString();
		const int length = MultiByteToWideChar(CP_UTF8, 0, text.c_str(),
			static_cast<int>(text.size()), nullptr, 0);
		if (length <= 0) return CString(L"未知错误");
		CString result;
		wchar_t* buffer = result.GetBuffer(length);
		MultiByteToWideChar(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()), buffer, length);
		result.ReleaseBuffer(length);
		return result;
	}

	void SetTextIfChanged(CWnd& control, const CString& text)
	{
		CString currentText;
		control.GetWindowTextW(currentText);
		if (currentText != text)
			control.SetWindowTextW(text);
	}

	void SetDialogItemTextIfChanged(CWnd& dialog, int controlId, const CString& text)
	{
		CWnd* control = dialog.GetDlgItem(controlId);
		ASSERT(control != nullptr);
		SetTextIfChanged(*control, text);
	}
}


// 用于应用程序“关于”菜单项的 CAboutDlg 对话框

class CAboutDlg : public CDialogEx
{
public:
	CAboutDlg();

// 对话框数据
#ifdef AFX_DESIGN_TIME
	enum { IDD = IDD_ABOUTBOX };
#endif

	protected:
	virtual void DoDataExchange(CDataExchange* pDX);    // DDX/DDV 支持

// 实现
protected:
	DECLARE_MESSAGE_MAP()
};

CAboutDlg::CAboutDlg() : CDialogEx(IDD_ABOUTBOX)
{
}

void CAboutDlg::DoDataExchange(CDataExchange* pDX)
{
	CDialogEx::DoDataExchange(pDX);
}

BEGIN_MESSAGE_MAP(CAboutDlg, CDialogEx)
END_MESSAGE_MAP()


// CElevatorSimulationDlg 对话框



CElevatorSimulationDlg::CElevatorSimulationDlg(CWnd* pParent /*=nullptr*/)
	: CDialogEx(IDD_ELEVATORSIMULATION_DIALOG, pParent)
{
	m_hIcon = AfxGetApp()->LoadIcon(IDR_MAINFRAME);
}

CElevatorSimulationDlg::~CElevatorSimulationDlg()
{
	if (m_simulationWorker) m_simulationWorker->Stop();
}

void CElevatorSimulationDlg::DoDataExchange(CDataExchange* pDX)
{
	CDialogEx::DoDataExchange(pDX);
	DDX_Control(pDX, IDC_LIST_ELEVATORS, m_elevatorList);
	DDX_Control(pDX, IDC_LIST_FLOORS, m_floorList);
	DDX_Control(pDX, IDC_LIST_HALL_CALLS, m_hallCallList);
}

BEGIN_MESSAGE_MAP(CElevatorSimulationDlg, CDialogEx)
	ON_WM_SYSCOMMAND()
	ON_WM_PAINT()
	ON_WM_QUERYDRAGICON()
	ON_WM_TIMER()
	ON_WM_DESTROY()
	ON_WM_SIZE()
	ON_WM_GETMINMAXINFO()
	ON_BN_CLICKED(IDC_BUTTON_START, &CElevatorSimulationDlg::OnBnClickedStart)
	ON_BN_CLICKED(IDC_BUTTON_PAUSE, &CElevatorSimulationDlg::OnBnClickedPause)
	ON_BN_CLICKED(IDC_BUTTON_RESUME, &CElevatorSimulationDlg::OnBnClickedResume)
	ON_BN_CLICKED(IDC_BUTTON_RESET, &CElevatorSimulationDlg::OnBnClickedReset)
	ON_BN_CLICKED(IDC_BUTTON_ADD_PASSENGERS,
		&CElevatorSimulationDlg::OnBnClickedAddPassengers)
	ON_WM_HSCROLL()
	ON_EN_CHANGE(IDC_EDIT_MANUAL_FLOOR, &CElevatorSimulationDlg::OnEnChangeManualFloor)
	ON_CBN_SELCHANGE(IDC_COMBO_TRAFFIC_SCENARIO,
		&CElevatorSimulationDlg::OnCbnSelchangeTrafficScenario)
	ON_BN_CLICKED(IDC_BUTTON_PANEL_TOGGLE, &CElevatorSimulationDlg::OnBnClickedPanelToggle)
	ON_NOTIFY(TCN_SELCHANGE, IDC_TAB_LEFT, &CElevatorSimulationDlg::OnTcnSelchangeLeftTabs)
	ON_NOTIFY(TCN_SELCHANGE, IDC_TAB_PAGES, &CElevatorSimulationDlg::OnTcnSelchangePages)
	ON_NOTIFY(TCN_SELCHANGE, IDC_TAB_RIGHT, &CElevatorSimulationDlg::OnTcnSelchangeRightTabs)
	ON_NOTIFY(NM_CLICK, IDC_LIST_HALL_CALLS, &CElevatorSimulationDlg::OnNMClickHallCallList)
	ON_MESSAGE(WM_ELEVATOR_SELECTION_CHANGED,
		&CElevatorSimulationDlg::OnElevatorSelectionChanged)
END_MESSAGE_MAP()


// CElevatorSimulationDlg 消息处理程序

BOOL CElevatorSimulationDlg::OnInitDialog()
{
	CDialogEx::OnInitDialog();

	// 将“关于...”菜单项添加到系统菜单中。

	// IDM_ABOUTBOX 必须在系统命令范围内。
	ASSERT((IDM_ABOUTBOX & 0xFFF0) == IDM_ABOUTBOX);
	ASSERT(IDM_ABOUTBOX < 0xF000);

	CMenu* pSysMenu = GetSystemMenu(FALSE);
	if (pSysMenu != nullptr)
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

	// 设置此对话框的图标。  当应用程序主窗口不是对话框时，框架将自动
	//  执行此操作
	SetIcon(m_hIcon, TRUE);			// 设置大图标
	SetIcon(m_hIcon, FALSE);		// 设置小图标

	SetWindowTextW(L"多电梯群控调度仿真系统");
	ModifyStyle(0, WS_THICKFRAME | WS_MAXIMIZEBOX);
	const UINT dpi = GetDpiForWindow(m_hWnd);
	SetWindowPos(nullptr, 0, 0, MulDiv(1440, dpi, 96), MulDiv(900, dpi, 96),
		SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
	CenterWindow();
	for (CWnd* child = GetWindow(GW_CHILD); child != nullptr; child = child->GetNextWindow())
		child->ShowWindow(SW_HIDE);
	CreateUIFramework();
	InitializeListControls();
	SetDlgItemTextW(IDC_EDIT_FLOOR_COUNT, L"20");
	SetDlgItemTextW(IDC_EDIT_ELEVATOR_COUNT, L"6");
	SetDlgItemTextW(IDC_EDIT_CAPACITY, L"10");
	SetDlgItemTextW(IDC_EDIT_MOVE_TIME, L"2.0");
	SetDlgItemTextW(IDC_EDIT_PERSON_TIME, L"1.0");
	SetDlgItemTextW(IDC_EDIT_DURATION, L"300");
	SetDlgItemTextW(IDC_EDIT_PASSENGER_RATE, L"0.2");
	SetDlgItemTextW(IDC_EDIT_SPEED, L"1");
	SetDlgItemTextW(IDC_EDIT_SEED, L"42");
	m_manualFloorEdit.SetWindowTextW(L"1");
	m_manualUpEdit.SetWindowTextW(L"1");
	m_manualDownEdit.SetWindowTextW(L"0");
	m_uiReady = true;
	m_speedSlider.SetPos(kSpeedSliderDefaultPos);
	UpdateSpeedDisplay(1.0);
	UpdateTabPageVisibility();

	SimulationConfig config;
	config.capacity = 10;
	config.personTime = 1.0;
	config.simulationDuration = 300.0;
	m_simulationWorker = std::make_unique<SimulationWorker>(config, 42,
		DispatcherExecutionMode::Parallel);
	if (SetTimer(SimulationTimerId, SimulationTimerIntervalMs, nullptr) == 0)
	{
		AfxMessageBox(L"无法创建界面刷新计时器。", MB_ICONERROR);
	}
	RefreshSimulationView(true);

	return TRUE;  // 除非将焦点设置到控件，否则返回 TRUE
}

void CElevatorSimulationDlg::CreateUIFramework()
{
	CRect client;
	GetClientRect(&client);
	const UINT dpi = GetDpiForWindow(m_hWnd);
	const UINT widthLimitedDpi = static_cast<UINT>(
		(std::max)(72, MulDiv(client.Width(), 96, 1240)));
	const UINT heightLimitedDpi = static_cast<UINT>(
		(std::max)(72, MulDiv(client.Height(), 96, 760)));
	const UINT visualDpi = (std::min)({ dpi, widthLimitedDpi, heightLimitedDpi });
	auto createFont = [visualDpi](CFont& font, int pointSize, LONG weight)
	{
		LOGFONT specification{};
		specification.lfHeight = -MulDiv(pointSize, visualDpi, 72);
		specification.lfWeight = weight;
		specification.lfQuality = CLEARTYPE_QUALITY;
		wcscpy_s(specification.lfFaceName, L"Microsoft YaHei UI");
		font.CreateFontIndirect(&specification);
	};
	createFont(m_bodyFont, 10, FW_NORMAL);
	createFont(m_titleFont, 18, FW_BOLD);
	createFont(m_sectionFont, 11, FW_SEMIBOLD);
	createFont(m_pageTabFont, 10, FW_SEMIBOLD);
	createFont(m_statValueFont, 11, FW_SEMIBOLD);

	const DWORD labelStyle = WS_CHILD | WS_VISIBLE | SS_LEFT | SS_CENTERIMAGE;
	m_headerTitle.Create(L"多电梯群控调度仿真系统", labelStyle, CRect(), this, IDC_HEADER_TITLE);
	m_headerTitle.SetFont(&m_titleFont);
	m_headerStateLabel.Create(L"运行状态：", labelStyle, CRect(), this, IDC_HEADER_STATE_LABEL);
	m_headerTimeLabel.Create(L"模型时间：", labelStyle, CRect(), this, IDC_HEADER_TIME_LABEL);
	m_headerSpeedLabel.Create(L"仿真倍速：", labelStyle, CRect(), this, IDC_HEADER_SPEED_LABEL);
	m_headerSpeed.Create(L"1 倍", labelStyle, CRect(), this, IDC_HEADER_SPEED);
	m_headerTraffic.Create(L"场景：固定模式 · 当前模式：均匀随机",
		labelStyle, CRect(), this, IDC_HEADER_TRAFFIC);

	m_leftPanel.Create(L"", WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | BS_GROUPBOX,
		CRect(), this, IDC_PANEL_LEFT);
	m_leftTabs.Create(WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_CLIPSIBLINGS |
		TCS_TABS | TCS_SINGLELINE,
		CRect(), this, IDC_TAB_LEFT);
	m_leftTabs.InsertItem(0, L"仿真配置");
	m_leftTabs.InsertItem(1, L"手动客流");
	m_leftTabs.SetCurSel(0);
	m_mainPanel.Create(L"实时电梯群控主视图", WS_CHILD | WS_VISIBLE | BS_GROUPBOX,
		CRect(), this, IDC_PANEL_MAIN);
	m_rightPanel.Create(L"", WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | BS_GROUPBOX,
		CRect(), this, IDC_PANEL_RIGHT);
	m_panelToggle.Create(L"<<", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
		CRect(), this, IDC_BUTTON_PANEL_TOGGLE);
	m_buildingView.Create(&m_mainPanel, IDC_BUILDING_VIEW);
	m_buildingView.SetFont(GetFont());

	m_parameterSection.Create(L"基础参数", labelStyle, CRect(), this, IDC_SECTION_PARAMETERS);
	m_manualSection.Create(L"手动添加乘客", labelStyle, CRect(), this, IDC_MANUAL_SECTION);
	m_manualDescription.Create(
		L"指定出发楼层及上下行人数；目的楼层由模型在对应方向内生成。",
		WS_CHILD | SS_LEFT, CRect(), this, IDC_MANUAL_DESCRIPTION);
	constexpr const wchar_t* ManualLabels[] = { L"出发楼层", L"上行人数", L"下行人数" };
	constexpr UINT ManualLabelIds[] = {
		IDC_MANUAL_LABEL_FLOOR, IDC_MANUAL_LABEL_UP, IDC_MANUAL_LABEL_DOWN
	};
	for (std::size_t index = 0; index < m_manualLabels.size(); ++index)
	{
		m_manualLabels[index].Create(ManualLabels[index], labelStyle, CRect(), this,
			ManualLabelIds[index]);
	}
	const DWORD editStyle = WS_CHILD | WS_TABSTOP | ES_NUMBER | ES_AUTOHSCROLL;
	m_manualFloorEdit.CreateEx(WS_EX_CLIENTEDGE, L"EDIT", L"", editStyle,
		CRect(), this, IDC_EDIT_MANUAL_FLOOR);
	m_manualUpEdit.CreateEx(WS_EX_CLIENTEDGE, L"EDIT", L"", editStyle,
		CRect(), this, IDC_EDIT_MANUAL_UP);
	m_manualDownEdit.CreateEx(WS_EX_CLIENTEDGE, L"EDIT", L"", editStyle,
		CRect(), this, IDC_EDIT_MANUAL_DOWN);
	m_addPassengersButton.Create(L"添加到当前仿真",
		WS_CHILD | WS_TABSTOP | BS_PUSHBUTTON, CRect(), this,
		IDC_BUTTON_ADD_PASSENGERS);
	m_manualFeedback.Create(L"运行或暂停时可添加；提交后立即进入当前模型时刻。",
		WS_CHILD | SS_LEFT | SS_CENTERIMAGE, CRect(), this, IDC_MANUAL_FEEDBACK);
	m_controlSection.Create(L"仿真控制", labelStyle, CRect(), this, IDC_SECTION_CONTROLS);
	m_speedSection.Create(L"快捷倍速", labelStyle, CRect(), this, IDC_SECTION_SPEED);
	m_parameterSection.SetFont(&m_sectionFont);
	m_manualSection.SetFont(&m_sectionFont);
	m_controlSection.SetFont(&m_sectionFont);
	m_speedSection.SetFont(&m_sectionFont);
	m_trafficScenarioCombo.Create(WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_VSCROLL |
		CBS_DROPDOWNLIST, CRect(), this, IDC_COMBO_TRAFFIC_SCENARIO);
	for (const wchar_t* item : { L"固定模式", L"办公楼日周期" })
		m_trafficScenarioCombo.AddString(item);
	m_trafficScenarioCombo.SetCurSel(0);
	m_trafficPatternCombo.Create(WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_VSCROLL |
		CBS_DROPDOWNLIST, CRect(), this, IDC_COMBO_TRAFFIC_PATTERN);
	for (const wchar_t* item : { L"均匀随机", L"上行高峰", L"下行高峰", L"层间交通" })
		m_trafficPatternCombo.AddString(item);
	m_trafficPatternCombo.SetCurSel(0);
	m_predictiveRebalancingCheck.Create(L"预测式运力再平衡",
		WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX,
		CRect(), this, IDC_CHECK_PREDICTIVE_REBALANCING);
	m_predictiveRebalancingCheck.SetCheck(BST_UNCHECKED);

	for (std::size_t index = 0; index < m_parameterLabels.size(); ++index)
	{
		m_parameterLabels[index].Create(ParameterLabels[index], labelStyle, CRect(), this,
			ParameterLabelIds[index]);
	}

	m_speedSlider.Create(WS_CHILD | WS_VISIBLE | WS_TABSTOP | TBS_HORZ | TBS_AUTOTICKS,
		CRect(), this, IDC_SLIDER_SPEED);
	m_speedSlider.SetRange(kSpeedSliderMin, kSpeedSliderMax, TRUE);
	m_speedSlider.SetTicFreq(10);
	m_speedSlider.SetPageSize(10);
	m_speedSlider.SetLineSize(1);

	m_rightTabs.Create(WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_CLIPSIBLINGS |
		TCS_TABS | TCS_SINGLELINE,
		CRect(), this, IDC_TAB_RIGHT);
	m_rightTabs.InsertItem(0, L"外呼请求");
	m_rightTabs.InsertItem(1, L"电梯详情");
	m_rightTabs.InsertItem(2, L"算法观察");
	m_rightTabs.SetCurSel(0);
	m_rightHallCallTitle.Create(L"外呼请求", labelStyle, CRect(), this,
		IDC_RIGHT_HALL_CALL_TITLE);
	m_rightHallCallTitle.SetFont(&m_sectionFont);
	m_elevatorDetailTitle.Create(L"电梯详情：未选择", labelStyle, CRect(), this,
		IDC_RIGHT_ELEVATOR_TITLE);
	m_elevatorDetailTitle.SetFont(&m_sectionFont);
	m_elevatorDetailBody.Create(L"请在中央视图选择一台电梯",
		WS_CHILD | WS_VISIBLE | SS_LEFT, CRect(), this, IDC_RIGHT_ELEVATOR_DETAILS);
	m_elevatorDetailBody.SetCompactMode(true);
	m_algorithmPlaceholder.Create(L"请选择外呼请求，或先选择一台电梯自动关联其外呼",
		WS_CHILD | WS_VISIBLE | WS_BORDER | SS_LEFT,
		CRect(), this, IDC_RIGHT_ALGORITHM_PLACEHOLDER);
	m_rightAlgorithmTitle.Create(L"候选分配评分", labelStyle, CRect(), this,
		IDC_RIGHT_ALGORITHM_TITLE);
	m_rightAlgorithmTitle.SetFont(&m_sectionFont);

	m_pageTabs.Create(WS_CHILD | WS_VISIBLE | WS_TABSTOP | TCS_TABS | TCS_SINGLELINE,
		CRect(), this, IDC_TAB_PAGES);
	m_pageTabs.InsertItem(0, L"实时监控");
	m_pageTabs.InsertItem(1, L"统计分析");
	m_pageTabs.InsertItem(2, L"算法观察");
	m_pageTabs.SetCurSel(0);
	m_pageTabs.SetFont(&m_pageTabFont);
	m_elevatorStateLegend.Create(L"", WS_CHILD | WS_VISIBLE, CRect(), this,
		IDC_ELEVATOR_STATE_LEGEND);
	m_elevatorStateLegend.SetFont(GetFont());
	m_pagePlaceholder.Create(L"", WS_CHILD | WS_BORDER | SS_CENTER | SS_CENTERIMAGE,
		CRect(), this, IDC_PAGE_PLACEHOLDER);
	m_statisticsTrendView.Create(this, IDC_STATISTICS_TREND_VIEW);
	m_statisticsTrendView.SetFont(GetFont());
	m_floorTrafficHeatmapView.Create(this, IDC_FLOOR_TRAFFIC_HEATMAP_VIEW);
	m_floorTrafficHeatmapView.SetFont(GetFont());
	m_algorithmPageSummary.Create(L"请选择外呼请求，或先选择一台电梯自动关联其外呼",
		WS_CHILD | WS_BORDER | SS_LEFT | SS_CENTERIMAGE, CRect(), this,
		IDC_ALGORITHM_PAGE_SUMMARY);
	m_algorithmCandidateList.Create(WS_CHILD | WS_BORDER | WS_TABSTOP | LVS_REPORT |
		LVS_SINGLESEL | LVS_SHOWSELALWAYS | LVS_NOSORTHEADER, CRect(), this,
		IDC_LIST_ALGORITHM_CANDIDATES);
	m_floorCoverageView.Create(this, IDC_FLOOR_COVERAGE_VIEW);
	m_floorCoverageView.SetFont(GetFont());

	m_kpiBar.Create(this, IDC_STAT_CARD_FIRST);
	m_kpiBar.SetFonts(&m_pageTabFont, &m_statValueFont);

	for (CWnd* child = GetWindow(GW_CHILD); child != nullptr; child = child->GetNextWindow())
		child->SetFont(&m_bodyFont);
	m_headerTitle.SetFont(&m_titleFont);
	m_parameterSection.SetFont(&m_sectionFont);
	m_manualSection.SetFont(&m_sectionFont);
	m_controlSection.SetFont(&m_sectionFont);
	m_speedSection.SetFont(&m_sectionFont);
	m_rightHallCallTitle.SetFont(&m_sectionFont);
	m_elevatorDetailTitle.SetFont(&m_sectionFont);
	m_rightAlgorithmTitle.SetFont(&m_sectionFont);
	m_pageTabs.SetFont(&m_pageTabFont);
	m_leftTabs.SetFont(&m_pageTabFont);
	m_rightTabs.SetFont(&m_pageTabFont);
	m_kpiBar.SetFonts(&m_pageTabFont, &m_statValueFont);

	for (int controlId : ParameterControlIds)
		GetDlgItem(controlId)->ShowWindow(SW_SHOW);
	for (int controlId : { IDC_BUTTON_START, IDC_BUTTON_PAUSE, IDC_BUTTON_RESUME,
		IDC_BUTTON_RESET, IDC_SIMULATION_STATE, IDC_MODEL_TIME,
		IDC_LIST_HALL_CALLS })
	{
		GetDlgItem(controlId)->ShowWindow(SW_SHOW);
	}
	m_elevatorList.ShowWindow(SW_HIDE);
	m_floorList.ShowWindow(SW_HIDE);
	UpdateLeftPanelVisibility();
}

void CElevatorSimulationDlg::RelayoutUI()
{
	if (!m_uiReady) return;

	CRect client;
	GetClientRect(&client);
	const UINT dpi = GetDpiForWindow(m_hWnd);
	const UINT widthLimitedDpi = static_cast<UINT>(
		(std::max)(72, MulDiv(client.Width(), 96, 1240)));
	const UINT heightLimitedDpi = static_cast<UINT>(
		(std::max)(72, MulDiv(client.Height(), 96, 760)));
	const UINT layoutDpi = (std::min)({ dpi, widthLimitedDpi, heightLimitedDpi });
	auto toDevice = [layoutDpi](int value) { return MulDiv(value, layoutDpi, 96); };
	const int clientWidth = MulDiv(client.Width(), 96, layoutDpi);
	const int clientHeight = MulDiv(client.Height(), 96, layoutDpi);
	const int margin = 16;
	const int gap = 10;
	const int headerHeight = 64;
	const int leftWidth = 260;
	const int tabsHeight = 38;
	const int statsHeight = 84;
	const int contentTop = headerHeight + gap;
	const int contentBottom = clientHeight - 16;
	const int centerX = margin + leftWidth + gap;
	const bool realTimePage = m_pageTabs.GetCurSel() == 0;
	const int rightWidth = m_rightPanelExpanded ? 350 : 48;
	const int rightX = clientWidth - margin - rightWidth;
	const int centerRight = realTimePage ? rightX - gap : clientWidth - margin;
	const int centerWidth = centerRight - centerX;
	const int navigationY = contentTop;
	const int mainTop = navigationY + tabsHeight + gap;
	const int statsY = contentBottom - statsHeight;
	const int mainBottom = statsY - gap;
	const int mainHeight = mainBottom - mainTop;
	auto place = [&toDevice](CWnd& control, int x, int y, int width, int height)
	{
		control.MoveWindow(toDevice(x), toDevice(y), toDevice(width), toDevice(height), FALSE);
	};
	auto move = [this, &toDevice](int controlId, int x, int y, int width, int height)
	{
		GetDlgItem(controlId)->MoveWindow(toDevice(x), toDevice(y),
			toDevice(width), toDevice(height), FALSE);
	};
	m_pageTabs.SetPadding(CSize(toDevice(centerWidth < 720 ? 12 : 20), toDevice(7)));
	m_leftTabs.SetPadding(CSize(toDevice(14), toDevice(6)));
	m_rightTabs.SetPadding(CSize(toDevice(12), toDevice(6)));

	place(m_headerTitle, margin + 2, 7, 360, 42);
	const int headerInfoX = (std::max)(390, centerX);
	const int headerInfoWidth = clientWidth - margin - headerInfoX;
	const int headerPart = headerInfoWidth / 3;
	place(m_headerStateLabel, headerInfoX, 4, 80, 27);
	move(IDC_SIMULATION_STATE, headerInfoX + 80, 4, headerPart - 80, 27);
	place(m_headerTimeLabel, headerInfoX + headerPart, 4, 80, 27);
	move(IDC_MODEL_TIME, headerInfoX + headerPart + 80, 4, headerPart - 80, 27);
	place(m_headerSpeedLabel, headerInfoX + headerPart * 2, 4, 80, 27);
	place(m_headerSpeed, headerInfoX + headerPart * 2 + 80, 4,
		headerInfoWidth - headerPart * 2 - 80, 27);
	place(m_headerTraffic, headerInfoX, 33, headerInfoWidth, 24);

	const int sidePanelTop = contentTop + tabsHeight + gap;
	const bool compactSidePanel = mainBottom - sidePanelTop < 600;
	place(m_leftTabs, margin, contentTop, leftWidth, tabsHeight);
	place(m_leftPanel, margin, sidePanelTop, leftWidth, mainBottom - sidePanelTop);
	const int leftInnerX = margin + 14;
	const int leftInnerWidth = leftWidth - 28;
	const int panelContentTop = sidePanelTop + 20;
	const int labelWidth = 116;
	const int editX = leftInnerX + labelWidth;
	const int editWidth = leftInnerWidth - labelWidth;
	place(m_parameterSection, leftInnerX, panelContentTop, leftInnerWidth, 24);
	const int firstRowY = panelContentTop + (compactSidePanel ? 24 : 29);
	const int rowHeight = compactSidePanel ? 23 : 29;
	for (std::size_t index = 0; index < m_parameterLabels.size(); ++index)
	{
		const int rowY = firstRowY + static_cast<int>(index) * rowHeight;
		place(m_parameterLabels[index], leftInnerX, rowY, labelWidth - 8, 24);
		const int controlId = ParameterControlIds[index];
		move(controlId, editX, rowY, editWidth,
			controlId == IDC_COMBO_TRAFFIC_SCENARIO ||
			controlId == IDC_COMBO_TRAFFIC_PATTERN ? 140 : 24);
	}

	const int rebalanceY = firstRowY + static_cast<int>(m_parameterLabels.size()) * rowHeight + 4;
	place(m_predictiveRebalancingCheck, leftInnerX, rebalanceY, leftInnerWidth, 24);

	place(m_manualSection, leftInnerX, panelContentTop, leftInnerWidth, 24);
	place(m_manualDescription, leftInnerX, panelContentTop + 31, leftInnerWidth,
		compactSidePanel ? 42 : 50);
	const int manualRowGap = compactSidePanel ? 34 : 38;
	const int manualFirstRowY = panelContentTop + (compactSidePanel ? 83 : 94);
	for (std::size_t index = 0; index < m_manualLabels.size(); ++index)
	{
		const int rowY = manualFirstRowY + static_cast<int>(index) * manualRowGap;
		place(m_manualLabels[index], leftInnerX, rowY, labelWidth - 8, 28);
	}
	place(m_manualFloorEdit, editX, manualFirstRowY, editWidth, 28);
	place(m_manualUpEdit, editX, manualFirstRowY + manualRowGap, editWidth, 28);
	place(m_manualDownEdit, editX, manualFirstRowY + manualRowGap * 2, editWidth, 28);
	const int manualButtonY = manualFirstRowY + manualRowGap * 3 +
		(compactSidePanel ? 10 : 12);
	place(m_addPassengersButton, leftInnerX, manualButtonY, leftInnerWidth,
		compactSidePanel ? 36 : 38);
	place(m_manualFeedback, leftInnerX,
		manualButtonY + (compactSidePanel ? 44 : 48), leftInnerWidth,
		compactSidePanel ? 42 : 56);

	const int speedButtonY = mainBottom - 43;
	const int speedY = speedButtonY - 28;
	const int actionY = speedY - 91;
	const int controlsY = actionY - 29;
	place(m_controlSection, leftInnerX, controlsY, leftInnerWidth, 23);
	const int actionWidth = (leftInnerWidth - 8) / 2;
	move(IDC_BUTTON_START, leftInnerX, actionY, actionWidth, 34);
	move(IDC_BUTTON_PAUSE, leftInnerX + actionWidth + 8, actionY, actionWidth, 34);
	move(IDC_BUTTON_RESUME, leftInnerX, actionY + 42, actionWidth, 34);
	move(IDC_BUTTON_RESET, leftInnerX + actionWidth + 8, actionY + 42, actionWidth, 34);

	place(m_speedSection, leftInnerX, speedY, leftInnerWidth, 22);
	place(m_speedSlider, leftInnerX, speedButtonY, leftInnerWidth, 34);

	const int legendPreferredWidth = 320;
	const int pageTabsMinimumWidth = 260;
	const int pageTabsWidth = (std::min)(360,
		(std::max)(pageTabsMinimumWidth, centerWidth - gap - legendPreferredWidth));
	place(m_pageTabs, centerX, navigationY, pageTabsWidth, tabsHeight);
	place(m_elevatorStateLegend, centerX + pageTabsWidth + gap, navigationY,
		(std::max)(0, centerWidth - pageTabsWidth - gap), tabsHeight);

	if (realTimePage)
	{
		place(m_mainPanel, centerX, mainTop, centerWidth, mainHeight);
		place(m_buildingView, 10, 22, centerWidth - 20, mainHeight - 32);

		place(m_rightPanel, rightX, sidePanelTop, rightWidth, mainBottom - sidePanelTop);
		place(m_panelToggle, rightX + rightWidth - 43, contentTop + 5, 32, 28);
		if (m_rightPanelExpanded)
		{
			const int innerX = rightX + 12;
			const int innerWidth = rightWidth - 24;
			place(m_rightTabs, rightX, contentTop, rightWidth - 48, tabsHeight);
			const int innerTop = sidePanelTop + 17;
			const int innerBottom = mainBottom - 10;
			const int sectionTitleHeight = 26;
			const int bodyTop = innerTop + sectionTitleHeight;
			place(m_rightHallCallTitle, innerX + 2, innerTop,
				innerWidth - 4, sectionTitleHeight);
			place(m_hallCallList, innerX, bodyTop, innerWidth, innerBottom - bodyTop);
			place(m_elevatorDetailTitle, innerX + 2, innerTop,
				innerWidth - 4, sectionTitleHeight);
			place(m_elevatorDetailBody, innerX, bodyTop, innerWidth, innerBottom - bodyTop);
			place(m_rightAlgorithmTitle, innerX + 2, innerTop,
				innerWidth - 4, sectionTitleHeight);
			place(m_algorithmPlaceholder, innerX, bodyTop, innerWidth, 58);
			place(m_algorithmCandidateList, innerX, bodyTop + 66, innerWidth,
				(std::max)(100, innerBottom - bodyTop - 66));
		}
	}
	else
	{
		const int pageWidth = clientWidth - margin - centerX;
		if (m_pageTabs.GetCurSel() == 1)
		{
			const int heatmapWidth = pageWidth * 38 / 100;
			place(m_statisticsTrendView, centerX, mainTop,
				pageWidth - heatmapWidth - gap, mainHeight);
			place(m_floorTrafficHeatmapView, centerX + pageWidth - heatmapWidth,
				mainTop, heatmapWidth, mainHeight);
		}
		else
		{
			place(m_algorithmPageSummary, centerX, mainTop, pageWidth, 64);
			const int coverageWidth = pageWidth * 42 / 100;
			place(m_algorithmCandidateList, centerX, mainTop + 72,
				pageWidth - coverageWidth - gap, mainHeight - 72);
			place(m_floorCoverageView, centerX + pageWidth - coverageWidth,
				mainTop + 72, coverageWidth, mainHeight - 72);
			if (m_algorithmCandidateList.GetHeaderCtrl() != nullptr)
			{
				const int listWidth = pageWidth - coverageWidth - gap;
				m_algorithmCandidateList.SetColumnWidth(0, toDevice(listWidth * 13 / 100));
				m_algorithmCandidateList.SetColumnWidth(1, toDevice(listWidth * 15 / 100));
				m_algorithmCandidateList.SetColumnWidth(2, toDevice(listWidth * 15 / 100));
				m_algorithmCandidateList.SetColumnWidth(3, toDevice(listWidth * 15 / 100));
				m_algorithmCandidateList.SetColumnWidth(4, toDevice(listWidth * 17 / 100));
				m_algorithmCandidateList.SetColumnWidth(5, toDevice(listWidth * 22 / 100));
			}
		}
	}

	place(m_kpiBar, margin, statsY, clientWidth - margin * 2, statsHeight);

	if (m_rightPanelExpanded && m_hallCallList.GetHeaderCtrl() != nullptr)
	{
		const int listWidth = rightWidth - 24;
		m_hallCallList.SetColumnWidth(0, toDevice(listWidth * 22 / 100));
		m_hallCallList.SetColumnWidth(1, toDevice(listWidth * 20 / 100));
		m_hallCallList.SetColumnWidth(2, toDevice(listWidth * 22 / 100));
		m_hallCallList.SetColumnWidth(3, toDevice(listWidth * 32 / 100));
	}
	if (realTimePage && m_rightPanelExpanded &&
		m_algorithmCandidateList.GetHeaderCtrl() != nullptr)
	{
		const int listWidth = rightWidth - 24;
		m_algorithmCandidateList.SetColumnWidth(0, toDevice(listWidth * 13 / 100));
		m_algorithmCandidateList.SetColumnWidth(1, toDevice(listWidth * 19 / 100));
		m_algorithmCandidateList.SetColumnWidth(2, toDevice(listWidth * 16 / 100));
		m_algorithmCandidateList.SetColumnWidth(3, toDevice(listWidth * 14 / 100));
		m_algorithmCandidateList.SetColumnWidth(4, 0);
		m_algorithmCandidateList.SetColumnWidth(5, toDevice(listWidth * 34 / 100));
	}
	RedrawWindow(nullptr, nullptr, RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN);
}

void CElevatorSimulationDlg::UpdateLeftPanelVisibility()
{
	const bool configurationPage = m_leftTabs.GetCurSel() != 1;
	const int configurationCommand = configurationPage ? SW_SHOW : SW_HIDE;
	const int manualCommand = configurationPage ? SW_HIDE : SW_SHOW;
	m_parameterSection.ShowWindow(configurationCommand);
	for (auto& label : m_parameterLabels)
		label.ShowWindow(configurationCommand);
	for (int controlId : ParameterControlIds)
		GetDlgItem(controlId)->ShowWindow(configurationCommand);
	m_predictiveRebalancingCheck.ShowWindow(configurationCommand);

	for (CWnd* control : {
		static_cast<CWnd*>(&m_manualSection), static_cast<CWnd*>(&m_manualDescription),
		static_cast<CWnd*>(&m_manualFloorEdit), static_cast<CWnd*>(&m_manualUpEdit),
		static_cast<CWnd*>(&m_manualDownEdit), static_cast<CWnd*>(&m_addPassengersButton),
		static_cast<CWnd*>(&m_manualFeedback) })
	{
		control->ShowWindow(manualCommand);
	}
	for (auto& label : m_manualLabels)
		label.ShowWindow(manualCommand);
	if (!configurationPage)
	{
		const auto snapshot = m_simulationWorker ? m_simulationWorker->GetLatestSnapshot() : nullptr;
		UpdateManualDirectionLocks(snapshot, true);
	}
}

void CElevatorSimulationDlg::UpdateManualDirectionLocks(
	const std::shared_ptr<const SimulationUISnapshot>& snapshot, bool updateHint)
{
	if (!m_uiReady) return;
	const bool manualAvailable = snapshot && snapshot->workerActive &&
		(snapshot->state == SimulationState::Running ||
		 snapshot->state == SimulationState::Paused);
	CString floorText;
	m_manualFloorEdit.GetWindowTextW(floorText);
	floorText.Trim();
	wchar_t* end = nullptr;
	const long floor = std::wcstol(floorText.GetString(), &end, 10);
	const bool validFloorText = !floorText.IsEmpty() && end != floorText.GetString() &&
		*end == L'\0';
	const bool groundFloorLock = validFloorText && floor == 1;
	const bool topFloorLock = validFloorText && snapshot &&
		floor == snapshot->config.floorCount;

	m_manualFloorEdit.EnableWindow(manualAvailable);
	m_manualUpEdit.EnableWindow(manualAvailable && !topFloorLock);
	m_manualDownEdit.EnableWindow(manualAvailable && !groundFloorLock);
	m_addPassengersButton.EnableWindow(manualAvailable);
	if (topFloorLock) m_manualUpEdit.SetWindowTextW(L"0");
	if (groundFloorLock) m_manualDownEdit.SetWindowTextW(L"0");
	if (!updateHint) return;
	if (topFloorLock)
		SetTextIfChanged(m_manualFeedback, L"安全锁定：最高层只能添加下行乘客。");
	else if (groundFloorLock)
		SetTextIfChanged(m_manualFeedback, L"安全锁定：1 层只能添加上行乘客。");
	else
		SetTextIfChanged(m_manualFeedback,
			L"运行或暂停时可添加；提交后立即进入当前模型时刻。");
}

void CElevatorSimulationDlg::UpdateTabPageVisibility()
{
	const int page = m_pageTabs.GetCurSel();
	const bool realTimePage = page == 0;
	const int realTimeCommand = realTimePage ? SW_SHOW : SW_HIDE;
	for (CWnd* control : { static_cast<CWnd*>(&m_mainPanel), static_cast<CWnd*>(&m_rightPanel),
		static_cast<CWnd*>(&m_panelToggle), static_cast<CWnd*>(&m_buildingView),
		static_cast<CWnd*>(&m_elevatorStateLegend) })
	{
		control->ShowWindow(realTimeCommand);
	}
	m_pagePlaceholder.ShowWindow(SW_HIDE);
	m_statisticsTrendView.ShowWindow(page == 1 ? SW_SHOW : SW_HIDE);
	m_floorTrafficHeatmapView.ShowWindow(page == 1 ? SW_SHOW : SW_HIDE);
	m_algorithmPageSummary.ShowWindow(page == 2 ? SW_SHOW : SW_HIDE);
	m_algorithmCandidateList.ShowWindow(page == 2 ? SW_SHOW : SW_HIDE);
	m_floorCoverageView.ShowWindow(page == 2 ? SW_SHOW : SW_HIDE);
	UpdateLeftPanelVisibility();
	UpdateRightPanelVisibility();
	RelayoutUI();
}

void CElevatorSimulationDlg::UpdateRightPanelVisibility()
{
	const bool panelVisible = m_pageTabs.GetCurSel() == 0 && m_rightPanelExpanded;
	const int selectedTab = m_rightTabs.GetCurSel();
	m_rightTabs.ShowWindow(panelVisible ? SW_SHOW : SW_HIDE);
	m_rightHallCallTitle.ShowWindow(panelVisible && selectedTab == 0 ? SW_SHOW : SW_HIDE);
	m_hallCallList.ShowWindow(panelVisible && selectedTab == 0 ? SW_SHOW : SW_HIDE);
	m_elevatorDetailTitle.ShowWindow(panelVisible && selectedTab == 1 ? SW_SHOW : SW_HIDE);
	m_elevatorDetailBody.ShowWindow(panelVisible && selectedTab == 1 ? SW_SHOW : SW_HIDE);
	m_rightAlgorithmTitle.ShowWindow(panelVisible && selectedTab == 2 ? SW_SHOW : SW_HIDE);
	m_algorithmPlaceholder.ShowWindow(panelVisible && selectedTab == 2 ? SW_SHOW : SW_HIDE);
	if (m_pageTabs.GetCurSel() == 0)
		m_algorithmCandidateList.ShowWindow(
			panelVisible && selectedTab == 2 ? SW_SHOW : SW_HIDE);
}

void CElevatorSimulationDlg::UpdateSpeedDisplay(double speed)
{
	CString text;
	text.Format(L"%g 倍", speed);
	SetTextIfChanged(m_headerSpeed, text);
}

void CElevatorSimulationDlg::OnSize(UINT nType, int cx, int cy)
{
	CDialogEx::OnSize(nType, cx, cy);
	if (nType != SIZE_MINIMIZED) RelayoutUI();
}

void CElevatorSimulationDlg::OnGetMinMaxInfo(MINMAXINFO* lpMMI)
{
	CDialogEx::OnGetMinMaxInfo(lpMMI);
	const UINT dpi = GetDpiForWindow(m_hWnd);
	lpMMI->ptMinTrackSize.x = MulDiv(1240, dpi, 96);
	lpMMI->ptMinTrackSize.y = MulDiv(760, dpi, 96);
}

double CElevatorSimulationDlg::SpeedFromSlider(int position) const
{
	return position * kSpeedSliderStep;
}

int CElevatorSimulationDlg::SliderFromSpeed(double speed) const
{
	const double clamped = (std::max)(kSpeedSliderMin * kSpeedSliderStep,
		(std::min)(speed, kSpeedSliderMax * kSpeedSliderStep));
	return (std::max)(kSpeedSliderMin,
		(std::min)(kSpeedSliderMax, static_cast<int>(std::lround(clamped / kSpeedSliderStep))));
}

void CElevatorSimulationDlg::OnHScroll(UINT nSBCode, UINT /*nPos*/, CScrollBar* pScrollBar)
{
	if (pScrollBar == nullptr ||
		pScrollBar->GetDlgCtrlID() != IDC_SLIDER_SPEED)
		return;
	if (nSBCode == SB_THUMBTRACK)
		m_speedSliderDragging = true;
	else if (nSBCode == SB_ENDSCROLL)
		m_speedSliderDragging = false;
	const double speed = SpeedFromSlider(m_speedSlider.GetPos());
	ApplySimulationSpeed(speed);
}

void CElevatorSimulationDlg::ApplySimulationSpeed(double speed)
{
	if (!m_uiReady || !m_simulationWorker)
		return;
	// 同步到配置编辑框与头部显示；若正在运行/暂停则实时生效到仿真线程。
	const auto snapshot = m_simulationWorker->GetLatestSnapshot();
	const bool active = snapshot && snapshot->workerActive;
	const bool liveChange = active &&
		(snapshot->state == SimulationState::Running ||
			snapshot->state == SimulationState::Paused);
	CString text;
	text.Format(L"%g", speed);
	SetDlgItemTextW(IDC_EDIT_SPEED, text);
	UpdateSpeedDisplay(speed);
	if (liveChange)
		m_simulationWorker->SetSimulationSpeed(speed);
}

void CElevatorSimulationDlg::OnTcnSelchangeLeftTabs(NMHDR*, LRESULT* pResult)
{
	UpdateLeftPanelVisibility();
	RelayoutUI();
	*pResult = 0;
}

void CElevatorSimulationDlg::OnEnChangeManualFloor()
{
	const auto snapshot = m_simulationWorker ? m_simulationWorker->GetLatestSnapshot() : nullptr;
	UpdateManualDirectionLocks(snapshot, true);
}

void CElevatorSimulationDlg::OnBnClickedAddPassengers()
{
	const auto snapshot = m_simulationWorker ? m_simulationWorker->GetLatestSnapshot() : nullptr;
	if (!snapshot || !snapshot->workerActive ||
		(snapshot->state != SimulationState::Running &&
		 snapshot->state != SimulationState::Paused))
	{
		CString message = L"请先启动仿真，再添加手动客流。";
		ShowManualInputError(m_manualFloorEdit, message);
		return;
	}

	int floor = 0;
	int upCount = 0;
	int downCount = 0;
	if (!ReadManualInteger(m_manualFloorEdit, L"出发楼层", floor) ||
		!ReadManualInteger(m_manualUpEdit, L"上行人数", upCount) ||
		!ReadManualInteger(m_manualDownEdit, L"下行人数", downCount))
	{
		return;
	}
	if (floor < 1 || floor > snapshot->config.floorCount)
	{
		CString message;
		message.Format(L"出发楼层范围是 1~%d。", snapshot->config.floorCount);
		ShowManualInputError(m_manualFloorEdit, message);
		return;
	}
	constexpr int MaximumManualCount = 500;
	if (upCount < 0 || upCount > MaximumManualCount)
	{
		ShowManualInputError(m_manualUpEdit, L"上行人数范围是 0~500。");
		return;
	}
	if (downCount < 0 || downCount > MaximumManualCount)
	{
		ShowManualInputError(m_manualDownEdit, L"下行人数范围是 0~500。");
		return;
	}
	if (upCount == 0 && downCount == 0)
	{
		ShowManualInputError(m_manualUpEdit, L"上行和下行人数不能同时为 0。");
		return;
	}
	if (floor == snapshot->config.floorCount && upCount > 0)
	{
		ShowManualInputError(m_manualUpEdit, L"最高层不能添加上行乘客。");
		return;
	}
	if (floor == 1 && downCount > 0)
	{
		ShowManualInputError(m_manualDownEdit, L"1 层不能添加下行乘客。");
		return;
	}

	m_simulationWorker->AddPassengers(floor, upCount, downCount);
	CString feedback;
	feedback.Format(L"已提交：%d 层，上行 %d 人，下行 %d 人。",
		floor, upCount, downCount);
	SetTextIfChanged(m_manualFeedback, feedback);
	m_manualUpEdit.SetWindowTextW(L"0");
	m_manualDownEdit.SetWindowTextW(L"0");
}

void CElevatorSimulationDlg::OnBnClickedPanelToggle()
{
	m_rightPanelExpanded = !m_rightPanelExpanded;
	m_panelToggle.SetWindowTextW(m_rightPanelExpanded ? L"<<" : L">>");
	UpdateTabPageVisibility();
	if (m_rightPanelExpanded) RefreshSimulationView();
}

void CElevatorSimulationDlg::OnTcnSelchangePages(NMHDR*, LRESULT* pResult)
{
	UpdateTabPageVisibility();
	if (m_pageTabs.GetCurSel() == 0)
		RefreshSimulationView(true);
	else if (m_pageTabs.GetCurSel() == 1)
		UpdateStatisticsTrend(m_simulationWorker ? m_simulationWorker->GetLatestSnapshot() : nullptr, true);
	else
		RefreshObservationViews(true);
	*pResult = 0;
}

void CElevatorSimulationDlg::OnTcnSelchangeRightTabs(NMHDR*, LRESULT* pResult)
{
	UpdateRightPanelVisibility();
	RelayoutUI();
	RefreshSimulationView();
	if (m_rightTabs.GetCurSel() == 2) RefreshObservationViews(true);
	*pResult = 0;
}

void CElevatorSimulationDlg::OnNMClickHallCallList(NMHDR* pNMHDR, LRESULT* pResult)
{
	if (!m_rebuildingHallCallList)
	{
		const auto* activate = reinterpret_cast<NMITEMACTIVATE*>(pNMHDR);
		if (activate->iItem >= 0)
		{
			const DWORD_PTR data = m_hallCallList.GetItemData(activate->iItem);
			HallCallIdentity identity;
			identity.floor = static_cast<int>(data >> 1);
			identity.direction = (data & 1) != 0 ? Direction::Up : Direction::Down;
			SelectHallCall(identity);
		}
	}
	*pResult = 0;
}

LRESULT CElevatorSimulationDlg::OnElevatorSelectionChanged(WPARAM wParam, LPARAM)
{
	const int selectedElevatorId = static_cast<int>(wParam);
	const auto snapshot = m_simulationWorker ? m_simulationWorker->GetLatestSnapshot() : nullptr;
	if (!SelectObservationForElevator(selectedElevatorId, snapshot))
	{
		ShowObservationEmptyState(
			L"当前没有可观察的外呼请求；新外呼出现后将自动显示候选评分");
	}
	m_rightTabs.SetCurSel(1);
	UpdateRightPanelVisibility();
	RelayoutUI();
	RefreshSimulationView();
	return 0;
}

void CElevatorSimulationDlg::InitializeListControls()
{
	const DWORD extendedStyle = LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES | LVS_EX_DOUBLEBUFFER;
	m_elevatorList.SetExtendedStyle(m_elevatorList.GetExtendedStyle() | extendedStyle);
	m_floorList.SetExtendedStyle(m_floorList.GetExtendedStyle() | extendedStyle);
	m_hallCallList.SetExtendedStyle(m_hallCallList.GetExtendedStyle() | extendedStyle);
	m_algorithmCandidateList.SetExtendedStyle(
		m_algorithmCandidateList.GetExtendedStyle() | extendedStyle);

	m_elevatorList.InsertColumn(0, L"电梯", LVCFMT_LEFT, 52);
	m_elevatorList.InsertColumn(1, L"楼层", LVCFMT_RIGHT, 58);
	m_elevatorList.InsertColumn(2, L"方向", LVCFMT_CENTER, 52);
	m_elevatorList.InsertColumn(3, L"状态", LVCFMT_LEFT, 92);
	m_elevatorList.InsertColumn(4, L"载客", LVCFMT_RIGHT, 72);

	m_floorList.InsertColumn(0, L"楼层", LVCFMT_RIGHT, 62);
	m_floorList.InsertColumn(1, L"上行等待", LVCFMT_RIGHT, 82);
	m_floorList.InsertColumn(2, L"下行等待", LVCFMT_RIGHT, 82);

	m_hallCallList.InsertColumn(0, L"楼层", LVCFMT_RIGHT, 58);
	m_hallCallList.InsertColumn(1, L"方向", LVCFMT_CENTER, 52);
	m_hallCallList.InsertColumn(2, L"等待", LVCFMT_RIGHT, 65);
	m_hallCallList.InsertColumn(3, L"归属", LVCFMT_LEFT, 88);

	m_algorithmCandidateList.InsertColumn(0, L"电梯", LVCFMT_LEFT, 90);
	m_algorithmCandidateList.InsertColumn(1, L"到达（秒）", LVCFMT_RIGHT, 110);
	m_algorithmCandidateList.InsertColumn(2, L"成本", LVCFMT_RIGHT, 90);
	m_algorithmCandidateList.InsertColumn(3, L"可行", LVCFMT_CENTER, 70);
	m_algorithmCandidateList.InsertColumn(4, L"估计载荷", LVCFMT_RIGHT, 110);
	m_algorithmCandidateList.InsertColumn(5, L"备注", LVCFMT_LEFT, 220);
}

bool CElevatorSimulationDlg::ReadIntControl(int controlId, const wchar_t* fieldName, int& value)
{
	CString text;
	GetDlgItemTextW(controlId, text);
	text.Trim();
	errno = 0;
	wchar_t* end = nullptr;
	const long parsed = std::wcstol(text.GetString(), &end, 10);
	if (text.IsEmpty() || end == text.GetString() || *end != L'\0' || errno == ERANGE ||
		parsed < (std::numeric_limits<int>::min)() || parsed > (std::numeric_limits<int>::max)())
	{
		CString message;
		message.Format(L"%s 必须是有效整数。", fieldName);
		ShowInputError(message);
		return false;
	}
	value = static_cast<int>(parsed);
	return true;
}

bool CElevatorSimulationDlg::ReadManualInteger(
	CEdit& control, const wchar_t* fieldName, int& value)
{
	CString text;
	control.GetWindowTextW(text);
	text.Trim();
	errno = 0;
	wchar_t* end = nullptr;
	const long parsed = std::wcstol(text.GetString(), &end, 10);
	if (text.IsEmpty() || end == text.GetString() || *end != L'\0' || errno == ERANGE ||
		parsed < (std::numeric_limits<int>::min)() || parsed > (std::numeric_limits<int>::max)())
	{
		CString message;
		message.Format(L"%s 必须是有效整数。", fieldName);
		ShowManualInputError(control, message);
		return false;
	}
	value = static_cast<int>(parsed);
	return true;
}

void CElevatorSimulationDlg::ShowManualInputError(CEdit& control, const CString& message)
{
	CString feedback = L"输入有误：";
	feedback += message;
	SetTextIfChanged(m_manualFeedback, feedback);
	MessageBeep(MB_ICONWARNING);
	control.SetFocus();
	control.SetSel(0, -1);
}

bool CElevatorSimulationDlg::ReadDoubleControl(int controlId, const wchar_t* fieldName, double& value)
{
	CString text;
	GetDlgItemTextW(controlId, text);
	text.Trim();
	errno = 0;
	wchar_t* end = nullptr;
	const double parsed = std::wcstod(text.GetString(), &end);
	if (text.IsEmpty() || end == text.GetString() || *end != L'\0' || errno == ERANGE)
	{
		CString message;
		message.Format(L"%s 必须是有效数字。", fieldName);
		ShowInputError(message);
		return false;
	}
	value = parsed;
	return true;
}

bool CElevatorSimulationDlg::ReadConfiguration(SimulationConfig& config, std::uint32_t& seed)
{
	if (!ReadIntControl(IDC_EDIT_FLOOR_COUNT, L"楼层数", config.floorCount) ||
		!ReadIntControl(IDC_EDIT_ELEVATOR_COUNT, L"电梯数", config.elevatorCount) ||
		!ReadIntControl(IDC_EDIT_CAPACITY, L"容量", config.capacity) ||
		!ReadDoubleControl(IDC_EDIT_MOVE_TIME, L"每层运行时间", config.moveTimePerFloor) ||
		!ReadDoubleControl(IDC_EDIT_PERSON_TIME, L"每人上下客时间", config.personTime) ||
		!ReadDoubleControl(IDC_EDIT_DURATION, L"仿真总时长", config.simulationDuration) ||
		!ReadDoubleControl(IDC_EDIT_PASSENGER_RATE, L"乘客产生率", config.passengerRate) ||
		!ReadDoubleControl(IDC_EDIT_SPEED, L"仿真倍速", config.simulationSpeed))
	{
		return false;
	}
	const int trafficPattern = m_trafficPatternCombo.GetCurSel();
	if (trafficPattern < 0 || trafficPattern > static_cast<int>(TrafficPattern::InterFloor))
	{
		ShowInputError(L"请选择有效的客流模式。");
		return false;
	}
	config.trafficPattern = static_cast<TrafficPattern>(trafficPattern);
	const int trafficScenario = m_trafficScenarioCombo.GetCurSel();
	if (trafficScenario < 0 || trafficScenario > static_cast<int>(TrafficScenario::OfficeDay))
	{
		ShowInputError(L"请选择有效的客流场景。");
		return false;
	}
	config.trafficScenario = static_cast<TrafficScenario>(trafficScenario);
	config.predictiveRebalancing =
		m_predictiveRebalancingCheck.GetCheck() == BST_CHECKED;

	CString seedText;
	GetDlgItemTextW(IDC_EDIT_SEED, seedText);
	seedText.Trim();
	errno = 0;
	wchar_t* end = nullptr;
	const unsigned long long parsedSeed = std::wcstoull(seedText.GetString(), &end, 10);
	if (seedText.IsEmpty() || end == seedText.GetString() || *end != L'\0' || errno == ERANGE ||
		parsedSeed > (std::numeric_limits<std::uint32_t>::max)())
	{
		ShowInputError(L"随机种子必须是 0~4294967295 的整数。");
		return false;
	}
	seed = static_cast<std::uint32_t>(parsedSeed);
	return true;
}

void CElevatorSimulationDlg::ShowInputError(const CString& message)
{
	SetDlgItemTextW(IDC_SIMULATION_STATE, L"参数无效");
	AfxMessageBox(message, MB_ICONWARNING);
}

void CElevatorSimulationDlg::UpdateControlStates(
	const std::shared_ptr<const SimulationUISnapshot>& snapshot)
{
	const SimulationState state = snapshot ? snapshot->state : SimulationState::Uninitialized;
	const bool active = snapshot && snapshot->workerActive;
	const bool ready = state == SimulationState::Ready || state == SimulationState::Uninitialized;
	GetDlgItem(IDC_BUTTON_START)->EnableWindow(active && ready);
	GetDlgItem(IDC_BUTTON_PAUSE)->EnableWindow(active && state == SimulationState::Running);
	GetDlgItem(IDC_BUTTON_RESUME)->EnableWindow(active && state == SimulationState::Paused);
	GetDlgItem(IDC_BUTTON_RESET)->EnableWindow(active && state != SimulationState::Uninitialized);

	for (int controlId : ParameterControlIds)
	{
		GetDlgItem(controlId)->EnableWindow(active && ready);
	}
	const bool fixedScenario =
		m_trafficScenarioCombo.GetCurSel() == static_cast<int>(TrafficScenario::Fixed);
	m_trafficPatternCombo.EnableWindow(active && ready && fixedScenario);
	const bool finished = state == SimulationState::Finished;
	// 倍速滑块在就绪/运行/暂停时都可调；运行与暂停实时生效，就绪时写入配置。
	m_speedSlider.EnableWindow(active && !finished);
	// 就绪时快照仍是旧默认值，不能用来覆盖滑块；运行/暂停时才由快照驱动，
	// 用户拖动期间不覆盖，避免回弹。
	if (active && snapshot && !ready && !finished && !m_speedSliderDragging)
	{
		m_speedSlider.SetPos(std::clamp(
			SliderFromSpeed(snapshot->config.simulationSpeed),
			kSpeedSliderMin, kSpeedSliderMax));
	}
	UpdateManualDirectionLocks(snapshot, false);
}

void CElevatorSimulationDlg::UpdateElevatorDetails(
	const std::shared_ptr<const SimulationUISnapshot>& snapshot)
{
	const int selectedElevatorId = m_buildingView.GetSelectedElevatorId();
	if (!snapshot || selectedElevatorId == InvalidElevatorId)
	{
		SetTextIfChanged(m_elevatorDetailTitle, L"电梯详情：未选择");
		m_elevatorDetailBody.SetWindowTextW(L"请在中央视图选择一台电梯");
		return;
	}

	const auto elevator = std::find_if(snapshot->elevators.begin(), snapshot->elevators.end(),
		[selectedElevatorId](const ElevatorSnapshot& item)
		{
			return item.id == selectedElevatorId;
		});
	if (elevator == snapshot->elevators.end())
	{
		SetTextIfChanged(m_elevatorDetailTitle, L"电梯详情：未选择");
		m_elevatorDetailBody.SetWindowTextW(L"请在中央视图选择一台电梯");
		return;
	}

	CString title;
	title.Format(L"电梯详情：E%d", elevator->id + 1);
	SetTextIfChanged(m_elevatorDetailTitle, title);
	CString details;
	CString repositionTarget = L"--";
	if (elevator->repositionTargetFloor != InvalidFloor)
		repositionTarget.Format(L"%d 层", elevator->repositionTargetFloor);
	details.Format(L"当前楼层：%d 层\r\n\r\n方向：%s\r\n\r\n状态：%s\r\n\r\n载客：%d / %d\r\n\r\n再平衡目标：%s",
		elevator->currentFloor, DirectionText(elevator->direction),
		ElevatorStateText(elevator->state), elevator->passengerCount, elevator->capacity,
		repositionTarget.GetString());
	m_elevatorDetailBody.SetWindowTextW(details);
}

void CElevatorSimulationDlg::ClearStatisticsTrend()
{
	m_statisticsTrend.clear();
	m_nextTrendSampleTime = 0.0;
	m_lastTrendSimulationTime = 0.0;
	m_trendHasSnapshot = false;
	m_statisticsRefreshScheduled = false;
	m_statisticsTrendView.SetTrendPoints(m_statisticsTrend);
}

void CElevatorSimulationDlg::UpdateStatisticsTrend(
	const std::shared_ptr<const SimulationUISnapshot>& snapshot, bool forceRefresh)
{
	if (!snapshot) return;
	if (m_trendHasSnapshot && snapshot->currentTime < m_lastTrendSimulationTime)
		ClearStatisticsTrend();
	m_lastTrendSimulationTime = snapshot->currentTime;
	m_trendHasSnapshot = true;

	bool sampled = false;
	if (snapshot->state == SimulationState::Running ||
		snapshot->state == SimulationState::Paused ||
		snapshot->state == SimulationState::Finished)
	{
		const double interval = (std::max)(0.5, snapshot->config.simulationDuration / 500.0);
		if (m_statisticsTrend.empty() || snapshot->currentTime >= m_nextTrendSampleTime)
		{
			StatisticsTrendPoint point;
			point.time = snapshot->currentTime;
			point.waitingCount = snapshot->statistics.waitingCount;
			point.arrivedCount = snapshot->statistics.arrivedCount;
			point.averageWaitingTime = snapshot->statistics.averageWaitingTime;
			if (m_statisticsTrend.size() < 500)
				m_statisticsTrend.push_back(point);
			else
				m_statisticsTrend.back() = point;
			m_nextTrendSampleTime = snapshot->currentTime + interval;
			sampled = true;
		}
	}

	const auto now = std::chrono::steady_clock::now();
	const bool refreshDue = !m_statisticsRefreshScheduled || now >= m_nextStatisticsRefresh;
	if (forceRefresh || (sampled && m_statisticsTrendView.IsWindowVisible() && refreshDue))
	{
		m_statisticsTrendView.SetTrendPoints(m_statisticsTrend);
		m_nextStatisticsRefresh = now + std::chrono::milliseconds(StatisticsRefreshMs);
		m_statisticsRefreshScheduled = true;
	}
}

void CElevatorSimulationDlg::SelectHallCall(HallCallIdentity identity)
{
	BeginHallCallObservation(identity);
	m_rightTabs.SetCurSel(2);
	UpdateRightPanelVisibility();
	RelayoutUI();
}

void CElevatorSimulationDlg::BeginHallCallObservation(HallCallIdentity identity)
{
	m_observedHallCall = identity;
	m_lastRenderedObservation.reset();
	if (m_simulationWorker)
		m_simulationWorker->ObserveHallCall(identity.floor, identity.direction);
	ShowObservationEmptyState(L"正在计算候选电梯评分……");
}

bool CElevatorSimulationDlg::SelectObservationForElevator(int elevatorId,
	const std::shared_ptr<const SimulationUISnapshot>& snapshot)
{
	if (elevatorId == InvalidElevatorId || !snapshot || snapshot->hallCalls.empty())
		return false;

	const auto rank = [elevatorId](const HallCallSnapshot& call)
	{
		return std::make_tuple(
			call.assignedElevatorId == elevatorId ? 0 : 1,
			call.firstRequestTime,
			call.floorNumber,
			call.direction == Direction::Up ? 0 : 1);
	};
	const auto selected = std::min_element(snapshot->hallCalls.begin(), snapshot->hallCalls.end(),
		[&rank](const HallCallSnapshot& left, const HallCallSnapshot& right)
		{
			return rank(left) < rank(right);
		});
	HallCallIdentity identity{ selected->floorNumber, selected->direction };
	if (!m_observedHallCall || m_observedHallCall->floor != identity.floor ||
		m_observedHallCall->direction != identity.direction)
	{
		BeginHallCallObservation(identity);
	}
	return true;
}

void CElevatorSimulationDlg::ClearHallCallObservation()
{
	if (m_simulationWorker) m_simulationWorker->ClearObservedHallCall();
	m_observedHallCall.reset();
	m_lastRenderedObservation.reset();
	ShowObservationEmptyState(L"请选择外呼请求，或先选择一台电梯自动关联其外呼");
}

void CElevatorSimulationDlg::ValidateObservedHallCall(
	const std::shared_ptr<const SimulationUISnapshot>& snapshot)
{
	if (!m_observedHallCall) return;
	const bool exists = snapshot && std::any_of(snapshot->hallCalls.begin(), snapshot->hallCalls.end(),
		[this](const HallCallSnapshot& call)
		{
			return call.floorNumber == m_observedHallCall->floor &&
				call.direction == m_observedHallCall->direction;
		});
	if (!exists) ClearHallCallObservation();
}

void CElevatorSimulationDlg::RefreshObservationViews(bool forceRefresh)
{
	if (!m_observedHallCall || !m_simulationWorker)
	{
		if (forceRefresh)
			ShowObservationEmptyState(L"请选择外呼请求，或先选择一台电梯自动关联其外呼");
		return;
	}
	const auto observation = m_simulationWorker->GetLatestObservation();
	if (!observation || observation->floor != m_observedHallCall->floor ||
		observation->direction != m_observedHallCall->direction)
	{
		if (forceRefresh) ShowObservationEmptyState(L"正在计算候选电梯评分……");
		return;
	}
	if (!observation->valid)
	{
		ClearHallCallObservation();
		return;
	}
	if (!forceRefresh && observation == m_lastRenderedObservation) return;
	m_lastRenderedObservation = observation;
	PopulateObservationViews(*observation);
}

void CElevatorSimulationDlg::ShowObservationEmptyState(const wchar_t* message)
{
	m_algorithmPlaceholder.SetWindowTextW(message);
	m_algorithmPageSummary.SetWindowTextW(message);
	m_algorithmCandidateList.DeleteAllItems();
}

void CElevatorSimulationDlg::PopulateObservationViews(
	const DispatchObservationSnapshot& observation)
{
	const auto best = std::find_if(observation.candidates.begin(), observation.candidates.end(),
		[](const DispatchCandidateObservation& candidate) { return candidate.feasible; });
	CString ownerText = L"未分配";
	if (observation.assignedElevatorId != InvalidElevatorId)
		ownerText.Format(L"E%d", observation.assignedElevatorId + 1);

	CString rightText;
	if (best == observation.candidates.end())
	{
		rightText.Format(L"请求：%d 层 %s    归属：%s\r\n最佳候选：无可行电梯",
			observation.floor, DirectionText(observation.direction), ownerText.GetString());
	}
	else
	{
		rightText.Format(L"请求：%d 层 %s    归属：%s\r\n最佳：E%d    到达：%.2f 秒    成本：%.2f",
			observation.floor, DirectionText(observation.direction), ownerText.GetString(),
			best->elevatorId + 1, best->eta, best->cost);
	}
	m_algorithmPlaceholder.SetWindowTextW(rightText);

	CString pageSummary;
	pageSummary.Format(L"%d 层 %s    等待人数：%zu    已等待：%.1f 秒    当前归属：%s\r\n候选为单请求评分；当前归属还会受到联合调度、动态改派与滞回保护影响。",
		observation.floor, DirectionText(observation.direction), observation.waitingCount,
		(std::max)(0.0, observation.currentTime - observation.firstRequestTime), ownerText.GetString());
	m_algorithmPageSummary.SetWindowTextW(pageSummary);

	std::vector<const DispatchCandidateObservation*> rows;
	const std::size_t topCount = (std::min)(std::size_t{ 10 }, observation.candidates.size());
	for (std::size_t index = 0; index < topCount; ++index)
		rows.push_back(&observation.candidates[index]);
	const auto owner = std::find_if(observation.candidates.begin(), observation.candidates.end(),
		[&observation](const DispatchCandidateObservation& candidate)
		{
			return candidate.elevatorId == observation.assignedElevatorId;
		});
	if (owner != observation.candidates.end() &&
		std::none_of(rows.begin(), rows.end(), [owner](const auto* candidate)
			{ return candidate->elevatorId == owner->elevatorId; }))
	{
		rows.push_back(&*owner);
	}
	const int selectedElevatorId = m_buildingView.GetSelectedElevatorId();
	const auto selectedElevator = std::find_if(observation.candidates.begin(),
		observation.candidates.end(),
		[selectedElevatorId](const DispatchCandidateObservation& candidate)
		{
			return candidate.elevatorId == selectedElevatorId;
		});
	if (selectedElevator != observation.candidates.end() &&
		std::none_of(rows.begin(), rows.end(), [selectedElevator](const auto* candidate)
			{ return candidate->elevatorId == selectedElevator->elevatorId; }))
	{
		rows.push_back(&*selectedElevator);
	}

	m_algorithmCandidateList.SetRedraw(FALSE);
	m_algorithmCandidateList.DeleteAllItems();
	for (std::size_t index = 0; index < rows.size(); ++index)
	{
		const auto& candidate = *rows[index];
		CString value;
		value.Format(L"E%d", candidate.elevatorId + 1);
		const int row = m_algorithmCandidateList.InsertItem(static_cast<int>(index), value);
		if (candidate.feasible)
		{
			value.Format(L"%.2f", candidate.eta);
			m_algorithmCandidateList.SetItemText(row, 1, value);
			value.Format(L"%.2f", candidate.cost);
			m_algorithmCandidateList.SetItemText(row, 2, value);
		}
		else
		{
			m_algorithmCandidateList.SetItemText(row, 1, L"—");
			m_algorithmCandidateList.SetItemText(row, 2, L"—");
		}
		m_algorithmCandidateList.SetItemText(row, 3, candidate.feasible ? L"是" : L"否");
		value.Format(L"%d", candidate.projectedOccupancy);
		m_algorithmCandidateList.SetItemText(row, 4, value);
		CString mark;
		if (candidate.elevatorId == selectedElevatorId)
			mark = L"已选电梯";
		if (best != observation.candidates.end() && candidate.elevatorId == best->elevatorId)
			mark += mark.IsEmpty() ? L"最佳单梯候选" : L" / 最佳单梯候选";
		if (candidate.elevatorId == observation.assignedElevatorId)
			mark += mark.IsEmpty() ? L"当前归属" : L" / 当前归属";
		m_algorithmCandidateList.SetItemText(row, 5, mark);
	}
	m_algorithmCandidateList.SetRedraw(TRUE);
	m_algorithmCandidateList.Invalidate(FALSE);
}

void CElevatorSimulationDlg::RefreshBuildingView(
	const std::shared_ptr<const SimulationUISnapshot>& snapshot, bool forceRefresh)
{
	if (!m_buildingView.IsWindowVisible()) return;

	const bool largeScaleMode = snapshot &&
		(snapshot->config.floorCount > 80 || snapshot->elevators.size() > 30);
	const bool modeChanged = m_buildingRefreshScheduled &&
		largeScaleMode != m_lastBuildingLargeScaleMode;
	const auto now = std::chrono::steady_clock::now();
	const auto interval = std::chrono::milliseconds(largeScaleMode
		? LargeBuildingRefreshMs : NormalBuildingRefreshMs);
	if (!forceRefresh && !modeChanged && m_buildingRefreshScheduled &&
		now < m_nextBuildingRefresh)
	{
		return;
	}

	m_buildingView.SetSnapshot(snapshot);
	if (forceRefresh || modeChanged || !m_buildingRefreshScheduled)
	{
		m_nextBuildingRefresh = now + interval;
	}
	else
	{
		do
		{
			m_nextBuildingRefresh += interval;
		} while (m_nextBuildingRefresh <= now);
	}
	m_buildingRefreshScheduled = true;
	m_lastBuildingLargeScaleMode = largeScaleMode;
}

void CElevatorSimulationDlg::RefreshSimulationView(bool forceBuildingRefresh)
{
	const auto snapshot = m_simulationWorker ? m_simulationWorker->GetLatestSnapshot() : nullptr;
	if (!snapshot)
	{
		SetDialogItemTextIfChanged(*this, IDC_SIMULATION_STATE, L"正在初始化");
		RefreshBuildingView(snapshot, forceBuildingRefresh);
		UpdateElevatorDetails(snapshot);
		UpdateControlStates(snapshot);
		return;
	}
	const auto& statistics = snapshot->statistics;
	const auto& hallCalls = snapshot->hallCalls;
	const auto& config = snapshot->config;
	CString stateText = snapshot->workerActive ? SimulationStateText(snapshot->state) : L"已停止";
	if (!snapshot->lastError.empty() &&
		(!snapshot->workerActive || snapshot->state == SimulationState::Uninitialized))
		stateText = L"错误：" + Utf8ToCString(snapshot->lastError);
	SetDialogItemTextIfChanged(*this, IDC_SIMULATION_STATE, stateText);
	CString modelTime;
	modelTime.Format(L"%.1f / %.1f 秒", snapshot->currentTime, config.simulationDuration);
	SetDialogItemTextIfChanged(*this, IDC_MODEL_TIME, modelTime);
	CString trafficText;
	CString dashboardTrafficText;
	if (snapshot->trafficScenario == TrafficScenario::OfficeDay)
	{
		trafficText.Format(L"场景：办公楼日周期 · 当前阶段：%s",
			OfficePhaseText(snapshot->trafficPhaseIndex));
		dashboardTrafficText.Format(L"办公楼日周期 · %s · %s",
			OfficePhaseText(snapshot->trafficPhaseIndex),
			TrafficPatternText(snapshot->activeTrafficPattern));
	}
	else
	{
		trafficText.Format(L"场景：固定模式 · 当前模式：%s",
			TrafficPatternText(snapshot->activeTrafficPattern));
		dashboardTrafficText.Format(L"固定模式 · %s",
			TrafficPatternText(snapshot->activeTrafficPattern));
	}
	SetTextIfChanged(m_headerTraffic, trafficText);
	m_elevatorDetailBody.SetTrafficText(dashboardTrafficText);
	if (snapshot->state != SimulationState::Ready &&
		snapshot->state != SimulationState::Uninitialized)
	{
		m_trafficPatternCombo.SetCurSel(static_cast<int>(snapshot->activeTrafficPattern));
	}
	if (snapshot->state == SimulationState::Ready)
	{
		CString speedText;
		GetDlgItemTextW(IDC_EDIT_SPEED, speedText);
		SetTextIfChanged(m_headerSpeed, speedText + L" 倍");
	}
	else
	{
		UpdateSpeedDisplay(config.simulationSpeed);
	}

	RefreshBuildingView(snapshot, forceBuildingRefresh);
	UpdateElevatorDetails(snapshot);
	UpdateStatisticsTrend(snapshot);
	m_floorTrafficHeatmapView.SetStatistics(statistics.floorTraffic);
	m_floorCoverageView.SetCoverage(snapshot->floorCoverage);
	ValidateObservedHallCall(snapshot);
	if (!m_observedHallCall)
		SelectObservationForElevator(m_buildingView.GetSelectedElevatorId(), snapshot);
	RefreshObservationViews();

	if (m_pageTabs.GetCurSel() == 0 && m_rightPanelExpanded)
	{
		m_rebuildingHallCallList = true;
		m_hallCallList.SetRedraw(FALSE);
		m_hallCallList.DeleteAllItems();
		for (std::size_t index = 0; index < hallCalls.size(); ++index)
		{
			const auto& call = hallCalls[index];
			const int row = static_cast<int>(index);
			CString value;
			value.Format(L"%d 层", call.floorNumber);
			m_hallCallList.InsertItem(row, value);
			const DWORD_PTR identity = (static_cast<DWORD_PTR>(call.floorNumber) << 1) |
				(call.direction == Direction::Up ? 1u : 0u);
			m_hallCallList.SetItemData(row, identity);
			m_hallCallList.SetItemText(row, 1, DirectionText(call.direction));
			value.Format(L"%zu", call.waitingCount);
			m_hallCallList.SetItemText(row, 2, value);
			if (call.assignedElevatorId == InvalidElevatorId)
				value = L"未分配";
			else
				value.Format(L"E%d", call.assignedElevatorId + 1);
			m_hallCallList.SetItemText(row, 3, value);
			if (m_observedHallCall && call.floorNumber == m_observedHallCall->floor &&
				call.direction == m_observedHallCall->direction)
			{
				m_hallCallList.SetItemState(row, LVIS_SELECTED | LVIS_FOCUSED,
					LVIS_SELECTED | LVIS_FOCUSED);
			}
		}
		m_hallCallList.SetRedraw(TRUE);
		m_hallCallList.Invalidate(FALSE);
		m_rebuildingHallCallList = false;
	}

	std::array<CString, 6> statisticValues;
	statisticValues[0].Format(L"%zu", statistics.totalPassengerCount);
	statisticValues[1].Format(L"%zu", statistics.waitingCount);
	statisticValues[2].Format(L"%zu", statistics.ridingCount);
	statisticValues[3].Format(L"%zu", statistics.arrivedCount);
	statisticValues[4].Format(L"%.2f 秒", statistics.averageWaitingTime);
	statisticValues[5].Format(L"%.2f 秒", statistics.maxWaitingTime);
	m_kpiBar.SetValues(statisticValues);
	UpdateControlStates(snapshot);
}

void CElevatorSimulationDlg::OnBnClickedStart()
{
	SimulationConfig config;
	std::uint32_t seed = 0;
	if (!ReadConfiguration(config, seed)) return;
	ClearStatisticsTrend();
	ClearHallCallObservation();
	if (m_simulationWorker) m_simulationWorker->Stop();
	m_simulationWorker = std::make_unique<SimulationWorker>(config, seed,
		DispatcherExecutionMode::Parallel);
	m_simulationWorker->Start();
	RefreshSimulationView(true);
}

void CElevatorSimulationDlg::OnCbnSelchangeTrafficScenario()
{
	const auto snapshot = m_simulationWorker ? m_simulationWorker->GetLatestSnapshot() : nullptr;
	UpdateControlStates(snapshot);
}

void CElevatorSimulationDlg::OnBnClickedPause()
{
	if (m_simulationWorker) m_simulationWorker->Pause();
	RefreshSimulationView(true);
}

void CElevatorSimulationDlg::OnBnClickedResume()
{
	if (m_simulationWorker) m_simulationWorker->Resume();
	RefreshSimulationView(true);
}

void CElevatorSimulationDlg::OnBnClickedReset()
{
	ClearStatisticsTrend();
	ClearHallCallObservation();
	if (m_simulationWorker) m_simulationWorker->Reset();
	RefreshSimulationView(true);
}

void CElevatorSimulationDlg::OnTimer(UINT_PTR nIDEvent)
{
	if (nIDEvent == SimulationTimerId)
		RefreshSimulationView();
	CDialogEx::OnTimer(nIDEvent);
}

void CElevatorSimulationDlg::OnDestroy()
{
	KillTimer(SimulationTimerId);
	if (m_simulationWorker)
	{
		m_simulationWorker->Stop();
		m_simulationWorker.reset();
	}
	CDialogEx::OnDestroy();
}

void CElevatorSimulationDlg::OnSysCommand(UINT nID, LPARAM lParam)
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

// 如果向对话框添加最小化按钮，则需要下面的代码
//  来绘制该图标。  对于使用文档/视图模型的 MFC 应用程序，
//  这将由框架自动完成。

void CElevatorSimulationDlg::OnPaint()
{
	if (IsIconic())
	{
		CPaintDC dc(this); // 用于绘制的设备上下文

		SendMessage(WM_ICONERASEBKGND, reinterpret_cast<WPARAM>(dc.GetSafeHdc()), 0);

		// 使图标在工作区矩形中居中
		int cxIcon = GetSystemMetrics(SM_CXICON);
		int cyIcon = GetSystemMetrics(SM_CYICON);
		CRect rect;
		GetClientRect(&rect);
		int x = (rect.Width() - cxIcon + 1) / 2;
		int y = (rect.Height() - cyIcon + 1) / 2;

		// 绘制图标
		dc.DrawIcon(x, y, m_hIcon);
	}
	else
	{
		CDialogEx::OnPaint();
	}
}

//当用户拖动最小化窗口时系统调用此函数取得光标
//显示。
HCURSOR CElevatorSimulationDlg::OnQueryDragIcon()
{
	return static_cast<HCURSOR>(m_hIcon);
}

