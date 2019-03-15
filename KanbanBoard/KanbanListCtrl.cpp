// KanbanListCtrl.cpp : implementation file
//

#include "stdafx.h"
#include "resource.h"
#include "KanbanListCtrl.h"
#include "Kanbanstatic.h"
#include "Kanbanenum.h"

#include "..\shared\graphicsMisc.h"
#include "..\shared\enstring.h"
#include "..\shared\enbitmap.h"
#include "..\shared\misc.h"
#include "..\shared\dialoghelper.h"
#include "..\shared\datehelper.h"
#include "..\shared\timehelper.h"
#include "..\shared\autoflag.h"
#include "..\shared\copywndcontents.h"
#include "..\shared\holdredraw.h"
#include "..\shared\winclasses.h"
#include "..\shared\wclassdefines.h"
#include "..\Shared\enimagelist.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif

/////////////////////////////////////////////////////////////////////////////

#ifndef LVS_EX_DOUBLEBUFFER
#	define LVS_EX_DOUBLEBUFFER  0x00010000
#endif

#ifndef LVS_EX_LABELTIP
#	define LVS_EX_LABELTIP		0x00004000
#endif

#ifndef TTM_ADJUSTRECT
#	define TTM_ADJUSTRECT       (WM_USER + 31)
#endif

/////////////////////////////////////////////////////////////////////////////

#ifndef WM_THEMECHANGED
#	define WM_THEMECHANGED 0x031A
#endif

/////////////////////////////////////////////////////////////////////////////

enum // checkbox images 
{ 
	KLCC_NONE,
	KLCC_UNCHECKED, 
	KLCC_CHECKED, 
	KLCC_MIXED, 
};

/////////////////////////////////////////////////////////////////////////////

const int MIN_LABEL_EDIT_WIDTH	= GraphicsMisc::ScaleByDPIFactor(200);
const int BAR_WIDTH				= GraphicsMisc::ScaleByDPIFactor(6);
const int NUM_TITLELINES		= 2;
const int LV_PADDING			= GraphicsMisc::ScaleByDPIFactor(3);
const int CHECKBOX_PADDING		= GraphicsMisc::ScaleByDPIFactor(1);
const int ATTRIB_INDENT			= GraphicsMisc::ScaleByDPIFactor(6);
const int TIP_PADDING			= GraphicsMisc::ScaleByDPIFactor(4);
const int IMAGE_SIZE			= GraphicsMisc::ScaleByDPIFactor(16);
const int IMAGE_PADDING			= 2/*GraphicsMisc::ScaleByDPIFactor(2)*/;
const int LEVEL_INDENT			= GraphicsMisc::ScaleByDPIFactor(16);

const CRect TEXT_BORDER			= CRect(2, 1, 3, 1);

/////////////////////////////////////////////////////////////////////////////
// CKanbanListCtrlEx

CKanbanListCtrl::CKanbanListCtrl(const CKanbanItemMap& data, const KANBANCOLUMN& columnDef,
								CFontCache& fonts, const CDWordArray& aPriorityColors, 
								const CKanbanAttributeArray& aDisplayAttrib)
	:
	m_data(data),
	m_tch(*this),
	m_columnDef(columnDef),
	m_aDisplayAttrib(aDisplayAttrib),
	m_bHideEmptyAttributes(FALSE),
	m_fonts(fonts),
	m_aPriorityColors(aPriorityColors),
	m_bStrikeThruDoneTasks(FALSE),
	m_bIndentSubtasks(FALSE),
	m_bTextColorIsBkgnd(FALSE),
	m_bSelected(FALSE),
	m_bShowTaskColorAsBar(FALSE),
	m_bShowCompletionCheckboxes(FALSE),
	m_bColorBarByPriority(FALSE),
	m_nItemTextHeight(-1),
	m_nItemTextBorder(-1),
	m_nAttribLabelVisiability(KBCAL_LONG),
	m_bSavingToImage(FALSE),
	m_bDropTarget(FALSE),
	m_bDrawTaskFlags(FALSE)
{
}

CKanbanListCtrl::~CKanbanListCtrl()
{
}

IMPLEMENT_DYNAMIC(CKanbanListCtrl, CTreeCtrl);

BEGIN_MESSAGE_MAP(CKanbanListCtrl, CTreeCtrl)
	//{{AFX_MSG_MAP(CKanbanListCtrlEx)
	ON_WM_CREATE()
	//}}AFX_MSG_MAP
	ON_NOTIFY_REFLECT(NM_CUSTOMDRAW, OnListCustomDraw)
	ON_MESSAGE(WM_THEMECHANGED, OnThemeChanged)
	ON_WM_LBUTTONDOWN()
	ON_WM_LBUTTONDBLCLK()
	ON_WM_SIZE()
	ON_WM_SETCURSOR()
	ON_NOTIFY(TTN_SHOW, 0, OnTooltipShow)
	ON_MESSAGE(WM_SETFONT, OnSetFont)
	ON_REGISTERED_MESSAGE(WM_TTC_TOOLHITTEST, OnToolHitTest)
END_MESSAGE_MAP()

/////////////////////////////////////////////////////////////////////////////
// CKanbanListCtrlEx message handlers

BOOL CKanbanListCtrl::Create(UINT nID, CWnd* pParentWnd)
{
	UINT nFlags = (WS_CHILD | WS_VISIBLE | LVS_REPORT | WS_TABSTOP |
				   TVS_NONEVENHEIGHT | TVS_SHOWSELALWAYS | TVS_EDITLABELS | 
				   TVS_FULLROWSELECT | TVS_NOTOOLTIPS | TVS_NOHSCROLL);

	return CTreeCtrl::Create(nFlags, CRect(0, 0, 0, 0), pParentWnd, nID);
}

int CKanbanListCtrl::OnCreate(LPCREATESTRUCT lpCreateStruct) 
{
	if (CTreeCtrl::OnCreate(lpCreateStruct) == -1)
		return -1;
	
	VERIFY(InitTooltip());

	m_ilFlags.Create(IDB_FLAG, 16, 1, RGB(255, 0, 255));
	CEnImageList::ScaleByDPIFactor(m_ilFlags);

	CFont* pFont = m_fonts.GetFont();
	ASSERT(pFont);

	if (pFont)
	{
		SendMessage(WM_SETFONT, (WPARAM)pFont->GetSafeHandle());
	}
	else
	{
		m_nItemTextHeight = -1;
		OnDisplayAttributeChanged();
	}

	if (GraphicsMisc::InitCheckboxImageList(*this, m_ilCheckboxes, IDB_CHECKBOXES, 255))
		SetImageList(&m_ilCheckboxes, TVSIL_STATE);

	SetExtendedStyle(TVS_EX_DOUBLEBUFFER, TVS_EX_DOUBLEBUFFER);
	RefreshBkgndColor();

	return 0;
}

void CKanbanListCtrl::SetDropTarget(BOOL bTarget)
{
	if (bTarget != m_bDropTarget)
	{
		m_bDropTarget = bTarget;
		RefreshBkgndColor();
	}
}

