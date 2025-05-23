#include "Cafe/OS/common/OSCommon.h"
#include "Cafe/HW/Espresso/PPCCallback.h"
#include <bitset>
#include <mutex>
#include "nsyshid.h"
#include "Cafe/OS/libs/coreinit/coreinit_Thread.h"
#include "Backend.h"
#include "Whitelist.h"

namespace nsyshid
{

	std::list<std::shared_ptr<Backend>> backendList;

	std::list<std::shared_ptr<Device>> deviceList;

	typedef struct _HIDClient_t
	{
		uint32be callbackFunc; // attach/detach callback
	} HIDClient_t;

	std::list<HIDClient_t*> HIDClientList;

	std::recursive_mutex hidMutex;

	void AttachClientToList(HIDClient_t* hidClient)
	{
		std::lock_guard<std::recursive_mutex> lock(hidMutex);
		// todo - append at the beginning or end of the list? List order matters because it also controls the order in which attach callbacks are called
		HIDClientList.push_front(hidClient);
	}

	void DetachClientFromList(HIDClient_t* hidClient)
	{
		std::lock_guard<std::recursive_mutex> lock(hidMutex);
		HIDClientList.remove(hidClient);
	}

	std::shared_ptr<Device> GetDeviceByHandle(uint32 handle, bool openIfClosed = false)
	{
		std::shared_ptr<Device> device;
		{
			std::lock_guard<std::recursive_mutex> lock(hidMutex);
			for (const auto& d : deviceList)
			{
				if (d->m_hid->handle == handle)
				{
					device = d;
					break;
				}
			}
		}
		if (device != nullptr)
		{
			if (openIfClosed && !device->IsOpened())
			{
				if (!device->Open())
				{
					return nullptr;
				}
			}
			return device;
		}
		return nullptr;
	}

	uint32 _lastGeneratedHidHandle = 1;

	uint32 GenerateHIDHandle()
	{
		std::lock_guard<std::recursive_mutex> lock(hidMutex);
		_lastGeneratedHidHandle++;
		return _lastGeneratedHidHandle;
	}

	const int HID_MAX_NUM_DEVICES = 128;

	SysAllocator<HID_t, HID_MAX_NUM_DEVICES> HIDPool;
	std::queue<size_t> HIDPoolIndexQueue;

	void InitHIDPoolIndexQueue()
	{
		static bool HIDPoolIndexQueueInitialized = false;
		std::lock_guard<std::recursive_mutex> lock(hidMutex);
		if (HIDPoolIndexQueueInitialized)
		{
			return;
		}
		HIDPoolIndexQueueInitialized = true;
		for (size_t i = 0; i < HID_MAX_NUM_DEVICES; i++)
		{
			HIDPoolIndexQueue.push(i);
		}
	}

	HID_t* GetFreeHID()
	{
		std::lock_guard<std::recursive_mutex> lock(hidMutex);
		InitHIDPoolIndexQueue();
		if (HIDPoolIndexQueue.empty())
		{
			return nullptr;
		}
		size_t index = HIDPoolIndexQueue.front();
		HIDPoolIndexQueue.pop();
		return HIDPool.GetPtr() + index;
	}

	void ReleaseHID(HID_t* device)
	{
		// this should never happen, but having a safeguard can't hurt
		if (device == nullptr)
		{
			cemu_assert_error();
		}
		std::lock_guard<std::recursive_mutex> lock(hidMutex);
		InitHIDPoolIndexQueue();
		size_t index = device - HIDPool.GetPtr();
		HIDPoolIndexQueue.push(index);
	}

	const int HID_CALLBACK_DETACH = 0;
	const int HID_CALLBACK_ATTACH = 1;

	uint32 DoAttachCallback(HIDClient_t* hidClient, const std::shared_ptr<Device>& device)
	{
		return PPCCoreCallback(hidClient->callbackFunc, memory_getVirtualOffsetFromPointer(hidClient),
							   memory_getVirtualOffsetFromPointer(device->m_hid), HID_CALLBACK_ATTACH);
	}

	void DoAttachCallbackAsync(HIDClient_t* hidClient, const std::shared_ptr<Device>& device)
	{
		coreinitAsyncCallback_add(hidClient->callbackFunc, 3, memory_getVirtualOffsetFromPointer(hidClient),
								  memory_getVirtualOffsetFromPointer(device->m_hid), HID_CALLBACK_ATTACH);
	}

	void DoDetachCallback(HIDClient_t* hidClient, const std::shared_ptr<Device>& device)
	{
		PPCCoreCallback(hidClient->callbackFunc, memory_getVirtualOffsetFromPointer(hidClient),
						memory_getVirtualOffsetFromPointer(device->m_hid), HID_CALLBACK_DETACH);
	}

