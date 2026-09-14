#include "pch.h"
#include "StatisticsTrendView.h"

#include <algorithm>
#include <cmath>

IMPLEMENT_DYNAMIC(StatisticsTrendView, CWnd)

BEGIN_MESSAGE_MAP(StatisticsTrendView, CWnd)
	ON_WM_PAINT()
	ON_WM_ERASEBKGND()
END_MESSAGE_MAP()

namespace
{
	double WaitingValue(const StatisticsTrendPoint& point)
	{
		return static_cast<double>(point.waitingCount);
	}

	double ArrivedValue(const StatisticsTrendPoint& point)
	{
		return static_cast<double>(point.arrivedCount);
	}

	double AverageWaitValue(const StatisticsTrendPoint& point)
	{
		return point.averageWaitingTime;
	}
}

bool StatisticsTrendView::Create(CWnd* parent, UINT controlId)
{
	const CString className = AfxRegisterWndClass(CS_HREDRAW | CS_VREDRAW,
		::LoadCursor(nullptr, IDC_ARROW), reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1), nullptr);
	return CreateEx(WS_EX_CLIENTEDGE, className, L"统计分析",
		WS_CHILD | WS_CLIPSIBLINGS, CRect(), parent, controlId) != FALSE;
}

void StatisticsTrendView::SetTrendPoints(const std::vector<StatisticsTrendPoint>& points)
{
	m_points = points;
	Invalidate(FALSE);
}

BOOL StatisticsTrendView::OnEraseBkgnd(CDC*)
{
	return TRUE;
}

