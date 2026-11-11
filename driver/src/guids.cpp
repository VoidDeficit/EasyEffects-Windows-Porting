/*
 * Instantiates the DEFINE_GUID IIDs from the PortCls headers
 * (IID_IMiniportWaveRT, IID_IMiniportTopology, IID_IUnknown, ...).
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
extern "C" {
#include <wdm.h>
}
#include <initguid.h>
#include <portcls.h>
#include <ksmedia.h>