LRESULT CKanbanListCtrl::OnThemeChanged(WPARAM /*wp*/, LPARAM /*lp*/)
{
	RefreshBkgndColor();
	return 0L;
}

void CKanbanListCtrl::RefreshBkgndColor()
{
	COLORREF crBack = GetSysColor(COLOR_WINDOW);

	if (!Misc::IsHighContrastActive())
	{
		if (m_columnDef.crBackground != CLR_NONE)
			crBack = m_columnDef.crBackground;

		if (m_bDropTarget)
			crBack = GraphicsMisc::Darker(crBack, 0.1);
	}

	TreeView_SetBkColor(*this, crBack);
}

void CKanbanListCtrl::SetBackgroundColor(COLORREF color)
{
	if (color == GetSysColor(COLOR_WINDOW))
		color = CLR_NONE;

	if (m_columnDef.crBackground != color)
	{
		m_columnDef.crBackground = color;
		RefreshBkgndColor();
	}
}

void CKanbanListCtrl::SetExcessColor(COLORREF color)
{
	// TODO
	ASSERT(0);
}

void CKanbanListCtrl::SetMaximumTaskCount(int nMaxTasks)
{
	// TODO
	ASSERT(0);
}

void CKanbanListCtrl::OnDisplayAttributeChanged()
{
	m_bDrawTaskFlags = (Misc::FindT(m_aDisplayAttrib, IUI_FLAG) != -1);

	RecalcItemLineHeight();
	RefreshItemLineHeights();
}

void CKanbanListCtrl::RefreshItemLineHeights()
{
	HTREEITEM hti = GetChildItem(NULL);
	TVITEMEX tvi = { TVIF_INTEGRAL, 0 };

	while (hti)
	{
		RefreshItemLineHeights(hti);
		hti = GetNextSiblingItem(hti);
	}
}

void CKanbanListCtrl::RefreshItemLineHeights(HTREEITEM hti)
{
	ASSERT(hti);

	const KANBANITEM* pKI = GetKanbanItem(GetTaskID(hti));
	ASSERT(pKI);

	if (pKI)
	{
		int nNumLines = (NUM_TITLELINES + GetItemDisplayAttributeCount(*pKI));

		TCH().SetItemIntegral(hti, nNumLines);
	}
}

int CKanbanListCtrl::GetItemDisplayAttributeCount(const KANBANITEM& ki) const
{
	int nCount = 0, nDisp = m_aDisplayAttrib.GetSize();

	while (nDisp--)
	{
		IUI_ATTRIBUTE nAttribID = m_aDisplayAttrib[nDisp];

		if (nAttribID != IUI_FLAG)
		{
			if (!m_bHideEmptyAttributes || ki.HasAttributeDisplayValue(nAttribID))
				nCount++;
		}
	}

	return nCount;
}

void CKanbanListCtrl::RefreshItemLineHeights(DWORD dwTaskID)
{
	HTREEITEM hti = TCH().FindItem(dwTaskID);

	if (hti)
		RefreshItemLineHeights(hti);
}

void CKanbanListCtrl::SetTextColorIsBackground(BOOL bSet)
{
	if (bSet != m_bTextColorIsBkgnd)
	{
		m_bTextColorIsBkgnd = bSet;

		if (GetSafeHwnd())
			Invalidate(FALSE);
	}
}

void CKanbanListCtrl::SetShowTaskColorAsBar(BOOL bSet)
{
	if (bSet != m_bShowTaskColorAsBar)
	{
		m_bShowTaskColorAsBar = bSet;
		
		if (GetSafeHwnd())
			Invalidate(FALSE);
	}
}

void CKanbanListCtrl::SetStrikeThruDoneTasks(BOOL bSet)
{
	if (bSet != m_bStrikeThruDoneTasks)
	{
		m_bStrikeThruDoneTasks = bSet;

		if (GetSafeHwnd())
			Invalidate(FALSE);
	}
}

void CKanbanListCtrl::SetColorTaskBarByPriority(BOOL bSet)
{
	if (bSet && (m_aPriorityColors.GetSize() != 11))
	{
		ASSERT(0);
		return;
	}

	// else
	m_bColorBarByPriority = bSet;

	if (GetSafeHwnd())
		Invalidate(TRUE);
}

void CKanbanListCtrl::SetAttributeLabelVisibility(KBC_ATTRIBLABELS nLabelVis)
{
	m_nAttribLabelVisiability = nLabelVis;

	if (GetSafeHwnd())
		Invalidate(TRUE);
}

void CKanbanListCtrl::SetSelected(BOOL bSelected)
{
	m_bSelected = bSelected;
}

void CKanbanListCtrl::SetShowCompletionCheckboxes(BOOL bShow)
{
	m_bShowCompletionCheckboxes = bShow;

	if (GetSafeHwnd())
		Invalidate(TRUE);
}

void CKanbanListCtrl::SetIndentSubtasks(BOOL bIndent)
{
	m_bIndentSubtasks = bIndent;

	if (GetSafeHwnd())
		Invalidate(TRUE);
}

void CKanbanListCtrl::SetHideEmptyAttributes(BOOL bHide)
{
	if (m_bHideEmptyAttributes != bHide)
	{
		m_bHideEmptyAttributes = bHide;

		if (GetSafeHwnd())
			RefreshItemLineHeights();
	}
}

int CKanbanListCtrl::CalcItemTitleTextHeight() const
{
	return (NUM_TITLELINES * m_nItemTextHeight);
}

HTREEITEM CKanbanListCtrl::AddTask(const KANBANITEM& ki, BOOL bSelect)
{
	HTREEITEM hti = FindTask(ki.dwTaskID);

	if (hti)
	{
		ASSERT(m_columnDef.aAttribValues.GetSize() > 1);
		return hti;
	}

	hti = InsertItem(TVIF_TEXT | TVIF_PARAM, 
					 ki.sTitle,
					 0,
					 0,
					 0,
					 0,
					 ki.dwTaskID,
					 TVI_ROOT,
					 TVI_LAST);
	ASSERT(hti);

	if (hti)
	{
		RefreshItemLineHeights(hti);

		// select item and make visible
		if (bSelect)
		{
			SetItemState(hti, TVIS_SELECTED, TVIS_SELECTED);
			EnsureVisible(hti);
		}
	}

	return hti;
}

CString CKanbanListCtrl::GetAttributeID() const 
{ 
	return m_columnDef.sAttribID; 
}

BOOL CKanbanListCtrl::IsBacklog() const
{
	return ((m_columnDef.aAttribValues.GetSize() == 1) && (m_columnDef.aAttribValues[0].IsEmpty()));
}

int CKanbanListCtrl::GetAttributeValues(CStringArray& aValues) const 
{ 
	aValues.Copy(m_columnDef.aAttribValues); 
	return aValues.GetSize();
}