	void DoDetachCallbackAsync(HIDClient_t* hidClient, const std::shared_ptr<Device>& device)
	{
		coreinitAsyncCallback_add(hidClient->callbackFunc, 3, memory_getVirtualOffsetFromPointer(hidClient),
								  memory_getVirtualOffsetFromPointer(device->m_hid), HID_CALLBACK_DETACH);
	}

	void AttachBackend(const std::shared_ptr<Backend>& backend)
	{
		{
			std::lock_guard<std::recursive_mutex> lock(hidMutex);
			backendList.push_back(backend);
		}
		backend->OnAttach();
	}

	void DetachBackend(const std::shared_ptr<Backend>& backend)
	{
		{
			std::lock_guard<std::recursive_mutex> lock(hidMutex);
			backendList.remove(backend);
		}
		backend->OnDetach();
	}

	void DetachAllBackends()
	{
		std::list<std::shared_ptr<Backend>> backendListCopy;
		{
			std::lock_guard<std::recursive_mutex> lock(hidMutex);
			backendListCopy = backendList;
			backendList.clear();
		}
		for (const auto& backend : backendListCopy)
		{
			backend->OnDetach();
		}
	}

	void AttachDefaultBackends()
	{
		backend::AttachDefaultBackends();
	}

	bool AttachDevice(const std::shared_ptr<Device>& device)
	{
		std::lock_guard<std::recursive_mutex> lock(hidMutex);

		// is the device already attached?
		{
			auto it = std::find(deviceList.begin(), deviceList.end(), device);
			if (it != deviceList.end())
			{
				cemuLog_logDebug(LogType::Force,
								 "nsyshid.AttachDevice(): failed to attach device: {:04x}:{:04x}: already attached",
								 device->m_vendorId,
								 device->m_productId);
				return false;
			}
		}

		HID_t* hidDevice = GetFreeHID();
		if (hidDevice == nullptr)
		{
			cemuLog_logDebug(LogType::Force,
							 "nsyshid.AttachDevice(): failed to attach device: {:04x}:{:04x}: no free device slots left",
							 device->m_vendorId,
							 device->m_productId);
			return false;
		}
		hidDevice->handle = GenerateHIDHandle();
		device->AssignHID(hidDevice);
		deviceList.push_back(device);

		// do attach callbacks
		for (auto client : HIDClientList)
		{
			DoAttachCallbackAsync(client, device);
		}

		cemuLog_logDebug(LogType::Force, "nsyshid.AttachDevice(): device attached: {:04x}:{:04x}",
						 device->m_vendorId,
						 device->m_productId);
		return true;
	}

	void DetachDevice(const std::shared_ptr<Device>& device)
	{
		{
			std::lock_guard<std::recursive_mutex> lock(hidMutex);

			// remove from list
			auto it = std::find(deviceList.begin(), deviceList.end(), device);
			if (it == deviceList.end())
			{
				cemuLog_logDebug(LogType::Force, "nsyshid.DetachDevice(): device not found: {:04x}:{:04x}",
								 device->m_vendorId,
								 device->m_productId);
				return;
			}
			deviceList.erase(it);

			// do detach callbacks
			for (auto client : HIDClientList)
			{
				DoDetachCallbackAsync(client, device);
			}
			ReleaseHID(device->m_hid);
		}

		device->Close();

		cemuLog_logDebug(LogType::Force, "nsyshid.DetachDevice(): device removed: {:04x}:{:04x}",
						 device->m_vendorId,
						 device->m_productId);
	}

	std::shared_ptr<Device> FindDeviceById(uint16 vendorId, uint16 productId)
	{
		std::lock_guard<std::recursive_mutex> lock(hidMutex);
		for (const auto& device : deviceList)
		{
			if (device->m_vendorId == vendorId && device->m_productId == productId)
			{
				return device;
			}
		}
		return nullptr;
	}

	void export_HIDAddClient(PPCInterpreter_t* hCPU)
	{
		ppcDefineParamTypePtr(hidClient, HIDClient_t, 0);
		ppcDefineParamMPTR(callbackFuncMPTR, 1);
		cemuLog_logDebug(LogType::Force, "nsyshid.HIDAddClient(0x{:08x},0x{:08x})", hCPU->gpr[3], hCPU->gpr[4]);
		hidClient->callbackFunc = callbackFuncMPTR;

		std::lock_guard<std::recursive_mutex> lock(hidMutex);
		AttachClientToList(hidClient);

		// do attach callbacks
		for (const auto& device : deviceList)
		{
			DoAttachCallback(hidClient, device);
		}

		osLib_returnFromFunction(hCPU, 0);
	}

