#pragma once

#include "ElevatorStatusPalette.h"
#include "Resource.h"

#include <afxcmn.h>
#include <afxwin.h>
#include <algorithm>
#include <array>
#include <cstddef>
#include <cwchar>

class DashboardKpiBar : public CStatic
{
public:
    BOOL Create(CWnd* parent, UINT controlId)
    {
        return CStatic::Create(L"", WS_CHILD | WS_VISIBLE | SS_NOTIFY,
            CRect(), parent, controlId);
    }

    void SetFonts(CFont* titleFont, CFont* valueFont)
    {
        m_titleFont = titleFont;
        m_valueFont = valueFont;
        if (GetSafeHwnd() != nullptr) Invalidate(FALSE);
    }

    void SetValues(const std::array<CString, 6>& values)
    {
        if (m_values == values) return;
        m_values = values;
        if (GetSafeHwnd() != nullptr) Invalidate(FALSE);
    }

protected:
    LRESULT WindowProc(UINT message, WPARAM wParam, LPARAM lParam) override
    {
        if (message == WM_ERASEBKGND) return TRUE;
        if (message == WM_PAINT)
        {
            PaintBar();
            return 0;
        }
        return CStatic::WindowProc(message, wParam, lParam);
    }

private:
    std::array<CString, 6> m_values{
        L"0", L"0", L"0", L"0", L"0.00 秒", L"0.00 秒"
    };
    CFont* m_titleFont = nullptr;
    CFont* m_valueFont = nullptr;

    void PaintBar()
    {
        CPaintDC paintDc(this);
        CRect client;
        GetClientRect(&client);
        if (client.IsRectEmpty()) return;

        CDC dc;
        dc.CreateCompatibleDC(&paintDc);
        CBitmap bitmap;
        bitmap.CreateCompatibleBitmap(&paintDc, client.Width(), client.Height());
        CBitmap* oldBitmap = dc.SelectObject(&bitmap);

        dc.FillSolidRect(client, ::GetSysColor(COLOR_3DFACE));
        CRect panel(client);
        panel.right -= 1;
        panel.bottom -= 1;
        dc.FillSolidRect(panel, RGB(250, 252, 255));
        dc.Draw3dRect(panel, RGB(207, 218, 231), RGB(207, 218, 231));

        static constexpr COLORREF Accents[] = {
            RGB(37, 99, 235), RGB(217, 119, 6), RGB(124, 58, 237),
            RGB(5, 150, 105), RGB(8, 145, 178), RGB(220, 38, 38)
        };
        static constexpr const wchar_t* Titles[] = {
            L"总生成", L"等待中", L"乘梯中", L"已到达", L"平均等待", L"最大等待"
        };

        const int count = static_cast<int>(m_values.size());
        const int columnWidth = panel.Width() / count;
        const int titleBandBottom = (std::min)(panel.bottom - 32,
            panel.top + (std::max)(34, panel.Height() * 42 / 100));
        dc.FillSolidRect(panel.left + 1, panel.top + 4,
            panel.Width() - 2, titleBandBottom - panel.top - 4, RGB(244, 248, 252));
        dc.SetBkMode(TRANSPARENT);

        for (int index = 0; index < count; ++index)
        {
            const int left = panel.left + index * columnWidth;
            const int right = index + 1 == count ? panel.right : left + columnWidth;
            dc.FillSolidRect(left + 1, panel.top + 1,
                (std::max)(0, right - left - 1), 4, Accents[index]);
            if (index > 0)
                dc.FillSolidRect(left, panel.top + 11, 1,
                    (std::max)(0, panel.Height() - 22), RGB(221, 228, 236));

            CRect titleRect(left + 12, panel.top + 7, right - 10, titleBandBottom);
            CFont* oldFont = nullptr;
            if (m_titleFont != nullptr) oldFont = dc.SelectObject(m_titleFont);
            dc.SetTextColor(RGB(51, 65, 85));
            dc.DrawTextW(Titles[index], titleRect,
                DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);

            CRect valueRect(left + 12, titleBandBottom + 2, right - 10, panel.bottom - 5);
            if (m_valueFont != nullptr) dc.SelectObject(m_valueFont);
            dc.SetTextColor(RGB(25, 39, 58));
            dc.DrawTextW(m_values[index], valueRect,
                DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);
            if (oldFont != nullptr) dc.SelectObject(oldFont);
        }

        paintDc.BitBlt(0, 0, client.Width(), client.Height(), &dc, 0, 0, SRCCOPY);
        dc.SelectObject(oldBitmap);
    }
};