int CKanbanListCtrl::GetAttributeValueIDs(CStringArray& aValueIDs) const 
{ 
	aValueIDs.Copy(m_columnDef.aAttribValues); 
	Misc::MakeUpper(aValueIDs);

	return aValueIDs.GetSize();
}

CString CKanbanListCtrl::GetAttributeValue() const
{
	return Misc::FormatArray(m_columnDef.aAttribValues);
}

CString CKanbanListCtrl::GetAttributeValueID() const
{
	return Misc::ToUpper(GetAttributeValue());
}

BOOL CKanbanListCtrl::HasMultipleValues() const
{
	return (m_columnDef.aAttribValues.GetSize() > 1);
}

BOOL CKanbanListCtrl::HasAnyValues() const
{
	return (m_columnDef.aAttribValues.GetSize() > 0);
}

int CKanbanListCtrl::CalcAvailableAttributeWidth(int nListWidth) const
{
	if (nListWidth < 0)
	{
		CRect rClient;
		GetClientRect(rClient);
		nListWidth = rClient.Width();
	}
	else
	{
		nListWidth -= GetSystemMetrics(SM_CXVSCROLL);
	}

	int nAvailWidth = (nListWidth - (2 * LV_PADDING));

	if (m_bShowTaskColorAsBar)
		nAvailWidth -= BAR_WIDTH;

	nAvailWidth -= (TEXT_BORDER.left + TEXT_BORDER.right);
	nAvailWidth -= ATTRIB_INDENT;

	return nAvailWidth;
}

void CKanbanListCtrl::OnSize(UINT nType, int cx, int cy)
{
	CTreeCtrl::OnSize(nType, cx, cy);

// 	if (!m_bSavingToImage && m_header.GetSafeHwnd() && m_header.GetCount())
// 	{
// 		if (GetStyle() & WS_VSCROLL)
// 			SetColumnWidth(0, (cx - 1));
// 		else
// 			SetColumnWidth(0, (cx - 1 - GetSystemMetrics(SM_CXVSCROLL)));
// 	}
}

void CKanbanListCtrl::FillItemBackground(CDC* pDC, const KANBANITEM* pKI, const CRect& rItem, COLORREF crText, BOOL bSelected) const
{
	if (bSelected)
	{
		BOOL bFocused = (bSelected && (::GetFocus() == GetSafeHwnd()));

		GM_ITEMSTATE nState = (bFocused ? GMIS_SELECTED : GMIS_SELECTEDNOTFOCUSED);
		crText = GraphicsMisc::GetExplorerItemTextColor(crText, nState, GMIB_THEMECLASSIC);

		GraphicsMisc::DrawExplorerItemBkgnd(pDC, GetSafeHwnd(), nState, rItem, GMIB_THEMECLASSIC);
	}
	else if (m_bShowTaskColorAsBar)
	{
		COLORREF crFill = GetSysColor(COLOR_WINDOW);
		COLORREF crBorder = GetSysColor(COLOR_WINDOWFRAME);

		GraphicsMisc::DrawRect(pDC, rItem, crFill, crBorder);
	}
	else // use task's own colour
	{
		COLORREF crFill = pKI->GetFillColor(m_bTextColorIsBkgnd);
		COLORREF crBorder = pKI->GetBorderColor(m_bTextColorIsBkgnd);

		GraphicsMisc::DrawRect(pDC, rItem, crFill, crBorder);
	}
}

void CKanbanListCtrl::OnListCustomDraw(NMHDR* pNMHDR, LRESULT* pResult) 
{
	NMTVCUSTOMDRAW* pTVCD = (NMTVCUSTOMDRAW*)pNMHDR;
	*pResult = CDRF_DODEFAULT;
	
	switch (pTVCD->nmcd.dwDrawStage)
	{
	case CDDS_PREPAINT:
		*pResult = CDRF_NOTIFYITEMDRAW;
		break;
		
	case CDDS_ITEMPREPAINT:
		{
			const KANBANITEM* pKI = GetKanbanItem(pTVCD->nmcd.lItemlParam);
			
			if (pKI)
			{
				HTREEITEM hti = (HTREEITEM)pTVCD->nmcd.dwItemSpec;
				CDC* pDC = CDC::FromHandle(pTVCD->nmcd.hdc);
		
				CRect rItem;
				GetItemRect(hti, rItem, NULL);
				rItem.DeflateRect(1, 1);

				// Checkbox
				DrawItemCheckbox(pDC, pKI, rItem);
				
				BOOL bSelected = (!m_bSavingToImage && (GetItemState(hti, TVIS_SELECTED) == TVIS_SELECTED));
				COLORREF crText = pKI->GetTextColor(bSelected, (m_bTextColorIsBkgnd && !m_bShowTaskColorAsBar));

				// Background
				FillItemBackground(pDC, pKI, rItem, crText, bSelected);
	
				// Bar affects everything else
				DrawItemBar(pDC, pKI, rItem);

				// Icons don't affect attributes
				CRect rAttributes(rItem);

				DrawItemIcons(pDC, pKI, rItem);
				DrawItemTitle(pDC, pKI, rItem, crText);

				if (!bSelected && !Misc::IsHighContrastActive() && !pKI->IsDone(TRUE))
					crText = pDC->SetTextColor(GraphicsMisc::Lighter(crText, 0.3));

				rAttributes.top += CalcItemTitleTextHeight();
				rAttributes.top += IMAGE_PADDING;

				DrawItemAttributes(pDC, pKI, rAttributes, crText);
			}
			
			*pResult |= CDRF_SKIPDEFAULT;
		}
	}
}

void CKanbanListCtrl::DrawItemTitle(CDC* pDC, const KANBANITEM* pKI, const CRect& rItem, COLORREF crText)
{
	CRect rTitle(rItem);

	pDC->SetBkMode(TRANSPARENT);
	pDC->SetTextColor(crText);

	CFont* pOldFont = NULL;
	DWORD dwFontFlags = 0;

	if (m_bStrikeThruDoneTasks && pKI->IsDone(FALSE))
		dwFontFlags |= GMFS_STRIKETHRU;

	if (pKI->dwParentID == 0)
		dwFontFlags |= GMFS_BOLD;

	if (dwFontFlags)
		pOldFont = pDC->SelectObject(m_fonts.GetFont(dwFontFlags));

	// first 'n' lines is the task title
	rTitle.DeflateRect(TEXT_BORDER);
	rTitle.bottom = (rTitle.top + CalcItemTitleTextHeight());
	rTitle.bottom = min(rTitle.bottom, rItem.bottom);

	int nFlags = (DT_LEFT | DT_END_ELLIPSIS | DT_NOPREFIX | DT_WORDBREAK); // multi-line for titles only

	pDC->DrawText(pKI->sTitle, rTitle, nFlags);

	if (pOldFont)
		pDC->SelectObject(pOldFont);
}