	void export_HIDDelClient(PPCInterpreter_t* hCPU)
	{
		ppcDefineParamTypePtr(hidClient, HIDClient_t, 0);
		cemuLog_logDebug(LogType::Force, "nsyshid.HIDDelClient(0x{:08x})", hCPU->gpr[3]);

		std::lock_guard<std::recursive_mutex> lock(hidMutex);
		DetachClientFromList(hidClient);

		// do detach callbacks
		for (const auto& device : deviceList)
		{
			DoDetachCallback(hidClient, device);
		}

		osLib_returnFromFunction(hCPU, 0);
	}

	void _debugPrintHex(const std::string prefix, const uint8* data, size_t size)
	{
		constexpr size_t BYTES_PER_LINE = 16;

		std::string out;
		for (size_t row_start = 0; row_start < size; row_start += BYTES_PER_LINE)
		{
			out += fmt::format("{:06x}: ", row_start);
			for (size_t i = 0; i < BYTES_PER_LINE; ++i)
			{
				if (row_start + i < size)
				{
					out += fmt::format("{:02x} ", data[row_start + i]);
				}
				else
				{
					out += "   ";
				}
			}
			out += " ";
			for (size_t i = 0; i < BYTES_PER_LINE; ++i)
			{
				if (row_start + i < size)
				{
					char c = static_cast<char>(data[row_start + i]);
					out += std::isprint(c, std::locale::classic()) ? c : '.';
				}
			}
			out += "\n";
		}
		cemuLog_logDebug(LogType::Force, "[{}] Data: \n{}", prefix, out);
	}

	void DoHIDTransferCallback(MPTR callbackFuncMPTR, MPTR callbackParamMPTR, uint32 hidHandle, uint32 errorCode,
							   MPTR buffer, sint32 length)
	{
		coreinitAsyncCallback_add(callbackFuncMPTR, 5, hidHandle, errorCode, buffer, length, callbackParamMPTR);
	}

	void _hidGetDescriptorAsync(std::shared_ptr<Device> device, uint8 descType, uint8 descIndex, uint16 lang, uint8* output, uint32 outputMaxLength, MPTR callbackFuncMPTR, MPTR callbackParamMPTR)
	{
		if (device->GetDescriptor(descType, descIndex, lang, output, outputMaxLength))
		{
			DoHIDTransferCallback(callbackFuncMPTR,
								  callbackParamMPTR,
								  device->m_hid->handle,
								  0,
								  0,
								  0);
		}
		else
		{
			DoHIDTransferCallback(callbackFuncMPTR,
								  callbackParamMPTR,
								  device->m_hid->handle,
								  -1,
								  0,
								  0);
		}
	}

	void export_HIDGetDescriptor(PPCInterpreter_t* hCPU)
	{
		ppcDefineParamU32(hidHandle, 0);	   // r3
		ppcDefineParamU8(descType, 1);		   // r4
		ppcDefineParamU8(descIndex, 2);		   // r5
		ppcDefineParamU16(lang, 3);			   // r6
		ppcDefineParamUStr(output, 4);		   // r7
		ppcDefineParamU32(outputMaxLength, 5); // r8
		ppcDefineParamMPTR(cbFuncMPTR, 6);	   // r9
		ppcDefineParamMPTR(cbParamMPTR, 7);	   // r10
		cemuLog_logDebug(LogType::Force, "nsyshid.HIDGetDescriptor(0x{:08x}, 0x{:02x}, 0x{:02x}, 0x{:04x}, 0x{:x}, 0x{:08x}, 0x{:08x}, 0x{:08x})",
					hCPU->gpr[3], hCPU->gpr[4], hCPU->gpr[5], hCPU->gpr[6], hCPU->gpr[7], hCPU->gpr[8], hCPU->gpr[9], hCPU->gpr[10]);

		std::shared_ptr<Device> device = GetDeviceByHandle(hidHandle, true);
		if (device == nullptr)
		{
			cemuLog_log(LogType::Force, "nsyshid.HIDGetDescriptor(): Unable to find device with hid handle {}", hidHandle);
			osLib_returnFromFunction(hCPU, -1);
			return;
		}

		// issue request (synchronous or asynchronous)
		sint32 returnCode = 0;
		if (cbFuncMPTR == MPTR_NULL)
		{
			// synchronous
			returnCode = -1;
			if (device->GetDescriptor(descType, descIndex, lang, output, outputMaxLength))
			{
				returnCode = outputMaxLength;
			}
		}
		else
		{
			// asynchronous
			std::thread(&_hidGetDescriptorAsync, device, descType, descIndex, lang, output, outputMaxLength, cbFuncMPTR, cbParamMPTR)
				.detach();
			returnCode = 0;
		}
		osLib_returnFromFunction(hCPU, returnCode);
	}