class HallCallDashboardList : public CListCtrl
{
protected:
    LRESULT WindowProc(UINT message, WPARAM wParam, LPARAM lParam) override
    {
        const LRESULT result = CListCtrl::WindowProc(message, wParam, lParam);
        if (message == WM_PAINT && GetSafeHwnd() != nullptr)
        {
            CClientDC dc(this);
            DrawSummary(dc);
        }
        return result;
    }

private:
    void DrawSummary(CDC& dc)
    {
        CRect client;
        GetClientRect(&client);
        if (client.Width() < 180 || client.Height() < 72) return;

        int contentTop = 10;
        if (CHeaderCtrl* header = GetHeaderCtrl())
        {
            CRect headerRect;
            header->GetWindowRect(&headerRect);
            ScreenToClient(&headerRect);
            const int candidate = static_cast<int>(headerRect.bottom) + 10;
            if (candidate > contentTop) contentTop = candidate;
        }

        const int itemCount = GetItemCount();
        if (itemCount > 0)
        {
            CRect lastItem;
            if (GetItemRect(itemCount - 1, &lastItem, LVIR_BOUNDS))
            {
                const int candidate = static_cast<int>(lastItem.bottom) + 16;
                if (candidate > contentTop) contentTop = candidate;
            }
        }

        const int clientBottom = static_cast<int>(client.bottom);
        if (clientBottom - contentTop < 28) return;

        int cardBottom = contentTop + 32;
        if (cardBottom > clientBottom - 4) cardBottom = clientBottom - 4;
        CRect card;
        card.SetRect(static_cast<int>(client.left) + 4, contentTop,
            static_cast<int>(client.right) - 4, cardBottom);
        if (card.Height() < 24) return;

        dc.FillSolidRect(card, RGB(248, 250, 252));
        CPen borderPen(PS_SOLID, 1, RGB(210, 216, 224));
        CPen* oldPen = dc.SelectObject(&borderPen);
        dc.Rectangle(card);
        dc.SelectObject(oldPen);
        dc.SetBkMode(TRANSPARENT);
        if (GetFont() != nullptr) dc.SelectObject(GetFont());

        std::size_t totalWaiting = 0;
        int assigned = 0;
        for (int row = 0; row < itemCount; ++row)
        {
            totalWaiting += static_cast<std::size_t>(_wtoi(GetItemText(row, 2)));
            const CString owner = GetItemText(row, 3);
            if (!owner.IsEmpty() && owner != L"未分配") ++assigned;
        }
        const int unassigned = itemCount - assigned;

        CString summary;
        summary.Format(L"外呼 %d    等待 %zu    已分配 %d    未分配 %d",
            itemCount, totalWaiting, assigned, unassigned);
        CRect textRect = card;
        textRect.DeflateRect(6, 1);
        dc.SetTextColor(RGB(58, 66, 77));
        dc.DrawTextW(summary, textRect,
            DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);
    }
};

class DashboardRightTabs : public CTabCtrl
{
};