void CKanbanListCtrl::DrawItemAttributes(CDC* pDC, const KANBANITEM* pKI, const CRect& rItem, COLORREF crText)
{
	pDC->SetBkMode(TRANSPARENT);

	CFont* pOldFont = NULL;
	DWORD dwFontFlags = 0;

	if (m_bStrikeThruDoneTasks && pKI->IsDone(FALSE))
		pOldFont = pDC->SelectObject(m_fonts.GetFont(GMFS_STRIKETHRU));

	CRect rAttrib(rItem);
	rAttrib.DeflateRect(TEXT_BORDER);

	int nFlags = (DT_LEFT | DT_END_ELLIPSIS | DT_NOPREFIX);

	for (int nDisp = 0; nDisp < m_aDisplayAttrib.GetSize(); nDisp++)
	{
		IUI_ATTRIBUTE nAttrib = m_aDisplayAttrib[nDisp];

		if (nAttrib != IUI_FLAG)
		{
			if (!m_bHideEmptyAttributes || pKI->HasAttributeDisplayValue(nAttrib))
				DrawAttribute(pDC, rAttrib, nAttrib, pKI->GetAttributeDisplayValue(nAttrib), nFlags);
		}
	}

	if (pOldFont)
		pDC->SelectObject(pOldFont);
}

void CKanbanListCtrl::DrawItemIcons(CDC* pDC, const KANBANITEM* pKI, CRect& rItem) const
{
	DWORD dwDrawn = 0;
	CRect rIcon(rItem);

	rIcon.left += IMAGE_PADDING;
	rIcon.top += IMAGE_PADDING;

	if (pKI->bHasIcon || pKI->bParent)
	{
		int iImageIndex = -1;
		HIMAGELIST hilTask = (HIMAGELIST)GetParent()->SendMessage(WM_KLCN_GETTASKICON, pKI->dwTaskID, (LPARAM)&iImageIndex);

		if (hilTask && (iImageIndex != -1))
			ImageList_Draw(hilTask, iImageIndex, *pDC, rIcon.left, rIcon.top, ILD_TRANSPARENT);
	}

	rIcon.top += (IMAGE_SIZE/* + IMAGE_PADDING*/);

	if (m_bDrawTaskFlags && pKI->bFlag)
		ImageList_Draw(m_ilFlags, 0, *pDC, rIcon.left, rIcon.top, ILD_TRANSPARENT);

	rItem.left = (rIcon.left + IMAGE_SIZE);
}

void CKanbanListCtrl::DrawItemBar(CDC* pDC, const KANBANITEM* pKI, CRect& rItem) const
{
	if (m_bShowTaskColorAsBar)
	{
		// Don't draw for completed items but ensure same indentation
		CRect rBar(rItem);

		rBar.DeflateRect(2, 2);
		rBar.right = (rBar.left + BAR_WIDTH);

		if (!pKI->IsDone(TRUE))
		{
			if (m_bColorBarByPriority)
			{
				int nPriority = pKI->GetPriority();

				if (nPriority != -2)
				{
					COLORREF crFill = m_aPriorityColors[nPriority];
					COLORREF crBorder = GraphicsMisc::Darker(crFill, 0.4);

					GraphicsMisc::DrawRect(pDC, rBar, crFill, crBorder);
				}
			}
			else if (pKI->HasColor())
			{
				COLORREF crFill = pKI->GetFillColor(TRUE);
				COLORREF crBorder = pKI->GetBorderColor(TRUE);

				GraphicsMisc::DrawRect(pDC, rBar, crFill, crBorder);
			}
		}

		rItem.left = rBar.right;
	}
}

void CKanbanListCtrl::DrawItemCheckbox(CDC* pDC, const KANBANITEM* pKI, CRect& rItem)
{
	if (m_bShowCompletionCheckboxes)
	{
		CRect rCheckbox(rItem);

		if (GetItemCheckboxRect(rCheckbox))
		{
			int iImage = KLCC_UNCHECKED;
		
			if (pKI->IsDone(FALSE))
			{
				iImage = KLCC_CHECKED;
			}
			else if (pKI->bSomeSubtaskDone)
			{
				iImage = KLCC_MIXED;
			}

			m_ilCheckboxes.Draw(pDC, iImage, rCheckbox.TopLeft(), ILD_TRANSPARENT);

			rItem.left = (rCheckbox.right + CHECKBOX_PADDING);
		}
	}
}

BOOL CKanbanListCtrl::GetItemCheckboxRect(HTREEITEM hti, CRect& rItem, const KANBANITEM* pKI) const
{
	if (m_bShowCompletionCheckboxes)
	{
		GetItemRect(hti, rItem, pKI);

		return GetItemCheckboxRect(rItem);
	}

	// else
	return FALSE;
}

BOOL CKanbanListCtrl::GetItemBounds(HTREEITEM hti, LPRECT lpRect) const
{
	return CTreeCtrl::GetItemRect(hti, lpRect, FALSE);
}

BOOL CKanbanListCtrl::GetItemRect(HTREEITEM hti, CRect& rItem, const KANBANITEM* pKI) const
{
	if (!GetItemBounds(hti, rItem))
		return FALSE;

	if (m_bIndentSubtasks)
	{
		if (!pKI)
			pKI = GetKanbanItem(GetTaskID(hti));

		ASSERT(pKI);

		// Indent to match level
		rItem.left += (pKI->nLevel * LEVEL_INDENT);
	}

	return TRUE;
}

BOOL CKanbanListCtrl::GetItemCheckboxRect(CRect& rItem) const
{
	if (m_bShowCompletionCheckboxes)
	{
		rItem.bottom = (rItem.top + GetItemHeight());

		rItem.DeflateRect(0, ((rItem.Height() - CEnImageList::GetImageSize(m_ilCheckboxes)) / 2));
		rItem.right = (rItem.left + rItem.Height());

		return TRUE;
	}

	// else
	return FALSE;
}

BOOL CKanbanListCtrl::GetItemLabelTextRect(HTREEITEM hti, CRect& rItem, BOOL bEdit, const KANBANITEM* pKI) const
{
	if (!GetItemRect(hti, rItem, pKI))
		return FALSE;

	rItem.DeflateRect(1, 1);

	CRect rCheckbox(rItem);
	VERIFY(GetItemCheckboxRect(rCheckbox));

	rItem.left = (rCheckbox.right + CHECKBOX_PADDING);
	rItem.left += (IMAGE_SIZE + IMAGE_PADDING);

	if (m_bShowTaskColorAsBar)
		rItem.left += (BAR_WIDTH + IMAGE_PADDING);

	if (!bEdit)
	{
		rItem.DeflateRect(TEXT_BORDER);
		rItem.bottom = (rItem.top + CalcItemTitleTextHeight());
		rItem.right -= 2;
	}
	else
	{
		// make width of list column or 200 whichever is larger
		int nWidth = max(rItem.Width(), MIN_LABEL_EDIT_WIDTH);
			
		rItem.right = rItem.left + nWidth;
		rItem.bottom = (rItem.top + m_nItemTextHeight + m_nItemTextBorder + 2);
	}

	return TRUE;
}