	void _hidSetIdleAsync(std::shared_ptr<Device> device, uint8 ifIndex, uint8 reportId, uint8 duration, MPTR callbackFuncMPTR, MPTR callbackParamMPTR)
	{
		if (device->SetIdle(ifIndex, reportId, duration))
		{
			DoHIDTransferCallback(callbackFuncMPTR,
								  callbackParamMPTR,
								  device->m_hid->handle,
								  0,
								  0,
								  0);
		}
		else
		{
			DoHIDTransferCallback(callbackFuncMPTR,
								  callbackParamMPTR,
								  device->m_hid->handle,
								  -1,
								  0,
								  0);
		}
	}

	void export_HIDSetIdle(PPCInterpreter_t* hCPU)
	{
		ppcDefineParamU32(hidHandle, 0);		  // r3
		ppcDefineParamU8(ifIndex, 1);			  // r4
		ppcDefineParamU8(reportId, 2);			  // r5
		ppcDefineParamU8(duration, 3);			  // r6
		ppcDefineParamMPTR(callbackFuncMPTR, 4);  // r7
		ppcDefineParamMPTR(callbackParamMPTR, 5); // r8
		cemuLog_logDebug(LogType::Force, "nsyshid.HIDSetIdle(0x{:08x}, 0x{:02x}, 0x{:02x}, 0x{:02x}, 0x{:08x}, 0x{:08x})", hCPU->gpr[3],
					hCPU->gpr[4], hCPU->gpr[5], hCPU->gpr[6], hCPU->gpr[7], hCPU->gpr[8]);

		std::shared_ptr<Device> device = GetDeviceByHandle(hidHandle, true);
		if (device == nullptr)
		{
			cemuLog_log(LogType::Force, "nsyshid.HIDSetIdle(): Unable to find device with hid handle {}", hidHandle);
			osLib_returnFromFunction(hCPU, -1);
			return;
		}

		// issue request (synchronous or asynchronous)
		sint32 returnCode = 0;
		if (callbackFuncMPTR == MPTR_NULL)
		{
			// synchronous
			returnCode = -1;
			if (device->SetIdle(ifIndex, reportId, duration))
			{
				returnCode = 0;
			}
		}
		else
		{
			// asynchronous
			std::thread(&_hidSetIdleAsync, device, ifIndex, reportId, duration, callbackFuncMPTR, callbackParamMPTR)
				.detach();
			returnCode = 0;
		}
		osLib_returnFromFunction(hCPU, returnCode);
	}

	void _hidSetProtocolAsync(std::shared_ptr<Device> device, uint8 ifIndex, uint8 protocol, MPTR callbackFuncMPTR, MPTR callbackParamMPTR)
	{
		if (device->SetProtocol(ifIndex, protocol))
		{
			DoHIDTransferCallback(callbackFuncMPTR,
								  callbackParamMPTR,
								  device->m_hid->handle,
								  0,
								  0,
								  0);
		}
		else
		{
			DoHIDTransferCallback(callbackFuncMPTR,
								  callbackParamMPTR,
								  device->m_hid->handle,
								  -1,
								  0,
								  0);
		}
	}

	void export_HIDSetProtocol(PPCInterpreter_t* hCPU)
	{
		ppcDefineParamU32(hidHandle, 0);		  // r3
		ppcDefineParamU8(ifIndex, 1);			  // r4
		ppcDefineParamU8(protocol, 2);			  // r5
		ppcDefineParamMPTR(callbackFuncMPTR, 3);  // r6
		ppcDefineParamMPTR(callbackParamMPTR, 4); // r7
		cemuLog_logDebug(LogType::Force, "nsyshid.HIDSetProtocol(0x{:08x}, 0x{:02x}, 0x{:02x}, 0x{:08x}, 0x{:08x})", hCPU->gpr[3],
					hCPU->gpr[4], hCPU->gpr[5], hCPU->gpr[6], hCPU->gpr[7]);

		std::shared_ptr<Device> device = GetDeviceByHandle(hidHandle, true);
		if (device == nullptr)
		{
			cemuLog_log(LogType::Force, "nsyshid.HIDSetProtocol(): Unable to find device with hid handle {}", hidHandle);
			osLib_returnFromFunction(hCPU, -1);
			return;
		}
		// issue request (synchronous or asynchronous)
		sint32 returnCode = 0;
		if (callbackFuncMPTR == MPTR_NULL)
		{
			// synchronous
			returnCode = -1;
			if (device->SetProtocol(ifIndex, protocol))
			{
				returnCode = 0;
			}
		}
		else
		{
			// asynchronous
			std::thread(&_hidSetProtocolAsync, device, ifIndex, protocol, callbackFuncMPTR, callbackParamMPTR)
				.detach();
			returnCode = 0;
		}
		osLib_returnFromFunction(hCPU, returnCode);
	}