void StatisticsTrendView::OnPaint()
{
	CPaintDC paintDc(this);
	CRect client;
	GetClientRect(&client);
	if (client.IsRectEmpty()) return;

	CDC bufferDc;
	bufferDc.CreateCompatibleDC(&paintDc);
	CBitmap bitmap;
	bitmap.CreateCompatibleBitmap(&paintDc, client.Width(), client.Height());
	CBitmap* oldBitmap = bufferDc.SelectObject(&bitmap);
	bufferDc.FillSolidRect(client, RGB(246, 248, 251));
	bufferDc.SetBkMode(TRANSPARENT);
	if (GetFont() != nullptr) bufferDc.SelectObject(GetFont());

	if (m_points.empty())
	{
		bufferDc.SetTextColor(RGB(88, 98, 112));
		bufferDc.DrawTextW(L"暂无统计趋势数据\r\n启动仿真后将按模型时间自动采样",
			client, DT_CENTER | DT_VCENTER | DT_WORDBREAK | DT_NOPREFIX);
		paintDc.BitBlt(0, 0, client.Width(), client.Height(), &bufferDc, 0, 0, SRCCOPY);
		bufferDc.SelectObject(oldBitmap);
		return;
	}

	const int outer = 14;
	const int headerHeight = 38;
	const int gap = 10;
	const int contentTop = outer + headerHeight;
	const int contentWidth = client.Width() - outer * 2;
	const int columnWidth = (contentWidth - gap) / 2;
	const int contentHeight = client.Height() - contentTop - outer;
	const int rowHeight = (contentHeight - gap) / 2;

	CString heading;
	heading.Format(L"统计分析    模型时间 %.1f 秒    采样点 %zu",
		m_points.back().time, m_points.size());
	bufferDc.SetTextColor(RGB(35, 43, 55));
	CRect headingRect(outer + 2, outer, client.right - outer, outer + 28);
	bufferDc.DrawTextW(heading, headingRect, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

	const CRect topLeft(outer, contentTop, outer + columnWidth, contentTop + rowHeight);
	const CRect topRight(outer + columnWidth + gap, contentTop,
		client.right - outer, contentTop + rowHeight);
	const CRect bottomLeft(outer, contentTop + rowHeight + gap,
		outer + columnWidth, client.bottom - outer);
	const CRect bottomRight(outer + columnWidth + gap, contentTop + rowHeight + gap,
		client.right - outer, client.bottom - outer);

	DrawChart(bufferDc, topLeft, L"等待人数趋势", RGB(194, 70, 68), WaitingValue, true);
	DrawChart(bufferDc, topRight, L"平均等待时间趋势（秒）", RGB(46, 103, 177), AverageWaitValue);
	DrawChart(bufferDc, bottomLeft, L"累计到达人数", RGB(45, 126, 78), ArrivedValue, true);
	DrawOverview(bufferDc, bottomRight);

	paintDc.BitBlt(0, 0, client.Width(), client.Height(), &bufferDc, 0, 0, SRCCOPY);
	bufferDc.SelectObject(oldBitmap);
}

void StatisticsTrendView::DrawChart(CDC& dc, const CRect& bounds, const wchar_t* title,
	COLORREF color, double (*valueOf)(const StatisticsTrendPoint&), bool integerValues) const
{
	dc.FillSolidRect(bounds, RGB(255, 255, 255));
	CPen borderPen(PS_SOLID, 1, RGB(214, 220, 228));
	CPen* oldPen = dc.SelectObject(&borderPen);
	dc.Rectangle(bounds);

	const double latestValue = valueOf(m_points.back());
	double dataMaximum = 0.0;
	for (const auto& point : m_points)
		dataMaximum = (std::max)(dataMaximum, valueOf(point));
	const double scaleMaximum = (std::max)(1.0, dataMaximum);

	CString heading;
	if (integerValues)
		heading.Format(L"%s    当前 %.0f    峰值 %.0f", title, latestValue, dataMaximum);
	else
		heading.Format(L"%s    当前 %.2f    峰值 %.2f", title, latestValue, dataMaximum);
	dc.SetTextColor(RGB(35, 43, 55));
	CRect headingRect = bounds;
	headingRect.DeflateRect(12, 7, 12, 0);
	dc.DrawTextW(heading, headingRect, DT_LEFT | DT_TOP | DT_SINGLELINE | DT_NOPREFIX);

	CRect plot = bounds;
	plot.DeflateRect(50, 31, 14, 25);
	if (plot.Width() <= 1 || plot.Height() <= 1)
	{
		dc.SelectObject(oldPen);
		return;
	}

	CPen gridPen(PS_DOT, 1, RGB(226, 230, 236));
	dc.SelectObject(&gridPen);
	for (int part = 0; part <= 4; ++part)
	{
		const int y = plot.bottom - MulDiv(plot.Height(), part, 4);
		dc.MoveTo(plot.left, y);
		dc.LineTo(plot.right, y);
		const int x = plot.left + MulDiv(plot.Width(), part, 4);
		dc.MoveTo(x, plot.top);
		dc.LineTo(x, plot.bottom);
	}

	dc.SetTextColor(RGB(92, 102, 115));
	CString scale;
	if (integerValues)
		scale.Format(L"%.0f", scaleMaximum);
	else
		scale.Format(L"%.1f", scaleMaximum);
	CRect topScale(bounds.left + 5, plot.top - 8, plot.left - 4, plot.top + 10);
	dc.DrawTextW(scale, topScale, DT_RIGHT | DT_SINGLELINE | DT_NOPREFIX);
	CRect zeroScale(bounds.left + 5, plot.bottom - 9, plot.left - 4, plot.bottom + 9);
	dc.DrawTextW(L"0", zeroScale, DT_RIGHT | DT_SINGLELINE | DT_NOPREFIX);

	// 横向仅显示最近 TrendWindowSeconds 秒的滚动窗口，避免随时间推移整段历史被压缩。
	constexpr double TrendWindowSeconds = 60.0;
	const double lastTime = (std::max)(m_points.back().time, 0.5);
	const double windowStart = lastTime > TrendWindowSeconds ? lastTime - TrendWindowSeconds : 0.0;
	const double windowSpan = (std::max)(lastTime - windowStart, 0.5);
	CRect timeRect(plot.left, plot.bottom + 4, plot.right, bounds.bottom - 2);
	CString startTimeLabel;
	startTimeLabel.Format(L"%.1f 秒", windowStart);
	dc.DrawTextW(startTimeLabel, timeRect, DT_LEFT | DT_SINGLELINE | DT_NOPREFIX);
	CString endTimeLabel;
	endTimeLabel.Format(L"%.1f 秒", lastTime);
	dc.DrawTextW(endTimeLabel, timeRect, DT_RIGHT | DT_SINGLELINE | DT_NOPREFIX);

	CPen dataPen(PS_SOLID, 2, color);
	dc.SelectObject(&dataPen);
	bool first = true;
	for (const auto& point : m_points)
	{
		// 只绘制最近窗口内的采样点；更早的历史不再参与横向布局。
		if (point.time < windowStart)
			continue;
		const int x = plot.left + static_cast<int>(std::lround(
			plot.Width() * (point.time - windowStart) / windowSpan));
		const int y = plot.bottom - static_cast<int>(std::lround(
			plot.Height() * valueOf(point) / scaleMaximum));
		if (first)
		{
			dc.MoveTo(x, y);
			first = false;
		}
		else
		{
			dc.LineTo(x, y);
		}
	}
	dc.SelectObject(oldPen);
}

void StatisticsTrendView::DrawOverview(CDC& dc, const CRect& bounds) const
{
	dc.FillSolidRect(bounds, RGB(255, 255, 255));
	CPen borderPen(PS_SOLID, 1, RGB(214, 220, 228));
	CPen* oldPen = dc.SelectObject(&borderPen);
	dc.Rectangle(bounds);

	const auto& latest = m_points.back();
	std::size_t peakWaiting = 0;
	double peakAverageWait = 0.0;
	for (const auto& point : m_points)
	{
		peakWaiting = (std::max)(peakWaiting, point.waitingCount);
		peakAverageWait = (std::max)(peakAverageWait, point.averageWaitingTime);
	}
	const double throughput = latest.time > 0.0 ?
		static_cast<double>(latest.arrivedCount) / latest.time : 0.0;

	dc.SetTextColor(RGB(35, 43, 55));
	CRect titleRect = bounds;
	titleRect.DeflateRect(12, 7, 12, 0);
	dc.DrawTextW(L"运行概览", titleRect, DT_LEFT | DT_TOP | DT_SINGLELINE | DT_NOPREFIX);

	CRect content = bounds;
	content.DeflateRect(16, 38, 16, 14);
	const int rowHeight = (std::max)(30, content.Height() / 3);
	const int columnGap = 12;
	const int columnWidth = (content.Width() - columnGap) / 2;

	struct Metric
	{
		const wchar_t* label;
		CString value;
	};
	Metric metrics[6];
	metrics[0].label = L"当前等待";
	metrics[0].value.Format(L"%zu 人", latest.waitingCount);
	metrics[1].label = L"累计到达";
	metrics[1].value.Format(L"%zu 人", latest.arrivedCount);
	metrics[2].label = L"平均等待";
	metrics[2].value.Format(L"%.2f 秒", latest.averageWaitingTime);
	metrics[3].label = L"峰值等待";
	metrics[3].value.Format(L"%zu 人", peakWaiting);
	metrics[4].label = L"峰值平均等待";
	metrics[4].value.Format(L"%.2f 秒", peakAverageWait);
	metrics[5].label = L"到达吞吐率";
	metrics[5].value.Format(L"%.2f 人/秒", throughput);

	for (int index = 0; index < 6; ++index)
	{
		const int column = index % 2;
		const int row = index / 2;
		CRect cell(content.left + column * (columnWidth + columnGap),
			content.top + row * rowHeight,
			content.left + column * (columnWidth + columnGap) + columnWidth,
			content.top + (row + 1) * rowHeight - 4);
		dc.FillSolidRect(cell, RGB(249, 250, 252));
		CPen cellPen(PS_SOLID, 1, RGB(229, 233, 239));
		CPen* previousPen = dc.SelectObject(&cellPen);
		dc.Rectangle(cell);
		dc.SelectObject(previousPen);

		CRect labelRect = cell;
		labelRect.DeflateRect(9, 5, 9, 0);
		dc.SetTextColor(RGB(98, 108, 121));
		dc.DrawTextW(metrics[index].label, labelRect,
			DT_LEFT | DT_TOP | DT_SINGLELINE | DT_NOPREFIX);

		CRect valueRect = cell;
		valueRect.DeflateRect(9, 19, 9, 4);
		dc.SetTextColor(RGB(32, 40, 51));
		dc.DrawTextW(metrics[index].value, valueRect,
			DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
	}

	dc.SelectObject(oldPen);
}