BOOL CKanbanListCtrl::GetItemTooltipRect(HTREEITEM hti, CRect& rTip, const KANBANITEM* pKI) const
{
	if (!pKI)
	{
		pKI = GetKanbanItem(GetTaskID(hti));

		if (!pKI)
		{
			ASSERT(0);
			return FALSE;
		}
	}

	if (!GetItemLabelTextRect(hti, rTip, FALSE, pKI))
	{
		ASSERT(0);
		return FALSE;
	}

	HFONT hFont = m_fonts.GetHFont((pKI->dwParentID == 0) ? GMFS_BOLD : 0);

	int nWidth = rTip.Width();
	int nAvailHeight = CalcItemTitleTextHeight();
	int nTextHeight = GraphicsMisc::GetTextHeight(pKI->sTitle, GetSafeHwnd(), nWidth, hFont);

	if (nTextHeight <= nAvailHeight)
		return FALSE;

	while (nTextHeight > nAvailHeight)
	{
		rTip.right += (nWidth / 2);
		nTextHeight = GraphicsMisc::GetTextHeight(pKI->sTitle, GetSafeHwnd(), rTip.Width(), hFont);
	}

	m_tooltip.AdjustRect(rTip, TRUE);
	ClientToScreen(&rTip);

	return TRUE;
}

UINT CKanbanListCtrl::GetDisplayFormat(IUI_ATTRIBUTE nAttrib, BOOL bLong)
{
	switch (nAttrib)
	{
	case IUI_ALLOCBY:		return (bLong ? IDS_DISPLAY_ALLOCBY : IDS_DISPLAY_ALLOCBY_SHORT);
	case IUI_ALLOCTO:		return (bLong ? IDS_DISPLAY_ALLOCTO : IDS_DISPLAY_ALLOCTO_SHORT);
	case IUI_CATEGORY:		return (bLong ? IDS_DISPLAY_CATEGORY : IDS_DISPLAY_CATEGORY_SHORT);
	case IUI_COST:			return (bLong ? IDS_DISPLAY_COST : IDS_DISPLAY_COST_SHORT);
	case IUI_CREATEDBY:		return (bLong ? IDS_DISPLAY_CREATEDBY : IDS_DISPLAY_CREATEDBY_SHORT);
	case IUI_CREATIONDATE:	return (bLong ? IDS_DISPLAY_CREATEDATE : IDS_DISPLAY_CREATEDATE_SHORT);
	case IUI_DONEDATE:		return (bLong ? IDS_DISPLAY_DONEDATE : IDS_DISPLAY_DONEDATE_SHORT);
	case IUI_DUEDATE:		return (bLong ? IDS_DISPLAY_DUEDATE : IDS_DISPLAY_DUEDATE_SHORT);
	case IUI_EXTERNALID:	return (bLong ? IDS_DISPLAY_EXTERNALID : IDS_DISPLAY_EXTERNALID_SHORT);
	case IUI_FLAG:			return (bLong ? IDS_DISPLAY_FLAG : IDS_DISPLAY_FLAG_SHORT);
	case IUI_FILEREF:		return (bLong ? IDS_DISPLAY_FILEREF : IDS_DISPLAY_FILEREF_SHORT);
	case IUI_ID:			return (bLong ? IDS_DISPLAY_TASKID : IDS_DISPLAY_TASKID_SHORT);
	case IUI_LASTMOD:		return (bLong ? IDS_DISPLAY_LASTMOD : IDS_DISPLAY_LASTMOD_SHORT);
	case IUI_PARENT:		return (bLong ? IDS_DISPLAY_PARENT : IDS_DISPLAY_PARENT_SHORT);
	case IUI_PERCENT:		return (bLong ? IDS_DISPLAY_PERCENT : IDS_DISPLAY_PERCENT_SHORT);
	case IUI_PRIORITY:		return (bLong ? IDS_DISPLAY_PRIORITY : IDS_DISPLAY_PRIORITY_SHORT);
	case IUI_RECURRENCE:	return (bLong ? IDS_DISPLAY_RECURRENCE : IDS_DISPLAY_RECURRENCE_SHORT);
	case IUI_RISK:			return (bLong ? IDS_DISPLAY_RISK : IDS_DISPLAY_RISK_SHORT);
	case IUI_STARTDATE:		return (bLong ? IDS_DISPLAY_STARTDATE : IDS_DISPLAY_STARTDATE_SHORT);
	case IUI_STATUS:		return (bLong ? IDS_DISPLAY_STATUS : IDS_DISPLAY_STATUS_SHORT);
	case IUI_TAGS:			return (bLong ? IDS_DISPLAY_TAGS : IDS_DISPLAY_TAGS_SHORT);
	case IUI_TIMEEST:		return (bLong ? IDS_DISPLAY_TIMEEST : IDS_DISPLAY_TIMEEST_SHORT);
	case IUI_TIMESPENT:		return (bLong ? IDS_DISPLAY_TIMESPENT : IDS_DISPLAY_TIMESPENT_SHORT);
	case IUI_VERSION:		return (bLong ? IDS_DISPLAY_VERSION : IDS_DISPLAY_VERSION_SHORT);
	}

	ASSERT(0);
	return 0;
}

void CKanbanListCtrl::DrawAttribute(CDC* pDC, CRect& rLine, IUI_ATTRIBUTE nAttrib, const CString& sValue, int nFlags) const
{
	KBC_ATTRIBLABELS nLabelVis = m_nAttribLabelVisiability;
	
	if (m_bSavingToImage)
		nLabelVis = KBCAL_LONG;

	CString sAttrib = FormatAttribute(nAttrib, sValue, nLabelVis);
	pDC->DrawText(sAttrib, rLine, nFlags);

	rLine.top += (m_nItemTextHeight + m_nItemTextBorder);
}

CString CKanbanListCtrl::FormatAttribute(IUI_ATTRIBUTE nAttrib, const CString& sValue, KBC_ATTRIBLABELS nLabelVis)
{
	UINT nFormatID = 0;
	
	if (nLabelVis != KBCAL_NONE)
		nFormatID = GetDisplayFormat(nAttrib, (nLabelVis == KBCAL_LONG));

	if (nFormatID == 0)
		return sValue;

	CEnString sAttrib(nFormatID);

	sAttrib += _T(": ");
	sAttrib += sValue;

	return sAttrib;
}

BOOL CKanbanListCtrl::GetLabelEditRect(LPRECT pEdit)
{
	if (!m_bSelected || !GetCount() || !GetSelectedCount())
	{
		ASSERT(0);
		return FALSE;
	}

	ASSERT(GetSelectedCount() == 1);

	HTREEITEM hti = GetSelectedItem();
	ASSERT(hti);

	// scroll into view first
	EnsureVisible(hti);

	CRect rItem;

	if (GetItemLabelTextRect(hti, rItem, TRUE))
	{
		*pEdit = rItem;
		return TRUE;
	}

	return FALSE;
}

DWORD CKanbanListCtrl::GetSelectedTaskID() const
{
	HTREEITEM hti = GetSelectedItem();

	return (hti ? GetTaskID(hti) : 0);
}