	// handler for async HIDSetReport transfers
	void _hidSetReportAsync(std::shared_ptr<Device> device, uint8 reportType, uint8 reportId, uint8* data, uint32 length,
							MPTR callbackFuncMPTR, MPTR callbackParamMPTR)
	{
		cemuLog_logDebug(LogType::Force, "_hidSetReportAsync begin");
		ReportMessage message(reportType, reportId, data, length);
		if (device->SetReport(&message))
		{
			DoHIDTransferCallback(callbackFuncMPTR,
								  callbackParamMPTR,
								  device->m_hid->handle,
								  0,
								  memory_getVirtualOffsetFromPointer(data),
								  length);
		}
		else
		{
			DoHIDTransferCallback(callbackFuncMPTR,
								  callbackParamMPTR,
								  device->m_hid->handle,
								  -1,
								  memory_getVirtualOffsetFromPointer(data),
								  length);
		}
	}

	// handler for synchronous HIDSetReport transfers
	sint32 _hidSetReportSync(std::shared_ptr<Device> device, uint8 reportType, uint8 reportId,
							 uint8* data, uint32 length, coreinit::OSEvent* event)
	{
		_debugPrintHex("_hidSetReportSync Begin", data, length);
		sint32 returnCode = 0;
		ReportMessage message(reportType, reportId, data, length);
		if (device->SetReport(&message))
		{
			returnCode = length;
		}
		cemuLog_logDebug(LogType::Force, "_hidSetReportSync end. returnCode: {}", returnCode);
		coreinit::OSSignalEvent(event);
		return returnCode;
	}

	void export_HIDSetReport(PPCInterpreter_t* hCPU)
	{
		ppcDefineParamU32(hidHandle, 0);		  // r3
		ppcDefineParamU8(reportType, 1);		  // r4
		ppcDefineParamU8(reportId, 2);			  // r5
		ppcDefineParamUStr(data, 3);			  // r6
		ppcDefineParamU32(dataLength, 4);		  // r7
		ppcDefineParamMPTR(callbackFuncMPTR, 5);  // r8
		ppcDefineParamMPTR(callbackParamMPTR, 6); // r9
		cemuLog_logDebug(LogType::Force, "nsyshid.HIDSetReport(0x{:08x}, 0x{:02x}, 0x{:02x}, 0x{:08x}, 0x{:08x}, 0x{:08x}, 0x{:08x})", hCPU->gpr[3],
					hCPU->gpr[4], hCPU->gpr[5], hCPU->gpr[6], hCPU->gpr[7], hCPU->gpr[8], hCPU->gpr[9]);

		_debugPrintHex("HIDSetReport", data, dataLength);

#ifdef CEMU_DEBUG_ASSERT
		if (reportType != 2 || reportId != 0)
			assert_dbg();
#endif

		std::shared_ptr<Device> device = GetDeviceByHandle(hidHandle, true);
		if (device == nullptr)
		{
			cemuLog_log(LogType::Force, "nsyshid.HIDSetReport(): Unable to find device with hid handle {}", hidHandle);
			osLib_returnFromFunction(hCPU, -1);
			return;
		}

		// issue request (synchronous or asynchronous)
		sint32 returnCode = 0;
		if (callbackFuncMPTR == MPTR_NULL)
		{
			// synchronous
			StackAllocator<coreinit::OSEvent> event;
			coreinit::OSInitEvent(&event, coreinit::OSEvent::EVENT_STATE::STATE_NOT_SIGNALED, coreinit::OSEvent::EVENT_MODE::MODE_AUTO);
			std::future<sint32> res = std::async(std::launch::async, &_hidSetReportSync, device, reportType, reportId, data, dataLength, &event);
			coreinit::OSWaitEvent(&event);
			returnCode = res.get();
		}
		else
		{
			// asynchronous
			std::thread(&_hidSetReportAsync, device, reportType, reportId, data, dataLength,
						callbackFuncMPTR, callbackParamMPTR)
				.detach();
			returnCode = 0;
		}
		osLib_returnFromFunction(hCPU, returnCode);
	}

