// HMXDataset.cpp: implementation of the CHMXDataset class.
//
//////////////////////////////////////////////////////////////////////

#include "stdafx.h"
#include "HMXDataset.h"

#ifdef _DEBUG
#undef THIS_FILE
static char THIS_FILE[]=__FILE__;
#define new DEBUG_NEW
#endif

//////////////////////////////////////////////////////////////////////
// Construction/Destruction
//////////////////////////////////////////////////////////////////////

CHMXDataset::CHMXDataset()
{
	m_nSize  = 2;
	m_nStyle = HMX_DATASET_STYLE_LINE;
	m_clr    = RGB(0, 0, 0);
	m_nMarker = HMX_DATASET_MARKER_NONE;

	m_bSetMinTo = m_bSetMaxTo = false;
	m_dSetMinTo = m_dSetMaxTo = -1;
}

CHMXDataset::~CHMXDataset()
{

}

bool CHMXDataset::ClearData()
{
	m_data.RemoveAll();
	return true;
}

bool CHMXDataset::AddData(double nData)
{
	m_data.Add(nData);

	return true;
}

bool CHMXDataset::SetData(int nIndex, double nData)
{
	if (nIndex < 0 || nIndex > GetDatasetSize())
		return false;
	
	m_data.SetAt(nIndex, nData);

	return true;
}


int CHMXDataset::GetDatasetSize()
{
	return m_data.GetSize();
}

bool CHMXDataset::GetData(int nCount, double &nSample)
{
	int nDatasetSize = GetDatasetSize();
	
	if (nCount < 0 || nCount >= nDatasetSize)
		return false;
	
	nSample = m_data.GetAt(nCount);
	
	return true;
}

bool CHMXDataset::SetStyle(HMX_DATASET_STYLE nStyle)
{
	m_nStyle = nStyle;
	
	return true;
}

bool CHMXDataset::SetColor(COLORREF clr)
{
	m_clr = clr;

	return true;
}

bool CHMXDataset::SetMarker(HMX_DATASET_MARKER nMarker)
{
	m_nMarker = nMarker;

	return true;
}

HMX_DATASET_MARKER CHMXDataset::GetMarker()
{
	return m_nMarker;
}

bool CHMXDataset::SetSize(int nSize)
{
	
	// between 1 an 10	
	m_nSize = min(nSize, 10);
	m_nSize = max(m_nSize,  1);

	return true;
}

bool CHMXDataset::GetMinMax(double& nMin, double& nMax)
{
	double dMin, dMax, temp;

	if (GetDatasetSize() > 0) 
	{
		GetData(0, temp);
		
		// following lines help me to solve some problems with invalid values
		if (temp == HMX_DATASET_VALUE_INVALID) 
		{
			dMin = HMX_DATASET_VALUE_INVALID;
			dMax = -HMX_DATASET_VALUE_INVALID;
		}
		else
			dMin = dMax = temp;

		for (int f=1; f<GetDatasetSize(); f++) 
		{
			GetData(f, temp);
			if (temp != HMX_DATASET_VALUE_INVALID) 
			{
				if (temp < dMin)
					dMin = temp;
				if (temp > dMax)
					dMax = temp;
			}
		}

		if (m_bSetMinTo)
			nMin = min(dMin, m_dSetMinTo);
		else
			nMin = dMin;

		if (m_bSetMaxTo)
			nMax = max(dMax, m_dSetMaxTo);
		else
			nMax = dMax;

		return true;
	} 
	else
		return false;
}

void CHMXDataset::SetMin(double dMin)
{
	m_bSetMinTo = true;
	m_dSetMinTo = dMin;
}

void CHMXDataset::SetMax(double dMax)
{
	m_bSetMaxTo = true;
	m_dSetMaxTo = dMax;
}

int CHMXDataset::GetSize()
{
	return m_nSize;
}

COLORREF CHMXDataset::GetColor()
{
	return m_clr;
}

HMX_DATASET_STYLE CHMXDataset::GetStyle()
{
	return m_nStyle;
}