void CKanbanListCtrl::ScrollToSelection()
{
	TCH().EnsureVisibleEx(GetSelectedItem(), FALSE);
}

void CKanbanListCtrl::ClearSelection()
{
	CTreeCtrl::SelectItem(NULL);
}

BOOL CKanbanListCtrl::SelectTask(DWORD dwTaskID)
{
	return CTreeCtrl::SelectItem(FindTask(dwTaskID));
}

const KANBANITEM* CKanbanListCtrl::GetKanbanItem(DWORD dwTaskID) const
{
	ASSERT(dwTaskID);
	
	KANBANITEM* pKI = NULL;
	
	if (dwTaskID && m_data.Lookup(dwTaskID, pKI))
	{
		ASSERT(pKI);
	}
	
	return pKI;
}

HTREEITEM CKanbanListCtrl::FindTask(DWORD dwTaskID) const
{
	return m_tch.FindItem(dwTaskID);
}

HTREEITEM CKanbanListCtrl::FindTask(const CPoint& ptScreen) const
{
	CPoint ptClient(ptScreen);
	ScreenToClient(&ptClient);
	
	return CTreeCtrl::HitTest(ptClient);
}

HTREEITEM CKanbanListCtrl::FindTask(const IUISELECTTASK& select, BOOL bNext, HTREEITEM htiStart) const
{
	HTREEITEM htiNext = NULL;

	if (htiStart == NULL)
		htiNext = (bNext ? m_tch.GetFirstItem() : m_tch.GetLastItem());
	else
		htiNext = GetNextItem(htiStart, (bNext ? TVGN_NEXT : TVGN_PREVIOUS));

	while (htiNext)
	{
		DWORD dwTaskID = GetTaskID(htiNext);
		const KANBANITEM* pKI = GetKanbanItem(dwTaskID);

		if (pKI->MatchesAttribute(select))
		{
			return htiNext;
		}

		htiNext = GetNextItem(htiNext, (bNext ? TVGN_NEXT : TVGN_PREVIOUS));
	}

	return NULL; // no match
}

BOOL CKanbanListCtrl::DeleteTask(DWORD dwTaskID)
{
	HTREEITEM hti = FindTask(dwTaskID);

	if (hti && DeleteItem(hti))
	{
		//RefreshColumnTitle();
		return TRUE;
	}

	return FALSE;
}

int CALLBACK CKanbanListCtrl::SortProc(LPARAM lParam1, LPARAM lParam2, LPARAM lParamSort)
{
	const KANBANSORT* pSort = (KANBANSORT*)lParamSort;
	
	const KANBANITEM* pKI1 = pSort->data.GetItem(lParam1);
	const KANBANITEM* pKI2 = pSort->data.GetItem(lParam2);

	int nCompare = 0;
	
	if (pKI1 && pKI2)
	{
		if (pSort->bSubtasksBelowParent && (pKI1->dwParentID != pKI2->dwParentID))
		{
			// If one is the parent of another always sort below
			if (pSort->IsParent(lParam2, pKI1))
				return 1;

			if (pSort->IsParent(lParam1, pKI2))
				return -1;

			// We can't sort items that are not in the same 
			// branch of the tree ie. they need to have the same parent

			// First we raise the items to the same level
			while (pKI1->nLevel > pKI2->nLevel)
				pKI1 = pSort->GetParent(pKI1);

			while (pKI2->nLevel > pKI1->nLevel)
				pKI2 = pSort->GetParent(pKI2);

			// Then we raise them to have the same parent
			while (pKI1->dwParentID != pKI2->dwParentID)
			{
				pKI1 = pSort->GetParent(pKI1);
				pKI2 = pSort->GetParent(pKI2);
			}
		}
	
		switch (pSort->nBy)
		{
		case IUI_TASKNAME:
			nCompare = Misc::NaturalCompare(pKI1->sTitle, pKI2->sTitle);
			break;
			
		case IUI_ALLOCBY:
		case IUI_ALLOCTO:
		case IUI_CATEGORY:
		case IUI_PRIORITY:
		case IUI_RISK:
		case IUI_STATUS:
		case IUI_TAGS:
		case IUI_VERSION:
		{
				ASSERT(!pSort->sAttribID.IsEmpty());

				CString sValue1 = pKI1->GetAttributeDisplayValue(pSort->nBy);
				CString sValue2 = pKI2->GetAttributeDisplayValue(pSort->nBy);

				nCompare = Misc::NaturalCompare(sValue1, sValue2);
			}
			break;

		case IUI_CUSTOMATTRIB:
			// TODO
			break;

		// Other display attributes
		case IUI_COST:
			nCompare = ((pKI1->dCost > pKI2->dCost) ? 1 : -1);
			break;
			
		case IUI_CREATIONDATE:
			nCompare = CDateHelper::Compare(pKI1->dtCreate, pKI2->dtCreate, DHC_COMPARETIME);
			break;
			
		case IUI_CREATEDBY:
			nCompare = Misc::NaturalCompare(pKI1->sCreatedBy, pKI2->sCreatedBy);
			break;
			
		case IUI_DONEDATE:
			nCompare = CDateHelper::Compare(pKI1->dtDone, pKI2->dtDone, (DHC_COMPARETIME | DHC_NOTIMEISENDOFDAY));
			break;
			
		case IUI_DUEDATE:
			nCompare = CDateHelper::Compare(pKI1->dtDue, pKI2->dtDue, (DHC_COMPARETIME | DHC_NOTIMEISENDOFDAY));
			break;
			
		case IUI_EXTERNALID:
			nCompare = Misc::NaturalCompare(pKI1->sExternalID, pKI2->sExternalID);
			break;
			
		case IUI_FLAG:
			if (pKI1->bFlag && pKI2->bFlag)
			{
				nCompare = 0;
			}
			else if (pKI1->bFlag)
			{
				nCompare = -1;
			}
			else if (pKI2->bFlag)
			{
				nCompare = 1;
			}
			break;
			
		case IUI_LASTMOD:
			nCompare = CDateHelper::Compare(pKI1->dtLastMod, pKI2->dtLastMod, DHC_COMPARETIME);
			break;
			
		case IUI_PERCENT:
			nCompare = ((pKI1->nPercent > pKI2->nPercent) ? 1 : -1);
			break;
			
		case IUI_RECURRENCE:
			nCompare = Misc::NaturalCompare(pKI1->sRecurrence, pKI2->sRecurrence);
			break;
			
		case IUI_STARTDATE:
			nCompare = CDateHelper::Compare(pKI1->dtStart, pKI2->dtStart, DHC_COMPARETIME);
			break;
			
		case IUI_TIMEEST:
			nCompare = CTimeHelper().Compare(pKI1->dTimeEst, MapUnitsToTHUnits(pKI1->nTimeEstUnits), 
											pKI2->dTimeEst, MapUnitsToTHUnits(pKI2->nTimeEstUnits));
			break;
			
		case IUI_TIMESPENT:
			nCompare = CTimeHelper().Compare(pKI1->dTimeSpent, MapUnitsToTHUnits(pKI1->nTimeSpentUnits), 
											pKI2->dTimeSpent, MapUnitsToTHUnits(pKI2->nTimeSpentUnits));
			break;

		case IUI_NONE: // Synonymous with IUI_POSITION
			ASSERT(pSort->bSubtasksBelowParent);

			// Avoid reversal of sign below
			return ((pKI1->nPosition > pKI2->nPosition) ? 1 : -1);
		}
	}
	
	return (pSort->bAscending ? nCompare : -nCompare);
}