	sint32 _hidReadInternalSync(std::shared_ptr<Device> device, uint8* data, sint32 maxLength)
	{
		cemuLog_logDebug(LogType::Force, "HidRead Begin (Length 0x{:08x})", maxLength);
		if (!device->IsOpened())
		{
			cemuLog_logDebug(LogType::Force, "nsyshid.hidReadInternalSync(): cannot read from a non-opened device");
			return -1;
		}
		memset(data, 0, maxLength);
		ReadMessage message(data, maxLength, 0);
		Device::ReadResult readResult = device->Read(&message);
		switch (readResult)
		{
		case Device::ReadResult::Success:
		{
			cemuLog_logDebug(LogType::Force, "nsyshid.hidReadInternalSync(): read {} of {} bytes",
							 message.bytesRead,
							 maxLength);
			return message.bytesRead;
		}
		break;
		case Device::ReadResult::Error:
		{
			cemuLog_logDebug(LogType::Force, "nsyshid.hidReadInternalSync(): read error");
			return -1;
		}
		break;
		case Device::ReadResult::ErrorTimeout:
		{
			cemuLog_logDebug(LogType::Force, "nsyshid.hidReadInternalSync(): read error: timeout");
			return -108;
		}
		break;
		}
		cemuLog_logDebug(LogType::Force, "nsyshid.hidReadInternalSync(): read error: unknown");
		return -1;
	}

	void _hidReadAsync(std::shared_ptr<Device> device,
					   uint8* data, sint32 maxLength,
					   MPTR callbackFuncMPTR,
					   MPTR callbackParamMPTR)
	{
		sint32 returnCode = _hidReadInternalSync(device, data, maxLength);
		sint32 errorCode = 0;
		if (returnCode < 0)
			errorCode = returnCode; // don't return number of bytes in error code
		DoHIDTransferCallback(callbackFuncMPTR, callbackParamMPTR, device->m_hid->handle, errorCode,
							  memory_getVirtualOffsetFromPointer(data), (returnCode > 0) ? returnCode : 0);
	}

	sint32 _hidReadSync(std::shared_ptr<Device> device,
						uint8* data,
						sint32 maxLength,
						coreinit::OSEvent* event)
	{
		sint32 returnCode = _hidReadInternalSync(device, data, maxLength);
		coreinit::OSSignalEvent(event);
		return returnCode;
	}

	void export_HIDRead(PPCInterpreter_t* hCPU)
	{
		ppcDefineParamU32(hidHandle, 0);		  // r3
		ppcDefineParamUStr(data, 1);			  // r4
		ppcDefineParamU32(maxLength, 2);		  // r5
		ppcDefineParamMPTR(callbackFuncMPTR, 3);  // r6
		ppcDefineParamMPTR(callbackParamMPTR, 4); // r7
		cemuLog_logDebug(LogType::Force, "nsyshid.HIDRead(0x{:x},0x{:08x},0x{:08x},0x{:08x},0x{:08x})", hCPU->gpr[3],
					hCPU->gpr[4], hCPU->gpr[5], hCPU->gpr[6], hCPU->gpr[7]);

		std::shared_ptr<Device> device = GetDeviceByHandle(hidHandle, true);
		if (device == nullptr)
		{
			cemuLog_log(LogType::Force, "nsyshid.HIDRead(): Unable to find device with hid handle {}", hidHandle);
			osLib_returnFromFunction(hCPU, -1);
			return;
		}
		sint32 returnCode = 0;
		if (callbackFuncMPTR != MPTR_NULL)
		{
			// asynchronous transfer
			std::thread(&_hidReadAsync, device, data, maxLength, callbackFuncMPTR, callbackParamMPTR).detach();
			returnCode = 0;
		}
		else
		{
			// synchronous transfer
			StackAllocator<coreinit::OSEvent> event;
			coreinit::OSInitEvent(&event, coreinit::OSEvent::EVENT_STATE::STATE_NOT_SIGNALED, coreinit::OSEvent::EVENT_MODE::MODE_AUTO);
			std::future<sint32> res = std::async(std::launch::async, &_hidReadSync, device, data, maxLength, &event);
			coreinit::OSWaitEvent(&event);
			returnCode = res.get();
		}

		osLib_returnFromFunction(hCPU, returnCode);
	}

