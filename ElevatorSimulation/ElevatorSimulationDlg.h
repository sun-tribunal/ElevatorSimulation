
// ElevatorSimulationDlg.h: 头文件
//

#pragma once

#include "Core/SimulationWorker.h"
#include "ElevatorBuildingView.h"
#include "FloorAnalyticsViews.h"
#include "StatisticsTrendView.h"
#include "DashboardControls.h"
#include "ElevatorDetailDashboardLarge.h"

#include <array>
#include <chrono>
#include <memory>
#include <optional>
#include <vector>


// CElevatorSimulationDlg 对话框
class CElevatorSimulationDlg : public CDialogEx
{
// 构造
public:
	CElevatorSimulationDlg(CWnd* pParent = nullptr);	// 标准构造函数
	~CElevatorSimulationDlg() override;

// 对话框数据
#ifdef AFX_DESIGN_TIME
	enum { IDD = IDD_ELEVATORSIMULATION_DIALOG };
#endif

	protected:
	virtual void DoDataExchange(CDataExchange* pDX);	// DDX/DDV 支持


// 实现
protected:
	HICON m_hIcon;

	// 生成的消息映射函数
	virtual BOOL OnInitDialog();
	afx_msg void OnSysCommand(UINT nID, LPARAM lParam);
	afx_msg void OnPaint();
	afx_msg HCURSOR OnQueryDragIcon();
	afx_msg void OnTimer(UINT_PTR nIDEvent);
	afx_msg void OnDestroy();
	afx_msg void OnSize(UINT nType, int cx, int cy);
	afx_msg void OnGetMinMaxInfo(MINMAXINFO* lpMMI);
	afx_msg void OnBnClickedStart();
	afx_msg void OnBnClickedPause();
	afx_msg void OnBnClickedResume();
	afx_msg void OnBnClickedReset();
	afx_msg void OnBnClickedAddPassengers();
	afx_msg void OnHScroll(UINT nSBCode, UINT nPos, CScrollBar* pScrollBar);
	afx_msg void OnEnChangeManualFloor();
	afx_msg void OnCbnSelchangeTrafficScenario();
	afx_msg void OnBnClickedPanelToggle();
	afx_msg void OnTcnSelchangeLeftTabs(NMHDR* pNMHDR, LRESULT* pResult);
	afx_msg void OnTcnSelchangePages(NMHDR* pNMHDR, LRESULT* pResult);
	afx_msg void OnTcnSelchangeRightTabs(NMHDR* pNMHDR, LRESULT* pResult);
	afx_msg void OnNMClickHallCallList(NMHDR* pNMHDR, LRESULT* pResult);
	afx_msg LRESULT OnElevatorSelectionChanged(WPARAM wParam, LPARAM lParam);
	DECLARE_MESSAGE_MAP()

private:
	static constexpr UINT_PTR SimulationTimerId = 1;
	static constexpr UINT SimulationTimerIntervalMs = 33;
	static constexpr int NormalBuildingRefreshMs = 33;
	static constexpr int LargeBuildingRefreshMs = 50;
	static constexpr int StatisticsRefreshMs = 250;

	struct HallCallIdentity
	{
		int floor = 1;
		Direction direction = Direction::Idle;

		bool operator==(const HallCallIdentity& other) const noexcept
		{
			return floor == other.floor && direction == other.direction;
		}
	};