void CKanbanListCtrl::Sort(IUI_ATTRIBUTE nBy, BOOL bAscending, BOOL bSubtasksBelowParent)
{
	KANBANSORT ks(m_data);
	
	ks.nBy = nBy;
	ks.bAscending = bAscending;
	ks.bSubtasksBelowParent = bSubtasksBelowParent;

	switch (nBy)
	{
	case IUI_STATUS:
	case IUI_ALLOCTO:
	case IUI_CATEGORY:
	case IUI_ALLOCBY:
	case IUI_TAGS:
	case IUI_RISK:
	case IUI_PRIORITY:
	case IUI_VERSION:
		ks.sAttribID = KANBANITEM::GetAttributeID(nBy);
		break;
	}

	TVSORTCB tvs = { NULL, SortProc, (DWORD)&ks };

	SortChildrenCB(&tvs);
}

void CKanbanListCtrl::OnLButtonDown(UINT nFlags, CPoint point)
{
	if (HandleLButtonClick(point, FALSE))
		return;

	HTREEITEM hti = HitTest(point);

	if (hti)
	{
		// Adjust 'point' to compensate for the indent
		CRect rTreeItem, rIndentedItem;

		CTreeCtrl::GetItemRect(hti, rTreeItem, FALSE);
		GetItemLabelTextRect(hti, rIndentedItem, FALSE);

		if (rTreeItem.left != rIndentedItem.left)
		{
			point.x -= (rIndentedItem.left - rTreeItem.left);
			DefWindowProc(WM_LBUTTONDOWN, nFlags, MAKELPARAM(point.x, point.y));
			return;
		}
	}
	
	//else
	CTreeCtrl::OnLButtonDown(nFlags, point);
}

void CKanbanListCtrl::OnLButtonDblClk(UINT nFlags, CPoint point)
{
	if (HandleLButtonClick(point, TRUE))
		return;

	// else
	CTreeCtrl::OnLButtonDblClk(nFlags, point);
}

BOOL CKanbanListCtrl::HandleLButtonClick(CPoint point, BOOL bDblClk)
{
	m_tooltip.Pop();

	// Ignore clicks not hitting an item
	HTREEITEM hti = HitTest(point);

	if (!hti)
		return TRUE;

	if (!m_bSelected)
	{
		CTreeCtrl::Default();

		if (bDblClk)
			return TRUE;
	}

	if (!bDblClk)
	{
		UINT nMsgID = 0;

		if (HitTestCheckbox(hti, point))
		{
			nMsgID = WM_KLCN_TOGGLETASKDONE;
		}
		else if (HitTestIcon(hti, point))
		{
			nMsgID = WM_KLCN_EDITTASKICON;
		}
		else if (HitTestFlag(hti, point))
		{
			nMsgID = WM_KLCN_TOGGLETASKFLAG;
		}

		if (nMsgID)
		{
			if (hti != GetSelectedItem())
				SelectItem(hti, TRUE);

			// Post message to let mouse-click time to complete
			GetParent()->PostMessage(nMsgID, (WPARAM)GetSafeHwnd(), GetTaskID(hti));
			return TRUE;
		}
	}

	// all else
	return FALSE;
}

BOOL CKanbanListCtrl::HitTestIcon(HTREEITEM hti, CPoint point) const
{
	CRect rIcon;
	GetItemLabelTextRect(hti, rIcon);

	rIcon.right = (rIcon.left - IMAGE_PADDING);
	rIcon.left = (rIcon.right - IMAGE_SIZE);
	rIcon.bottom = (rIcon.top + IMAGE_SIZE);

	return rIcon.PtInRect(point);
}

BOOL CKanbanListCtrl::HitTestFlag(HTREEITEM hti, CPoint point) const
{
	if (!m_bDrawTaskFlags)
		return FALSE;

	CRect rFlag;
	GetItemLabelTextRect(hti, rFlag);

	rFlag.right = (rFlag.left - IMAGE_PADDING);
	rFlag.left = (rFlag.right - IMAGE_SIZE);
	rFlag.top += (IMAGE_SIZE + IMAGE_PADDING);
	rFlag.bottom = (rFlag.top + IMAGE_SIZE);

	return rFlag.PtInRect(point);
}

BOOL CKanbanListCtrl::OnSetCursor(CWnd* pWnd, UINT nHitTest, UINT message)
{
	CPoint point(::GetMessagePos());
	ScreenToClient(&point);

	HTREEITEM hti = HitTest(point);

	if (hti && (HitTestIcon(hti, point) || HitTestFlag(hti, point)))
		return GraphicsMisc::SetHandCursor();

	// else
	return CTreeCtrl::OnSetCursor(pWnd, nHitTest, message);
}

BOOL CKanbanListCtrl::HitTestCheckbox(HTREEITEM hti, CPoint point) const
{
	CRect rCheckbox;

	return (GetItemCheckboxRect(hti, rCheckbox, NULL) && rCheckbox.PtInRect(point));
}

BOOL CKanbanListCtrl::SaveToImage(CBitmap& bmImage, const CSize& reqColSize)
{
	CAutoFlag af(m_bSavingToImage, TRUE);

	// resize our window to remove any vertical scrollbar
	CRect rPrev = CDialogHelper::GetChildRect(this);
	CRect rTemp(rPrev.TopLeft(), reqColSize);
	
	MoveWindow(rTemp);

	// Do the copy
	BOOL bRes = CCopyWndContents(*this).DoCopy(bmImage);

	// Restore size
	MoveWindow(rPrev);

	return bRes;
}