	sint32 _hidWriteInternalSync(std::shared_ptr<Device> device, uint8* data, sint32 maxLength)
	{
		cemuLog_logDebug(LogType::Force, "HidWrite Begin (Length 0x{:08x})", maxLength);
		if (!device->IsOpened())
		{
			cemuLog_logDebug(LogType::Force, "nsyshid.hidWriteInternalSync(): cannot write to a non-opened device");
			return -1;
		}
		WriteMessage message(data, maxLength, 0);
		Device::WriteResult writeResult = device->Write(&message);
		switch (writeResult)
		{
		case Device::WriteResult::Success:
		{
			cemuLog_logDebug(LogType::Force, "nsyshid.hidWriteInternalSync(): wrote {} of {} bytes", message.bytesWritten,
							 maxLength);
			return message.bytesWritten;
		}
		break;
		case Device::WriteResult::Error:
		{
			cemuLog_logDebug(LogType::Force, "nsyshid.hidWriteInternalSync(): write error");
			return -1;
		}
		break;
		case Device::WriteResult::ErrorTimeout:
		{
			cemuLog_logDebug(LogType::Force, "nsyshid.hidWriteInternalSync(): write error: timeout");
			return -108;
		}
		break;
		}
		cemuLog_logDebug(LogType::Force, "nsyshid.hidWriteInternalSync(): write error: unknown");
		return -1;
	}

	void _hidWriteAsync(std::shared_ptr<Device> device,
						uint8* data,
						sint32 maxLength,
						MPTR callbackFuncMPTR,
						MPTR callbackParamMPTR)
	{
		sint32 returnCode = _hidWriteInternalSync(device, data, maxLength);
		sint32 errorCode = 0;
		if (returnCode < 0)
			errorCode = returnCode; // don't return number of bytes in error code
		DoHIDTransferCallback(callbackFuncMPTR, callbackParamMPTR, device->m_hid->handle, errorCode,
							  memory_getVirtualOffsetFromPointer(data), (returnCode > 0) ? returnCode : 0);
	}

	sint32 _hidWriteSync(std::shared_ptr<Device> device,
						 uint8* data,
						 sint32 maxLength,
						 coreinit::OSEvent* event)
	{
		sint32 returnCode = _hidWriteInternalSync(device, data, maxLength);
		coreinit::OSSignalEvent(event);
		return returnCode;
	}

	void export_HIDWrite(PPCInterpreter_t* hCPU)
	{
		ppcDefineParamU32(hidHandle, 0);		  // r3
		ppcDefineParamUStr(data, 1);			  // r4
		ppcDefineParamU32(maxLength, 2);		  // r5
		ppcDefineParamMPTR(callbackFuncMPTR, 3);  // r6
		ppcDefineParamMPTR(callbackParamMPTR, 4); // r7
		cemuLog_logDebug(LogType::Force, "nsyshid.HIDWrite(0x{:x},0x{:08x},0x{:08x},0x{:08x},0x{:08x})", hCPU->gpr[3],
					hCPU->gpr[4], hCPU->gpr[5], hCPU->gpr[6], hCPU->gpr[7]);

		std::shared_ptr<Device> device = GetDeviceByHandle(hidHandle, true);
		if (device == nullptr)
		{
			cemuLog_log(LogType::Force, "nsyshid.HIDWrite(): Unable to find device with hid handle {}", hidHandle);
			osLib_returnFromFunction(hCPU, -1);
			return;
		}
		sint32 returnCode = 0;
		if (callbackFuncMPTR != MPTR_NULL)
		{
			// asynchronous transfer
			std::thread(&_hidWriteAsync, device, data, maxLength, callbackFuncMPTR, callbackParamMPTR).detach();
			returnCode = 0;
		}
		else
		{
			// synchronous transfer
			StackAllocator<coreinit::OSEvent> event;
			coreinit::OSInitEvent(&event, coreinit::OSEvent::EVENT_STATE::STATE_NOT_SIGNALED, coreinit::OSEvent::EVENT_MODE::MODE_AUTO);
			std::future<sint32> res = std::async(std::launch::async, &_hidWriteSync, device, data, maxLength, &event);
			coreinit::OSWaitEvent(&event);
			returnCode = res.get();
		}

		osLib_returnFromFunction(hCPU, returnCode);
	}

	void export_HIDDecodeError(PPCInterpreter_t* hCPU)
	{
		ppcDefineParamU32(errorCode, 0);
		ppcDefineParamTypePtr(ukn0, uint32be, 1);
		ppcDefineParamTypePtr(ukn1, uint32be, 2);
		cemuLog_logDebug(LogType::Force, "nsyshid.HIDDecodeError(0x{:08x},0x{:08x},0x{:08x})", hCPU->gpr[3],
					hCPU->gpr[4], hCPU->gpr[5]);

		// todo
		*ukn0 = 0x3FF;
		*ukn1 = (uint32)-0x7FFF;

		osLib_returnFromFunction(hCPU, 0);
	}