	std::unique_ptr<SimulationWorker> m_simulationWorker;
	CListCtrl m_elevatorList;
	CListCtrl m_floorList;
	HallCallDashboardList m_hallCallList;
	CComboBox m_trafficScenarioCombo;
	CComboBox m_trafficPatternCombo;
	CButton m_predictiveRebalancingCheck;
	CFont m_bodyFont;
	CFont m_titleFont;
	CFont m_sectionFont;
	CFont m_pageTabFont;
	CFont m_statValueFont;
	CStatic m_headerTitle;
	CStatic m_headerStateLabel;
	CStatic m_headerTimeLabel;
	CStatic m_headerSpeedLabel;
	CStatic m_headerSpeed;
	CStatic m_headerTraffic;
	CButton m_leftPanel;
	CTabCtrl m_leftTabs;
	CButton m_mainPanel;
	CButton m_rightPanel;
	CButton m_panelToggle;
	ElevatorBuildingView m_buildingView;
	StatisticsTrendView m_statisticsTrendView;
	FloorTrafficHeatmapView m_floorTrafficHeatmapView;
	CTabCtrl m_pageTabs;
	ElevatorStateLegend m_elevatorStateLegend;
	DashboardRightTabs m_rightTabs;
	CStatic m_rightHallCallTitle;
	CStatic m_rightAlgorithmTitle;
	CStatic m_pagePlaceholder;
	CStatic m_algorithmPageSummary;
	CListCtrl m_algorithmCandidateList;
	FloorCoverageView m_floorCoverageView;
	CStatic m_elevatorDetailTitle;
	ElevatorDetailDashboardLarge m_elevatorDetailBody;
	CStatic m_algorithmPlaceholder;
	CStatic m_parameterSection;
	CStatic m_manualSection;
	CStatic m_manualDescription;
	std::array<CStatic, 3> m_manualLabels;
	CEdit m_manualFloorEdit;
	CEdit m_manualUpEdit;
	CEdit m_manualDownEdit;
	CButton m_addPassengersButton;
	CStatic m_manualFeedback;
	CStatic m_controlSection;
	CStatic m_speedSection;
	std::array<CStatic, 11> m_parameterLabels;
	CSliderCtrl m_speedSlider;
	DashboardKpiBar m_kpiBar;
	bool m_uiReady = false;
	bool m_rightPanelExpanded = true;
	bool m_buildingRefreshScheduled = false;
	bool m_lastBuildingLargeScaleMode = false;
	std::chrono::steady_clock::time_point m_nextBuildingRefresh;
	std::vector<StatisticsTrendPoint> m_statisticsTrend;
	double m_nextTrendSampleTime = 0.0;
	double m_lastTrendSimulationTime = 0.0;
	bool m_trendHasSnapshot = false;
	bool m_statisticsRefreshScheduled = false;
	std::chrono::steady_clock::time_point m_nextStatisticsRefresh;
	std::optional<HallCallIdentity> m_observedHallCall;
	std::shared_ptr<const DispatchObservationSnapshot> m_lastRenderedObservation;
	bool m_rebuildingHallCallList = false;
	bool m_speedSliderDragging = false;

	void CreateUIFramework();
	void InitializeListControls();
	void RelayoutUI();
	void UpdateLeftPanelVisibility();
	void UpdateManualDirectionLocks(
		const std::shared_ptr<const SimulationUISnapshot>& snapshot, bool updateHint);
	void UpdateTabPageVisibility();
	void UpdateRightPanelVisibility();
	void UpdateElevatorDetails(const std::shared_ptr<const SimulationUISnapshot>& snapshot);
	void ClearStatisticsTrend();
	void UpdateStatisticsTrend(const std::shared_ptr<const SimulationUISnapshot>& snapshot,
		bool forceRefresh = false);
	void BeginHallCallObservation(HallCallIdentity identity);
	bool SelectObservationForElevator(int elevatorId,
		const std::shared_ptr<const SimulationUISnapshot>& snapshot);
	void SelectHallCall(HallCallIdentity identity);
	void ClearHallCallObservation();
	void ValidateObservedHallCall(const std::shared_ptr<const SimulationUISnapshot>& snapshot);
	void RefreshObservationViews(bool forceRefresh = false);
	void ShowObservationEmptyState(const wchar_t* message);
	void PopulateObservationViews(const DispatchObservationSnapshot& observation);
	void UpdateSpeedDisplay(double speed);
	double SpeedFromSlider(int position) const;
	int SliderFromSpeed(double speed) const;
	void ApplySimulationSpeed(double speed);
	bool ReadConfiguration(SimulationConfig& config, std::uint32_t& seed);
	bool ReadIntControl(int controlId, const wchar_t* fieldName, int& value);
	bool ReadDoubleControl(int controlId, const wchar_t* fieldName, double& value);
	bool ReadManualInteger(CEdit& control, const wchar_t* fieldName, int& value);
	void ShowManualInputError(CEdit& control, const CString& message);
	void ShowInputError(const CString& message);
	void UpdateControlStates(const std::shared_ptr<const SimulationUISnapshot>& snapshot);
	void RefreshBuildingView(const std::shared_ptr<const SimulationUISnapshot>& snapshot,
		bool forceRefresh);
	void RefreshSimulationView(bool forceBuildingRefresh = false);
};
