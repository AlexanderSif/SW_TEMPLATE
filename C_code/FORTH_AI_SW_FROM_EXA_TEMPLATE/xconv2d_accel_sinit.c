// ==============================================================
// Vitis HLS - High-Level Synthesis from C, C++ and OpenCL v2022.2 (64-bit)
// Tool Version Limit: 2019.12
// Copyright 1986-2022 Xilinx, Inc. All Rights Reserved.
// ==============================================================
#ifndef __linux__

#include "xstatus.h"
#include "xparameters.h"
#include "xconv2d_accel.h"

extern XConv2d_accel_Config XConv2d_accel_ConfigTable[];

XConv2d_accel_Config *XConv2d_accel_LookupConfig(u16 DeviceId) {
	XConv2d_accel_Config *ConfigPtr = NULL;

	int Index;

	for (Index = 0; Index < XPAR_XCONV2D_ACCEL_NUM_INSTANCES; Index++) {
		if (XConv2d_accel_ConfigTable[Index].DeviceId == DeviceId) {
			ConfigPtr = &XConv2d_accel_ConfigTable[Index];
			break;
		}
	}

	return ConfigPtr;
}

int XConv2d_accel_Initialize(XConv2d_accel *InstancePtr, u16 DeviceId) {
	XConv2d_accel_Config *ConfigPtr;

	Xil_AssertNonvoid(InstancePtr != NULL);

	ConfigPtr = XConv2d_accel_LookupConfig(DeviceId);
	if (ConfigPtr == NULL) {
		InstancePtr->IsReady = 0;
		return (XST_DEVICE_NOT_FOUND);
	}

	return XConv2d_accel_CfgInitialize(InstancePtr, ConfigPtr);
}

#endif