CSize CKanbanListCtrl::CalcRequiredSizeForImage() const
{
	CClientDC dc(const_cast<CKanbanListCtrl*>(this));
	float fAveCharWidth = GraphicsMisc::GetAverageCharWidth(&dc);

	int nMinHeaderWidth = ((int)(m_columnDef.sTitle.GetLength() * fAveCharWidth) + (2 * LV_PADDING));
	CSize reqSize(nMinHeaderWidth, 0);
	
	HTREEITEM hti = GetChildItem(NULL);
	
	while (hti)
	{
		DWORD dwTaskID = GetTaskID(hti);
		const KANBANITEM* pKI = GetKanbanItem(dwTaskID);
		ASSERT(pKI);

		if (pKI)
		{
			int nItemWidth = (int)(pKI->sTitle.GetLength() * fAveCharWidth / 2); // title is on two lines
			nItemWidth += (pKI->nLevel * LEVEL_INDENT);
			nItemWidth += (IMAGE_SIZE + IMAGE_PADDING);
			
			for (int nDisp = 0; nDisp < m_aDisplayAttrib.GetSize(); nDisp++)
			{
				IUI_ATTRIBUTE nAttrib = m_aDisplayAttrib[nDisp];

				if (nAttrib != IUI_FLAG)
				{
					CString sAttrib = FormatAttribute(nAttrib, pKI->GetAttributeDisplayValue(nAttrib), KBCAL_LONG);
					nItemWidth = max(nItemWidth, ((int)(sAttrib.GetLength() * fAveCharWidth) + ATTRIB_INDENT));
				}
			}

			reqSize.cx = max(reqSize.cx, nItemWidth);
		}

		hti = GetNextSiblingItem(hti);
	}

	CRect rFirst, rLast;

	if (GetItemRect(TCH().GetFirstItem(), rFirst, FALSE) &&
		GetItemRect(TCH().GetLastVisibleItem(), rLast, FALSE))
	{
		reqSize.cy = (rLast.bottom - rFirst.top);
	}
		
	return reqSize;
}

BOOL CKanbanListCtrl::SelectionHasLockedTasks() const
{
	HTREEITEM hti = GetSelectedItem();

	if (hti)
	{
		DWORD dwTaskID = GetTaskID(hti);

		if (m_data.IsLocked(dwTaskID))
			return TRUE;
	}

	return FALSE;
}

LRESULT CKanbanListCtrl::OnSetFont(WPARAM wp, LPARAM lp)
{
	LRESULT lr = Default();

	m_nItemTextHeight = -1;
	OnDisplayAttributeChanged();

	return lr;
}

void CKanbanListCtrl::RecalcItemLineHeight()
{
	if (m_nItemTextHeight == -1)
	{
		CDC* pDC = GetDC();
		HGDIOBJ hPrev = pDC->SelectObject(m_fonts.GetHFont());

		TEXTMETRIC metrics = { 0 };
		pDC->GetTextMetrics(&metrics);

		ReleaseDC(pDC);

		m_nItemTextHeight = (metrics.tmHeight + metrics.tmExternalLeading);
	}

	if (m_nItemTextBorder == -1)
		m_nItemTextBorder = (GetItemHeight() - m_nItemTextHeight);

	int nItemHeight = (m_nItemTextHeight + m_nItemTextBorder);
		
	if (m_bDrawTaskFlags && (m_aDisplayAttrib.GetSize() == 1))
		nItemHeight = max(nItemHeight, (IMAGE_SIZE + IMAGE_PADDING));
	
	SetItemHeight(nItemHeight);
}

BOOL CKanbanListCtrl::SelectItem(HTREEITEM hItem, BOOL bByMouse)
{
	if (!CTreeCtrl::SelectItem(hItem))
		return FALSE;

	// We must synthesize our own notification because the default
	// one will be ignored because its action is TVC_UNKNOWN
	NMTREEVIEW nmtv = { 0 };

	nmtv.hdr.hwndFrom = GetSafeHwnd();
	nmtv.hdr.idFrom = GetDlgCtrlID();
	nmtv.hdr.code = TVN_SELCHANGED;

	nmtv.itemNew.hItem = hItem;
	nmtv.itemNew.state = TVIS_SELECTED;
	nmtv.itemNew.mask = TVIF_STATE;
	nmtv.itemNew.lParam = GetTaskID(hItem);

	nmtv.action = (bByMouse ? TVC_BYMOUSE : TVC_BYKEYBOARD);

	GetParent()->SendMessage(WM_NOTIFY, nmtv.hdr.idFrom, (LPARAM)&nmtv);

	return TRUE;
}

BOOL CKanbanListCtrl::InitTooltip()
{
	if (!m_tooltip.GetSafeHwnd())
	{
		if (!m_tooltip.Create(this))
			return FALSE;

		m_tooltip.ModifyStyleEx(0, WS_EX_TRANSPARENT);
		m_tooltip.SetDelayTime(TTDT_INITIAL, 50);
		m_tooltip.SetDelayTime(TTDT_AUTOPOP, 10000);
		m_tooltip.SetMaxTipWidth((UINT)(WORD)-1);

		HWND hwndTooltips = (HWND)SendMessage(LVM_GETTOOLTIPS);
		::SendMessage(hwndTooltips, TTM_ACTIVATE, FALSE, 0);
	}

	return TRUE;
}

LRESULT CKanbanListCtrl::OnToolHitTest(WPARAM wp, LPARAM lp)
{
	CPoint point(wp);
	TOOLINFO* pTI = (TOOLINFO*)lp;
	
	HTREEITEM hti = HitTest(point);

	if (hti)
	{
		CRect rTip;

		if (GetItemTooltipRect(hti, rTip, NULL))
		{
			pTI->hwnd = GetSafeHwnd();
			pTI->uId = GetTaskID(hti);
			pTI->uFlags |= TTF_TRANSPARENT;
			pTI->lpszText = _tcsdup(GetItemText(hti)); // MFC will free the duplicated string

			GetItemRect(hti, rTip, NULL);
			pTI->rect = rTip;
			
			return pTI->uId;
		}
	}

	// else
	return -1;
}

void CKanbanListCtrl::OnTooltipShow(NMHDR* /*pNMHDR*/, LRESULT* pResult)
{
	DWORD dwTaskID = m_tooltip.GetLastHitToolInfo().uId;
	ASSERT(dwTaskID);

	HTREEITEM hti = FindTask(dwTaskID);
	ASSERT(hti);

	const KANBANITEM* pKI = m_data.GetItem(dwTaskID);
	ASSERT(pKI);

	CRect rTip;
	VERIFY(GetItemTooltipRect(hti, rTip, pKI));

	m_tooltip.SetMaxTipWidth(rTip.Width());
	m_tooltip.SetFont(m_fonts.GetFont((pKI->dwParentID == 0) ? GMFS_BOLD : 0));
	m_tooltip.SetWindowPos(NULL, rTip.left, rTip.top, rTip.Width(), rTip.Height(), SWP_NOACTIVATE | SWP_NOZORDER);

	*pResult = TRUE; // we do the positioning
}

BOOL CKanbanListCtrl::AttributeValuesMatch(const CKanbanListCtrl& other) const
{
	CStringArray aOtherValues;
	other.GetAttributeValues(aOtherValues);

	return Misc::MatchAll(m_columnDef.aAttribValues, aOtherValues);
}

BOOL CKanbanListCtrl::CanDrag(const CKanbanListCtrl* pSrcList, const CKanbanListCtrl* pDestList)
{
	if (!pDestList)
		return FALSE;
	
	if (pDestList == pSrcList)
		return FALSE;
		
	if (pSrcList->AttributeValuesMatch(*pDestList))
		return FALSE;

	// Can't copy FROM the backlog
	if (pSrcList->IsBacklog() && Misc::ModKeysArePressed(MKS_CTRL))
		return FALSE;

	return TRUE;
}