	void Backend::DetachAllDevices()
	{
		std::lock_guard<std::recursive_mutex> lock(this->m_devicesMutex);
		if (m_isAttached)
		{
			for (const auto& device : this->m_devices)
			{
				nsyshid::DetachDevice(device);
			}
			this->m_devices.clear();
		}
	}

	bool Backend::AttachDevice(const std::shared_ptr<Device>& device)
	{
		std::lock_guard<std::recursive_mutex> lock(this->m_devicesMutex);
		if (m_isAttached && nsyshid::AttachDevice(device))
		{
			this->m_devices.push_back(device);
			return true;
		}
		return false;
	}

	void Backend::DetachDevice(const std::shared_ptr<Device>& device)
	{
		std::lock_guard<std::recursive_mutex> lock(this->m_devicesMutex);
		if (m_isAttached)
		{
			nsyshid::DetachDevice(device);
			this->m_devices.remove(device);
		}
	}

	std::shared_ptr<Device> Backend::FindDevice(std::function<bool(const std::shared_ptr<Device>&)> isWantedDevice)
	{
		std::lock_guard<std::recursive_mutex> lock(this->m_devicesMutex);
		auto it = std::find_if(this->m_devices.begin(), this->m_devices.end(), std::move(isWantedDevice));
		if (it != this->m_devices.end())
		{
			return *it;
		}
		return nullptr;
	}

	std::shared_ptr<Device> Backend::FindDeviceById(uint16 vendorId, uint16 productId)
	{
		return nsyshid::FindDeviceById(vendorId, productId);
	}

	bool Backend::IsDeviceWhitelisted(uint16 vendorId, uint16 productId)
	{
		return Whitelist::GetInstance().IsDeviceWhitelisted(vendorId, productId);
	}

	Backend::Backend()
		: m_isAttached(false)
	{
	}

	void Backend::OnAttach()
	{
		std::lock_guard<std::recursive_mutex> lock(this->m_devicesMutex);
		m_isAttached = true;
		AttachVisibleDevices();
	}

	void Backend::OnDetach()
	{
		std::lock_guard<std::recursive_mutex> lock(this->m_devicesMutex);
		DetachAllDevices();
		m_isAttached = false;
	}

	bool Backend::IsBackendAttached()
	{
		std::lock_guard<std::recursive_mutex> lock(this->m_devicesMutex);
		return m_isAttached;
	}

	Device::Device(uint16 vendorId,
				   uint16 productId,
				   uint8 interfaceIndex,
				   uint8 interfaceSubClass,
				   uint8 protocol)
		: m_hid(nullptr),
		  m_vendorId(vendorId),
		  m_productId(productId),
		  m_interfaceIndex(interfaceIndex),
		  m_interfaceSubClass(interfaceSubClass),
		  m_protocol(protocol),
		  m_maxPacketSizeRX(0x20),
		  m_maxPacketSizeTX(0x20)
	{
	}

	void Device::AssignHID(HID_t* hid)
	{
		if (hid != nullptr)
		{
			hid->vendorId = this->m_vendorId;
			hid->productId = this->m_productId;
			hid->ifIndex = this->m_interfaceIndex;
			hid->subClass = this->m_interfaceSubClass;
			hid->protocol = this->m_protocol;
			hid->ukn04 = 0x11223344;
			hid->paddingGuessed0F = 0;
			hid->maxPacketSizeRX = this->m_maxPacketSizeRX;
			hid->maxPacketSizeTX = this->m_maxPacketSizeTX;
		}
		this->m_hid = hid;
	}

	void load()
	{
		osLib_addFunction("nsyshid", "HIDAddClient", export_HIDAddClient);
		osLib_addFunction("nsyshid", "HIDDelClient", export_HIDDelClient);
		osLib_addFunction("nsyshid", "HIDGetDescriptor", export_HIDGetDescriptor);
		osLib_addFunction("nsyshid", "HIDSetIdle", export_HIDSetIdle);
		osLib_addFunction("nsyshid", "HIDSetProtocol", export_HIDSetProtocol);
		osLib_addFunction("nsyshid", "HIDSetReport", export_HIDSetReport);

		osLib_addFunction("nsyshid", "HIDRead", export_HIDRead);
		osLib_addFunction("nsyshid", "HIDWrite", export_HIDWrite);

		osLib_addFunction("nsyshid", "HIDDecodeError", export_HIDDecodeError);

		// initialise whitelist
		Whitelist::GetInstance();

		AttachDefaultBackends();
	}
} // namespace nsyshid