class ElevatorStateLegend : public CStatic
{
protected:
    LRESULT WindowProc(UINT message, WPARAM wParam, LPARAM lParam) override
    {
        if (message == WM_ERASEBKGND) return TRUE;
        if (message != WM_PAINT)
            return CStatic::WindowProc(message, wParam, lParam);

        CPaintDC paintDc(this);
        CRect client;
        GetClientRect(&client);
        if (client.IsRectEmpty()) return 0;

        CDC dc;
        dc.CreateCompatibleDC(&paintDc);
        CBitmap bitmap;
        bitmap.CreateCompatibleBitmap(&paintDc, client.Width(), client.Height());
        CBitmap* oldBitmap = dc.SelectObject(&bitmap);
        dc.FillSolidRect(client, ::GetSysColor(COLOR_3DFACE));
        dc.SetBkMode(TRANSPARENT);
        dc.SetTextColor(RGB(45, 52, 62));
        if (GetFont() != nullptr) dc.SelectObject(GetFont());

        static constexpr const wchar_t* Labels[] = {
            L"上行", L"下行", L"服务", L"满载", L"空闲"
        };
        static constexpr COLORREF Colors[] = {
            ElevatorStatusPalette::MovingUpFill,
            ElevatorStatusPalette::MovingDownFill,
            ElevatorStatusPalette::ServicingFill,
            ElevatorStatusPalette::FullFill,
            ElevatorStatusPalette::IdleFill
        };
        constexpr int ItemCount = static_cast<int>(_countof(Labels));
        constexpr int MarkerTextGap = 4;
        const int singleRowMarkerSize = (std::max)(8, (std::min)(11, client.Height() / 3));
        int itemWidths[ItemCount]{};
        for (int index = 0; index < ItemCount; ++index)
        {
            itemWidths[index] = singleRowMarkerSize + MarkerTextGap +
                dc.GetTextExtent(Labels[index]).cx;
        }

        auto rowWidth = [&itemWidths](int begin, int end, int itemGap)
        {
            int width = itemGap * (end - begin - 1);
            for (int index = begin; index < end; ++index) width += itemWidths[index];
            return width;
        };
        auto drawRow = [&](int begin, int end, const CRect& row, int itemGap)
        {
            const int markerSize = (std::max)(7,
                (std::min)(singleRowMarkerSize, row.Height() - 4));
            const int totalWidth = rowWidth(begin, end, itemGap);
            int x = row.left + (std::max)(0, (row.Width() - totalWidth) / 2);
            const int markerTop = row.top + (row.Height() - markerSize) / 2;
            for (int index = begin; index < end; ++index)
            {
                CRect marker(x, markerTop, x + markerSize, markerTop + markerSize);
                dc.FillSolidRect(marker, Colors[index]);
                dc.Draw3dRect(marker, Colors[index], Colors[index]);
                x += markerSize + MarkerTextGap;
                const int textWidth = itemWidths[index] - singleRowMarkerSize - MarkerTextGap;
                CRect labelRect(x, row.top, x + textWidth, row.bottom);
                dc.DrawTextW(Labels[index], labelRect,
                    DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
                x += textWidth + itemGap;
            }
        };
        auto drawCell = [&](int index, const CRect& cell)
        {
            const int markerSize = (std::max)(7,
                (std::min)(singleRowMarkerSize, cell.Height() - 4));
            const int textWidth = dc.GetTextExtent(Labels[index]).cx;
            const int totalWidth = markerSize + MarkerTextGap + textWidth;
            int x = cell.left + (std::max)(0, (cell.Width() - totalWidth) / 2);
            const int markerTop = cell.top + (cell.Height() - markerSize) / 2;
            CRect marker(x, markerTop, x + markerSize, markerTop + markerSize);
            dc.FillSolidRect(marker, Colors[index]);
            dc.Draw3dRect(marker, Colors[index], Colors[index]);
            x += markerSize + MarkerTextGap;
            CRect labelRect(x, cell.top, x + textWidth, cell.bottom);
            dc.DrawTextW(Labels[index], labelRect,
                DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
        };

        const bool needsTwoRows = client.Width() < 300 ||
            rowWidth(0, ItemCount, 4) > client.Width();
        if (needsTwoRows)
        {
            CRect firstRow = client;
            firstRow.bottom = client.top + client.Height() / 2;
            CRect secondRow = client;
            secondRow.top = firstRow.bottom;
            const int firstCellWidth = firstRow.Width() / 3;
            for (int index = 0; index < 3; ++index)
            {
                CRect cell(firstRow.left + index * firstCellWidth, firstRow.top,
                    index == 2 ? firstRow.right : firstRow.left + (index + 1) * firstCellWidth,
                    firstRow.bottom);
                drawCell(index, cell);
            }
            const int secondCellWidth = secondRow.Width() / 2;
            for (int index = 3; index < ItemCount; ++index)
            {
                const int column = index - 3;
                CRect cell(secondRow.left + column * secondCellWidth, secondRow.top,
                    index + 1 == ItemCount ? secondRow.right :
                    secondRow.left + (column + 1) * secondCellWidth, secondRow.bottom);
                drawCell(index, cell);
            }
        }
        else if (rowWidth(0, ItemCount, 10) <= client.Width())
        {
            drawRow(0, ItemCount, client, 10);
        }
        else
        {
            drawRow(0, ItemCount, client, 4);
        }

        paintDc.BitBlt(0, 0, client.Width(), client.Height(), &dc, 0, 0, SRCCOPY);
        dc.SelectObject(oldBitmap);
        return 0;
    }
};

class ElevatorDetailDashboard : public CStatic
{
public:
    void SetTrafficText(const CString& text)
    {
        if (m_trafficText == text) return;
        m_trafficText = text;
        Invalidate(FALSE);
    }

protected:
    LRESULT WindowProc(UINT message, WPARAM wParam, LPARAM lParam) override
    {
        if (message == WM_WINDOWPOSCHANGING)
        {
            WINDOWPOS* position = reinterpret_cast<WINDOWPOS*>(lParam);
            if (position != nullptr && (position->flags & SWP_NOSIZE) == 0)
            {
                if (CWnd* parent = GetParent())
                {
                    CRect parentClient;
                    parent->GetClientRect(&parentClient);
                    const int available = static_cast<int>(parentClient.bottom) - position->y - 24;
                    if (available > position->cy) position->cy = available;
                }
            }
            return CStatic::WindowProc(message, wParam, lParam);
        }
        if (message == WM_SETTEXT)
        {
            const wchar_t* incoming = reinterpret_cast<const wchar_t*>(lParam);
            const CString next = incoming != nullptr ? incoming : L"";
            if (next != m_sourceText)
            {
                m_sourceText = next;
                Invalidate(FALSE);
            }
            return TRUE;
        }
        if (message == WM_GETTEXTLENGTH)
            return static_cast<LRESULT>(m_sourceText.GetLength());
        if (message == WM_GETTEXT)
        {
            if (lParam == 0 || wParam == 0) return 0;
            wchar_t* buffer = reinterpret_cast<wchar_t*>(lParam);
            const int capacity = static_cast<int>(wParam);
            const int count = m_sourceText.GetLength() < capacity - 1 ?
                m_sourceText.GetLength() : capacity - 1;
            if (count > 0) std::wmemcpy(buffer, m_sourceText.GetString(), count);
            buffer[count] = L'\0';
            return count;
        }
        if (message == WM_ERASEBKGND)
            return TRUE;
        if (message == WM_LBUTTONUP)
        {
            const int x = static_cast<short>(LOWORD(lParam));
            const int y = static_cast<short>(HIWORD(lParam));
            if (m_backRect.PtInRect(CPoint(x, y)))
            {
                ReturnToHallCalls();
                return 0;
            }
        }
        if (message == WM_SETCURSOR)
        {
            CPoint point;
            ::GetCursorPos(&point);
            ScreenToClient(&point);
            if (m_backRect.PtInRect(point))
            {
                ::SetCursor(::LoadCursor(nullptr, IDC_HAND));
                return TRUE;
            }
        }
        if (message == WM_PAINT)
        {
            PaintDashboard();
            return 0;
        }
        return CStatic::WindowProc(message, wParam, lParam);
    }

private:
    CString m_sourceText;
    CString m_trafficText = L"--";
    CRect m_backRect;

    static CString ExtractField(const CString& source, const wchar_t* label)
    {
        const int labelPosition = source.Find(label);
        if (labelPosition < 0) return L"--";
        const int start = labelPosition + static_cast<int>(std::wcslen(label));
        int end = source.Find(L"\r\n", start);
        if (end < 0) end = source.GetLength();
        CString value = source.Mid(start, end - start);
        value.Trim();
        return value.IsEmpty() ? CString(L"--") : value;
    }

    void PaintDashboard()
    {
        CPaintDC paintDc(this);
        CRect client;
        GetClientRect(&client);
        if (client.IsRectEmpty()) return;

        CDC dc;
        dc.CreateCompatibleDC(&paintDc);
        CBitmap bitmap;
        bitmap.CreateCompatibleBitmap(&paintDc, client.Width(), client.Height());
        CBitmap* oldBitmap = dc.SelectObject(&bitmap);

        dc.FillSolidRect(client, ::GetSysColor(COLOR_3DFACE));
        dc.SetBkMode(TRANSPARENT);
        if (GetFont() != nullptr) dc.SelectObject(GetFont());

        const CString floor = ExtractField(m_sourceText, L"当前楼层：");
        const CString direction = ExtractField(m_sourceText, L"方向：");
        const CString state = ExtractField(m_sourceText, L"状态：");
        const CString load = ExtractField(m_sourceText, L"载客：");

        m_backRect.SetRect(static_cast<int>(client.left) + 4,
            static_cast<int>(client.top) + 4,
            static_cast<int>(client.right) - 4,
            static_cast<int>(client.top) + 38);
        dc.FillSolidRect(m_backRect, RGB(245, 248, 252));
        CPen buttonPen(PS_SOLID, 1, RGB(194, 204, 218));
        CPen* oldPen = dc.SelectObject(&buttonPen);
        dc.Rectangle(m_backRect);
        dc.SelectObject(oldPen);
        dc.SetTextColor(RGB(48, 89, 145));
        dc.DrawTextW(L"←  返回外呼列表", m_backRect,
            DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

        CRect sectionTitle;
        sectionTitle.SetRect(static_cast<int>(client.left) + 4,
            static_cast<int>(m_backRect.bottom) + 15,
            static_cast<int>(client.right) - 4,
            static_cast<int>(m_backRect.bottom) + 40);
        dc.SetTextColor(RGB(35, 42, 52));
        dc.DrawTextW(L"实时状态", sectionTitle,
            DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

        const int gap = 8;
        const int left = static_cast<int>(client.left) + 4;
        const int right = static_cast<int>(client.right) - 4;
        const int cellWidth = (right - left - gap) / 2;
        const int firstTop = static_cast<int>(sectionTitle.bottom) + 6;
        const int cellHeight = 64;

        DrawMetric(dc, CRect(left, firstTop,
            left + cellWidth, firstTop + cellHeight), L"当前楼层", floor);
        DrawMetric(dc, CRect(left + cellWidth + gap, firstTop,
            right, firstTop + cellHeight), L"运行方向", direction);
        DrawMetric(dc, CRect(left, firstTop + cellHeight + gap,
            left + cellWidth, firstTop + cellHeight * 2 + gap), L"运行状态", state);
        DrawMetric(dc, CRect(left + cellWidth + gap, firstTop + cellHeight + gap,
            right, firstTop + cellHeight * 2 + gap), L"载客情况", load);

        int cardTop = firstTop + cellHeight * 2 + gap + 14;
        CRect taskCard(left, cardTop, right, cardTop + 86);
        DrawCardBorder(dc, taskCard);
        CRect taskText = taskCard;
        taskText.DeflateRect(10, 8, 10, 6);
        CString task;
        if (state == L"空闲")
            task = L"当前任务\r\n空闲待命，等待新的群控分配。";
        else if (state == L"上行中" || state == L"下行中")
            task = L"当前任务\r\n正在按顺向扫描规则保持运行方向；新请求不会打断当前楼层间动作。";
        else if (state == L"上客中")
            task = L"当前任务\r\n正在执行乘客登梯服务。";
        else if (state == L"下客中")
            task = L"当前任务\r\n正在执行乘客离梯服务。";
        else
            task = L"当前任务\r\n正在处理当前停站服务。";
        dc.SetTextColor(RGB(55, 64, 75));
        dc.DrawTextW(task, taskText, DT_LEFT | DT_TOP | DT_WORDBREAK | DT_NOPREFIX);

        cardTop = static_cast<int>(taskCard.bottom) + 10;
        CRect groupCard(left, cardTop, right, cardTop + 104);
        DrawCardBorder(dc, groupCard);
        CRect groupText = groupCard;
        groupText.DeflateRect(10, 8, 10, 6);
        dc.SetTextColor(RGB(55, 64, 75));
        dc.DrawTextW(
            L"群控参与\r\n该电梯作为候选参与事件级预计到达时间与调度成本评分。实际外呼归属还会受到联合调度、动态改派与滞回策略影响。",
            groupText, DT_LEFT | DT_TOP | DT_WORDBREAK | DT_NOPREFIX);

        cardTop = static_cast<int>(groupCard.bottom) + 10;
        CRect trafficRect(left, cardTop, right, cardTop + 48);
        CString trafficText;
        trafficText.Format(L"当前客流：%s", m_trafficText.GetString());
        dc.SetTextColor(RGB(45, 83, 128));
        dc.DrawTextW(trafficText, trafficRect,
            DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

        paintDc.BitBlt(0, 0, client.Width(), client.Height(), &dc, 0, 0, SRCCOPY);
        dc.SelectObject(oldBitmap);
    }

    static void DrawCardBorder(CDC& dc, const CRect& bounds)
    {
        dc.FillSolidRect(bounds, RGB(248, 250, 252));
        CPen pen(PS_SOLID, 1, RGB(216, 222, 230));
        CPen* oldPen = dc.SelectObject(&pen);
        dc.Rectangle(bounds);
        dc.SelectObject(oldPen);
    }

    static void DrawMetric(CDC& dc, const CRect& bounds,
        const wchar_t* label, const CString& value)
    {
        dc.FillSolidRect(bounds, RGB(250, 251, 253));
        CPen pen(PS_SOLID, 1, RGB(218, 224, 232));
        CPen* oldPen = dc.SelectObject(&pen);
        dc.Rectangle(bounds);
        dc.SelectObject(oldPen);

        CRect labelRect = bounds;
        labelRect.DeflateRect(8, 6, 8, 0);
        labelRect.bottom = labelRect.top + 20;
        dc.SetTextColor(RGB(102, 111, 123));
        dc.DrawTextW(label, labelRect,
            DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

        CRect valueRect = bounds;
        valueRect.DeflateRect(8, 25, 8, 5);
        dc.SetTextColor(RGB(32, 40, 51));
        dc.DrawTextW(value, valueRect,
            DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    }

    void ReturnToHallCalls()
    {
        CWnd* parent = GetParent();
        if (parent == nullptr) return;
        CWnd* tabsWindow = parent->GetDlgItem(IDC_TAB_RIGHT);
        if (tabsWindow == nullptr) return;

        CTabCtrl* tabs = static_cast<CTabCtrl*>(tabsWindow);
        tabs->SetCurSel(0);

        NMHDR notification{};
        notification.hwndFrom = tabs->GetSafeHwnd();
        notification.idFrom = IDC_TAB_RIGHT;
        notification.code = TCN_SELCHANGE;
        parent->SendMessage(WM_NOTIFY, IDC_TAB_RIGHT,
            reinterpret_cast<LPARAM>(&notification));
    }
};
